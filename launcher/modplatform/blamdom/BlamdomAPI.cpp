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

#include "BlamdomAPI.h"

#include <QDebug>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>

#include "Application.h"
#include "FileSystem.h"
#include "InstanceList.h"
#include "Json.h"
#include "QObjectPtr.h"
#include "net/NetJob.h"
#include "net/Request.h"
#include "ui/dialogs/ProgressDialog.h"

#include "minecraft/mod/Resource.h"

#include <QDir>
#include <QFile>
#include <QRegularExpression>

namespace Blamdom {

const QString BlamdomAPI::PROD_URL = QStringLiteral("https://mine.blamdom.dev");

std::pair<Task::Ptr, QByteArray*> BlamdomAPI::fetchPacks()
{
    auto job = makeShared<NetJob>("Blamdom::Packs", APPLICATION->network());
    auto url = QUrl(PROD_URL + QStringLiteral("/modpacks.json"));
    auto [action, response] = Net::Request::makeByteArray(url);
    job->addNetAction(action);
    return { Task::Ptr(job), response };
}

std::pair<Task::Ptr, QByteArray*> BlamdomAPI::fetchMods()
{
    auto job = makeShared<NetJob>("Blamdom::Mods", APPLICATION->network());
    auto url = QUrl(PROD_URL + QStringLiteral("/mods.json"));
    auto [action, response] = Net::Request::makeByteArray(url);
    job->addNetAction(action);
    return { Task::Ptr(job), response };
}

QList<Blamdom::Pack> BlamdomAPI::parsePacks(const QByteArray& json)
{
    QList<Pack> packs;

    auto doc = Json::requireDocument(json);
    if (!doc) {
        qWarning() << "Blamdom: malformed modpacks.json:" << doc.error();
        return packs;
    }

    const auto arr = doc->object()["packs"].toArray();
    for (const auto& v : arr) {
        const auto o = v.toObject();
        Pack p;
        p.name = o["name"].toString();
        p.description = o["description"].toString();
        p.minecraft = o["minecraft"].toString();
        p.loader = o["loader"].toString();
        p.version = o["version"].toString();
        p.format = o["format"].toString();
        p.download = o["download"].toString();
        p.sha256 = o["sha256"].toString();
        p.sizeBytes = o["size_bytes"].toVariant().toLongLong();
        p.released = o["released"].toString();
        if (!p.name.isEmpty() && p.download.startsWith(QStringLiteral("https://"))) {
            packs.append(p);
        }
    }
    return packs;
}

QList<Blamdom::InstalledPack> BlamdomAPI::findInstalled(const QList<Pack>& livePacks)
{
    QList<InstalledPack> out;
    if (livePacks.isEmpty()) {
        return out;
    }

    auto matchLive = [&livePacks](const QString& name) -> const Pack* {
        if (name.trimmed().isEmpty()) {
            return nullptr;
        }
        const auto norm = normalizePackName(name);
        for (const auto& live : livePacks) {
            if (normalizePackName(live.name) == norm) {
                return &live;
            }
        }
        return nullptr;
    };

    //! Read one copy of the pack index the import keeps in the instance.
    auto readIndexFile = [](const QString& path, QString& name, QString& versionId) {
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly)) {
            return false;
        }
        auto doc = QJsonDocument::fromJson(f.readAll());
        f.close();
        if (!doc.isObject()) {
            return false;
        }
        return indexIdentity(doc.object(), name, versionId);
    };

    auto* instances = APPLICATION->instances();
    for (int i = 0; i < instances->count(); ++i) {
        auto* inst = instances->at(i);
        if (!inst) {
            continue;
        }

        QString name;      // what the instance says it was installed from
        QString versionId; // may stay empty (then the update flow still works)

        // (1) The copy of the pack index kept inside the instance:
        //     mrpack/modrinth.index.json — with the instance root as the
        //     fallback location for very old installs.
        if (!readIndexFile(FS::PathCombine(inst->instanceRoot(), "mrpack", "modrinth.index.json"), name, versionId)) {
            readIndexFile(FS::PathCombine(inst->instanceRoot(), "modrinth.index.json"), name, versionId);
        }
        // (2) The instance's own managed-pack settings (written by every
        //     import the launcher performed; survives a lost index file).
        if (name.isEmpty() && inst->getManagedPackType() == QLatin1String("modrinth") && !inst->getManagedPackName().isEmpty()) {
            name = inst->getManagedPackName();
            versionId = inst->getManagedPackVersionID();
        }
        // (3) Last resort: an instance whose display name is one of our pack
        //     names (installed by an older launcher or another tool).
        if (name.isEmpty() && matchLive(inst->name())) {
            name = inst->name();
        }

        const auto* live = matchLive(name);
        if (!live) {
            continue;
        }

        InstalledPack ip;
        ip.instanceId = inst->id();
        ip.instanceName = inst->name();
        // Store the live pack's canonical name so callers can match it
        // straight against the manifest without re-normalizing.
        ip.packName = live->name;
        ip.installedVersion = versionId;
        out.append(ip);
    }
    return out;
}

// ---------------------------------------------------------------------------
// Local resolution

namespace {
QList<Mod> s_mods;
bool s_modsLoaded = false;     // a non-empty catalog was parsed
bool s_modsAttempted = false;  // a fetch was attempted
}  // namespace

const QList<Mod>& BlamdomAPI::cachedMods()
{
    return s_mods;
}

bool BlamdomAPI::catalogIsLoaded()
{
    return s_modsLoaded;
}

bool BlamdomAPI::catalogAttempted()
{
    return s_modsAttempted;
}

void BlamdomAPI::setCachedMods(const QByteArray& json)
{
    s_mods = parseMods(json);
    s_modsLoaded = !s_mods.isEmpty();
    s_modsAttempted = true;
}

QList<Mod> BlamdomAPI::fetchModsSync(QWidget* parent)
{
    auto [job, response] = fetchMods();
    if (!job) {
        s_modsAttempted = true;
        return {};
    }

    // Bound the wait: the catalog is a few KB, but the network is what it is.
    QTimer timeout;
    timeout.setSingleShot(true);
    QObject::connect(&timeout, &QTimer::timeout, job.get(), &Task::abort);
    timeout.start(15000);

    ProgressDialog pg(parent);
    pg.setWindowTitle(QObject::tr("Loading Blamdom catalog"));
    pg.execWithTask(job.get());
    timeout.stop();

    if (!job->wasSuccessful()) {
        s_modsAttempted = true;
        return {};
    }
    setCachedMods(*response);
    return s_mods;
}

QList<Pack> BlamdomAPI::fetchPacksSync(QWidget* parent)
{
    auto [job, response] = fetchPacks();
    if (!job) {
        return {};
    }

    QTimer timeout;
    timeout.setSingleShot(true);
    QObject::connect(&timeout, &QTimer::timeout, job.get(), &Task::abort);
    timeout.start(15000);

    ProgressDialog pg(parent);
    pg.setWindowTitle(QObject::tr("Loading Blamdom packs"));
    pg.execWithTask(job.get());
    timeout.stop();

    if (!job->wasSuccessful()) {
        return {};
    }
    return parsePacks(std::move(*response));
}

void BlamdomAPI::writeModMetadata(const QDir& indexDir, Packwiz::V1::Mod& meta)
{
    if (!meta.isValid()) {
        qCritical() << "Blamdom: refusing to write metadata for an invalid mod entry";
        return;
    }
    FS::ensureFilePathExists(indexDir.absolutePath());
    Packwiz::V1::updateModIndex(indexDir, meta);
}

bool BlamdomAPI::resolveCandidate(Resource* resource, const QDir& indexDir)
{
    if (!resource) {
        return false;
    }
    const auto* mod = matchModFile(s_mods, resource->fileinfo().fileName());
    if (!mod) {
        return false;
    }
    auto meta = buildModMetadata(*mod, resource->fileinfo().fileName());
    writeModMetadata(indexDir, meta);
    resource->setMetadata(meta);
    return true;
}

}  // namespace Blamdom
