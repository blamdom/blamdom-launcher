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

#include "BlamdomPage.h"
#include "ui_BlamdomPage.h"

#include <QAbstractItemView>
#include <QDirIterator>
#include <QFileInfo>
#include <QItemSelectionModel>
#include <QMap>
#include <QMessageBox>
#include <QStandardItemModel>
#include <QUrl>

#include <functional>

#include "Application.h"
#include "FileSystem.h"
#include "InstanceImportTask.h"
#include "InstanceList.h"
#include "SysInfo.h"
#include "java/JavaInstall.h"
#include "java/JavaInstallList.h"
#include "java/download/ArchiveDownloadTask.h"
#include "meta/Index.h"
#include "meta/VersionList.h"
#include "minecraft/VersionFile.h"
#include "minecraft/mod/MetadataHandler.h"
#include "modplatform/blamdom/ModUpdateTask.h"
#include "net/ApiRequest.h"
#include "net/ChecksumValidator.h"
#include "net/NetJob.h"
#include "ui/dialogs/CustomMessageBox.h"
#include "ui/dialogs/NewInstanceDialog.h"
#include "ui/dialogs/ProgressDialog.h"

namespace {

QByteArray humanSize(qint64 bytes)
{
    if (bytes >= 1024 * 1024) {
        return QStringLiteral("%1 MB").arg(QString::number(bytes / (1024.0 * 1024.0), 'f', 1)).toUtf8();
    }
    if (bytes >= 1024) {
        return QStringLiteral("%1 KB").arg(QString::number(bytes / 1024.0, 'f', 0)).toUtf8();
    }
    return QStringLiteral("%1 B").arg(bytes).toUtf8();
}

//! Recursively visit every file under a mods folder.
void forModsFiles(const QString& dir, const std::function<void(const QFileInfo&)>& fn)
{
    QDirIterator it(dir, QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot);
    while (it.hasNext()) {
        fn(it.nextFileInfo());
    }
}

}  // namespace

BlamdomPage::BlamdomPage(NewInstanceDialog* dialog, QWidget* parent) : QWidget(parent), m_ui(new Ui::BlamdomPage), m_dialog(dialog)
{
    m_ui->setupUi(this);

    auto* model = new QStandardItemModel(0, 2, this);
    model->setHorizontalHeaderLabels({ tr("Pack"), tr("Version") });
    m_ui->packView->setModel(model);
    m_ui->packView->setRootIsDecorated(false);
    m_ui->packView->setUniformRowHeights(true);

    connect(m_ui->searchEdit, &QLineEdit::textChanged, this, &BlamdomPage::onSearchChanged);
    connect(m_ui->packView->selectionModel(), &QItemSelectionModel::currentChanged, this,
            [this](const QModelIndex&, const QModelIndex&) { onSelectionChanged(); });
    // No connectSlotsByName here — wire the status-row buttons explicitly.
    connect(m_ui->updateButton, &QPushButton::clicked, this, &BlamdomPage::onUpdateButton_clicked);
    connect(m_ui->javaButton, &QPushButton::clicked, this, &BlamdomPage::onJavaButton_clicked);
    connect(m_ui->modsUpdateButton, &QPushButton::clicked, this, &BlamdomPage::onModsUpdateButton_clicked);
}

BlamdomPage::~BlamdomPage()
{
    delete m_ui;
}

void BlamdomPage::retranslate()
{
    m_ui->retranslateUi(this);
    auto* model = qobject_cast<QStandardItemModel*>(m_ui->packView->model());
    if (model) {
        model->setHorizontalHeaderLabels({ tr("Pack"), tr("Version") });
    }
}

void BlamdomPage::openedImpl()
{
    // Re-fetch on every open: releases publish while the launcher runs, and
    // keeping the first session's catalog would hide them until a restart
    // (the documented contract is "fetched fresh on every open of the page").
    if (m_loaded) {
        m_loaded = false;
    }
    refresh();
}

void BlamdomPage::closedImpl()
{
    if (m_packsJob) {
        m_packsJob->abort();
    }
    if (m_modsJob) {
        m_modsJob->abort();
    }
}

void BlamdomPage::refresh()
{
    if (m_loaded || m_packsJob || m_modsJob) {
        return;
    }

    m_ui->packDescription->setHtml(tr("<p>Loading packs from <b>mine.blamdom.dev</b>…</p>"));

    auto [packsJob, packsResponse] = Blamdom::BlamdomAPI::fetchPacks();
    QObject::connect(packsJob.get(), &NetJob::succeeded, this, [this, packsResponse] { packsLoaded(packsResponse); });
    QObject::connect(packsJob.get(), &NetJob::failed, this,
                     [this](const QString& reason) { requestFailed(tr("Failed to load packs: %1").arg(reason)); });
    m_packsJob = packsJob;
    m_packsJob->start();

    auto [modsJob, modsResponse] = Blamdom::BlamdomAPI::fetchMods();
    QObject::connect(modsJob.get(), &NetJob::succeeded, this, [this, modsResponse] { modsLoaded(modsResponse); });
    QObject::connect(modsJob.get(), &NetJob::failed, this,
                     [this](const QString& reason) { qDebug() << "Blamdom: mods manifest failed:" << reason; });
    m_modsJob = modsJob;
    m_modsJob->start();
}

void BlamdomPage::packsLoaded(QByteArray* response)
{
    m_packsJob.reset();
    m_packs = Blamdom::BlamdomAPI::parsePacks(std::move(*response));
    m_installed = Blamdom::BlamdomAPI::findInstalled(m_packs);
    m_loaded = true;
    applyFilter();
    if (m_packs.isEmpty()) {
        m_ui->packDescription->setHtml(tr("<p>No packs published yet.</p>"));
    }
    if (!m_mods.isEmpty()) {
        resolveLocalMods();
    }
}

void BlamdomPage::modsLoaded(QByteArray* response)
{
    m_modsJob.reset();
    const QByteArray raw = std::move(*response);
    m_mods = Blamdom::BlamdomAPI::parseMods(raw);
    // Keep the shared in-memory catalog in sync with what the page shows, so
    // other code paths (pack import, the update-check dialog) resolve our mods
    // without fetching again.
    Blamdom::BlamdomAPI::setCachedMods(raw);

    if (m_mods.isEmpty()) {
        m_ui->modsText->setHtml(tr("<p>No mods published yet.</p>"));
        return;
    }

    QString html;
    for (const auto& m : m_mods) {
        html += QStringLiteral("<p><b>%1</b> — v%2 (%3)</p><p>%4<br>")
                    .arg(m.name.toHtmlEscaped(), m.version.toHtmlEscaped(), (m.minecraft + " " + m.loader + ", " + m.side).toHtmlEscaped(),
                         m.description.toHtmlEscaped());
        html +=
            QStringLiteral("<a href=\"%1\">%2</a> · %3</p><hr>").arg(m.download, tr("download"), QString::fromUtf8(humanSize(m.sizeBytes)));
    }
    m_ui->modsText->setHtml(html);

    if (m_loaded) {
        resolveLocalMods();
    }
}

void BlamdomPage::requestFailed(const QString& reason)
{
    m_packsJob.reset();
    m_ui->packDescription->setHtml(
        tr("<p><b>%1</b></p><p>Packs will appear here as soon as the manifest is reachable.</p>").arg(reason.toHtmlEscaped()));
}

void BlamdomPage::onSearchChanged()
{
    applyFilter();
}

void BlamdomPage::applyFilter()
{
    auto* model = qobject_cast<QStandardItemModel*>(m_ui->packView->model());
    if (!model) {
        return;
    }

    const QString term = m_ui->searchEdit->text().trimmed().toLower();
    model->removeRows(0, model->rowCount());

    for (const auto& p : m_packs) {
        const QString hay = (p.name + " " + p.description + " " + p.minecraft).toLower();
        if (!term.isEmpty() && !hay.contains(term)) {
            continue;
        }
        auto* nameItem = new QStandardItem(p.name);
        nameItem->setData(p.name, Qt::UserRole);
        auto* verItem = new QStandardItem(QStringLiteral("v%1 · MC %2").arg(p.version, p.minecraft));
        model->appendRow({ nameItem, verItem });
    }

    // Blamdom fork: keep a selection so the details panel and the dialog's OK
    // button are ready immediately (upstream leaves the list unselected, which
    // left the panel stuck on "Loading…" and OK disabled).
    if (model->rowCount() > 0 && !m_ui->packView->currentIndex().isValid()) {
        m_ui->packView->setCurrentIndex(model->index(0, 0));
    }
    // Drive the details + suggested-pack from the current selection. (A
    // programmatic setCurrentIndex doesn't always emit currentChanged.)
    if (m_ui->packView->currentIndex().isValid()) {
        onSelectionChanged();
    }
}

void BlamdomPage::onSelectionChanged()
{
    auto* model = qobject_cast<QStandardItemModel*>(m_ui->packView->model());
    const auto idx = m_ui->packView->currentIndex();
    m_selected = {};
    if (model && idx.isValid()) {
        const auto name = model->itemFromIndex(model->index(idx.row(), 0))->data(Qt::UserRole).toString();
        for (const auto& p : m_packs) {
            if (p.name == name) {
                m_selected = p;
                break;
            }
        }
    }

    if (m_selected.name.isEmpty()) {
        m_ui->packDescription->setHtml(tr("<p>Select a pack to see the details.</p>"));
    } else {
        m_ui->packDescription->setHtml(tr("<h2>%1 — v%2</h2>"
                                          "<p><b>Minecraft</b>: %3 (%4)</p>"
                                          "<p>%5</p>"
                                          "<p><b>Size</b>: %6 · <b>Released</b>: %7</p>"
                                          "<p><b>sha256</b>: <code>%8</code></p>")
                                           .arg(m_selected.name.toHtmlEscaped(), m_selected.version.toHtmlEscaped(),
                                                m_selected.minecraft.toHtmlEscaped(), m_selected.loader.toHtmlEscaped(),
                                                m_selected.description.toHtmlEscaped(), QString::fromUtf8(humanSize(m_selected.sizeBytes)),
                                                m_selected.released.toHtmlEscaped(), m_selected.sha256.toHtmlEscaped()));
    }

    suggestCurrent();
    updatePackStatus();
}

Blamdom::InstalledPack BlamdomPage::installedFor(const Blamdom::Pack& pack) const
{
    for (const auto& ip : m_installed) {
        if (ip.packName == pack.name) {
            return ip;
        }
    }
    return {};
}

int BlamdomPage::requiredJavaMajor(const QString& minecraft)
{
    // "1.20.1" → major 1, minor 20, patch 1
    const auto parts = minecraft.split('.');
    if (parts.size() < 2) {
        return 17;
    }
    const int major = parts[0].toInt();
    const int minor = parts[1].toInt();
    const int patch = parts.size() > 2 ? parts[2].section('.', 0, 0).toInt() : 0;
    if (major > 1) {
        return 21;
    }
    if (minor >= 21) {
        return 21;
    }
    if (minor == 20) {
        return patch >= 5 ? 21 : 17;
    }
    if (minor >= 17) {
        return 17;
    }
    return 8;
}

bool BlamdomPage::hasJavaMajor(int major)
{
    auto* javalist = APPLICATION->javalist();
    for (int i = 0; i < javalist->count(); ++i) {
        auto java = std::dynamic_pointer_cast<JavaInstall>(javalist->at(i));
        if (java && java->id.major() == major) {
            return true;
        }
    }
    return false;
}

void BlamdomPage::updatePackStatus()
{
    m_ui->updateButton->setVisible(false);
    m_ui->javaButton->setVisible(false);
    if (m_selected.name.isEmpty()) {
        m_ui->packStatusLabel->setText(tr("Select a pack to see its install status."));
        m_ui->javaStatusLabel->clear();
        return;
    }

    const auto installed = installedFor(m_selected);
    QString line;
    if (installed.instanceId.isEmpty()) {
        line = tr("<b>Not installed yet</b> — select it and press OK to install.");
    } else if (!installed.installedVersion.isEmpty() && installed.installedVersion == m_selected.version) {
        line = tr("<b>Installed</b>: v%1 on “%2” — up to date.")
                   .arg(installed.installedVersion.toHtmlEscaped(), installed.instanceName.toHtmlEscaped());
    } else {
        // Behind the live catalog — or installed so the launcher lost track of
        // the version (old install, foreign import). The in-place update
        // works in both cases.
        const QString old = installed.installedVersion.isEmpty()
                                ? tr("version unknown")
                                : tr("v%1").arg(installed.installedVersion.toHtmlEscaped());
        line = tr("<b>Installed</b>: %1 on “%2” — <b>v%3</b> is available.")
                   .arg(old, installed.instanceName.toHtmlEscaped(), m_selected.version.toHtmlEscaped());
        m_ui->updateButton->setVisible(true);
        m_ui->updateButton->setText(tr("Update to v%1").arg(m_selected.version));
    }
    m_ui->packStatusLabel->setText(line);

    // Java requirement for this pack's Minecraft version, on its own line.
    const int major = requiredJavaMajor(m_selected.minecraft);
    auto* javalist = APPLICATION->javalist();
    if (javalist->isLoaded()) {
        if (hasJavaMajor(major)) {
            m_ui->javaStatusLabel->setText(tr("Java %1: found.").arg(major));
        } else {
            m_ui->javaStatusLabel->setText(tr("Java %1: <b>not installed</b>.").arg(major));
            m_ui->javaButton->setVisible(true);
        }
    } else {
        m_ui->javaStatusLabel->setText(tr("Java %1: checking…").arg(major));
        auto task = javalist->getLoadTask();
        QObject::connect(task.get(), &Task::finished, this, [this] { updatePackStatus(); });
        if (!task->isRunning()) {
            task->start();
        }
    }
}

void BlamdomPage::onUpdateButton_clicked()
{
    const auto installed = installedFor(m_selected);
    if (installed.instanceId.isEmpty() || m_updateJob) {
        return;
    }

    auto response = CustomMessageBox::selectable(this, tr("Update %1?").arg(m_selected.name),
                                                 tr("This updates the installed pack to <b>v%1</b> in place.<br>"
                                                    "Your saves and settings are kept; pack files are updated and "
                                                    "old versions of pack files are removed.")
                                                     .arg(m_selected.version.toHtmlEscaped()),
                                                 QMessageBox::Question, QMessageBox::Yes | QMessageBox::No)
                        ->exec();
    if (response != QMessageBox::Yes) {
        return;
    }
    runUpdate(installed);
}

void BlamdomPage::runUpdate(const Blamdom::InstalledPack& installed)
{
    auto* inst = APPLICATION->instances()->getInstanceById(installed.instanceId);
    if (!inst) {
        m_ui->packStatusLabel->setText(tr("The installed instance is no longer available."));
        return;
    }

    // With original_instance_id set, the stock Modrinth import reuses the
    // existing instance: files whose hash is unchanged are skipped, files
    // the old pack installed and the new one dropped are removed, and the
    // pack's overrides are re-applied. That is the whole update.
    QMap<QString, QString> extra;
    extra.insert("pack_id", m_selected.name);
    extra.insert("pack_version_id", m_selected.version);
    extra.insert("original_instance_id", installed.instanceId);

    auto* task = new InstanceImportTask(QUrl(m_selected.download), true, this, extra);
    task->setName(inst->name());
    task->setGroup(APPLICATION->instances()->getInstanceGroup(installed.instanceId));
    task->setIcon(inst->iconKey());
    task->setConfirmUpdate(false);  // the user already clicked Update

    m_updateJob = Task::Ptr(APPLICATION->instances()->wrapInstanceTask(task));

    QObject::connect(m_updateJob.get(), &Task::failed, this, [this](const QString& reason) {
        m_ui->packStatusLabel->setText(tr("Update failed: %1").arg(reason.toHtmlEscaped()));
        CustomMessageBox::selectable(this, tr("Update failed"), reason, QMessageBox::Critical)->show();
    });
    QObject::connect(m_updateJob.get(), &Task::succeeded, this, [this] {
        m_installed = Blamdom::BlamdomAPI::findInstalled(m_packs);
        updatePackStatus();
        CustomMessageBox::selectable(
            this, tr("Pack updated"),
            tr("%1 is now on <b>v%2</b>.").arg(m_selected.name.toHtmlEscaped(), m_selected.version.toHtmlEscaped()),
            QMessageBox::Information)
            ->show();
    });

    m_ui->updateButton->setEnabled(false);
    m_ui->packStatusLabel->setText(tr("Updating to <b>v%1</b>… (saves and settings are kept)").arg(m_selected.version.toHtmlEscaped()));

    ProgressDialog pg(this);
    pg.showSkipButton();
    pg.execWithTask(m_updateJob.get());
    m_updateJob.reset();
}

void BlamdomPage::resolveLocalMods()
{
    m_modUpdates.clear();
    if (m_mods.isEmpty() || m_installed.isEmpty()) {
        updateModsStatus();
        return;
    }

    for (const auto& ip : m_installed) {
        auto* inst = APPLICATION->instances()->getInstanceById(ip.instanceId);
        if (!inst) {
            continue;
        }
        const QString modsDir = inst->modsRoot();
        QDir mods(modsDir);
        if (!mods.exists()) {
            continue;
        }
        const QDir indexDir(FS::PathCombine(modsDir, ".index"));

        // Sweep temp names left behind by a crashed mod update (the old jar
        // itself is never lost — these are just the staging/backup names).
        forModsFiles(modsDir, [](const QFileInfo& entry) {
            const QString fn = entry.fileName();
            if (fn.endsWith(QStringLiteral(".update-new")) || fn.endsWith(QStringLiteral(".update-old"))) {
                QFile::remove(entry.filePath());
            }
        });

        forModsFiles(modsDir, [&](const QFileInfo& entry) {
            const auto* mod = Blamdom::BlamdomAPI::matchModFile(m_mods, entry.fileName());
            if (!mod) {
                return;
            }

            // (a) Our own mod with no (or a foreign) provider entry: write the
            //     Blamdom metadata so the mod list shows "Blamdom", never "Unknown".
            auto meta = Metadata::get(indexDir, mod->id);
            if (!meta.isValid() || meta.provider != ModPlatform::ResourceProvider::BLAMDOM) {
                meta = Blamdom::BlamdomAPI::buildModMetadata(*mod, entry.fileName());
                Blamdom::BlamdomAPI::writeModMetadata(indexDir, meta);
            }

            // (b) Installed file behind the live catalog?
            const QString installedVer = Blamdom::BlamdomAPI::modFileVersion(mod->id, entry.fileName());
            if (Blamdom::BlamdomAPI::isNewerVersion(mod->version, installedVer)) {
                ModUpdate up;
                up.instanceId = inst->id();
                up.instanceName = inst->name();
                up.mod = *mod;
                up.fileName = mods.relativeFilePath(entry.filePath());
                up.oldVersion = installedVer;
                m_modUpdates.append(up);
            }
        });
    }
    updateModsStatus();
}

void BlamdomPage::updateModsStatus()
{
    m_ui->modsUpdateButton->setVisible(false);
    if (m_modUpdates.isEmpty()) {
        if (m_installed.isEmpty()) {
            m_ui->modsStatusLabel->clear();
        } else {
            m_ui->modsStatusLabel->setText(tr("Our mods are up to date on your installed packs."));
        }
        return;
    }

    QStringList lines;
    for (const auto& up : m_modUpdates) {
        lines << tr("%1 on “%2”: v%3 → v%4")
                     .arg(up.mod.name.toHtmlEscaped(), up.instanceName.toHtmlEscaped(),
                          up.oldVersion.isEmpty() ? QStringLiteral("?") : up.oldVersion.toHtmlEscaped(), up.mod.version.toHtmlEscaped());
    }
    m_ui->modsStatusLabel->setText(tr("<b>Updates available:</b> %1").arg(lines.join(QStringLiteral(" · "))));
    m_ui->modsUpdateButton->setVisible(true);
    if (m_modUpdates.size() == 1) {
        m_ui->modsUpdateButton->setText(tr("Update %1 to v%2").arg(m_modUpdates.first().mod.name, m_modUpdates.first().mod.version));
    } else {
        m_ui->modsUpdateButton->setText(tr("Update %1 mods to latest").arg(m_modUpdates.size()));
    }
}

void BlamdomPage::onModsUpdateButton_clicked()
{
    if (m_modUpdates.isEmpty() || m_modUpdateJob) {
        return;
    }

    QString what;
    if (m_modUpdates.size() == 1) {
        what = tr("This updates <b>%1</b> to <b>v%2</b> in place.<br>Jar is verified against the sha256 in our catalog.")
                   .arg(m_modUpdates.first().mod.name.toHtmlEscaped(), m_modUpdates.first().mod.version.toHtmlEscaped());
    } else {
        QStringList lines;
        for (const auto& up : m_modUpdates) {
            lines << tr("%1 (v%2 → v%3)").arg(up.mod.name).arg(up.oldVersion).arg(up.mod.version);
        }
        what = tr("This updates <b>%1</b> mods in place:<br>%2<br>Each jar is verified against the sha256 in our catalog.")
                   .arg(m_modUpdates.size())
                   .arg(lines.join(QStringLiteral("<br>")));
    }

    auto response =
        CustomMessageBox::selectable(this, tr("Update Blamdom mods?"), what, QMessageBox::Question, QMessageBox::Yes | QMessageBox::No)
            ->exec();
    if (response != QMessageBox::Yes) {
        return;
    }
    runModUpdates();
}

void BlamdomPage::runModUpdates()
{
    m_pendingMods = m_modUpdates;
    m_ui->modsUpdateButton->setEnabled(false);
    startNextModUpdate();
}

void BlamdomPage::startNextModUpdate()
{
    if (m_pendingMods.isEmpty()) {
        m_modUpdateJob.reset();
        m_ui->modsUpdateButton->setEnabled(true);
        m_ui->modsStatusLabel->setText(tr("Our mods are up to date."));
        m_modUpdates.clear();
        CustomMessageBox::selectable(this, tr("Mods updated"), tr("Your Blamdom mods are up to date."), QMessageBox::Information)->show();
        return;
    }

    auto up = m_pendingMods.takeFirst();

    auto* inst = APPLICATION->instances()->getInstanceById(up.instanceId);
    const QString modsDir = inst ? inst->modsRoot() : QString();
    const QString oldPath = FS::PathCombine(modsDir, up.fileName);

    if (!inst || !QFile::exists(oldPath)) {
        m_ui->modsStatusLabel->setText(tr("Update failed: the installed file of %1 is missing.").arg(up.mod.name.toHtmlEscaped()));
        CustomMessageBox::selectable(this, tr("Mod update failed"),
                                     tr("Could not find <b>%1</b> in the instance “%2” — it may have been removed.")
                                         .arg(up.mod.name.toHtmlEscaped(), up.instanceName.toHtmlEscaped()),
                                     QMessageBox::Critical)
            ->show();
        m_pendingMods.clear();
        m_ui->modsUpdateButton->setEnabled(true);
        return;
    }

    m_ui->modsStatusLabel->setText(
        tr("Updating <b>%1</b> to <b>v%2</b>…").arg(up.mod.name.toHtmlEscaped(), up.mod.version.toHtmlEscaped()));
    m_ui->modsUpdateButton->setEnabled(false);

    m_modUpdateJob = makeShared<Blamdom::ModUpdateTask>(up.mod, oldPath, FS::PathCombine(modsDir, QStringLiteral(".index")));
    QObject::connect(m_modUpdateJob.get(), &Task::failed, this, [this, up](const QString& reason) {
        m_modUpdateJob.reset();
        m_ui->modsStatusLabel->setText(tr("Update failed: %1").arg(reason.toHtmlEscaped()));
        CustomMessageBox::selectable(this, tr("Mod update failed"), reason, QMessageBox::Critical)->show();
        m_pendingMods.clear();
        m_ui->modsUpdateButton->setEnabled(true);
    });
    QObject::connect(m_modUpdateJob.get(), &Task::succeeded, this, [this, up]() {
        m_modUpdateJob.reset();
        m_ui->modsStatusLabel->setText(tr("%1 is now on <b>v%2</b>.").arg(up.mod.name.toHtmlEscaped(), up.mod.version.toHtmlEscaped()));
        startNextModUpdate();
    });
    QObject::connect(m_modUpdateJob.get(), &Task::aborted, this, [this]() {
        m_modUpdateJob.reset();
        m_pendingMods.clear();
        m_ui->modsUpdateButton->setEnabled(true);
    });

    ProgressDialog pg(this);
    pg.showSkipButton();
    pg.execWithTask(m_modUpdateJob.get());
}

void BlamdomPage::onJavaButton_clicked()
{
    if (m_selected.name.isEmpty() || m_javaJob) {
        return;
    }
    downloadTemurin(requiredJavaMajor(m_selected.minecraft));
}

void BlamdomPage::downloadTemurin(int major)
{
    const QString majorId = QStringLiteral("java%1").arg(major);
    m_ui->javaButton->setEnabled(false);
    m_ui->javaStatusLabel->setText(tr("Loading the Temurin list…"));

    auto job = APPLICATION->metadataIndex()->loadVersion("net.adoptium.java", majorId);
    if (!job) {
        m_ui->javaButton->setEnabled(true);
        m_ui->javaStatusLabel->setText(tr("Couldn't load the Java version list."));
        return;
    }

    QObject::connect(job.get(), &Task::succeeded, this, [this, major, majorId] {
        auto ver = APPLICATION->metadataIndex()->get("net.adoptium.java")->getVersion(majorId);
        const auto arch = SysInfo::getSupportedJavaArchitecture();
        if (!ver->isLoaded() || !ver->data() || arch.isEmpty()) {
            m_ui->javaButton->setEnabled(true);
            m_ui->javaStatusLabel->setText(tr("No Temurin %1 metadata available for this system.").arg(major));
            return;
        }

        // Newest JRE for this system (fall back to a JDK if there is no JRE).
        auto newer = [](const Java::MetadataPtr& a, const Java::MetadataPtr& b) {
            if (a->version.major() != b->version.major())
                return a->version.major() > b->version.major();
            if (a->version.minor() != b->version.minor())
                return a->version.minor() > b->version.minor();
            return a->version.security() > b->version.security();
        };
        Java::MetadataPtr jre, jdk;
        for (const auto& rt : ver->data()->runtimes) {
            if (rt->runtimeOS != arch) {
                continue;
            }
            if (rt->packageType == "jre" && (!jre || newer(rt, jre))) {
                jre = rt;
            } else if (rt->packageType == "jdk" && (!jdk || newer(rt, jdk))) {
                jdk = rt;
            }
        }
        auto pick = jre ? jre : jdk;
        if (!pick) {
            m_ui->javaButton->setEnabled(true);
            m_ui->javaStatusLabel->setText(tr("No Temurin %1 build for this system (%2).").arg(QString::number(major), arch));
            return;
        }

        m_ui->javaStatusLabel->setText(
            tr("Downloading <b>%1</b> (Eclipse Temurin %2, ~45 MB)…").arg(pick->m_name.toHtmlEscaped(), QString::number(major)));
        auto dl = makeShared<Java::ArchiveDownloadTask>(pick->url, FS::PathCombine(APPLICATION->javaPath(), pick->m_name),
                                                        pick->checksumType, pick->checksumHash);

        QObject::connect(dl.get(), &Task::failed, this, [this](const QString& reason) {
            m_ui->javaButton->setEnabled(true);
            m_ui->javaStatusLabel->setText(tr("Java download failed: %1").arg(reason.toHtmlEscaped()));
            CustomMessageBox::selectable(this, tr("Java download failed"), reason, QMessageBox::Warning)->show();
        });
        QObject::connect(dl.get(), &Task::succeeded, this, [this, major] {
            // Make sure the launcher picks the new installation up, then
            // re-render the status line (it will show "Java N: found").
            auto refresh = APPLICATION->javalist()->getLoadTask(true);
            QObject::connect(refresh.get(), &Task::finished, this, [this] { updatePackStatus(); });
            if (!refresh->isRunning()) {
                refresh->start();
            }
            CustomMessageBox::selectable(this, tr("Java installed"),
                                         tr("Eclipse Temurin %1 is installed. You can launch the game now.").arg(major),
                                         QMessageBox::Information)
                ->show();
        });

        m_javaJob = dl;
        ProgressDialog pg(this);
        pg.showSkipButton();
        pg.execWithTask(dl.get());
        m_javaJob.reset();
        m_ui->javaButton->setEnabled(true);
    });
    QObject::connect(job.get(), &Task::failed, this, [this](const QString& reason) {
        m_ui->javaButton->setEnabled(true);
        m_ui->javaStatusLabel->setText(tr("Couldn't load the Java list: %1").arg(reason.toHtmlEscaped()));
        CustomMessageBox::selectable(this, tr("Java list unavailable"), reason, QMessageBox::Warning)->show();
    });
    m_javaJob = job;
    if (!job->isRunning()) {
        job->start();
    }
}

void BlamdomPage::suggestCurrent()
{
    if (!isOpened || m_selected.name.isEmpty()) {
        m_dialog->setSuggestedPack();
        return;
    }

    // Our packs ship as .mrpack (Modrinth pack format) from our own host.
    // The stock import task downloads and installs it — no third party involved.
    m_dialog->setSuggestedPack(m_selected.name, new InstanceImportTask(QUrl(m_selected.download), false, this));
    m_dialog->setSuggestedIcon("default");
}

void BlamdomPage::setSearchTerm(QString term)
{
    m_ui->searchEdit->setText(term);
}

QString BlamdomPage::getSerachTerm() const
{
    return m_ui->searchEdit->text();
}
