/*
 * This file is part of the KDE project.
 *
 * Copyright (C) 2008 Dirk Mueller <mueller@kde.org>
 * Copyright (C) 2008 Urs Wolfer <uwolfer @ kde.org>
 * Copyright (C) 2008 Michael Howell <mhowell123@gmail.com>
 * Copyright (C) 2009,2010 Dawit Alemayehu <adawit@kde.org>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public License
 * along with this library; see the file COPYING.LIB.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 *
 */

// Own
#include "kwebpage.h"
#include "kwebwallet.h"

// Local
#include "kwebpluginfactory.h"

// KDE
#include <kprotocolmanager.h>
#include <kjobuidelegate.h>
#include <krun.h>
#include <kjobwidgets.h>

#include <kstandardshortcut.h>
#include <QDebug>
#include <kshell.h>
#include <kmimetypetrader.h>
#include <kio/accessmanager.h>
#include <kio/job.h>
#include <kio/copyjob.h>
#include <kio/jobuidelegate.h>
#include <KIO/CommandLauncherJob>
#include <KIO/OpenUrlJob>
#include <kio/renamedialog.h>
#include <kio/scheduler.h>
#include <kparts/browseropenorsavequestion.h>

// Qt
#include <QPointer>
#include <QFileInfo>
#include <QCoreApplication>
#include <QAction>
#include <QFileDialog>
#include <QWebFrame>
#include <QNetworkReply>
#include <qtemporaryfile.h>

#define QL1S(x)  QLatin1String(x)
#define QL1C(x)  QLatin1Char(x)

static void reloadRequestWithoutDisposition(QNetworkReply *reply)
{
    QNetworkRequest req(reply->request());
    req.setRawHeader("x-kdewebkit-ignore-disposition", "true");

    QWebFrame *frame = qobject_cast<QWebFrame *> (req.originatingObject());
    if (!frame) {
        return;
    }

    frame->load(req);
}

static bool isMimeTypeAssociatedWithSelf(const KService::Ptr &offer)
{
    if (!offer) {
        return false;
    }

    // qDebug() << offer->desktopEntryName();

    const QString &appName = QCoreApplication::applicationName();

    if (appName == offer->desktopEntryName() || offer->exec().trimmed().startsWith(appName)) {
        return true;
    }

    // konqueror exception since it uses kfmclient to open html content...
    if (appName == QL1S("konqueror") && offer->exec().trimmed().startsWith(QL1S("kfmclient"))) {
        return true;
    }

    return false;
}

static void extractMimeType(const QNetworkReply *reply, QString &mimeType)
{
    mimeType.clear();
    const KIO::MetaData &metaData = reply->attribute(static_cast<QNetworkRequest::Attribute>(KIO::AccessManager::MetaData)).toMap();
    if (metaData.contains(QL1S("content-type"))) {
        mimeType = metaData.value(QL1S("content-type"));
    }

    if (!mimeType.isEmpty()) {
        return;
    }

    if (!reply->hasRawHeader("Content-Type")) {
        return;
    }

    const QString value(QL1S(reply->rawHeader("Content-Type").simplified().constData()));
    const int index = value.indexOf(QL1C(';'));
    mimeType = ((index == -1) ? value : value.left(index));
}

static bool downloadResource(const QUrl &srcUrl, const QString &suggestedName = QString(),
                             QWidget *parent = nullptr, const KIO::MetaData &metaData = KIO::MetaData())
{
    const QString fileName = suggestedName.isEmpty() ? srcUrl.fileName() : suggestedName;
    // convert filename to URL using fromPath to avoid trouble with ':' in filenames (#184202)
    QUrl destUrl = QUrl::fromLocalFile(QFileDialog::getSaveFileName(parent, QString(), fileName));
    if (!destUrl.isValid()) {
        return false;
    }

    // Using KIO::copy rather than file_copy, to benefit from "dest already exists" dialogs.
    KIO::Job *job = KIO::copy(srcUrl, destUrl);

    if (!metaData.isEmpty()) {
        job->setMetaData(metaData);
    }

    job->addMetaData(QL1S("MaxCacheSize"), QL1S("0")); // Don't store in http cache.
    job->addMetaData(QL1S("cache"), QL1S("cache")); // Use entry from cache if available.
    KJobWidgets::setWindow(job, parent ? parent->window() : nullptr);
    job->uiDelegate()->setAutoErrorHandlingEnabled(true);
    return true;
}

static bool isReplyStatusOk(const QNetworkReply *reply)
{
    if (!reply || reply->error() != QNetworkReply::NoError) {
        return false;
    }

    // Check HTTP status code only for http and webdav protocols...
    const QString scheme = reply->url().scheme();
    if (scheme.startsWith(QLatin1String("http"), Qt::CaseInsensitive) ||
            scheme.startsWith(QLatin1String("webdav"), Qt::CaseInsensitive)) {
        bool ok = false;
        const int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt(&ok);
        if (!ok || statusCode < 200 || statusCode > 299) {
            return false;
        }
    }

    return true;
}

class KWebPage::KWebPagePrivate
{
public:
    KWebPagePrivate(KWebPage *page)
        : q(page)
        , inPrivateBrowsingMode(false)
    {
    }

    QWidget *windowWidget()
    {
        return (window ? window.data() : q->view());
    }

    void _k_copyResultToTempFile(KJob *job)
    {
        KIO::FileCopyJob *cJob = qobject_cast<KIO::FileCopyJob *>(job);
        if (cJob && !cJob->error()) {
            // Same as KRun::foundMimeType but with a different URL
            auto *job = new KIO::OpenUrlJob(cJob->destUrl(), mimeType);
            job->setUiDelegate(new KIO::JobUiDelegate(KJobUiDelegate::AutoHandlingEnabled, window));
            job->start();
        }
    }

    void _k_receivedContentType(KIO::Job *job, const QString &mimetype)
    {
        KIO::TransferJob *tJob = qobject_cast<KIO::TransferJob *>(job);
        if (tJob && !tJob->error()) {
            tJob->putOnHold();
            KIO::Scheduler::publishSlaveOnHold();
            // Get suggested file name...
            mimeType = mimetype;
            const QString suggestedFileName(tJob->queryMetaData(QL1S("content-disposition-filename")));
            // qDebug() << "suggested filename:" << suggestedFileName << ", mimetype:" << mimetype;
            (void) downloadResource(tJob->url(), suggestedFileName, window, tJob->metaData());
        }
    }

    void _k_contentTypeCheckFailed(KJob *job)
    {
        KIO::TransferJob *tJob = qobject_cast<KIO::TransferJob *>(job);
        // On error simply call downloadResource which will probably fail as well.
        if (tJob && tJob->error()) {
            (void)downloadResource(tJob->url(), QString(), window, tJob->metaData());
        }
    }

    KWebPage *q;
    QPointer<QWidget> window;
    QString mimeType;
    QPointer<KWebWallet> wallet;
    bool inPrivateBrowsingMode;
};

static void setActionIcon(QAction *action, const QIcon &icon)
{
    if (action) {
        action->setIcon(icon);
    }
}

static void setActionShortcut(QAction *action, const QList<QKeySequence> &shortcut)
{
    if (action) {
        action->setShortcuts(shortcut);
    }
}

KWebPage::KWebPage(QObject *parent, Integration flags)
    : QWebPage(parent), d(new KWebPagePrivate(this))
{
    // KDE KParts integration for <embed> tag...
    if (!flags || (flags & KPartsIntegration)) {
        setPluginFactory(new KWebPluginFactory(this));
    }

    QWidget *parentWidget = qobject_cast<QWidget *>(parent);
    d->window = (parentWidget ? parentWidget->window() : nullptr);

    // KDE IO (KIO) integration...
    if (!flags || (flags & KIOIntegration)) {
        KIO::Integration::AccessManager *manager = new KIO::Integration::AccessManager(this);
        // Disable QtWebKit's internal cache to avoid duplication with the one in KIO...
        manager->setCache(nullptr);
        manager->setWindow(d->window);
        manager->setEmitReadyReadOnMetaDataChange(true);
        setNetworkAccessManager(manager);
    }

    // KWallet integration...
    if (!flags || (flags & KWalletIntegration)) {
        setWallet(new KWebWallet(nullptr, (d->window ? d->window->winId() : 0)));
    }

    setActionIcon(action(Back), QIcon::fromTheme("go-previous"));
    setActionIcon(action(Forward), QIcon::fromTheme("go-next"));
    setActionIcon(action(Reload), QIcon::fromTheme("view-refresh"));
    setActionIcon(action(Stop), QIcon::fromTheme("process-stop"));
    setActionIcon(action(Cut), QIcon::fromTheme("edit-cut"));
    setActionIcon(action(Copy), QIcon::fromTheme("edit-copy"));
    setActionIcon(action(Paste), QIcon::fromTheme("edit-paste"));
    setActionIcon(action(Undo), QIcon::fromTheme("edit-undo"));
    setActionIcon(action(Redo), QIcon::fromTheme("edit-redo"));
    setActionIcon(action(SelectAll), QIcon::fromTheme("edit-select-all"));
    setActionIcon(action(InspectElement), QIcon::fromTheme("view-process-all"));
    setActionIcon(action(OpenLinkInNewWindow), QIcon::fromTheme("window-new"));
    setActionIcon(action(OpenFrameInNewWindow), QIcon::fromTheme("window-new"));
    setActionIcon(action(OpenImageInNewWindow), QIcon::fromTheme("window-new"));
    setActionIcon(action(CopyLinkToClipboard), QIcon::fromTheme("edit-copy"));
    setActionIcon(action(CopyImageToClipboard), QIcon::fromTheme("edit-copy"));
    setActionIcon(action(ToggleBold), QIcon::fromTheme("format-text-bold"));
    setActionIcon(action(ToggleItalic), QIcon::fromTheme("format-text-italic"));
    setActionIcon(action(ToggleUnderline), QIcon::fromTheme("format-text-underline"));
    setActionIcon(action(DownloadLinkToDisk), QIcon::fromTheme("document-save"));
    setActionIcon(action(DownloadImageToDisk), QIcon::fromTheme("document-save"));

    settings()->setWebGraphic(QWebSettings::MissingPluginGraphic, QIcon::fromTheme("preferences-plugin").pixmap(32, 32));
    settings()->setWebGraphic(QWebSettings::MissingImageGraphic, QIcon::fromTheme("image-missing").pixmap(32, 32));
    settings()->setWebGraphic(QWebSettings::DefaultFrameIconGraphic, QIcon::fromTheme("applications-internet").pixmap(32, 32));

    setActionShortcut(action(Back), KStandardShortcut::back());
    setActionShortcut(action(Forward), KStandardShortcut::forward());
    setActionShortcut(action(Reload), KStandardShortcut::reload());
    setActionShortcut(action(Stop), QList<QKeySequence>() << QKeySequence(Qt::Key_Escape));
    setActionShortcut(action(Cut), KStandardShortcut::cut());
    setActionShortcut(action(Copy), KStandardShortcut::copy());
    setActionShortcut(action(Paste), KStandardShortcut::paste());
    setActionShortcut(action(Undo), KStandardShortcut::undo());
    setActionShortcut(action(Redo), KStandardShortcut::redo());
    setActionShortcut(action(SelectAll), KStandardShortcut::selectAll());
}

KWebPage::~KWebPage()
{
    delete d;
}

bool KWebPage::isExternalContentAllowed() const
{
    KIO::AccessManager *manager = qobject_cast<KIO::AccessManager *>(networkAccessManager());
    if (manager) {
        return manager->isExternalContentAllowed();
    }
    return true;
}

KWebWallet *KWebPage::wallet() const
{
    return d->wallet;
}

void KWebPage::setAllowExternalContent(bool allow)
{
    KIO::AccessManager *manager = qobject_cast<KIO::AccessManager *>(networkAccessManager());
    if (manager) {
        manager->setExternalContentAllowed(allow);
    }
}

void KWebPage::setWallet(KWebWallet *wallet)
{
    // Delete the current wallet if this object is its parent...
    if (d->wallet && this == d->wallet->parent()) {
        delete d->wallet;
    }

    d->wallet = wallet;

    if (d->wallet) {
        d->wallet->setParent(this);
    }
}

void KWebPage::downloadRequest(const QNetworkRequest &request)
{
    KIO::TransferJob *job = KIO::get(request.url());
    connect(job, &KIO::TransferJob::mimeTypeFound,
            this, [this](KIO::Job *job, const QString &mimeType) { d->_k_receivedContentType(job, mimeType); });

    job->setMetaData(request.attribute(static_cast<QNetworkRequest::Attribute>(KIO::AccessManager::MetaData)).toMap());
    job->addMetaData(QL1S("MaxCacheSize"), QL1S("0")); // Don't store in http cache.
    job->addMetaData(QL1S("cache"), QL1S("cache")); // Use entry from cache if available.
    KJobWidgets::setWindow(job, d->windowWidget());
}

void KWebPage::downloadUrl(const QUrl &url)
{
    downloadRequest(QNetworkRequest(url));
}

void KWebPage::downloadResponse(QNetworkReply *reply)
{
    Q_ASSERT(reply);

    if (!reply) {
        return;
    }

    // Put the job on hold only for the protocols we know about (read: http).
    KIO::Integration::AccessManager::putReplyOnHold(reply);

    QString mimeType;
    KIO::MetaData metaData;

    if (handleReply(reply, &mimeType, &metaData)) {
        return;
    }

    // Ask OpenUrlJob to handle the response when mimetype is unknown
    // or when mimetype is inode/*
    if (mimeType.isEmpty() || mimeType.startsWith(QL1S("inode/"), Qt::CaseInsensitive)) {
        auto *job = new KIO::OpenUrlJob(reply->url(), mimeType);
        job->setSuggestedFileName(metaData.value(QL1S("content-disposition-filename")));
        job->setUiDelegate(new KIO::JobUiDelegate(KJobUiDelegate::AutoHandlingEnabled, d->windowWidget()));
        job->start();
        return;
    }
}

QString KWebPage::sessionMetaData(const QString &key) const
{
    QString value;

    KIO::Integration::AccessManager *manager = qobject_cast<KIO::Integration::AccessManager *>(networkAccessManager());
    if (manager) {
        value = manager->sessionMetaData().value(key);
    }

    return value;
}

QString KWebPage::requestMetaData(const QString &key) const
{
    QString value;

    KIO::Integration::AccessManager *manager = qobject_cast<KIO::Integration::AccessManager *>(networkAccessManager());
    if (manager) {
        value = manager->requestMetaData().value(key);
    }

    return value;
}

void KWebPage::setSessionMetaData(const QString &key, const QString &value)
{
    KIO::Integration::AccessManager *manager = qobject_cast<KIO::Integration::AccessManager *>(networkAccessManager());
    if (manager) {
        manager->sessionMetaData()[key] = value;
    }
}

void KWebPage::setRequestMetaData(const QString &key, const QString &value)
{
    KIO::Integration::AccessManager *manager = qobject_cast<KIO::Integration::AccessManager *>(networkAccessManager());
    if (manager) {
        manager->requestMetaData()[key] = value;
    }
}

void KWebPage::removeSessionMetaData(const QString &key)
{
    KIO::Integration::AccessManager *manager = qobject_cast<KIO::Integration::AccessManager *>(networkAccessManager());
    if (manager) {
        manager->sessionMetaData().remove(key);
    }
}

void KWebPage::removeRequestMetaData(const QString &key)
{
    KIO::Integration::AccessManager *manager = qobject_cast<KIO::Integration::AccessManager *>(networkAccessManager());
    if (manager) {
        manager->requestMetaData().remove(key);
    }
}

QString KWebPage::userAgentForUrl(const QUrl &_url) const
{
    const QUrl url(_url);
    const QString userAgent = KProtocolManager::userAgentForHost((url.isLocalFile() ? QL1S("localhost") : url.host()));

    if (userAgent == KProtocolManager::defaultUserAgent()) {
        return QWebPage::userAgentForUrl(_url);
    }

    return userAgent;
}

static void setDisableCookieJarStorage(QNetworkAccessManager *manager, bool status)
{
    if (manager) {
        KIO::Integration::CookieJar *cookieJar = manager ? qobject_cast<KIO::Integration::CookieJar *>(manager->cookieJar()) : nullptr;
        if (cookieJar) {
            //qDebug() << "Store cookies ?" << !status;
            cookieJar->setDisableCookieStorage(status);
        }
    }
}

bool KWebPage::acceptNavigationRequest(QWebFrame *frame, const QNetworkRequest &request, NavigationType type)
{
    // qDebug() << "url:" << request.url() << ", type:" << type << ", frame:" << frame;

    if (frame && d->wallet && type == QWebPage::NavigationTypeFormSubmitted) {
        d->wallet->saveFormData(frame);
    }

    // Make sure nothing is cached when private browsing mode is enabled...
    if (settings()->testAttribute(QWebSettings::PrivateBrowsingEnabled)) {
        if (!d->inPrivateBrowsingMode) {
            setDisableCookieJarStorage(networkAccessManager(), true);
            setSessionMetaData(QL1S("no-cache"), QL1S("true"));
            d->inPrivateBrowsingMode = true;
        }
    } else  {
        if (d->inPrivateBrowsingMode) {
            setDisableCookieJarStorage(networkAccessManager(), false);
            removeSessionMetaData(QL1S("no-cache"));
            d->inPrivateBrowsingMode = false;
        }
    }

    /*
      If the navigation request is from the main frame, set the cross-domain
      meta-data value to the current url for proper integration with KCookieJar...
    */
    if (frame == mainFrame() && type != QWebPage::NavigationTypeReload) {
        setSessionMetaData(QL1S("cross-domain"), request.url().toString());
    }

    return QWebPage::acceptNavigationRequest(frame, request, type);
}

bool KWebPage::handleReply(QNetworkReply *reply, QString *contentType, KIO::MetaData *metaData)
{
    // Reply url...
    const QUrl replyUrl(reply->url());

    // Get suggested file name...
    const KIO::MetaData &data = reply->attribute(static_cast<QNetworkRequest::Attribute>(KIO::AccessManager::MetaData)).toMap();
    const QString suggestedFileName = data.value(QL1S("content-disposition-filename"));
    if (metaData) {
        *metaData = data;
    }

    // Get the mime-type...
    QString mimeType;
    extractMimeType(reply, mimeType);
    if (contentType) {
        *contentType = mimeType;
    }

    // Let the calling function deal with handling empty or inode/* mimetypes...
    if (mimeType.isEmpty() || mimeType.startsWith(QL1S("inode/"), Qt::CaseInsensitive)) {
        return false;
    }

    // Convert executable text files to plain text...
    if (KParts::BrowserRun::isTextExecutable(mimeType)) {
        mimeType = QL1S("text/plain");
    }

    //qDebug() << "Content-disposition:" << suggestedFileName;
    //qDebug() << "Got unsupported content of type:" << mimeType << "URL:" << replyUrl;
    //qDebug() << "Error code:" << reply->error() << reply->errorString();

    if (isReplyStatusOk(reply)) {
        while (true) {
            KParts::BrowserOpenOrSaveQuestion::Result result;
            KParts::BrowserOpenOrSaveQuestion dlg(d->windowWidget(), replyUrl, mimeType);
            dlg.setSuggestedFileName(suggestedFileName);
            dlg.setFeatures(KParts::BrowserOpenOrSaveQuestion::ServiceSelection);
            result = dlg.askOpenOrSave();

            switch (result) {
            case KParts::BrowserOpenOrSaveQuestion::Open:
                // Handle Post operations that return content...
                if (reply->operation() == QNetworkAccessManager::PostOperation) {
                    d->mimeType = mimeType;
                    QFileInfo finfo(suggestedFileName.isEmpty() ? replyUrl.fileName() : suggestedFileName);
                    QTemporaryFile tempFile(QDir::tempPath() + QLatin1String("/kwebpage_XXXXXX.") + finfo.suffix());
                    tempFile.setAutoRemove(false);
                    tempFile.open();
                    const QUrl destUrl = QUrl::fromLocalFile(tempFile.fileName());
                    KIO::Job *job = KIO::file_copy(replyUrl, destUrl, 0600, KIO::Overwrite);
                    KJobWidgets::setWindow(job, d->windowWidget());
                    job->uiDelegate()->setAutoErrorHandlingEnabled(true);
                    connect(job, SIGNAL(result(KJob*)),
                            this, SLOT(_k_copyResultToTempFile(KJob*)));
                    return true;
                }

                // Ask before running any executables...
                if (KParts::BrowserRun::allowExecution(mimeType, replyUrl)) {
                    KService::Ptr offer = dlg.selectedService();
                    // HACK: The check below is necessary to break an infinite
                    // recursion that occurs whenever this function is called as a result
                    // of receiving content that can be rendered by the app using this engine.
                    // For example a text/html header that containing a content-disposition
                    // header is received by the app using this class.
                    if (isMimeTypeAssociatedWithSelf(offer)) {
                        reloadRequestWithoutDisposition(reply);
                    } else {
                        QList<QUrl> list;
                        list.append(replyUrl);
                        bool success = false;
                        // qDebug() << "Suggested file name:" << suggestedFileName;
                        if (offer) {
                            success = KRun::runService(*offer, list, d->windowWidget(), false, suggestedFileName);
                        } else {
                            success = KRun::displayOpenWithDialog(list, d->windowWidget(), false, suggestedFileName);
                            if (!success) {
                                break;
                            }
                        }
                        // For non KIO apps and cancelled Open With dialog, remove slave on hold.
                        if (!success || (offer && !offer->categories().contains(QL1S("KDE")))) {
                            KIO::SimpleJob::removeOnHold(); // Remove any slave-on-hold...
                        }
                    }
                    return true;
                }
                // TODO: Instead of silently failing when allowExecution fails, notify
                // the user why the requested action cannot be fulfilled...
                return false;
            case KParts::BrowserOpenOrSaveQuestion::Save:
                // Do not download local files...
                if (!replyUrl.isLocalFile()) {
                    QString downloadCmd(reply->property("DownloadManagerExe").toString());
                    if (!downloadCmd.isEmpty()) {
                        downloadCmd += QLatin1Char(' ');
                        downloadCmd += KShell::quoteArg(replyUrl.url());
                        if (!suggestedFileName.isEmpty()) {
                            downloadCmd += QLatin1Char(' ');
                            downloadCmd += KShell::quoteArg(suggestedFileName);
                        }
                        // qDebug() << "download command:" << downloadCmd;
                        if (KRun::runCommand(downloadCmd, view())) {
                            return true;
                        }
                    }
                    if (!downloadResource(replyUrl, suggestedFileName, d->windowWidget())) {
                        return true; // file dialog was cancelled, stop here
                    }
                }
                return true;
            case KParts::BrowserOpenOrSaveQuestion::Cancel:
            default:
                KIO::SimpleJob::removeOnHold(); // Remove any slave-on-hold...
                return true;
            }
        }
    } else {
        KService::Ptr offer = KMimeTypeTrader::self()->preferredService(mimeType);
        if (isMimeTypeAssociatedWithSelf(offer)) {
            reloadRequestWithoutDisposition(reply);
            return true;
        }
    }

    return false;
}

#include "moc_kwebpage.cpp"

