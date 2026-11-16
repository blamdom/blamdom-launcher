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

#include "Application.h"
#include "Json.h"
#include "QObjectPtr.h"
#include "net/NetJob.h"
#include "net/Request.h"

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

QList<Blamdom::Mod> BlamdomAPI::parseMods(const QByteArray& json)
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

}  // namespace Blamdom
