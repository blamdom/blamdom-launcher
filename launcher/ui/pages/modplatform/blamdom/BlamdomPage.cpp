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
#include <QItemSelectionModel>
#include <QStandardItemModel>
#include <QUrl>

#include "InstanceImportTask.h"
#include "net/NetJob.h"
#include "ui/dialogs/NewInstanceDialog.h"

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

}  // namespace

BlamdomPage::BlamdomPage(NewInstanceDialog* dialog, QWidget* parent)
    : QWidget(parent), m_ui(new Ui::BlamdomPage), m_dialog(dialog)
{
    m_ui->setupUi(this);

    auto* model = new QStandardItemModel(0, 2, this);
    model->setHorizontalHeaderLabels({tr("Pack"), tr("Version")});
    m_ui->packView->setModel(model);
    m_ui->packView->setRootIsDecorated(false);
    m_ui->packView->setUniformRowHeights(true);

    connect(m_ui->searchEdit, &QLineEdit::textChanged, this, &BlamdomPage::onSearchChanged);
    connect(m_ui->packView->selectionModel(), &QItemSelectionModel::currentChanged,
            this, [this](const QModelIndex&, const QModelIndex&) { onSelectionChanged(); });
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
        model->setHorizontalHeaderLabels({tr("Pack"), tr("Version")});
    }
}

void BlamdomPage::openedImpl()
{
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

    auto [packsJob, packsResponse] = BlamdomAPI::fetchPacks();
    QObject::connect(packsJob.get(), &NetJob::succeeded, this,
                     [this, packsResponse](bool ok) { if (ok) packsLoaded(packsResponse); });
    QObject::connect(packsJob.get(), &NetJob::failed, this, [this](const QString& reason) {
        requestFailed(tr("Failed to load packs: %1").arg(reason));
    });
    m_packsJob = packsJob;
    m_packsJob->start();

    auto [modsJob, modsResponse] = BlamdomAPI::fetchMods();
    QObject::connect(modsJob.get(), &NetJob::succeeded, this,
                     [this, modsResponse](bool ok) { if (ok) modsLoaded(modsResponse); });
    QObject::connect(modsJob.get(), &NetJob::failed, this, [this](const QString& reason) {
        qDebug() << "Blamdom: mods manifest failed:" << reason;
    });
    m_modsJob = modsJob;
    m_modsJob->start();
}

void BlamdomPage::packsLoaded(QByteArray* response)
{
    m_packsJob.reset();
    m_packs = BlamdomAPI::parsePacks(std::move(*response));
    m_loaded = true;
    applyFilter();
    if (m_packs.isEmpty()) {
        m_ui->packDescription->setHtml(tr("<p>No packs published yet.</p>"));
    }
}

void BlamdomPage::modsLoaded(QByteArray* response)
{
    m_modsJob.reset();
    m_mods = BlamdomAPI::parseMods(std::move(*response));

    if (m_mods.isEmpty()) {
        m_ui->modsText->setHtml(tr("<p>No mods published yet.</p>"));
        return;
    }

    QString html;
    for (const auto& m : m_mods) {
        html += QStringLiteral("<p><b>%1</b> — v%2 (%3)</p><p>%4<br>")
                    .arg(m.name.toHtmlEscaped(), m.version.toHtmlEscaped(),
                         m.minecraft + " " + m.loader + ", " + m.side.toHtmlEscaped(),
                         m.description.toHtmlEscaped());
        html += QStringLiteral("<a href=\"%1\">%2</a> · %3</p><hr>")
                    .arg(m.download, tr("download"), QString::fromUtf8(humanSize(m.sizeBytes)));
    }
    m_ui->modsText->setHtml(html);
}

void BlamdomPage::requestFailed(const QString& reason)
{
    m_packsJob.reset();
    m_ui->packDescription->setHtml(tr("<p><b>%1</b></p><p>Packs will appear here as soon as the manifest is reachable.</p>")
                                       .arg(reason.toHtmlEscaped()));
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
        nameItem->setData(p.name);
        auto* verItem = new QStandardItem(QStringLiteral("v%1 · MC %2").arg(p.version, p.minecraft));
        model->appendRow({nameItem, verItem});
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
        m_ui->packDescription->setHtml(tr(
            "<h2>%1 — v%2</h2>"
            "<p><b>Minecraft</b>: %3 (%4)</p>"
            "<p>%5</p>"
            "<p><b>Size</b>: %6 · <b>Released</b>: %7</p>"
            "<p><b>sha256</b>: <code>%8</code></p>")
            .arg(m_selected.name.toHtmlEscaped(), m_selected.version.toHtmlEscaped(),
                 m_selected.minecraft.toHtmlEscaped(), m_selected.loader.toHtmlEscaped(),
                 m_selected.description.toHtmlEscaped(),
                 QString::fromUtf8(humanSize(m_selected.sizeBytes)), m_selected.released.toHtmlEscaped(),
                 m_selected.sha256.toHtmlEscaped()));
    }

    suggestCurrent();
}

void BlamdomPage::suggestCurrent()
{
    if (!isOpened || m_selected.name.isEmpty()) {
        m_dialog->setSuggestedPack();
        return;
    }

    // Our packs ship as .mrpack (Modrinth pack format) from our own host.
    // The stock import task downloads and installs it — no third party involved.
    m_dialog->setSuggestedPack(m_selected.name,
                               new InstanceImportTask(QUrl(m_selected.download), false, this));
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
