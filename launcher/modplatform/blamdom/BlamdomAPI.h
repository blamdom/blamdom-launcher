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
#include <QJsonObject>
#include <QList>
#include <QRegularExpression>
#include <QString>
#include <QUrl>

#include <algorithm>

#include "Json.h"
#include "tasks/Task.h"

#include "modplatform/packwiz/Packwiz.h"

class Resource;
class QWidget;

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
//! Detected, in order, from: (1) the copy of the pack index the Modrinth
//! import keeps inside the instance (mrpack/modrinth.index.json, or at the
//! instance root for older installs), (2) the instance's own managed-pack
//! settings (written by every import), (3) the instance's display name
//! matching a live pack (old or foreign installs; the version is then
//! unknown, but the in-place update works without it).
struct InstalledPack {
    QString instanceId;
    QString instanceName;
    QString packName;
    QString installedVersion;  //!< may be empty (name-only match)
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
    //! (Inline on purpose: the catalog self-test links this without the rest
    //! of BlamdomAPI.cpp.)
    static QList<Mod> parseMods(const QByteArray& json)
    {
        QList<Mod> mods;

        auto doc = Json::requireDocument(json);
        if (!doc) {
            qWarning() << "Blamdom: malformed mods.json:" << doc.error();
            return mods;
        }

        const auto arr = doc->object()["mods"].toArray();
        for (const auto& v : arr) {
            const auto o = v.toObject();
            Mod m;
            m.name = o["name"].toString();
            m.id = o["id"].toString();
            m.version = o["version"].toString();
            m.minecraft = o["minecraft"].toString();
            m.loader = o["loader"].toString();
            m.side = o["side"].toString();
            m.description = o["description"].toString();
            m.download = o["download"].toString();
            m.sha256 = o["sha256"].toString();
            m.sizeBytes = o["size_bytes"].toVariant().toLongLong();
            m.released = o["released"].toString();
            if (!m.name.isEmpty() && m.download.startsWith(QStringLiteral("https://"))) {
                mods.append(m);
            }
        }
        return mods;
    }

    //! Scan local instances for imports of one of our live packs. Detection
    //! falls back through the index file, the instance's managed-pack
    //! settings, and the instance name — so old installs (different index
    //! location or missing) and hand-installed packs are still recognized.
    static QList<InstalledPack> findInstalled(const QList<Pack>& livePacks);

    //! Canonical form of a pack name for matching: trimmed, lowercased, with
    //! a trailing " (working title)" dropped (early builds used it, so old
    //! installs keep matching the same pack after the name settled).
    //! Pure — lives in the header so the catalog selftest can link it.
    static QString normalizePackName(const QString& name)
    {
        static const QRegularExpression s_title(QStringLiteral("\\s*\\(working title\\)$"), QRegularExpression::CaseInsensitiveOption);
        return name.trimmed().replace(s_title, QString()).trimmed().toLower();
    }

    //! The (name, versionId) pair a modrinth.index.json body carries. Returns
    //! false if the object has no usable pack name; versionId may
    //! legitimately be empty (hand-made indexes). Pure — unit-tested in
    //! tests/blamdom-catalog.
    static bool indexIdentity(const QJsonObject& o, QString& name, QString& versionId)
    {
        const QString n = o.value("name").toString().trimmed();
        if (n.isEmpty()) {
            return false;
        }
        name = n;
        versionId = o.value("versionId").toString().trimmed();
        return true;
    }

    // ------------------------------------------------------------------
    // Mod catalog matching — pure logic, no I/O, unit-tested in
    // tests/blamdom-catalog.
    // ------------------------------------------------------------------

    //! Version part of a "mod file name": "<id>-<version>.jar" (optionally with
    //! a trailing ".disabled"), lowercased. Returns the version
    //! (e.g. "0.1.7"), or empty if the name is not of that shape.
    static QString modFileVersion(const QString& modId, const QString& fileName)
    {
        if (modId.isEmpty()) {
            return {};
        }
        const QString lower = fileName.toLower();
        const QString prefix = modId.toLower() + QLatin1Char('-');
        if (!lower.startsWith(prefix)) {
            return {};
        }
        QString rest = lower.mid(prefix.size());
        if (rest.endsWith(QStringLiteral(".disabled"))) {
            rest.chop(QStringLiteral(".disabled").size());
        }
        // Only jars: the mods folder can hold other files that must never
        // false-positive against one of our catalog ids.
        if (!rest.endsWith(QStringLiteral(".jar"))) {
            return {};
        }
        rest.chop(QStringLiteral(".jar").size());
        if (rest.isEmpty()) {
            return {};
        }
        // Version-ish: digits, letters, '.', '-', '+' (e.g. 0.1.7, 1.0-beta).
        for (const QChar c : rest) {
            if (!(c.isDigit() || c.isLetter() || c == QLatin1Char('.') || c == QLatin1Char('-') || c == QLatin1Char('+'))) {
                return {};
            }
        }
        return rest;
    }

    //! Match a mod file name to a catalog entry: the (lowercased) file name
    //! must start with "<id>-" for exactly one entry — an id that is a prefix
    //! of a longer id can never match, because the '-' has to come right after
    //! the id — and the remainder must look like a version.
    //! Returns the matching entry, or nullptr.
    static const Mod* matchModFile(const QList<Mod>& mods, const QString& fileName)
    {
        if (fileName.isEmpty()) {
            return nullptr;
        }
        for (const auto& m : mods) {
            if (modFileVersion(m.id, fileName).isEmpty()) {
                continue;
            }
            return &m;
        }
        return nullptr;
    }

    //! Segment-wise numeric version comparison: isNewerVersion("0.1.7", "0.1.1") is
    //! true, ("0.10.0", "0.9.0") is true, ("1.0", "1.0-beta") is true (a
    //! non-numeric tail segment is a prerelease marker and sorts lower),
    //! ("1.0.1", "1.0") is true. Equal or unparseable inputs are never "newer".
    static bool isNewerVersion(const QString& candidate, const QString& installed)
    {
        auto split = [](const QString& v) { return v.split(QRegularExpression(QStringLiteral("[.-]")), Qt::SkipEmptyParts); };
        auto isNum = [](const QString& s) { return !s.isEmpty() && std::all_of(s.begin(), s.end(), [](QChar c) { return c.isDigit(); }); };

        const auto a = split(candidate);
        const auto b = split(installed);
        for (qsizetype i = 0; i < qMax<qsizetype>(a.size(), b.size()); ++i) {
            const QString sa = i < a.size() ? a[i] : QString();
            const QString sb = i < b.size() ? b[i] : QString();
            const bool an = isNum(sa);
            const bool bn = isNum(sb);
            if (an && bn) {
                const long long x = sa.toLongLong();
                const long long y = sb.toLongLong();
                if (x != y) {
                    return x > y;
                }
                continue;
            }
            if (an != bn) {
                // A numeric segment is newer than a prerelease tag or a missing one:
                // 1.0.1 > 1.0, 1.0-1 > 1.0-beta.
                return an;
            }
            if (sa != sb) {
                // Both non-numeric: a missing segment beats a prerelease tag
                // (1.0 > 1.0-beta), otherwise plain lexicographic.
                if (sa.isEmpty()) {
                    return true;
                }
                if (sb.isEmpty()) {
                    return false;
                }
                return sa > sb;
            }
        }
        return false;
    }

    //! True if the URL points at our own host over https (the only place we
    //! claim to host files from).
    static bool isProdHost(const QUrl& url) { return url.scheme() == QLatin1String("https") && url.host() == QUrl(PROD_URL).host(); }

    //! Build a .pw.toml metadata entry for one of our mods (provider = BLAMDOM).
    //! `fileName` is the file name as it sits in the instance's mods folder.
    static Packwiz::V1::Mod buildModMetadata(const Mod& mod, const QString& fileName)
    {
        using namespace ModPlatform;
        Packwiz::V1::Mod meta;
        meta.slug = mod.id;
        meta.name = mod.name;
        meta.filename = fileName;
        meta.mode = QStringLiteral("url");
        meta.url = QUrl(mod.download);
        meta.hashFormat = QStringLiteral("sha256");
        meta.hash = mod.sha256;
        meta.provider = ResourceProvider::BLAMDOM;
        meta.projectId = mod.id;
        meta.fileId = mod.version;  // the catalog version at import/update time
        meta.versionNumber = mod.version;
        meta.side = mod.side == QStringLiteral("client")   ? SideType::ClientSide
                    : mod.side == QStringLiteral("server") ? SideType::ServerSide
                                                           : SideType::UniversalSide;
        meta.loaders = mod.loader == QStringLiteral("quilt") ? ModLoaderTypes(ModLoaderType::Quilt) : ModLoaderTypes(ModLoaderType::Fabric);
        if (!mod.minecraft.isEmpty()) {
            meta.mcVersions = { mod.minecraft };
        }
        meta.releaseType = IndexedVersionType::Release;
        meta.lockUpdate = false;
        return meta;
    }

    // ------------------------------------------------------------------
    // Local resolution — writes metadata next to mod files, keeps the
    // in-memory catalog cache. Callers: pack import, the Blamdom section,
    // and the update-check dialog (which never prompts for our own mods).
    // ------------------------------------------------------------------

    //! The catalog currently held in memory (empty until one of the fetchers runs).
    static const QList<Mod>& cachedMods();
    //! A non-empty catalog was parsed at least once.
    static bool catalogIsLoaded();
    //! A catalog fetch was attempted at least once (so a failed fetch is not retried in a tight loop).
    static bool catalogAttempted();
    //! Parse a /mods.json response body and install it as the in-memory catalog.
    static void setCachedMods(const QByteArray& json);
    //! Fetch /mods.json and block until it is done (bounded by a timeout, UI kept
    //! responsive in a progress dialog) — for synchronous flows like the
    //! update-check dialog. Returns the parsed list (empty on failure).
    static QList<Mod> fetchModsSync(QWidget* parent);

    //! Same, for /modpacks.json — the instance context menu's update flow.
    static QList<Pack> fetchPacksSync(QWidget* parent);

    //! Write `meta` as "<slug>.pw.toml" in `indexDir` (the mod folder's .index dir).
    static void writeModMetadata(const QDir& indexDir, Packwiz::V1::Mod& meta);

    //! If `resource`'s file name matches the in-memory catalog, write BLAMDOM
    //! metadata for it into `indexDir`, attach the metadata to the resource,
    //! and return true. Returns false for files that are not one of ours —
    //! callers then keep the stock provider flow.
    static bool resolveCandidate(Resource* resource, const QDir& indexDir);
};

}  // namespace Blamdom
