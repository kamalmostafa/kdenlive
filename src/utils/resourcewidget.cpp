/***************************************************************************
 *   Copyright (C) 2011 by Jean-Baptiste Mardelle (jb@kdenlive.org)        *
 *                                                                         *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA          *
 ***************************************************************************/

/*
 *
 */
#include "resourcewidget.h"
#include "freesound.h"
#include "openclipart.h"
#include "archiveorg.h"
#include "kdenlivesettings.h"

#include <QPushButton>
#include <QListWidget>
#include <QAction>
#include <QMenu>
#include <QFileDialog>
#include <QNetworkConfigurationManager>
#include <QDebug>
#include <QFontDatabase>
#include <QTemporaryFile>

#include <KSharedConfig>
#include <klocalizedstring.h>
#include <kio/job.h>
#include <KIO/SimpleJob>
#include <KRun>
#include <KConfigGroup>
#include <KPixmapSequence>
#include <KFileItem>
#include <KMessageBox>
#include <QUrlQuery>
#include <QNetworkRequest>
#include <QNetworkAccessManager>
#include <QJsonParseError>
#include <QJsonDocument>
#include <QMovie>
#include <QPixmap>

#ifdef QT5_USE_WEBKIT
#include "qt-oauth-lib/oauth2.h"
#endif

ResourceWidget::ResourceWidget(const QString & folder, QWidget * parent) :
    QDialog(parent),
    m_folder(folder),
    m_currentService(NULL),
    m_movie(NULL)
{
    setFont(QFontDatabase::systemFont(QFontDatabase::SmallestReadableFont));
    setupUi(this);
    setAttribute(Qt::WA_DeleteOnClose);

    m_tmpThumbFile = new QTemporaryFile;
    service_list->addItem(i18n("Freesound Audio Library"), FREESOUND);
    service_list->addItem(i18n("Archive.org Video Library"), ARCHIVEORG);
    service_list->addItem(i18n("Open Clip Art Graphic Library"), OPENCLIPART);
    setWindowTitle(i18n("Search Online Resources"));
    QPalette p = palette();
    p.setBrush(QPalette::Base, p.window());
    info_browser->setPalette(p);
    connect(button_search, SIGNAL(clicked()), this, SLOT(slotStartSearch()));
    connect(search_results, SIGNAL(currentRowChanged(int)), this, SLOT(slotUpdateCurrentSound()));
    connect(button_preview, SIGNAL(clicked()), this, SLOT(slotPlaySound()));
    connect(button_import, SIGNAL(clicked()), this, SLOT(slotSaveItem()));
    connect(item_license, SIGNAL(leftClickedUrl(QString)), this, SLOT(slotOpenUrl(QString)));
    connect(service_list, SIGNAL(currentIndexChanged(int)), this, SLOT(slotChangeService()));

    m_networkManager = new QNetworkConfigurationManager(this);

    if (!m_networkManager->isOnline()) {
        slotOnlineChanged(false);
    }
    connect(m_networkManager, SIGNAL(onlineStateChanged(bool)), this, SLOT(slotOnlineChanged(bool)));
    connect(page_next, SIGNAL(clicked()), this, SLOT(slotNextPage()));
    connect(page_prev, SIGNAL(clicked()), this, SLOT(slotPreviousPage()));
    connect(page_number, SIGNAL(valueChanged(int)), this, SLOT(slotStartSearch(int)));
    connect(info_browser, SIGNAL(anchorClicked(QUrl)), this, SLOT(slotOpenLink(QUrl)));

    m_networkAccessManager = new QNetworkAccessManager(this);

    m_autoPlay = new QAction(i18n("Auto Play"), this);
    m_autoPlay->setCheckable(true);
    QMenu *resourceMenu = new QMenu;
    resourceMenu->addAction(m_autoPlay);
    config_button->setMenu(resourceMenu);
    config_button->setIcon(QIcon::fromTheme(QStringLiteral("configure")));

    sound_box->setEnabled(false);
    search_text->setFocus();
    connect(search_text, SIGNAL(returnPressed()), this, SLOT(slotStartSearch()));

#ifdef QT5_USE_WEBKIT
    m_pOAuth2 = new OAuth2(this);
    connect(m_pOAuth2, SIGNAL(accessTokenReceived(QString)), this, SLOT(slotAccessTokenReceived(QString)));
    connect(m_pOAuth2, SIGNAL(accessDenied()), this, SLOT(slotFreesoundAccessDenied()));
    connect(m_pOAuth2, SIGNAL( UseHQPreview()), this, SLOT(slotFreesoundUseHQPreview()));
    connect(m_pOAuth2, SIGNAL(Canceled()), this, SLOT(slotFreesoundCanceled()));
#endif
    m_currentService = new FreeSound(search_results);
    m_currentService->slotStartSearch("dummy", 0);// Run a dummy search to initialise the search.
                                    // for reasons I (ttguy) can not fathom the first search that gets run fails
                                    // with The file or folder http://www.freesound.org/apiv2/search/t<blah blay> does not exist.
                                    // but subsequent identicle search will work. With this kludge in place we do not have to click the search button
                                    // twice to get search to run
    slotChangeService();
    loadConfig();
}
/**
 * @brief ResourceWidget::~ResourceWidget
 */
ResourceWidget::~ResourceWidget()
{
    delete m_currentService;
    delete m_tmpThumbFile;
    delete m_movie;
    delete m_networkAccessManager;
    saveConfig();
}
/**
 * @brief ResourceWidget::loadConfig
 */
void ResourceWidget::loadConfig()
{
    KSharedConfigPtr config = KSharedConfig::openConfig();
    KConfigGroup resourceConfig(config, "ResourceWidget");
    QList<int> size;
    size << 100 << 400;
    splitter->setSizes(resourceConfig.readEntry( "mainSplitter", size));
}
/**
 * @brief ResourceWidget::saveConfig
 */
void ResourceWidget::saveConfig()
{
    KSharedConfigPtr config = KSharedConfig::openConfig();
    KConfigGroup resourceConfig(config, "ResourceWidget");
    resourceConfig.writeEntry(QStringLiteral("mainsplitter"), splitter->size());
    config->sync();
}
/**
 * @brief ResourceWidget::slotStartSearch
 * @param page
 * connected to the button_search clicked signal in ResourceWidget constructor
 * also connected to the page_number value changed signal in ResourceWidget constructor
 * Calls slotStartSearch on the object for the currently selected service. Ie calls
 * FreeSound::slotStartSearch , ArchiveOrg:slotStartSearch and OpenClipArt:slotStartSearch
 */
void ResourceWidget::slotStartSearch(int page)
{
    this->setCursor(Qt::WaitCursor);
    info_browser->clear();
    page_number->blockSignals(true);
    page_number->setValue(page);
    page_number->blockSignals(false);
    m_currentService->slotStartSearch(search_text->text(), page);
}
/**
 * @brief ResourceWidget::slotUpdateCurrentSound - Fires when user selects a different item in the list of found items

 * This is not just for sounds. It fires for clip art and videos too.
 */
void ResourceWidget::slotUpdateCurrentSound()
{
    if (!m_autoPlay->isChecked()){
        m_currentService->stopItemPreview(NULL);
         button_preview->setText(i18n("Preview"));
    }
    item_license->clear();
    m_title.clear();


    GifLabel->clear();
    m_desc.clear();
    m_meta.clear();
    QListWidgetItem *item = search_results->currentItem();// get the item the user selected
    if (!item) {
        sound_box->setEnabled(false);// sound_box  is the group of objects in the Online resources window on the RHS with the
        // preview and import buttons and the details about the currently selected item.
        // if nothing is selected then we disable all these
        return;
    }
    m_currentInfo = m_currentService->displayItemDetails(item); // Not so much displaying the items details
                                                        // This getting the items details into m_currentInfo
    
    if (m_autoPlay->isChecked() && m_currentService->hasPreview)
        m_currentService->startItemPreview(item);
    sound_box->setEnabled(true);// enable the group with the preview and import buttons
    QString title = "<h3>" + m_currentInfo.itemName; // title is not just a title. It is a whole lot of HTML for displaying the
                                                    // the info for the current item.
                                                    // updateLayout() adds  m_image,m_desc and m_meta to the title to make up the html that displays on the widget
    if (!m_currentInfo.infoUrl.isEmpty())
        title += QStringLiteral(" (<a href=\"%1\">%2</a>)").arg(m_currentInfo.infoUrl).arg(i18nc("the url link pointing to a web page", "link"));
    title.append("</h3>");
    
    if (!m_currentInfo.authorUrl.isEmpty()) {
        title += QStringLiteral("<a href=\"%1\">").arg(m_currentInfo.authorUrl);
        if (!m_currentInfo.author.isEmpty())
            title.append(m_currentInfo.author);
        else title.append(i18n("Author"));
        title.append("</a><br />");
    }
    else if (!m_currentInfo.author.isEmpty())
        title.append(m_currentInfo.author + "<br />");
    else
        title.append("<br />");
    
    slotSetTitle(title);// updates the m_title var with the new HTML. Calls updateLayout()
                        // that sets/ updates the html in  info_browser
    if (!m_currentInfo.description.isEmpty()) slotSetDescription(m_currentInfo.description);
    if (!m_currentInfo.license.isEmpty()) parseLicense(m_currentInfo.license);
}

/**
 * @brief  Downloads thumbnail file from url and saves it to tmp directory - temporyFile
 *
 * The same temp file is recycled.
 * Loads the image into the ResourceWidget window.
 * Connected to signal AbstractService::GotThumb
 * */
void ResourceWidget::slotLoadThumb(const QString &url)
{
    QUrl img(url);
    if (img.isEmpty()) return;
    m_tmpThumbFile->close();
    if (m_tmpThumbFile->open()) {
        KIO::FileCopyJob *copyjob = KIO::file_copy(img, QUrl::fromLocalFile(m_tmpThumbFile->fileName()), -1, KIO::HideProgressInfo | KIO::Overwrite);
        if (copyjob->exec()) {
            slotSetImage(m_tmpThumbFile->fileName());
        }
    }
}
/**
 * @brief ResourceWidget::slotLoadPreview
 * @param url
 * Only in use by the ArchiveOrg video search. This slot starts a job to copy down the amimated GIF for use as a preview file
 * slotLoadAnimatedGif is called when the download finishes.
 * The clipart search does not have a preview option and the freesound search loads the preview file on demand via slotPlaySound()
 */
void ResourceWidget::slotLoadPreview(const QString &url)
{
    QUrl gif_url(url);
    if (gif_url.isEmpty()) return;
    m_tmpThumbFile->close();
    if (m_tmpThumbFile->open()) {
        KIO::FileCopyJob *copyjob = KIO::file_copy(gif_url, QUrl::fromLocalFile(m_tmpThumbFile->fileName()), -1, KIO::HideProgressInfo | KIO::Overwrite);
        connect(copyjob, SIGNAL(result(KJob*)), this, SLOT(slotLoadAnimatedGif(KJob*)));
        copyjob->start();
    }
}
/**
 * @brief ResourceWidget::slotLoadAnimatedGif
 * @param job
 * Notified when the download of the animated gif is completed.  connected via ResourceWidget::slotLoadPreview
 * Displays this gif in the QLabel
 */
void ResourceWidget::slotLoadAnimatedGif(KJob *job)
{
 if (!job->error() )
 {
    if (m_movie) {
        delete m_movie;
    }
    m_movie = new QMovie(m_tmpThumbFile->fileName());
    GifLabel->clear();
    GifLabel->setMovie(m_movie);// pass a pointer to a QMovie
    m_movie->start();
 }
}

/**
 * @brief ResourceWidget::slotDisplayMetaInfo - Fires when meta info has been updated
 *
 * connected to gotMetaInfo(QMap) slot in each of the services classes (FreeSound, ArchiveOrg). Copies itemDownload
 * from metaInfo into m_currentInfo - used by FreeSound case because with new freesound API the
 * item download data is obtained from a secondary location and is populated into metaInfo
 */
void ResourceWidget::slotDisplayMetaInfo(const QMap<QString, QString> &metaInfo)
{
    if (metaInfo.contains(QStringLiteral("license"))) {
        parseLicense(metaInfo.value(QStringLiteral("license")));
    }
    if (metaInfo.contains(QStringLiteral("description"))) {
        slotSetDescription(metaInfo.value(QStringLiteral("description")));
    }
    if (metaInfo.contains(QLatin1String("itemDownload"))) {
        m_currentInfo.itemDownload=metaInfo.value(QLatin1String("itemDownload"));
    }
    if (metaInfo.contains(QLatin1String("itemPreview"))) {
        if (m_autoPlay->isChecked()) {
            m_currentService->startItemPreview(search_results->currentItem());
            button_preview->setText(i18n("Stop"));
        }
    }
    if (metaInfo.contains(QLatin1String("fileType"))) {
         m_currentInfo.fileType=metaInfo.value(QLatin1String("fileType"));
    }
    if (metaInfo.contains(QLatin1String("HQpreview"))) {
         m_currentInfo.HQpreview=metaInfo.value(QLatin1String("HQpreview"));
    }



}


/**
 * @brief ResourceWidget::slotPlaySound
 * fires when button_preview is clicked. This button is clicked again to stop the preview
 */
void ResourceWidget::slotPlaySound()
{
    QString caption;
    QString sStop =i18n("Stop");
    QString sPreview = i18n("Preview");
    if (!m_currentService)
        return;
    caption= button_preview->text();
    if (caption.contains(sPreview))
    {
        const bool started = m_currentService->startItemPreview(search_results->currentItem());
        if (started)
            button_preview->setText(i18n("Stop"));
    }
    else
    {
        m_currentService->stopItemPreview(search_results->currentItem());

        button_preview->setText(i18n("Preview"));
    }
}

/**
 * @brief Fires when import button on the ResourceWidget is clicked and also called by slotOpenLink()
 *
 * Opens a dialog for user to choose a save location.
 * If not freesound Starts a file download job and Connects the job to slotGotFile().
   If is freesound  creates an OAuth2 object to connect to freesounds oauth2 authentication.
  The  OAuth2 object is connected to a number of slots including  ResourceWidget::slotAccessTokenReceived
  Calls OAuth2::obtainAccessToken to kick off the freesound authentication
*/
void ResourceWidget::slotSaveItem(const QString &originalUrl)
{
    QUrl saveUrl;

    QListWidgetItem *item = search_results->currentItem();
    if (!item) return;
    QString path = m_folder;
    QString ext;
    QString sFileExt;
    if (!path.endsWith('/')) path.append('/');
    if (!originalUrl.isEmpty()) {
        path.append(QUrl(originalUrl).fileName());
        ext = "*." + QUrl(originalUrl).fileName().section('.', -1);
        m_currentInfo.itemDownload = originalUrl;
    }
    else {
        path.append(m_currentService->getDefaultDownloadName(item));

        if(m_currentService->serviceType==FREESOUND)
        {
#ifdef QT5_USE_WEBKIT
            sFileExt = m_currentService->getExtension(search_results->currentItem());
#else
            sFileExt = QStringLiteral("*.") + m_currentInfo.HQpreview.section('.', -1);
#endif
            if ( sFileExt.isEmpty())
                sFileExt=QStringLiteral("*.") + m_currentInfo.fileType;// if the file name had no extension then use the file type freesound says it is.
            ext ="Audio ("+ sFileExt +");;All Files(*.*)";

        }
        else if(m_currentService->serviceType==OPENCLIPART)
        {
            ext = "Images (" + m_currentService->getExtension(search_results->currentItem()) +");;All Files(*.*)";
        }
        else
        {
             ext = "Video (" + m_currentService->getExtension(search_results->currentItem()) +");;All Files(*.*)";
        }
    }
    QUrl srcUrl(m_currentInfo.itemDownload);
    mSaveLocation = GetSaveFileNameAndPathS(path, ext );
    if (mSaveLocation.isEmpty())//user canceled save
        return;

    if (m_currentService->serviceType != FREESOUND)
    {
        saveUrl=QUrl::fromLocalFile (mSaveLocation);
    }
    slotSetDescription("");
    button_import->setEnabled(false); // disable buttons while download runs. enabled in slotGotFile
#ifdef QT5_USE_WEBKIT
    if(m_currentService->serviceType==FREESOUND)// open a dialog to authenticate with free sound and download the file
    {
        m_pOAuth2->obtainAccessToken();// when  job finished   ResourceWidget::slotAccessTokenReceived will be called
    }
    else// not freesound - do file download via a KIO file copy job
    {
        DoFileDownload(srcUrl, QUrl (saveUrl));
    }
#else
    saveUrl=QUrl::fromLocalFile (mSaveLocation);
    if(m_currentService->serviceType==FREESOUND) {
        // No OAuth, default to HQ preview
        srcUrl = QUrl(m_currentInfo.HQpreview);
    }
    DoFileDownload(srcUrl, QUrl (saveUrl));
#endif
}

/**
 * @brief ResourceWidget::DoFileDownload
 * @param srcUrl source url
 * @param saveUrl destination url
 * Called by ResourceWidget::slotSaveItem() for non freesound downloads. Called by ResourceWidget::slotFreesoundUseHQPreview()
 * when user chooses to use HQ preview file from freesound
 * Starts a file copy job to download the file. When file finishes dowloading slotGotFile will be called
 */
void ResourceWidget::DoFileDownload(QUrl srcUrl, QUrl saveUrl)
{
    KIO::FileCopyJob * getJob = KIO::file_copy(srcUrl, saveUrl, -1, KIO::Overwrite);
    KFileItem info(srcUrl);
    getJob->setSourceSize(info.size());
    getJob->setProperty("license", item_license->text());
    getJob->setProperty("licenseurl", item_license->url());
    getJob->setProperty("originurl", m_currentInfo.itemDownload);
    if (!m_currentInfo.authorUrl.isEmpty()) getJob->setProperty("author", m_currentInfo.authorUrl);
    else if (!m_currentInfo.author.isEmpty()) getJob->setProperty("author", m_currentInfo.author);
    connect(getJob, SIGNAL(result(KJob*)), this, SLOT(slotGotFile(KJob*)));
    getJob->start();
}

/**
 * @brief ResourceWidget::slotFreesoundUseHQPreview
 * Fires when user clicks the Use HQ preview button on the free sound login dialog
 */
void ResourceWidget::slotFreesoundUseHQPreview()
{

    mSaveLocation=mSaveLocation + ".mp3";// HQ previews are .mp3 files - so append this to file name previously choosen
    if (QFile::exists(mSaveLocation))// check that this newly created file name file does not already exist
    {
        int ret = QMessageBox::warning(this, i18n("File Exists"),
                                       i18n("HQ preview files are all mp3 files. We have added .mp3 as a file extension to the destination file name you chose. However, there is an existing file of this name present. \n Do you want to overwrite the existing file?. ") + "\n" + mSaveLocation,
                                       QMessageBox::Yes | QMessageBox::No,
                                       QMessageBox::No);
        if (ret==QMessageBox::No)
        {
            button_import->setEnabled(true);
            return ;
        }
    }
    QUrl saveUrl;
    saveUrl=QUrl::fromLocalFile (mSaveLocation);
    QUrl srcUrl(m_currentInfo.HQpreview);
    DoFileDownload( srcUrl,  saveUrl);


}
/**
 * @brief ResourceWidget::slotFreesoundCanceled
 * Fires when user cancels out of the Freesound Login dialog
 */
void ResourceWidget::slotFreesoundCanceled()
{
    button_import->setEnabled(true);
}

/**
 * @brief ResourceWidget::slotGotFile - fires when the file copy job started by  ResourceWidget::slotSaveItem() completes
 * emits addClip which causes clip to be added to the project bin.
 * Enables the import button
 * @param job
 */
void ResourceWidget::slotGotFile(KJob *job)

{
    QString errTxt;
    button_import->setEnabled(true);
    if (job->error() )
    {

        errTxt  =job->errorString();
        KMessageBox::sorry(this, errTxt, i18n("Error Loading Data"));

        qDebug()<<"//file import job errored: "<<errTxt;
        return;
    }
    else
    {
        KIO::FileCopyJob* copyJob = static_cast<KIO::FileCopyJob*>( job );
        const QUrl filePath = copyJob->destUrl();

        KMessageBox::information(this,i18n( "Resource saved to ") + filePath.path(), i18n("Data Imported"));
        emit addClip(filePath);
    }
}


/**
 * @brief ResourceWidget::slotOpenUrl. Opens the file on the URL using the associated application via a KRun object
 *
 *
 * called by slotOpenLink() so this will open .html in the users associated browser
 * @param url
 */
void ResourceWidget::slotOpenUrl(const QString &url)
{
    new KRun(QUrl(url), this);
}
/**
 * @brief ResourceWidget::slotChangeService - fires when user changes what online resource they want to search agains via the dropdown list

  Also fires when widget first opens
*/
void ResourceWidget::slotChangeService()
{
    info_browser->clear();
    delete m_currentService;
    m_currentService = NULL;
    SERVICETYPE service = (SERVICETYPE) service_list->itemData(service_list->currentIndex()).toInt();
    if (service == FREESOUND) {
        m_currentService = new FreeSound(search_results);
    } else if (service == OPENCLIPART) {
        m_currentService = new OpenClipArt(search_results);
    } else if (service == ARCHIVEORG) {
        m_currentService = new ArchiveOrg(search_results);
        connect(m_currentService, SIGNAL(gotPreview(QString)), this, SLOT(slotLoadPreview(QString)));
    } else return;

    connect(m_currentService, SIGNAL(gotMetaInfo(QString)), this, SLOT(slotSetMetadata(QString)));
    connect(m_currentService, SIGNAL(gotMetaInfo(QMap<QString,QString>)), this, SLOT(slotDisplayMetaInfo(QMap<QString,QString>)));
    connect(m_currentService, SIGNAL(maxPages(int)), this, SLOT(slotSetMaximum(int)));
    connect(m_currentService, SIGNAL(searchInfo(QString)), search_info, SLOT(setText(QString)));
    connect(m_currentService, SIGNAL(gotThumb(QString)), this, SLOT(slotLoadThumb(QString)));
    connect(m_currentService, SIGNAL(searchDone()), this, SLOT(slotSearchFinished()));
    if (m_currentService->hasPreview)
        connect (m_currentService,SIGNAL(previewFinished()),this, SLOT(slotPreviewFinished()));

    button_preview->setVisible(m_currentService->hasPreview);

    button_import->setVisible(!m_currentService->inlineDownload);

    search_info->setText(QString());
    if (!search_text->text().isEmpty())
        slotStartSearch();// automatically kick of a search if we have search text and we switch services.
}

void ResourceWidget::slotSearchFinished()
{
    this->setCursor(Qt::ArrowCursor);
}

/**
 * @brief ResourceWidget::slotSetMaximum
 * @param max
 */
void ResourceWidget::slotSetMaximum(int max)
{
    page_number->setMaximum(max);
}
/**
 * @brief ResourceWidget::slotOnlineChanged
 * @param online
 */
void ResourceWidget::slotOnlineChanged(bool online)
{
    
    button_search->setEnabled(online);
    search_info->setText(online ? QString() : i18n("You need to be online\n for searching"));
}
/**
 * @brief ResourceWidget::slotNextPage
 */
void ResourceWidget::slotNextPage()
{
    const int ix = page_number->value();
    if (search_results->count() > 0)
        page_number->setValue(ix + 1);
}
/**
 * @brief ResourceWidget::slotPreviousPage
 */
void ResourceWidget::slotPreviousPage()
{
    const int ix = page_number->value();
    if (ix > 1)
        page_number->setValue(ix - 1);
}
/**
 * @brief ResourceWidget::parseLicense provides a name for the licence based on the license URL
 * called by  ResourceWidget::slotDisplayMetaInfo and by  ResourceWidget::slotUpdateCurrentSound
 * @param licenseUrl
 */
void ResourceWidget::parseLicense(const QString &licenseUrl)
{
    QString licenseName;

    if (licenseUrl.contains(QStringLiteral("/sampling+/")))
        licenseName = "Sampling+";
    else if (licenseUrl.contains(QStringLiteral("/by/")))
        licenseName = "Attribution";
    else if (licenseUrl.contains(QStringLiteral("/by-nd/")))
        licenseName = "Attribution-NoDerivs";
    else if (licenseUrl.contains(QStringLiteral("/by-nc-sa/")))
        licenseName = "Attribution-NonCommercial-ShareAlike";
    else if (licenseUrl.contains(QStringLiteral("/by-sa/")))
        licenseName = "Attribution-ShareAlike";
    else if (licenseUrl.contains(QStringLiteral("/by-nc/")))
        licenseName = "Attribution-NonCommercial";
    else if (licenseUrl.contains(QStringLiteral("/by-nc-nd/")))
        licenseName = "Attribution-NonCommercial-NoDerivs";

    else if (licenseUrl.contains("/publicdomain/zero/") )
        licenseName = "Creative Commons 0";
    else if (licenseUrl.endsWith(QLatin1String("/publicdomain"))||licenseUrl.contains("openclipart.org/share"))

        licenseName = "Public Domain";

    else licenseName = i18n("Unknown");
    item_license->setText(licenseName);
    item_license->setUrl(licenseUrl);
}


/**
 * @brief ResourceWidget::slotOpenLink. Fires when Url in the resource wizard is clicked
 * @param url
 * Connected to anchorClicked(). If the url ends in _import it downloads the file at the end of the url via slotSaveItem().
 * We have created URLs deliberately tagged with _import in ArchiveOrg::slotParseResults. If the URL is tagged in this way we remove the tag
 * and download the file.
 * Otherwise it opens the URL via slotOpenUrl() which opens the URL in the systems default web browser
 */
void ResourceWidget::slotOpenLink(const QUrl &url)
{
    QString path = url.toEncoded();
    if (path.endsWith(QLatin1String("_import"))) {//
        path.chop(7);
        // import file in Kdenlive
        slotSaveItem(path);
    }
    else {
        slotOpenUrl(path);
    }
}
/**
 * @brief ResourceWidget::slotSetDescription Updates the display with the description text
 * @param desc /n
 * The description is either the detailed description of the file or is progress messages on the download process
 *
*/
void ResourceWidget::slotSetDescription(const QString &desc)
{
    if(m_desc != desc) {
        m_desc = desc;
        updateLayout();
    }
}
/**
 * @brief ResourceWidget::slotSetMetadata updates the display with the metadata.
 * @param desc /n
 * The meta data is info on the sounds length, samplerate, filesize and  number of channels. This is called when gotMetaInfo(Qstring) signal fires. That signal is passing html in the parameter
 * This function is updating the html (m_meta) in the ResourceWidget and then calls  updateLayout()
 * which updates actual widget
 */
void ResourceWidget::slotSetMetadata(const QString &metadata)
{
    if (m_meta != metadata) {
        m_meta = metadata;
        updateLayout();
    }
}
/**
 * @brief ResourceWidget::slotSetImage Sets a thumbnail on the widget
 * @param desc
 * called by ResourceWidget::slotLoadThumb \n
 * This sets a thumb nail image onto a label on the resource widget. If it is a animated .gif it will play
 */
void ResourceWidget::slotSetImage(const QString &desc)
{
    QPixmap pic(desc);
    GifLabel->setPixmap(pic);// pass a pointer as a parameter. Display the pic in our lable

}
/** @brief updates the display with infomation on the seleted item. The title consists of the sounds file name and the author
 *
 * Called by ResourceWidget::slotUpdateCurrentSound()
*/
void ResourceWidget::slotSetTitle(const QString &title)
{
    if (m_title != title) {
        m_title = title;
        updateLayout();
    }
}
/**
 * @brief ResourceWidget::updateLayout
 * This concats the html in m_title, m_desc and m_meta and sets the resulting
 * html markup into the content of the ResourceWidget \n
 * Called by slotSetTitle() , slotSetMetadata() ,slotSetDescription()
 */
void ResourceWidget::updateLayout()
{
    QString content = m_title;

    if (!m_desc.isEmpty())
        content.append(m_desc);
    if (!m_meta.isEmpty())
        content.append(m_meta);
    info_browser->setHtml(content);
}
/**
 * @brief ResourceWidget::slotPreviewFinished
 * connected to FreeSound previewFinished signal
 */
void  ResourceWidget::slotPreviewFinished()
{
    button_preview->setText(i18n("Preview"));
}


/**
 * @brief ResourceWidget::slotFreesoundAccessDenied
 * Will fire if freesound denies access - eg wrong password entered.
 */
void ResourceWidget::slotFreesoundAccessDenied()
{
 button_import->setEnabled(true);
 info_browser->setHtml("<html>"  + i18n("Access Denied from Freesound.  Have you authorised the Kdenlive application on your freesound account?")+"</html>");
}

/**
 * @brief ResourceWidget::slotAccessTokenReceived
 * @param sAccessToken - the access token obtained from freesound website \n
 * Connected to OAuth2::accessTokenReceived signal in ResourceWidget constructor.
 * Fires when the OAuth2 object has obtained an access token. This slot then goes ahead
 * and starts the download of the requested file. ResourceWidget::DownloadRequestFinished will be
 * notified when that job finishes
 */
void ResourceWidget::slotAccessTokenReceived(QString sAccessToken)
{
     //qDebug() << "slotAccessTokenReceived: " <<sAccessToken;
     if (sAccessToken !="")
     {
        // QNetworkAccessManager *networkManager = new QNetworkAccessManager(this);

         QNetworkRequest request;
         QUrl srcUrl(m_currentInfo.itemDownload);
         request.setUrl(srcUrl);//  Download url of a freesound file
         // eg https://www.freesound.org/apiv2/sounds/39206/download/
         request.setRawHeader(QByteArray("Authorization"),QByteArray( "Bearer ").append( sAccessToken));

         m_meta="";
         m_desc="<br><b>" +  i18n("Starting File Download") + "</b><br>";
         updateLayout();

         QNetworkReply *reply2 = m_networkAccessManager->get(request);
         connect(reply2, SIGNAL(readyRead()), this, SLOT(slotReadyRead()));
         connect(m_networkAccessManager, SIGNAL(finished(QNetworkReply*)), this, SLOT(DownloadRequestFinished(QNetworkReply*)));
     }
     else
     {

         m_meta="";
         m_desc="<br><b>" +  i18n("Error Getting Access Token from Freesound.") + "</b>";
         m_desc.append("<br><b>" +  i18n("Try importing again to obtain a new freesound connection")+ "</b>");
         updateLayout();
     }
}
/**
 * @brief ResourceWidget::GetSaveFileNameAndPath
 * @param path - starting path where dialog will open on
 * @param ext - file exension filter to have in play on the dialog
 * @return QUrl of the choosen path
 *  * Prompts user to choose a file name and path as to where to save a file.
 * Returns a QUrl of the path and file name
 */
QUrl ResourceWidget::GetSaveFileNameAndPath(QString path,QString ext)
{
    QString savloc = GetSaveFileNameAndPathS( path, ext);
    return (QUrl::fromLocalFile (savloc));
}
/**
 * @brief ResourceWidget::GetSaveFileNameAndPathS
 * @param path -  starting path where dialog will open on
 * @param ext - file exension filter to have in play on the dialog
 * @return  QString of the choosen path
 * Prompts user to choose a file name and path as to where to save a file.
 * Returns a QString of the path and file name or empty string if user  cancels
 */
QString ResourceWidget::GetSaveFileNameAndPathS(QString path,QString ext)
{
    QString saveUrlstring = QFileDialog::getSaveFileName(this, QString(), path, ext);
    if ( saveUrlstring.isEmpty() )
        // only check if the save  url is empty (ie if user cancels the save.)
        //If the URL has no file at the end we trap this error in slotGotFile.
        return saveUrlstring;
    if (QFile::exists(saveUrlstring))
    {
        int ret = QMessageBox::warning(this, i18n("File Exists"),
                                       i18n("Do you want to overwrite the existing file?"),
                                       QMessageBox::Yes | QMessageBox::No,
                                       QMessageBox::No);
        if (ret==QMessageBox::No)
        {
            return "";
        }
    }
    return saveUrlstring;
}
/**
 * @brief ResourceWidget::slotReadyRead
 * Fires each time the download of the freesound file grabs some more data.
 * Prints dots to the dialog indicating download is progressing.
 */
void ResourceWidget::slotReadyRead()
{
    m_desc.append(".");
    updateLayout();
}
/**
 * @brief ResourceWidget::DownloadRequestFinished
 * @param reply
 * Fires when the download of the freesound file completes.
 * If the download was successfull this saves the data from memory to the file system. Emits an ResourceWidget::addClip signal.
 * MainWindow::slotDownloadResources() links this signal to MainWindow::slotAddProjectClip
 * If the download has failed with AuthenticationRequiredError then it requests a new access token via the refresh token method
 * and then the download process will retry.
 * If the download fails for other reasons this reports an error and clears out the access token from memory.
 * If the user requests the file import again it will request a login to free sound again and the download might suceed on this second try
 */
void ResourceWidget::DownloadRequestFinished(QNetworkReply* reply)
{

    button_import->setEnabled(true);
    if (reply->isFinished())
    {
        if (reply->error()==QNetworkReply::NoError)
        {
            QByteArray	aSoundData = reply->readAll();
            QFile file(mSaveLocation);
            if (file.open(QIODevice::WriteOnly))
            {
                file.write(aSoundData );
                file.close();

                  KMessageBox::information(this, i18n("Resource saved to ") + mSaveLocation, i18n("Data Imported"));
                  emit addClip(QUrl(mSaveLocation));// MainWindow::slotDownloadResources() links this signal to MainWindow::slotAddProjectClip

                  m_desc.append("<br>" + i18n( "Saved file to") + "<br>");
                  m_desc.append(mSaveLocation);
                  updateLayout();
            }
            else
            {
#ifdef QT5_USE_WEBKIT
                m_pOAuth2->ForgetAccessToken();
#endif
                m_desc.append("<br>" + i18n("Error Saving File"));
                updateLayout();


            }
        }
        else
        {

             if (reply->error()==QNetworkReply::AuthenticationRequiredError)
             {
                 QString sErrorText = reply->readAll();
#ifdef QT5_USE_WEBKIT
                 m_pOAuth2->obtainNewAccessToken();
#endif
             }
             else
             {
#ifdef QT5_USE_WEBKIT
                 m_pOAuth2->ForgetAccessToken();
#endif
                  m_desc.append("<br>" + i18n( "Error Downloading File. Error code: ") + reply->error() + "<br>");
                  m_desc.append("<br><b>" +  i18n("Try importing again to obtain a new freesound connection")+ "</b>");
                  updateLayout();

             }
        }

    }
    reply->deleteLater();
}
