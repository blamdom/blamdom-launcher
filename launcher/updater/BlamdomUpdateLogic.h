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

#pragma once

#include <QByteArray>
#include <QList>
#include <QString>
#include <optional>

// Pure logic for the Blamdom self-updater (no GUI, no Application dependency), kept
// separate so it can be tested on its own.
namespace BlamdomUpdate {

struct Asset {
    QString name;
    QString url;
    qint64 size = 0;
    QString sha256;  // from the GitHub "digest" field when present
};

struct Release {
    QString tag;      // e.g. "v0.2.0"
    QString version;  // e.g. "0.2.0"
    QString name;
    QString notes;
    QString htmlUrl;
    QList<Asset> assets;
};

/// "v1.2.3" / "1.2.3-beta" -> {1,2,3}; empty list if the text is not a version.
QList<int> parseVersion(const QString& text);

/// True only when both parse and candidate is strictly greater than current.
bool isNewer(const QString& candidate, const QString& current);

/// Parse a GitHub "latest release" API reply. Drafts and prereleases are rejected.
std::optional<Release> parseRelease(const QByteArray& json, QString* error = nullptr);

/// Platform key used in release asset names, e.g. "linux-x86_64".
QString platformKey();

/// First archive asset whose name contains platformKey and ends with extension (".tar.gz").
std::optional<Asset> pickAsset(const Release& release, const QString& platformKey, const QString& extension);

/// Look up fileName in the text of a SHA256SUMS file ("<hex>  <name>"). Empty if absent.
QString findChecksum(const QString& sumsText, const QString& fileName);

/// True when the file's sha256 equals expectedHex (case-insensitive).
bool verifySha256(const QString& filePath, const QString& expectedHex);

/// Whether this install can replace itself. False (with a reason) for read-only system
/// installs, AppImages, and portable installs whose user data lives inside the install dir.
bool canSelfUpdate(const QString& installDir, const QString& dataDir, bool portable, QString* why);

/// Extract a .tar.gz (stripping the single top-level folder) into destDir.
bool extractTarGz(const QString& archive, const QString& destDir, QString* error);

/// Extract a .zip (stripping the single top-level folder) into destDir. Windows builds.
/// Rejects archives without exactly one top-level folder and paths that try to escape destDir.
bool extractZip(const QString& archive, const QString& destDir, QString* error);

/// Recursively copy every entry of srcDir into dstDir, overwriting existing files.
/// The pending-update marker is never copied. Used to install a Windows update into the
/// folder the user's shortcuts point at.
bool copyIntoDir(const QString& srcDir, const QString& dstDir, QString* error);

/// The staged folder must contain an executable named exeName.
bool validateStaged(const QString& stagedDir, const QString& exeName, QString* error);

/// installDir -> installDir + ".update-old", stagedDir -> installDir. Rolls back if the
/// second step fails, so a failure never leaves the user without a working install.
bool swapInstall(const QString& installDir, const QString& stagedDir, QString* error);

/// Remove leftovers (".update-old", ".update-new") next to installDir.
void cleanupLeftovers(const QString& installDir);

/// Marker file kept inside a staged ".update" folder; line 1 is the pre-update install dir.
QString pendingUpdateMarkerName();

/// Result of the self-update bootstrap run at startup.
enum class UpdateBootstrap {
    Nothing,          // no pending update (a stale staged folder was removed if present)
    Completed,        // this process is the staged instance; the new version now owns the canonical folder
    RelaunchNeeded,   // a staged folder is pending; the caller should relaunch into `relaunchExe`
    Failed            // the pending update could not be applied yet; it is retried on the next start
};

/// Run at startup with the install dir this process is running from.
/// - Staged instance (running from "<dir>.update" with a marker): moves the old folder aside,
///   renames the staged folder into the canonical name, removes the marker. Returns Completed,
///   or Failed (the marker stays, so the next start retries).
/// - Canonical instance with a pending staged sibling: returns RelaunchNeeded with relaunchExe.
/// - Canonical instance with a stale staged sibling (no marker): removes it, returns Nothing.
UpdateBootstrap applyPendingUpdate(const QString& installDir, const QString& exeName, QString* relaunchExe, QString* error);

/// Write + detach a small script (bat on Windows, sh on Unix) that starts exePath after
/// `delaySeconds`, so the new process begins only after this one has released its files.
bool relaunchLater(const QString& exePath, int delaySeconds, QString* error);

}  // namespace BlamdomUpdate
