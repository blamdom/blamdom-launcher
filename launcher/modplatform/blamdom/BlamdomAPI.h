// SPDX-License-Identifier: GPL-3.0-only
/*
 *  Blamdom Launcher - Minecraft Launcher
 *  A fork of Prism Launcher with the Blamdom section: our packs, our mods, our launcher.
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

#include "tasks/Task.h"

namespace Blamdom {

//! One entry from /modpacks.json
struct Pack {
    QString name;
    QString description;
    QString minecraft;
    QString loader;
    QString version;
    QString format;
    QString download;
    QString sha256;
    qint64 sizeBytes = 0;
    QString released;
};

//! A pack of ours installed locally.
//!
//! Detected from the copy of the pack index the stock Modrinth import keeps
//! inside every imported instance (mrpack/modrinth.index.json) — its `name`
//! and `versionId` fields are ours, so no extra bookkeeping is needed to know
//! which local instances came from Blamdom.
struct InstalledPack {
    QString instanceId;
    QString instanceName;
    QString packName;
    QString installedVersion;
};

//! One entry from /mods.json
struct Mod {
    QString name;
    QString id;
    QString version;
    QString minecraft;
    QString loader;
    QString side;
    QString description;
    QString download;
    QString sha256;
    qint64 sizeBytes = 0;
    QString released;
};

/**
 * Blamdom manifest API — the single source of truth for the Blamdom section.
 *
 * Manifests are served by our own host (no third party), fetched fresh on
 * every open of the page, and every file is sha256-pinned in the manifest.
 */
class BlamdomAPI final {
   public:
    //! Base URL of the Blamdom hub.
    static const QString PROD_URL;

    //! GET /modpacks.json — task plus response buffer (owned by the task's job).
    static std::pair<Task::Ptr, QByteArray*> fetchPacks();

    //! GET /mods.json — task plus response buffer (owned by the task's job).
    static std::pair<Task::Ptr, QByteArray*> fetchMods();

    //! Parse a /modpacks.json body. Returns an empty list on malformed input.
    static QList<Pack> parsePacks(const QByteArray& json);

    //! Parse a /mods.json body. Returns an empty list on malformed input.
    static QList<Mod> parseMods(const QByteArray& json);

    //! Scan local instances for imports of one of our live packs (matched by
    //! the pack name stored in the instance's kept copy of the pack index).
    static QList<InstalledPack> findInstalled(const QList<Pack>& livePacks);

    //! Canonical form of a pack name for matching: trimmed, lowercased, with
    //! a trailing " (working title)" dropped (early builds used it, so old
    //! installs keep matching the same pack after the name settled).
    static QString normalizePackName(const QString& name);
};

}  // namespace Blamdom
