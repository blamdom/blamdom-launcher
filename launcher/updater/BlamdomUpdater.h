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

#include <QDateTime>
#include <QDir>
#include <QPointer>
#include <QSettings>
#include <QTimer>
#include <memory>

#include "ExternalUpdater.h"
#include "updater/BlamdomUpdateLogic.h"

class QWidget;

/*!
 * Updates the launcher from the newest GitHub release of BuildConfig.UPDATER_GITHUB_REPO.
 *
 * Linux: downloads the release tarball, verifies its sha256 (GitHub's asset digest, or the
 * release's SHA256SUMS), unpacks it next to the install, swaps the folders (with rollback)
 * and restarts. Installs that cannot replace themselves (system-wide, portable, AppImage)
 * get the same prompt but are pointed at the release page instead.
 */
class BlamdomUpdater : public ExternalUpdater {
    Q_OBJECT

   public:
    BlamdomUpdater(QWidget* parent, const QString& appDir, const QString& dataDir);
    ~BlamdomUpdater() override = default;

    void checkForUpdates() override;
    bool getAutomaticallyChecksForUpdates() override;
    double getUpdateCheckInterval() override;
    bool getBetaAllowed() override;
    void setAutomaticallyChecksForUpdates(bool check) override;
    void setUpdateCheckInterval(double seconds) override;
    void setBetaAllowed(bool allowed) override;

   private:
    void check(bool userTriggered);
    void handleRelease(const BlamdomUpdate::Release& release, bool userTriggered);
    void resolveChecksumAndOffer(const BlamdomUpdate::Release& release, BlamdomUpdate::Asset asset, bool userTriggered);
    void offer(const BlamdomUpdate::Release& release, const BlamdomUpdate::Asset& asset, bool userTriggered);
    void install(const BlamdomUpdate::Release& release, const BlamdomUpdate::Asset& asset);
    void finishInstall(const QString& archivePath, const BlamdomUpdate::Asset& asset);
    void openReleasePage(const BlamdomUpdate::Release& release) const;
    void scheduleAutoCheck();
    void notify(const QString& title, const QString& text, bool warning = false) const;
    QString apiBase() const;
    QString repoSlug() const;

    QPointer<QWidget> m_parent;
    QString m_installDir;
    QString m_dataDir;
    std::unique_ptr<QSettings> m_settings;
    QTimer m_timer;
    bool m_busy = false;
    BlamdomUpdate::Release m_pending;
};
