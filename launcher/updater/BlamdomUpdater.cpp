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

#include "BlamdomUpdater.h"

#include <QCoreApplication>
#include <QFile>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QProgressDialog>
#include <QStandardPaths>
#include <QTemporaryFile>
#include <QUrl>

#include "Application.h"
#include "BuildConfig.h"
#include "DesktopServices.h"
#include "modplatform/blamdom/BlamdomAPI.h"
#include "ui/dialogs/UpdateAvailableDialog.h"

using namespace BlamdomUpdate;

namespace {
constexpr int kMinAutoCheckDelayMs = 5000;  // never block startup with a network call

QNetworkRequest githubRequest(const QUrl& url)
{
    QNetworkRequest req(url);
    req.setRawHeader("Accept", "application/vnd.github+json");
    req.setRawHeader("X-GitHub-Api-Version", "2022-11-28");
    req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("BlamdomLauncher/%1").arg(BuildConfig.versionString()).toUtf8());
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    req.setTransferTimeout(30000);
    return req;
}
}  // namespace

BlamdomUpdater::BlamdomUpdater(QWidget* parent, const QString& appDir, const QString& dataDir)
    : m_parent(parent), m_installDir(QCoreApplication::applicationDirPath()), m_dataDir(dataDir)
{
    Q_UNUSED(appDir)
    cleanupLeftovers(m_installDir);  // previous version kept by the last update

    m_settings = std::make_unique<QSettings>(QDir(dataDir).absoluteFilePath(QStringLiteral("blamdom_update.cfg")), QSettings::IniFormat);
    m_timer.setSingleShot(true);
    connect(&m_timer, &QTimer::timeout, this, [this] {
        check(false);
        scheduleAutoCheck();
    });
    scheduleAutoCheck();
}

// ---------- settings ----------
bool BlamdomUpdater::getAutomaticallyChecksForUpdates()
{
    return m_settings->value(QStringLiteral("auto_check"), true).toBool();
}
double BlamdomUpdater::getUpdateCheckInterval()
{
    bool ok = false;
    const double v = m_settings->value(QStringLiteral("update_interval"), 86400).toDouble(&ok);
    return ok ? v : 86400;
}
bool BlamdomUpdater::getBetaAllowed()
{
    return false;  // releases only; /releases/latest never returns prereleases
}
void BlamdomUpdater::setAutomaticallyChecksForUpdates(bool check)
{
    m_settings->setValue(QStringLiteral("auto_check"), check);
    m_settings->sync();
    scheduleAutoCheck();
}
void BlamdomUpdater::setUpdateCheckInterval(double seconds)
{
    m_settings->setValue(QStringLiteral("update_interval"), seconds);
    m_settings->sync();
    scheduleAutoCheck();
}
void BlamdomUpdater::setBetaAllowed(bool)
{
}

void BlamdomUpdater::scheduleAutoCheck()
{
    m_timer.stop();
    if (!getAutomaticallyChecksForUpdates()) {
        return;
    }
    const auto last = QDateTime::fromString(m_settings->value(QStringLiteral("last_check")).toString(), Qt::ISODate);
    qint64 delayMs = kMinAutoCheckDelayMs;
    if (last.isValid()) {
        const qint64 dueInMs = last.addSecs(static_cast<qint64>(getUpdateCheckInterval())).toMSecsSinceEpoch() - QDateTime::currentMSecsSinceEpoch();
        delayMs = qMax<qint64>(kMinAutoCheckDelayMs, dueInMs);
    }
    // QTimer interval is an int; cap at ~24 days and re-evaluate
    m_timer.start(static_cast<int>(qMin<qint64>(delayMs, 2000000000LL)));
}

QString BlamdomUpdater::apiBase() const
{
    const auto override = qEnvironmentVariable("BLAMDOM_UPDATE_API");  // for testing against a local server
    return override.isEmpty() ? QStringLiteral("https://api.github.com") : override;
}

QString BlamdomUpdater::repoSlug() const
{
    QString slug = BuildConfig.UPDATER_GITHUB_REPO;
    slug.remove(QStringLiteral("https://github.com/"));
    while (slug.endsWith('/')) {
        slug.chop(1);
    }
    return slug;
}

void BlamdomUpdater::notify(const QString& title, const QString& text, bool warning) const
{
    QMessageBox box(warning ? QMessageBox::Warning : QMessageBox::Information, title, text, QMessageBox::Ok, m_parent);
    box.setMinimumWidth(460);
    box.exec();
}

// ---------- check ----------
void BlamdomUpdater::checkForUpdates()
{
    check(true);
}

void BlamdomUpdater::check(bool userTriggered)
{
    if (m_busy) {
        return;
    }
    m_busy = true;
    m_settings->setValue(QStringLiteral("last_check"), QDateTime::currentDateTime().toString(Qt::ISODate));
    m_settings->sync();

    const QUrl url(QStringLiteral("%1/repos/%2/releases/latest").arg(apiBase(), repoSlug()));
    qDebug() << "Checking for launcher updates:" << url.toString();
    auto* reply = APPLICATION->network()->get(githubRequest(url));
    connect(reply, &QNetworkReply::finished, this, [this, reply, userTriggered] {
        reply->deleteLater();
        const QByteArray body = reply->readAll();
        const auto status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        m_busy = false;

        if (reply->error() != QNetworkReply::NoError && status != 200) {
            qWarning() << "Update check failed:" << reply->errorString();
            if (userTriggered) {
                notify(tr("Update check failed"), tr("Could not reach the update server:\n%1").arg(reply->errorString()), true);
            }
            return;
        }
        QString err;
        const auto release = parseRelease(body, &err);
        if (!release) {
            qWarning() << "Update check: no usable release:" << err;
            if (userTriggered) {
                notify(tr("No update available"), tr("There is no published release to update to yet."));
            }
            return;
        }
        handleRelease(*release, userTriggered);
    });
}

void BlamdomUpdater::handleRelease(const Release& release, bool userTriggered)
{
    const QString current = BuildConfig.versionString();
    if (!isNewer(release.version, current)) {
        qDebug() << "Launcher is up to date:" << current << "(latest" << release.version << ")";
        if (userTriggered) {
            notify(tr("No Update Available"), tr("You are running the latest version (%1).").arg(current));
        }
        return;
    }
    if (!userTriggered && m_settings->value(QStringLiteral("skipped_version")).toString() == release.version) {
        qDebug() << "Update" << release.version << "was skipped by the user";
        return;
    }
#if defined(Q_OS_WIN)
    const auto asset = pickAsset(release, platformKey(), QStringLiteral(".zip"));
#else
    const auto asset = pickAsset(release, platformKey(), QStringLiteral(".tar.gz"));
#endif
    if (!asset) {
        qWarning() << "Release" << release.tag << "has no build for" << platformKey();
        if (userTriggered) {
            notify(tr("Update available"),
                   tr("Version %1 is out, but it has no download for this system (%2). See the release page.").arg(release.version, platformKey()));
            openReleasePage(release);
        }
        return;
    }
    resolveChecksumAndOffer(release, *asset, userTriggered);
}

void BlamdomUpdater::resolveChecksumAndOffer(const Release& release, Asset asset, bool userTriggered)
{
    if (!asset.sha256.isEmpty()) {
        offer(release, asset, userTriggered);
        return;
    }
    // No digest from the API: fall back to the release's SHA256SUMS file.
    for (const auto& a : release.assets) {
        if (a.name == QStringLiteral("SHA256SUMS")) {
            m_busy = true;
            auto* reply = APPLICATION->network()->get(githubRequest(QUrl(a.url)));
            connect(reply, &QNetworkReply::finished, this, [this, reply, release, asset, userTriggered]() mutable {
                reply->deleteLater();
                m_busy = false;
                if (reply->error() == QNetworkReply::NoError) {
                    asset.sha256 = findChecksum(QString::fromUtf8(reply->readAll()), asset.name);
                }
                if (asset.sha256.isEmpty()) {
                    qWarning() << "No checksum for" << asset.name << "- refusing to auto-install";
                    if (userTriggered) {
                        notify(tr("Update available"), tr("Version %1 is out but its checksum is missing, so it cannot be installed automatically.").arg(release.version));
                        openReleasePage(release);
                    }
                    return;
                }
                offer(release, asset, userTriggered);
            });
            return;
        }
    }
    qWarning() << "Release has neither an asset digest nor SHA256SUMS - refusing to auto-install";
    if (userTriggered) {
        notify(tr("Update available"), tr("Version %1 is out but is not verifiable, so it cannot be installed automatically.").arg(release.version));
        openReleasePage(release);
    }
}

void BlamdomUpdater::offer(const Release& release, const Asset& asset, bool userTriggered)
{
    Q_UNUSED(userTriggered)
    int result = UpdateAvailableDialog::Install;
    if (qEnvironmentVariableIsEmpty("BLAMDOM_UPDATE_AUTOINSTALL")) {  // the env var exists only so the update path can be tested unattended
        UpdateAvailableDialog dlg(BuildConfig.versionString(), release.version, release.notes, m_parent);
        result = dlg.exec();
    }
    switch (result) {
        case UpdateAvailableDialog::Install:
            install(release, asset);
            break;
        case UpdateAvailableDialog::Skip:
            m_settings->setValue(QStringLiteral("skipped_version"), release.version);
            m_settings->sync();
            break;
        default:
            break;
    }
}

// ---------- install ----------
void BlamdomUpdater::openReleasePage(const Release& release) const
{
    const QString url = release.htmlUrl.isEmpty() ? QStringLiteral("%1/releases/latest").arg(BuildConfig.UPDATER_GITHUB_REPO) : release.htmlUrl;
    DesktopServices::openUrl(QUrl(url));
}

void BlamdomUpdater::install(const Release& release, const Asset& asset)
{
    QString why;
    if (!canSelfUpdate(m_installDir, m_dataDir, APPLICATION->isPortable(), &why)) {
        const QString fix = tr("For self-updates, install per-user instead (no root needed):\n"
                               "    curl -fsSL %1/install.sh | sh\n"
                               "It installs to your home folder, and the old system-wide install can be removed afterwards — the installer tells you how.")
                                .arg(Blamdom::BlamdomAPI::PROD_URL);
        notify(tr("Update available"),
               tr("Version %1 is available, but this install can't update itself:\n\n%2\n\n%3\n\nThe download page will open instead.")
                   .arg(release.version, why, fix));
        openReleasePage(release);
        return;
    }

#if defined(Q_OS_WIN)
    auto* file = new QTemporaryFile(QDir::temp().filePath(QStringLiteral("blamdom-update-XXXXXX.zip")), this);
#else
    auto* file = new QTemporaryFile(QDir::temp().filePath(QStringLiteral("blamdom-update-XXXXXX.tar.gz")), this);
#endif
    if (!file->open()) {
        notify(tr("Update failed"), tr("Could not create a temporary file: %1").arg(file->errorString()), true);
        file->deleteLater();
        return;
    }

    auto* progress = new QProgressDialog(tr("Downloading Blamdom Launcher %1…").arg(release.version), tr("Cancel"), 0, 100, m_parent);
    progress->setWindowModality(Qt::WindowModal);
    progress->setMinimumDuration(0);
    progress->setAutoClose(false);
    progress->setAutoReset(false);
    progress->setValue(0);

    auto* reply = APPLICATION->network()->get(githubRequest(QUrl(asset.url)));
    m_busy = true;
    connect(reply, &QNetworkReply::readyRead, this, [file, reply] { file->write(reply->readAll()); });
    connect(reply, &QNetworkReply::downloadProgress, this, [progress, asset](qint64 got, qint64 total) {
        const qint64 t = total > 0 ? total : asset.size;
        if (t > 0) {
            progress->setValue(static_cast<int>(got * 100 / t));
        }
    });
    connect(progress, &QProgressDialog::canceled, reply, &QNetworkReply::abort);
    connect(reply, &QNetworkReply::finished, this, [this, reply, file, progress, asset] {
        reply->deleteLater();
        file->write(reply->readAll());
        file->flush();
        const auto path = file->fileName();
        const bool ok = reply->error() == QNetworkReply::NoError;
        const auto errText = reply->errorString();
        disconnect(progress, &QProgressDialog::canceled, reply, nullptr);  // closing a QProgressDialog emits canceled()
        progress->close();
        progress->deleteLater();
        m_busy = false;
        if (!ok) {
            if (reply->error() != QNetworkReply::OperationCanceledError) {
                notify(tr("Update failed"), tr("The download failed: %1").arg(errText), true);
            }
            file->deleteLater();
            return;
        }
        file->setAutoRemove(false);  // kept until finishInstall has used it
        finishInstall(path, asset);
        QFile::remove(path);
        file->deleteLater();
    });
}

void BlamdomUpdater::finishInstall(const QString& archivePath, const Asset& asset)
{
    auto fail = [this](const QString& why) {
        qWarning() << "Update failed:" << why;
        notify(tr("Update failed"), tr("%1\n\nYour current version was not changed.").arg(why), true);
    };

    if (!verifySha256(archivePath, asset.sha256)) {
        fail(tr("The downloaded file does not match its published checksum, so it was discarded."));
        return;
    }

    const QString exeName = QFileInfo(QCoreApplication::applicationFilePath()).fileName();

#if defined(Q_OS_WIN)
    // Windows cannot replace a running executable in place: stage the new version in a
    // sibling folder, write the pending-update marker, and relaunch from the staged copy.
    // The new process moves itself into the canonical folder at startup
    // (BlamdomUpdate::applyPendingUpdate), keeping the user's shortcuts working.
    const QString staging = m_installDir + QStringLiteral(".update");
    QString err;
    QDir(staging).removeRecursively();
    if (!extractZip(archivePath, staging, &err) || !validateStaged(staging, exeName, &err)) {
        QDir(staging).removeRecursively();
        fail(err);
        return;
    }
    {
        QFile marker(QDir(staging).filePath(pendingUpdateMarkerName()));
        if (!marker.open(QIODevice::WriteOnly)) {
            QDir(staging).removeRecursively();
            fail(tr("Could not write the update marker, so the update was discarded."));
            return;
        }
        marker.write(QDir(m_installDir).absolutePath().toUtf8() + "\n");
        marker.close();
    }
    qInfo() << "Windows update staged at" << staging << "- restarting from there";
    QString relErr;
    if (!relaunchLater(QDir(staging).filePath(exeName), 3, &relErr)) {
        QDir(staging).removeRecursively();
        fail(relErr);
        return;
    }
    QCoreApplication::quit();
#else
    const QString staging = m_installDir + QStringLiteral(".update-new");
    QString err;
    QDir(staging).removeRecursively();
    if (!extractTarGz(archivePath, staging, &err) || !validateStaged(staging, exeName, &err)) {
        QDir(staging).removeRecursively();
        fail(err);
        return;
    }

    const QString exePath = QDir(m_installDir).filePath(exeName);  // computed BEFORE the swap: afterwards applicationFilePath() points into the old folder
    QStringList args = QCoreApplication::arguments();
    args.removeFirst();

    if (!swapInstall(m_installDir, staging, &err)) {
        QDir(staging).removeRecursively();
        fail(err);
        return;
    }
    qInfo() << "Update installed; restarting from" << exePath;

    // Relaunch after a short delay: the new process must not meet this one still holding the single-instance lock.
    QStringList shArgs = { QStringLiteral("-c"), QStringLiteral("sleep 2; exec \"$0\" \"$@\""), exePath };
    shArgs += args;
    QProcess::startDetached(QStringLiteral("/bin/sh"), shArgs);
    QCoreApplication::quit();
#endif
}
