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

/// The staged folder must contain an executable named exeName.
bool validateStaged(const QString& stagedDir, const QString& exeName, QString* error);

/// installDir -> installDir + ".update-old", stagedDir -> installDir. Rolls back if the
/// second step fails, so a failure never leaves the user without a working install.
bool swapInstall(const QString& installDir, const QString& stagedDir, QString* error);

/// Remove leftovers (".update-old", ".update-new") next to installDir.
void cleanupLeftovers(const QString& installDir);

}  // namespace BlamdomUpdate
