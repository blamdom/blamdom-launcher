// SPDX-License-Identifier: GPL-3.0-only
/*
 *  Blamdom Launcher
 *  Copyright (C) 2026 Blamdom
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 3.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include "BlamdomUpdateLogic.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QRegularExpression>
#include <QSysInfo>
#include <QTemporaryFile>
#include <QThread>
#include <zlib.h>
#include <algorithm>

namespace BlamdomUpdate {

QList<int> parseVersion(const QString& text)
{
    static const QRegularExpression re(QStringLiteral(R"(^\s*[vV]?(\d+)\.(\d+)\.(\d+))"));
    const auto m = re.match(text);
    if (!m.hasMatch()) {
        return {};
    }
    return { m.captured(1).toInt(), m.captured(2).toInt(), m.captured(3).toInt() };
}

bool isNewer(const QString& candidate, const QString& current)
{
    const auto a = parseVersion(candidate);
    const auto b = parseVersion(current);
    if (a.isEmpty() || b.isEmpty()) {
        return false;
    }
    return std::lexicographical_compare(b.begin(), b.end(), a.begin(), a.end());
}

std::optional<Release> parseRelease(const QByteArray& json, QString* error)
{
    auto fail = [error](const QString& why) -> std::optional<Release> {
        if (error) {
            *error = why;
        }
        return std::nullopt;
    };

    QJsonParseError parseError;
    const auto doc = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        return fail(QStringLiteral("The update server sent something that is not valid JSON."));
    }
    const auto obj = doc.object();
    if (obj.value(QStringLiteral("draft")).toBool() || obj.value(QStringLiteral("prerelease")).toBool()) {
        return fail(QStringLiteral("The newest release is a draft or prerelease."));
    }

    Release r;
    r.tag = obj.value(QStringLiteral("tag_name")).toString();
    r.name = obj.value(QStringLiteral("name")).toString();
    r.notes = obj.value(QStringLiteral("body")).toString();
    r.htmlUrl = obj.value(QStringLiteral("html_url")).toString();
    if (parseVersion(r.tag).isEmpty()) {
        return fail(QStringLiteral("The release tag '%1' is not a version like v1.2.3.").arg(r.tag));
    }
    const auto v = parseVersion(r.tag);
    r.version = QStringLiteral("%1.%2.%3").arg(v[0]).arg(v[1]).arg(v[2]);

    for (const auto& item : obj.value(QStringLiteral("assets")).toArray()) {
        const auto a = item.toObject();
        Asset asset;
        asset.name = a.value(QStringLiteral("name")).toString();
        asset.url = a.value(QStringLiteral("browser_download_url")).toString();
        asset.size = static_cast<qint64>(a.value(QStringLiteral("size")).toDouble());
        const auto digest = a.value(QStringLiteral("digest")).toString();
        if (digest.startsWith(QStringLiteral("sha256:"), Qt::CaseInsensitive)) {
            asset.sha256 = digest.mid(7).toLower();
        }
        if (!asset.name.isEmpty() && !asset.url.isEmpty()) {
            r.assets.append(asset);
        }
    }
    return r;
}

QString platformKey()
{
    QString os;
#if defined(Q_OS_WIN)
    os = QStringLiteral("windows");
#elif defined(Q_OS_MACOS)
    os = QStringLiteral("macos");
#else
    os = QStringLiteral("linux");
#endif
    QString arch = QSysInfo::currentCpuArchitecture();
    if (arch == QStringLiteral("x86_64") || arch == QStringLiteral("amd64")) {
        arch = QStringLiteral("x86_64");
    } else if (arch == QStringLiteral("arm64") || arch == QStringLiteral("aarch64")) {
        arch = QStringLiteral("arm64");
    }
    return os + QStringLiteral("-") + arch;
}

std::optional<Asset> pickAsset(const Release& release, const QString& key, const QString& extension)
{
    for (const auto& a : release.assets) {
        const auto n = a.name.toLower();
        if (n.endsWith(QStringLiteral(".zsync")) || !n.endsWith(extension.toLower())) {
            continue;
        }
        if (n.contains(key.toLower())) {
            return a;
        }
    }
    return std::nullopt;
}

QString findChecksum(const QString& sumsText, const QString& fileName)
{
    for (const auto& line : sumsText.split('\n')) {
        const auto parts = line.simplified().split(' ');
        if (parts.size() < 2) {
            continue;
        }
        QString name = parts.mid(1).join(' ');
        if (name.startsWith('*')) {
            name = name.mid(1);
        }
        if (name == fileName && parts[0].size() == 64) {
            return parts[0].toLower();
        }
    }
    return {};
}

bool verifySha256(const QString& filePath, const QString& expectedHex)
{
    if (expectedHex.size() != 64) {
        return false;
    }
    QFile f(filePath);
    if (!f.open(QIODevice::ReadOnly)) {
        return false;
    }
    QCryptographicHash h(QCryptographicHash::Sha256);
    if (!h.addData(&f)) {
        return false;
    }
    return QString::fromLatin1(h.result().toHex()).compare(expectedHex, Qt::CaseInsensitive) == 0;
}

bool canSelfUpdate(const QString& installDir, const QString& dataDir, bool portable, QString* why)
{
    auto no = [why](const QString& reason) {
        if (why) {
            *why = reason;
        }
        return false;
    };
#if defined(Q_OS_LINUX)
    if (installDir.startsWith(QStringLiteral("/tmp/.mount_"))) {
        return no(QStringLiteral("This is an AppImage; it cannot replace itself."));
    }
#endif
    if (portable) {
        return no(QStringLiteral("This is a portable install (your data lives next to the program), so it must be updated by hand."));
    }
    // canonicalPath() is empty for a folder that does not exist yet; fall back to the cleaned absolute path
    // so "data inside the install dir" is still caught (that is the check that protects user data)
    auto resolved = [](const QString& p) {
        const auto c = QDir(p).canonicalPath();
        return c.isEmpty() ? QDir::cleanPath(QDir(p).absolutePath()) : c;
    };
    const auto canonInst = resolved(installDir);
    const auto canonData = resolved(dataDir);
    if (!canonData.isEmpty() && (canonData == canonInst || canonData.startsWith(canonInst + QLatin1Char('/')))) {
        return no(QStringLiteral("Your data is stored inside the install folder, so replacing it could delete your data."));
    }
    const QFileInfo inst(installDir);
    if (!inst.isDir() || !inst.isWritable() || !QFileInfo(inst.absolutePath()).isWritable()) {
        return no(QStringLiteral("%1 is not writable by your user (a system-wide install). Reinstall per-user, or update from the download page.")
                      .arg(installDir));
    }
#if defined(Q_OS_WIN)
    // Windows stages the new version in a sibling folder next to the install, so the
    // containing folder must be writable too (Program Files is not, without admin).
    const QFileInfo parentDir(QFileInfo(inst.absoluteFilePath()).absolutePath());
    if (!parentDir.isWritable()) {
        return no(QStringLiteral("The folder that contains your install (%1) is not writable by your user, so the update cannot be staged next to it.")
                      .arg(parentDir.absoluteFilePath()));
    }
#endif
    return true;
}

bool extractTarGz(const QString& archive, const QString& destDir, QString* error)
{
    QDir().mkpath(destDir);
    QProcess p;
    p.start(QStringLiteral("tar"), { QStringLiteral("-xzf"), archive, QStringLiteral("-C"), destDir, QStringLiteral("--strip-components=1") });
    if (!p.waitForStarted(5000)) {
        if (error) {
            *error = QStringLiteral("Could not run 'tar': %1").arg(p.errorString());
        }
        return false;
    }
    if (!p.waitForFinished(180000) || p.exitStatus() != QProcess::NormalExit || p.exitCode() != 0) {
        if (error) {
            *error = QStringLiteral("Extracting the update failed: %1").arg(QString::fromUtf8(p.readAllStandardError()).trimmed());
        }
        return false;
    }
    return true;
}

bool validateStaged(const QString& stagedDir, const QString& exeName, QString* error)
{
    const QFileInfo exe(QDir(stagedDir).filePath(exeName));
    if (!exe.isFile() || !exe.isExecutable()) {
        if (error) {
            *error = QStringLiteral("The downloaded update does not contain a runnable %1.").arg(exeName);
        }
        return false;
    }
    return true;
}

bool swapInstall(const QString& installDir, const QString& stagedDir, QString* error)
{
    auto fail = [error](const QString& why) {
        if (error) {
            *error = why;
        }
        return false;
    };
    const QString oldDir = installDir + QStringLiteral(".update-old");
    if (QFileInfo::exists(oldDir) && !QDir(oldDir).removeRecursively()) {
        return fail(QStringLiteral("Could not remove a stale folder: %1").arg(oldDir));
    }
    if (!QDir().rename(installDir, oldDir)) {
        return fail(QStringLiteral("Could not move the current install aside."));
    }
    if (!QDir().rename(stagedDir, installDir)) {
        // put the working install back before reporting failure
        if (!QDir().rename(oldDir, installDir)) {
            return fail(QStringLiteral("Update failed AND the rollback failed. Your install is at %1").arg(oldDir));
        }
        return fail(QStringLiteral("Could not move the new version into place; nothing was changed."));
    }
    return true;
}

void cleanupLeftovers(const QString& installDir)
{
    for (const auto* suffix : { ".update-old", ".update-new" }) {
        const QString d = installDir + QString::fromLatin1(suffix);
        if (QFileInfo::exists(d)) {
            QDir(d).removeRecursively();
        }
    }
}

QString pendingUpdateMarkerName()
{
    return QStringLiteral("PENDING-BLAMDOM-UPDATE");
}

namespace {

// Minimal zip container reader (central-directory based). Enough for the release
// archives we ship: per-entry deflate or stored, no zip64, no encryption.
struct ZipEntry {
    QString name;  // normalized to '/' separators
    quint16 method = 0;
    quint32 compSize = 0;
    quint32 uncompSize = 0;
    quint32 localOffset = 0;
};

inline quint16 zipU16(const QByteArray& d, int off)
{
    return static_cast<quint16>(static_cast<quint8>(d.at(off)) | (static_cast<quint8>(d.at(off + 1)) << 8));
}

inline quint32 zipU32(const QByteArray& d, int off)
{
    return static_cast<quint32>(static_cast<quint8>(d.at(off)) | (static_cast<quint8>(d.at(off + 1)) << 8)
                                | (static_cast<quint8>(d.at(off + 2)) << 16) | (static_cast<quint8>(d.at(off + 3)) << 24));
}

bool readZipEntries(const QByteArray& data, QList<ZipEntry>& out, QString* error)
{
    auto fail = [error](const QString& msg) {
        if (error) {
            *error = msg;
        }
        return false;
    };
    // End of central directory record: scan back from the end (22 bytes + any comment).
    int eocd = -1;
    const int minPos = data.size() > 22 ? data.size() - 22 - 65536 : 0;
    const auto sig = QByteArray::fromHex("504b0506");
    for (int i = data.size() - 22; i >= minPos && i >= 0; --i) {
        if (data.mid(i, 4) == sig) {
            eocd = i;
            break;
        }
    }
    if (eocd < 0) {
        return fail(QStringLiteral("Not a zip archive."));
    }
    // EOCD layout: sig(4) disk(2) cdDisk(2) countThisDisk(2) countTotal(2) cdSize(4) cdOffset(4) commentLen(2)
    const quint16 count = zipU16(data, eocd + 10);
    const quint32 cdOffset = zipU32(data, eocd + 16);
    if (count == 0xFFFFu || cdOffset == 0xFFFFFFFFu) {
        return fail(QStringLiteral("Zip64 archives are not supported."));
    }
    int pos = static_cast<int>(cdOffset);
    const auto cdSig = QByteArray::fromHex("504b0102");
    for (quint32 i = 0; i < count; ++i) {
        if (pos < 0 || pos + 46 > data.size() || data.mid(pos, 4) != cdSig) {
            return fail(QStringLiteral("Corrupt central directory."));
        }
        ZipEntry e;
        e.method = zipU16(data, pos + 10);
        e.compSize = zipU32(data, pos + 20);
        e.uncompSize = zipU32(data, pos + 24);
        e.localOffset = zipU32(data, pos + 42);
        if (e.compSize == 0xFFFFFFFFu || e.uncompSize == 0xFFFFFFFFu || e.localOffset == 0xFFFFFFFFu) {
            return fail(QStringLiteral("Zip64 archives are not supported."));
        }
        const quint16 nameLen = zipU16(data, pos + 28);
        const quint16 extraLen = zipU16(data, pos + 30);
        const quint16 commentLen = zipU16(data, pos + 32);
        if (pos + 46 + nameLen > data.size()) {
            return fail(QStringLiteral("Corrupt archive."));
        }
        e.name = QString::fromUtf8(data.mid(pos + 46, nameLen));
        e.name.replace('\\', '/');  // some writers (e.g. PowerShell) use backslashes
        if (e.name.isEmpty()) {
            return fail(QStringLiteral("Archive contains an unnamed entry."));
        }
        out.append(e);
        pos += 46 + nameLen + extraLen + commentLen;
    }
    return true;
}

bool zipEntryData(const QByteArray& data, const ZipEntry& e, QByteArray& out, QString* error)
{
    auto fail = [error](const QString& msg) {
        if (error) {
            *error = msg;
        }
        return false;
    };
    const int lh = static_cast<int>(e.localOffset);
    if (lh < 0 || lh + 30 > data.size() || data.mid(lh, 4) != QByteArray::fromHex("504b0304")) {
        return fail(QStringLiteral("Corrupt local file header."));
    }
    const int start = lh + 30 + zipU16(data, lh + 26) + zipU16(data, lh + 28);
    if (start < 0 || start + static_cast<int>(e.compSize) > data.size()) {
        return fail(QStringLiteral("Corrupt archive (entry data out of bounds)."));
    }
    const QByteArray payload = data.mid(start, e.compSize);
    if (e.method == 0) {  // stored
        out = payload;
    } else if (e.method == 8) {  // deflate
        z_stream strm {};
        if (inflateInit2(&strm, -15) != Z_OK) {  // -15: raw deflate stream (no zlib header)
            return fail(QStringLiteral("Could not initialize decompression."));
        }
        strm.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(payload.constData()));
        strm.avail_in = static_cast<uInt>(payload.size());
        QByteArray inflated;
        char buf[32768];
        int rc = Z_OK;
        do {
            strm.next_out = reinterpret_cast<Bytef*>(buf);
            strm.avail_out = sizeof(buf);
            rc = inflate(&strm, Z_NO_FLUSH);
            if (rc != Z_OK && rc != Z_STREAM_END && rc != Z_BUF_ERROR) {
                inflateEnd(&strm);
                return fail(QStringLiteral("Could not decompress %1.").arg(e.name));
            }
            inflated.append(buf, static_cast<int>(sizeof(buf) - strm.avail_out));
        } while (rc != Z_STREAM_END);
        inflateEnd(&strm);
        if (static_cast<quint32>(inflated.size()) != e.uncompSize) {
            return fail(QStringLiteral("Decompressed size mismatch for %1.").arg(e.name));
        }
        out = inflated;
    } else {
        return fail(QStringLiteral("Unsupported compression method %1 in %2.").arg(e.method).arg(e.name));
    }
    return true;
}

}  // namespace

bool extractZip(const QString& archive, const QString& destDir, QString* error)
{
    auto fail = [error](const QString& msg) {
        if (error) {
            *error = msg;
        }
        return false;
    };
    QFile f(archive);
    if (!f.open(QIODevice::ReadOnly)) {
        return fail(QStringLiteral("Could not open the update archive: %1").arg(f.errorString()));
    }
    const QByteArray data = f.readAll();
    QList<ZipEntry> entries;
    if (!readZipEntries(data, entries, error)) {
        return false;
    }
    // Our zips (like the release zips) have exactly one top-level folder; find it.
    QString top;
    for (const auto& e : entries) {
        const int slash = e.name.indexOf('/');
        if (slash < 0) {
            if (!e.name.endsWith('/')) {
                return fail(QStringLiteral("The update archive has files outside a top-level folder."));
            }
            continue;
        }
        const QString first = e.name.left(slash);
        if (top.isEmpty()) {
            top = first;
        } else if (first != top) {
            return fail(QStringLiteral("The update archive has more than one top-level folder."));
        }
    }
    if (top.isEmpty()) {
        return fail(QStringLiteral("The update archive is empty."));
    }
    QDir dest(destDir);
    for (const auto& e : entries) {
        const int slash = e.name.indexOf('/');
        if (slash < 0) {
            continue;  // the top-level folder entry itself
        }
        const QString name = e.name.mid(slash + 1);
        if (name.isEmpty() || name.contains(QLatin1String(".."))) {
            return fail(QStringLiteral("The update archive contains an invalid path."));
        }
        const QString target = destDir + QLatin1Char('/') + name;
        if (e.name.endsWith('/')) {
            dest.mkpath(target);
            continue;
        }
        QByteArray content;
        if (!zipEntryData(data, e, content, error)) {
            return false;
        }
        QDir().mkpath(QFileInfo(target).absolutePath());
        QFile out(target);
        if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            return fail(QStringLiteral("Could not write %1.").arg(name));
        }
        out.write(content);
    }
    return true;
}

static bool copyRecursive(const QString& srcDir, const QString& dstDir, QString* error)
{
    const QDir src(srcDir);
    for (const auto& entry : src.entryList(QDir::AllEntries | QDir::NoDotAndDotDot, QDir::DirsFirst)) {
        if (entry == pendingUpdateMarkerName()) {
            continue;  // never carry the marker into the canonical folder
        }
        const QString s = src.filePath(entry);
        const QString d = QDir(dstDir).filePath(entry);
        if (QFileInfo(s).isDir()) {
            QDir(d).mkpath(QStringLiteral("."));
            if (!copyRecursive(s, d, error)) {
                return false;
            }
        } else {
            if (QFile::exists(d) && !QFile::remove(d)) {
                if (error) {
                    *error = QStringLiteral("Could not replace %1").arg(d);
                }
                return false;
            }
            if (!QFile::copy(s, d)) {
                if (error) {
                    *error = QStringLiteral("Could not copy %1 to %2").arg(s, d);
                }
                return false;
            }
        }
    }
    return true;
}

bool copyIntoDir(const QString& srcDir, const QString& dstDir, QString* error)
{
    if (!QDir(dstDir).exists() && !QDir(dstDir).mkpath(dstDir)) {
        if (error) {
            *error = QStringLiteral("Could not create %1").arg(dstDir);
        }
        return false;
    }
    return copyRecursive(srcDir, dstDir, error);
}

UpdateBootstrap applyPendingUpdate(const QString& installDir, const QString& exeName, QString* relaunchExe, QString* error)
{
    const QString markerPath = QDir(installDir).filePath(pendingUpdateMarkerName());
    if (QFile::exists(markerPath)) {
        // We are the staged instance: move into the canonical folder now that the old
        // version's files are no longer in use.
        QString oldDir;
        {
            QFile m(markerPath);
            if (m.open(QIODevice::ReadOnly)) {
                oldDir = QString::fromUtf8(m.readLine()).trimmed();
            }
        }
        if (!oldDir.isEmpty() && QFileInfo(oldDir).isDir()) {
            const QString oldBackup = oldDir + QStringLiteral(".update-old");
            if (QFileInfo::exists(oldBackup)) {
                QDir(oldBackup).removeRecursively();
            }
            // Renaming a folder works even while a process still runs from it, but a
            // shutting-down instance may keep failing; retry briefly.
            bool movedOld = false;
            for (int attempt = 0; attempt < 5 && !movedOld; ++attempt) {
                if (attempt > 0) {
                    QThread::msleep(1500);
                }
                movedOld = QDir().rename(oldDir, oldBackup);
            }
            if (!movedOld) {
                if (error) {
                    *error = QStringLiteral("The old install folder %1 could not be moved aside (is an older version still running?). The update will be retried on the next start.")
                                    .arg(oldDir);
                }
                return UpdateBootstrap::Failed;
            }
            if (!QDir().rename(installDir, oldDir)) {
                QDir().rename(oldBackup, oldDir);  // roll the old version back into place
                if (error) {
                    *error = QStringLiteral("Could not move the new version into %1; the old version was restored and the update will be retried on the next start.")
                                    .arg(oldDir);
                }
                return UpdateBootstrap::Failed;
            }
            // Best effort: remove the old version. Its files stay locked until the old
            // process is really gone; whatever is left is cleaned on the next start.
            for (int attempt = 0; attempt < 5; ++attempt) {
                if (!QFileInfo::exists(oldBackup) || QDir(oldBackup).removeRecursively()) {
                    break;
                }
                QThread::msleep(1000);
            }
            if (QFileInfo::exists(oldBackup)) {
                qWarning() << "Old install folder left behind at" << oldBackup << "- it will be removed on the next start";
            }
        }
        // After a successful rename the marker lives in the (now canonical) folder
        QFile::remove(QDir(oldDir.isEmpty() ? installDir : oldDir).filePath(pendingUpdateMarkerName()));
        qInfo() << "Self-update applied; the install is now at" << (oldDir.isEmpty() ? installDir : oldDir);
        return UpdateBootstrap::Completed;
    }
    // Canonical instance: look for a staged sibling.
    const QString sibling = installDir + QStringLiteral(".update");
    if (QFileInfo::exists(sibling)) {
        if (QFile::exists(QDir(sibling).filePath(pendingUpdateMarkerName()))) {
            if (relaunchExe) {
                *relaunchExe = QDir(sibling).filePath(exeName);
            }
            return UpdateBootstrap::RelaunchNeeded;
        }
        QDir(sibling).removeRecursively();  // partial stage from an interrupted update
    }
    return UpdateBootstrap::Nothing;
}

bool relaunchLater(const QString& exePath, int delaySeconds, QString* error)
{
    QString script;
    QString process;
    QStringList processArgs;
#if defined(Q_OS_WIN)
    const QString dir = QFileInfo(exePath).absolutePath();
    script = QStringLiteral("@echo off\r\ncd /d \"%1\"\r\ntimeout /t %2 /nobreak >nul\r\nstart \"\" \"%3\"\r\n")
                 .arg(dir)
                 .arg(delaySeconds)
                 .arg(exePath);
    process = QStringLiteral("cmd");
    processArgs = { QStringLiteral("/c") };
#else
    script = QStringLiteral("sleep %1; exec \"%2\"\n").arg(delaySeconds).arg(exePath);
    process = QStringLiteral("/bin/sh");
#endif
    auto* f = new QTemporaryFile(QStringLiteral("blamdom-relaunch-XXXXXX.") + (process == "cmd" ? QStringLiteral("bat") : QStringLiteral("sh")));
    if (!f->open()) {
        if (error) {
            *error = f->errorString();
        }
        f->deleteLater();
        return false;
    }
    if (f->write(script.toUtf8()) < 0) {
        if (error) {
            *error = QStringLiteral("Could not write the relaunch script.");
        }
        f->remove();
        f->deleteLater();
        return false;
    }
    const QString path = f->fileName();
    f->setAutoRemove(false);
    f->deleteLater();  // the file itself stays until the OS cleans the temp dir
    if (!QProcess::startDetached(process, processArgs + QStringList{ path })) {
        if (error) {
            *error = QStringLiteral("Could not start the relaunch script %1").arg(path);
        }
        QFile::remove(path);
        return false;
    }
    return true;
}

}  // namespace BlamdomUpdate
