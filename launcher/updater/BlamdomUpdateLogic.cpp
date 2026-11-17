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
#if !defined(Q_OS_LINUX)
    Q_UNUSED(installDir)
    Q_UNUSED(dataDir)
    Q_UNUSED(portable)
    return no(QStringLiteral("Automatic installation is only available on Linux so far."));
#else
    const QFileInfo inst(installDir);
    if (installDir.startsWith(QStringLiteral("/tmp/.mount_"))) {
        return no(QStringLiteral("This is an AppImage; it cannot replace itself."));
    }
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
    if (!inst.isDir() || !inst.isWritable() || !QFileInfo(inst.absolutePath()).isWritable()) {
        return no(QStringLiteral("%1 is not writable by your user (a system-wide install). Reinstall per-user, or run the installer again.")
                      .arg(installDir));
    }
    return true;
#endif
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

}  // namespace BlamdomUpdate
