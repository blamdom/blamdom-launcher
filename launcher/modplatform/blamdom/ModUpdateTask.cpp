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

#include "modplatform/blamdom/ModUpdateTask.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QUrl>

#include "Application.h"
#include "net/ApiRequest.h"
#include "net/ChecksumValidator.h"
#include "net/NetJob.h"

namespace Blamdom {

ModUpdateTask::ModUpdateTask(const Mod& mod, const QString& oldPath, const QString& indexDir)
    : Task(true), m_mod(mod), m_oldPath(oldPath), m_indexDir(indexDir)
{
    // single step: download (verified) + swap + metadata
    setProgress(0, 1);
}

ModUpdateTask::~ModUpdateTask()
{
    if (m_job) {
        m_job->abort();
        m_job->deleteLater();
        m_job = nullptr;
    }
}

void ModUpdateTask::executeTask()
{
    setAbortable(true);
    setDetails(tr("Downloading %1 v%2").arg(m_mod.name, m_mod.version));

    const QString tmpPath = m_oldPath + QStringLiteral(".update-new");
    QFile::remove(tmpPath);  // stale staging file from a crashed run

    m_job = new NetJob(tr("Blamdom mod update"), APPLICATION->network());
    auto dl = Net::ApiRequest::makeFile(QUrl(m_mod.download), tmpPath);
    dl->addValidator(new Net::ChecksumValidator(QCryptographicHash::Sha256, m_mod.sha256));
    m_job->addNetAction(dl);
    propagateFromOther(m_job);

    QObject::connect(m_job, &Task::failed, this, [this, tmpPath](QString reason) {
        QFile::remove(tmpPath);
        emitFailed(reason);
    });
    QObject::connect(m_job, &Task::aborted, this, [this, tmpPath]() {
        QFile::remove(tmpPath);
        emitAborted();
    });
    QObject::connect(m_job, &Task::succeeded, this, [this, tmpPath]() {
        // Swap via rename so a crash can never lose the old jar: move the old
        // file aside first, put the verified new one in place, then drop the
        // backup.
        const QString oldBackup = m_oldPath + QStringLiteral(".update-old");
        QFile::remove(oldBackup);
        const bool hadOld = QFile::exists(m_oldPath);
        if (hadOld && !QFile::rename(m_oldPath, oldBackup)) {
            emitFailed(tr("Could not move the old file of %1 aside.").arg(m_mod.name));
            return;
        }
        if (!QFile::rename(tmpPath, m_oldPath)) {
            if (hadOld) {
                QFile::rename(oldBackup, m_oldPath);  // roll back
            }
            emitFailed(tr("Could not install the new version of %1. The old version is intact.").arg(m_mod.name));
            return;
        }
        QFile::remove(oldBackup);

        // Point the metadata at the new version (same file name).
        auto meta = BlamdomAPI::buildModMetadata(m_mod, QFileInfo(m_oldPath).fileName());
        BlamdomAPI::writeModMetadata(QDir(m_indexDir), meta);

        emitSucceeded();
    });

    m_job->start();
}

}  // namespace Blamdom
