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

#include "modplatform/blamdom/BlamdomAPI.h"
#include "tasks/Task.h"

#include <QList>
#include <QWidget>

#include "ui/pages/BasePage.h"
#include "ui/pages/modplatform/ModpackProviderBasePage.h"

namespace Ui {
class BlamdomPage;
}

class NewInstanceDialog;

/**
 * The Blamdom section of the New Instance dialog.
 *
 * Lists every pack and mod we host on mine.blamdom.dev, straight from our own
 * manifests. Selecting a pack offers a one-click install — the .mrpack is
 * imported by the stock Modrinth-pack import task, so no third-party service
 * is involved.
 */
class BlamdomPage : public QWidget, public ModpackProviderBasePage {
    Q_OBJECT

   public:
    explicit BlamdomPage(NewInstanceDialog* dialog, QWidget* parent = nullptr);
    virtual ~BlamdomPage() override;

    virtual QString id() const override { return QStringLiteral("blamdom"); }
    virtual QString displayName() const override { return tr("Blamdom"); }
    virtual QIcon icon() const override { return QIcon::fromTheme(QStringLiteral("blamdom")); }
    virtual bool shouldDisplay() const override { return true; }
    virtual QString helpPage() const override { return QStringLiteral("Blamdom-platform"); }

    void retranslate() override;

    void openedImpl() override;
    void closedImpl() override;

    /** Programatically set the term in the search bar. */
    virtual void setSearchTerm(QString term) override;
    /** Get the current term in the search bar. */
    [[nodiscard]] virtual QString getSerachTerm() const override;

   private:
    void refresh();
    void suggestCurrent();
    void applyFilter();

   private slots:
    void onSelectionChanged();
    void onSearchChanged();
    void packsLoaded(QByteArray* response);
    void modsLoaded(QByteArray* response);
    void requestFailed(const QString& reason);

   private:
    Ui::BlamdomPage* m_ui = nullptr;
    NewInstanceDialog* m_dialog = nullptr;

    QList<Blamdom::Pack> m_packs;
    QList<Blamdom::Mod> m_mods;

    Blamdom::Pack m_selected;
    bool m_loaded = false;

    Task::Ptr m_packsJob;
    Task::Ptr m_modsJob;
};
