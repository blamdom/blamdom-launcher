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

    std::printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
