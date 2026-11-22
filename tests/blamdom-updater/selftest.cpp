// SPDX-License-Identifier: GPL-3.0-only
// Standalone self-test for BlamdomUpdateLogic (no GUI, no launcher build needed).
//   cmake -S tests/blamdom-updater -B /tmp/bl-selftest -DCMAKE_PREFIX_PATH=<qt> && cmake --build /tmp/bl-selftest && /tmp/bl-selftest/selftest
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QProcess>
#include <QTemporaryDir>
#include <cstdio>

#include "BlamdomUpdateLogic.h"

using namespace BlamdomUpdate;

static int g_fail = 0, g_pass = 0;
#define CHECK(cond)                                                                \
    do {                                                                           \
        if (cond) {                                                                \
            ++g_pass;                                                              \
        } else {                                                                   \
            ++g_fail;                                                              \
            std::printf("FAIL line %d: %s\n", __LINE__, #cond);                    \
        }                                                                          \
    } while (0)

static void write(const QString& path, const QByteArray& data, bool exec = false)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    f.open(QIODevice::WriteOnly);
    f.write(data);
    f.close();
    if (exec) {
        f.setPermissions(f.permissions() | QFileDevice::ExeUser | QFileDevice::ExeGroup | QFileDevice::ExeOther);
    }
}
static QByteArray read(const QString& path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray("<missing>");
}

static const char* kRelease = R"({
  "tag_name": "v0.2.0", "name": "Blamdom 0.2.0", "body": "- new stuff", "draft": false, "prerelease": false,
  "html_url": "https://github.com/blamdom/blamdom-launcher/releases/tag/v0.2.0",
  "assets": [
    {"name": "blamdom-launcher-linux-x86_64.tar.gz.zsync", "browser_download_url": "https://x/zsync", "size": 10},
    {"name": "blamdom-launcher-windows-x86_64.zip", "browser_download_url": "https://x/win", "size": 20},
    {"name": "blamdom-launcher-linux-x86_64.tar.gz", "browser_download_url": "https://x/linux", "size": 30,
     "digest": "sha256:ABCDEF0123456789abcdef0123456789abcdef0123456789abcdef0123456789"},
    {"name": "SHA256SUMS", "browser_download_url": "https://x/sums", "size": 5}
  ]})";

int main(int argc, char** argv)
{
    QCoreApplication qtApp(argc, argv);

    // ---- versions
    CHECK(parseVersion("v0.1.1") == QList<int>({ 0, 1, 1 }));
    CHECK(parseVersion("12.0.0-develop") == QList<int>({ 12, 0, 0 }));
    CHECK(parseVersion("nightly").isEmpty());
    CHECK(isNewer("v0.1.1", "0.1.0"));
    CHECK(!isNewer("0.1.0", "0.1.0"));
    CHECK(!isNewer("0.0.9", "0.1.0"));
    CHECK(isNewer("0.10.0", "0.9.0"));   // numeric, not text, comparison
    CHECK(isNewer("1.0.0", "0.99.99"));
    CHECK(!isNewer("garbage", "0.1.0"));
    CHECK(!isNewer("0.2.0", "garbage"));
    CHECK(!isNewer("0.1.0-beta", "0.1.0"));
    CHECK(!isNewer("0.1.1", "12.0.0-develop"));  // old 12.x test builds never see 0.x releases (documented)

    // ---- release parsing
    QString err;
    auto rel = parseRelease(kRelease, &err);
    CHECK(rel.has_value());
    if (rel) {
        CHECK(rel->version == "0.2.0");
        CHECK(rel->tag == "v0.2.0");
        CHECK(rel->notes == "- new stuff");
        CHECK(rel->assets.size() == 4);
        auto picked = pickAsset(*rel, "linux-x86_64", ".tar.gz");
        CHECK(picked.has_value());
        if (picked) {
            CHECK(picked->name == "blamdom-launcher-linux-x86_64.tar.gz");   // not the .zsync, not windows
            CHECK(picked->sha256 == "abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789");
        }
        CHECK(!pickAsset(*rel, "macos-arm64", ".tar.gz").has_value());
        CHECK(pickAsset(*rel, "windows-x86_64", ".zip").has_value());
    }
    CHECK(!parseRelease(R"({"tag_name":"v1.0.0","draft":true})", &err).has_value());
    CHECK(!parseRelease(R"({"tag_name":"v1.0.0","prerelease":true})", &err).has_value());
    CHECK(!parseRelease(R"({"tag_name":"nightly"})", &err).has_value());
    CHECK(!parseRelease("<html>rate limited</html>", &err).has_value());
    CHECK(!parseRelease("", &err).has_value());
    CHECK(platformKey().startsWith("linux-"));

    // ---- checksums
    const QString h = QString(64, 'a');
    CHECK(findChecksum(h + "  blamdom.tar.gz\n" + QString(64, 'b') + " *other.zip\n", "blamdom.tar.gz") == h);
    CHECK(findChecksum(h + "  blamdom.tar.gz\n" + QString(64, 'b') + " *other.zip\n", "other.zip") == QString(64, 'b'));
    CHECK(findChecksum(h + "  blamdom.tar.gz\n", "missing.tar.gz").isEmpty());
    CHECK(findChecksum("short  blamdom.tar.gz\n", "blamdom.tar.gz").isEmpty());

    QTemporaryDir tmp;
    CHECK(tmp.isValid());
    const QString root = tmp.path();
    write(root + "/blob.bin", "hello blamdom");
    const QString good = QString::fromLatin1(QCryptographicHash::hash("hello blamdom", QCryptographicHash::Sha256).toHex());
    CHECK(verifySha256(root + "/blob.bin", good));
    CHECK(verifySha256(root + "/blob.bin", good.toUpper()));
    CHECK(!verifySha256(root + "/blob.bin", QString(64, '0')));
    CHECK(!verifySha256(root + "/blob.bin", "tooshort"));
    CHECK(!verifySha256(root + "/nope.bin", good));

    // ---- extract + swap (the dangerous part)
    // current install "app" (v1) and a release tarball with a top-level folder (v2)
    write(root + "/app/blamdom-launcher", "#!/bin/sh\necho v1\n", true);
    write(root + "/app/lib/libfoo.so", "old-lib");
    write(root + "/pkg/blamdom-launcher-linux-x86_64/blamdom-launcher", "#!/bin/sh\necho v2\n", true);
    write(root + "/pkg/blamdom-launcher-linux-x86_64/lib/libfoo.so", "new-lib");
    write(root + "/pkg/blamdom-launcher-linux-x86_64/lib/libbar.so", "added-lib");
    {
        QProcess t;
        t.setWorkingDirectory(root + "/pkg");
        t.start("tar", { "-czf", root + "/update.tar.gz", "blamdom-launcher-linux-x86_64" });
        CHECK(t.waitForFinished(20000) && t.exitCode() == 0);
    }
    const QString inst = root + "/app", staged = inst + ".update-new";
    CHECK(extractTarGz(root + "/update.tar.gz", staged, &err));
    CHECK(validateStaged(staged, "blamdom-launcher", &err));
    CHECK(read(staged + "/lib/libbar.so") == "added-lib");               // strip-components worked
    CHECK(!validateStaged(staged, "does-not-exist", &err));
    CHECK(!extractTarGz(root + "/blob.bin", root + "/junk", &err));       // not a tarball -> clean failure

    // rollback: staged dir missing -> swap must fail and leave v1 untouched
    CHECK(!swapInstall(inst, root + "/no-such-staged", &err));
    CHECK(read(inst + "/lib/libfoo.so") == "old-lib");
    CHECK(QFileInfo(inst + "/blamdom-launcher").isExecutable());
    CHECK(!QFileInfo::exists(inst + ".update-old"));                      // rolled back, nothing left behind

    // real swap
    CHECK(swapInstall(inst, staged, &err));
    CHECK(read(inst + "/lib/libfoo.so") == "new-lib");
    CHECK(read(inst + "/lib/libbar.so") == "added-lib");
    CHECK(QFileInfo(inst + "/blamdom-launcher").isExecutable());
    CHECK(read(inst + ".update-old/lib/libfoo.so") == "old-lib");         // previous version kept until cleanup
    CHECK(!QFileInfo::exists(staged));
    {   // the swapped-in binary actually runs
        QProcess p;
        p.start(inst + "/blamdom-launcher");
        CHECK(p.waitForFinished(5000));
        CHECK(p.readAllStandardOutput().trimmed() == "v2");
    }
    cleanupLeftovers(inst);
    CHECK(!QFileInfo::exists(inst + ".update-old"));

    // stale .update-old from an earlier failed run does not block a new swap
    write(inst + ".update-old/stale", "x");
    write(root + "/staged2/blamdom-launcher", "#!/bin/sh\necho v3\n", true);
    CHECK(swapInstall(inst, root + "/staged2", &err));
    CHECK(QFileInfo(inst + "/blamdom-launcher").isExecutable());
    CHECK(!QFileInfo::exists(inst + ".update-old/stale"));

    // ---- can-self-update policy
    QString why;
    CHECK(canSelfUpdate(inst, root + "/userdata", false, &why));                       // user-writable, data elsewhere
    CHECK(!canSelfUpdate(inst, root + "/userdata", true, &why) && !why.isEmpty());     // portable
    CHECK(!canSelfUpdate(inst, inst + "/UserData", false, &why) && !why.isEmpty());     // data inside install dir
    CHECK(!canSelfUpdate("/usr/lib", root + "/userdata", false, &why));               // system dir, not writable
    CHECK(!canSelfUpdate("/tmp/.mount_BlamdomXYZ", root + "/userdata", false, &why)); // AppImage mount

    // ---- zip extraction (the Windows path)
    // build a zip the way CI does: one top-level folder, nested files
    write(root + "/zpkg/blamdom-launcher-windows-x86_64/app.exe", "MZ-new");
    write(root + "/zpkg/blamdom-launcher-windows-x86_64/Qt6Core.dll", "dll-new");
    write(root + "/zpkg/blamdom-launcher-windows-x86_64/jars/JavaCheck.jar", "jar-new");
    write(root + "/zpkg/blamdom-launcher-windows-x86_64/jars/NewLaunch.jar", "jar-new2");
    {
        bool made = false;
        QProcess z;
        z.setWorkingDirectory(root + "/zpkg");
        z.start("zip", { "-qr", root + "/update.zip", "blamdom-launcher-windows-x86_64" });
        made = z.waitForFinished(20000) && z.exitCode() == 0;
        if (!made) {  // no zip CLI on this box: python3's zipfile makes the same layout
            z.start("python3", { "-c",
                                 "import zipfile, os, sys\n"
                                 "root, out = sys.argv[1], sys.argv[2]\n"
                                 "z = zipfile.ZipFile(out, 'w', zipfile.ZIP_DEFLATED)\n"
                                 "z.write(os.path.join(root, 'blamdom-launcher-windows-x86_64'), 'blamdom-launcher-windows-x86_64/')\n"
                                 "for dirpath, dirnames, filenames in os.walk(root):\n"
                                 "    for fn in filenames:\n"
                                 "        p = os.path.join(dirpath, fn)\n"
                                 "        z.write(p, os.path.relpath(p, root))\n"
                                 "z.close()\n",
                                 root + "/zpkg",
                                 root + "/update.zip" });
            made = z.waitForFinished(20000) && z.exitCode() == 0;
        }
        CHECK(made);
    }
    const QString zstaged = root + "/zipapp";
    CHECK(extractZip(root + "/update.zip", zstaged, &err));
    // zip extraction does not restore unix permission bits (irrelevant on Windows, the only
    // platform that uses this path) - mark the fake exe so validateStaged can check it
    QFile(zstaged + "/app.exe").setPermissions(QFile::permissions(zstaged + "/app.exe") | QFileDevice::ExeUser);
    CHECK(read(zstaged + "/app.exe") == "MZ-new");
    CHECK(read(zstaged + "/jars/JavaCheck.jar") == "jar-new");
    CHECK(read(zstaged + "/jars/NewLaunch.jar") == "jar-new2");
    CHECK(!QFileInfo::exists(zstaged + "/blamdom-launcher-windows-x86_64"));  // top folder stripped
    CHECK(validateStaged(zstaged, "app.exe", &err));
    CHECK(!extractZip(root + "/update.tar.gz", root + "/zipjunk", &err));      // not a zip -> clean failure
    CHECK(!extractZip(root + "/blob.bin", root + "/zipjunk2", &err));          // junk -> clean failure
    // backslash entry names (as written by some tools) are normalized to paths
    {
        QProcess z;
        z.start("python3", { "-c",
                             "import zipfile, sys\n"
                             "z = zipfile.ZipFile(sys.argv[1], 'w', zipfile.ZIP_DEFLATED)\n"
                             "zi = zipfile.ZipInfo('blamdom-launcher-windows-x86_64\\\\nested\\\\app.exe')\n"
                             "zi.compress_type = zipfile.ZIP_DEFLATED\n"
                             "z.writestr(zi, 'MZ-backslash')\n"
                             "z.close()\n",
                             root + "/backslash.zip" });
        CHECK(z.waitForFinished(20000) && z.exitCode() == 0);
        const QString bdest = root + "/zipback";
        CHECK(extractZip(root + "/backslash.zip", bdest, &err));
        CHECK(read(bdest + "/nested/app.exe") == "MZ-backslash");
    }

    // ---- Windows update bootstrap (the folder surgery)
    // "win" is the canonical install dir; a staged sibling with a marker waits to take over
    write(root + "/win/app.exe", "MZ-old");
    write(root + "/win/lib.dll", "dll-old");
    const QString wstaged = root + "/win.update";
    write(wstaged + "/app.exe", "MZ-new");
    write(wstaged + "/lib.dll", "dll-new");
    write(wstaged + "/new.dll", "dll-added");
    write(wstaged + "/" + pendingUpdateMarkerName(), (root + "/win\n").toUtf8());
    {
        QString relaunchExe;
        CHECK(applyPendingUpdate(wstaged, "app.exe", &relaunchExe, &err) == UpdateBootstrap::Completed);
        CHECK(read(root + "/win/app.exe") == "MZ-new");    // new version now owns the canonical folder
        CHECK(read(root + "/win/lib.dll") == "dll-new");
        CHECK(read(root + "/win/new.dll") == "dll-added");
        CHECK(!QFileInfo::exists(wstaged));                // staged folder renamed away
        CHECK(!QFileInfo::exists(root + "/win.update-old"));  // old version removed
        CHECK(!QFileInfo::exists(root + "/win/" + pendingUpdateMarkerName()));  // marker not carried over
    }
    // a pending sibling seen from the canonical dir asks for a relaunch into it
    write(root + "/win/app.exe", "MZ-old2");
    write(root + "/win.update/app.exe", "MZ-old3");
    write(root + "/win.update/" + pendingUpdateMarkerName(), (root + "/win\n").toUtf8());
    {
        QString relaunchExe;
        CHECK(applyPendingUpdate(root + "/win", "app.exe", &relaunchExe, &err) == UpdateBootstrap::RelaunchNeeded);
        CHECK(relaunchExe == root + "/win.update/app.exe");
    }
    // a stale sibling (no marker) is simply removed; then there is nothing to do
    QDir(root + "/win.update").removeRecursively();
    write(root + "/win.update/partial.bin", "x");
    {
        QString relaunchExe;
        CHECK(applyPendingUpdate(root + "/win", "app.exe", &relaunchExe, &err) == UpdateBootstrap::Nothing);
        CHECK(!QFileInfo::exists(root + "/win.update"));
        CHECK(applyPendingUpdate(root + "/win", "app.exe", &relaunchExe, &err) == UpdateBootstrap::Nothing);
    }
    // copyIntoDir overwrites files, merges dirs, never carries the marker
    write(root + "/src/a.txt", "src-a");
    write(root + "/src/sub/b.txt", "src-b");
    write(root + "/src/" + pendingUpdateMarkerName(), "marker");
    write(root + "/dst/a.txt", "dst-a");
    write(root + "/dst/keep.txt", "kept");
    CHECK(copyIntoDir(root + "/src", root + "/dst", &err));
    CHECK(read(root + "/dst/a.txt") == "src-a");
    CHECK(read(root + "/dst/sub/b.txt") == "src-b");
    CHECK(read(root + "/dst/keep.txt") == "kept");
    CHECK(!QFileInfo::exists(root + "/dst/" + pendingUpdateMarkerName()));

    std::printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
