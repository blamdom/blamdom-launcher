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

    //! Scan our installed pack instances: write Blamdom provider metadata for
    //! our own mods found in their mods folders, and collect the ones that
    //! are behind the live catalog.
    void resolveLocalMods();
    void updateModsStatus();
    void runModUpdates();
    void startNextModUpdate();

    //! One installed mod file that the live catalog has moved past.
    struct ModUpdate {
        QString instanceId;
        QString instanceName;
        Blamdom::Mod mod;  //!< the catalog entry (latest version)
        QString fileName;  //!< path of the installed file, relative to the mods folder
        QString oldVersion;
    };

    //! Which Java major a pack's Minecraft version needs (1.20.1 → 17, …).
    static int requiredJavaMajor(const QString& minecraft);
    //! True if the launcher knows a Java installation with this major.
    static bool hasJavaMajor(int major);
    Blamdom::InstalledPack installedFor(const Blamdom::Pack& pack) const;
    void updatePackStatus();
    void runUpdate(const Blamdom::InstalledPack& installed);
    void downloadTemurin(int major);

   private slots:
    void onSelectionChanged();
    void onSearchChanged();
    void packsLoaded(QByteArray* response);
    void modsLoaded(QByteArray* response);
    void requestFailed(const QString& reason);
    void onUpdateButton_clicked();
    void onJavaButton_clicked();
    void onModsUpdateButton_clicked();

   private:
    Ui::BlamdomPage* m_ui = nullptr;
    NewInstanceDialog* m_dialog = nullptr;

    QList<Blamdom::Pack> m_packs;
    QList<Blamdom::Mod> m_mods;
    QList<Blamdom::InstalledPack> m_installed;
    QList<ModUpdate> m_modUpdates;
    QList<ModUpdate> m_pendingMods;

    Blamdom::Pack m_selected;
    bool m_loaded = false;

    Task::Ptr m_packsJob;
    Task::Ptr m_modsJob;
    Task::Ptr m_updateJob;
    Task::Ptr m_javaJob;
    Task::Ptr m_modUpdateJob;
};
