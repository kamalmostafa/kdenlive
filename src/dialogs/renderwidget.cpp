/***************************************************************************
 *   Copyright (C) 2008 by Jean-Baptiste Mardelle (jb@kdenlive.org)        *
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


#include "renderwidget.h"
#include "kdenlivesettings.h"
#include "ui_saveprofile_ui.h"
#include "timecode.h"
#include "dialogs/profilesdialog.h"
#include "utils/KoIconUtils.h"

#include "klocalizedstring.h"
#include <KMessageBox>
#include <KRun>
#include <KColorScheme>
#include <KNotification>

#include <qglobal.h>
#include <qstring.h>
#include <QDebug>
#include <QDomDocument>
#include <QTreeWidgetItem>
#include <QTreeWidgetItem>
#include <QHeaderView>
#include <QInputDialog>
#include <QProcess>
#include <QDBusConnectionInterface>
#include <QThread>
#include <QScriptEngine>
#include <QKeyEvent>
#include <QTimer>
#include <QStandardPaths>
#include <QDir>

#include <locale>
#ifdef Q_OS_MAC
#include <xlocale.h>
#endif


// Render profiles roles
enum {GroupRole = Qt::UserRole,
      ExtensionRole,
      StandardRole,
      RenderRole,
      ParamsRole,
      EditableRole,
      ExtraRole,
      BitratesRole,
      DefaultBitrateRole,
      AudioBitratesRole,
      DefaultAudioBitrateRole,
      SpeedsRole,
      ErrorRole
     };

// Render job roles
const int ParametersRole = Qt::UserRole + 1;
const int TimeRole = Qt::UserRole + 2;
const int ProgressRole = Qt::UserRole + 3;
const int ExtraInfoRole = Qt::UserRole + 5;

const int DirectRenderType = QTreeWidgetItem::Type;
const int ScriptRenderType = QTreeWidgetItem::UserType;


// Running job status
enum JOBSTATUS {
    WAITINGJOB = 0,
    STARTINGJOB,
    RUNNINGJOB,
    FINISHEDJOB,
    FAILEDJOB,
    ABORTEDJOB
};


RenderJobItem::RenderJobItem(QTreeWidget * parent, const QStringList & strings, int type)
    : QTreeWidgetItem(parent, strings, type),
    m_status(-1)
{
    setSizeHint(1, QSize(parent->columnWidth(1), parent->fontMetrics().height() * 3));
    setStatus(WAITINGJOB);
}

void RenderJobItem::setStatus(int status)
{
    if (m_status == status)
        return;
    m_status = status;
    switch (status) {
        case WAITINGJOB:
            setIcon(0, KoIconUtils::themedIcon(QStringLiteral("media-playback-pause")));
            setData(1, Qt::UserRole, i18n("Waiting..."));
            break;
        case FINISHEDJOB:
            setData(1, Qt::UserRole, i18n("Rendering finished"));
            setIcon(0, KoIconUtils::themedIcon(QStringLiteral("dialog-ok")));
            setData(1, ProgressRole, 100);
            break;
        case FAILEDJOB:
            setData(1, Qt::UserRole, i18n("Rendering crashed"));
            setIcon(0, KoIconUtils::themedIcon(QStringLiteral("dialog-close")));
            setData(1, ProgressRole, 100);
            break;
        case ABORTEDJOB:
            setData(1, Qt::UserRole, i18n("Rendering aborted"));
            setIcon(0, KoIconUtils::themedIcon(QStringLiteral("dialog-cancel")));
            setData(1, ProgressRole, 100);
        default:
            break;
    }
}

int RenderJobItem::status() const
{
    return m_status;
}

void RenderJobItem::setMetadata(const QString &data)
{
    m_data = data;
}

const QString RenderJobItem::metadata() const
{
    return m_data;
}


RenderWidget::RenderWidget(const QString &projectfolder, bool enableProxy, const MltVideoProfile &profile, QWidget * parent) :
        QDialog(parent),
        m_projectFolder(projectfolder),
        m_profile(profile),
        m_blockProcessing(false)
{
    m_view.setupUi(this);
    int size = style()->pixelMetric(QStyle::PM_SmallIconSize);
    QSize iconSize(size, size);

    setWindowTitle(i18n("Rendering"));
    m_view.buttonDelete->setIconSize(iconSize);
    m_view.buttonEdit->setIconSize(iconSize);
    m_view.buttonSave->setIconSize(iconSize);
    m_view.buttonFavorite->setIconSize(iconSize);

    m_view.buttonDelete->setIcon(KoIconUtils::themedIcon(QStringLiteral("trash-empty")));
    m_view.buttonDelete->setToolTip(i18n("Delete profile"));
    m_view.buttonDelete->setEnabled(false);

    m_view.buttonEdit->setIcon(KoIconUtils::themedIcon(QStringLiteral("document-properties")));
    m_view.buttonEdit->setToolTip(i18n("Edit profile"));
    m_view.buttonEdit->setEnabled(false);

    m_view.buttonSave->setIcon(KoIconUtils::themedIcon(QStringLiteral("document-new")));
    m_view.buttonSave->setToolTip(i18n("Create new profile"));

    m_view.hide_log->setIcon(KoIconUtils::themedIcon(QStringLiteral("go-down")));

    m_view.buttonFavorite->setIcon(KoIconUtils::themedIcon(QStringLiteral("favorite")));
    m_view.buttonFavorite->setToolTip(i18n("Copy profile to favorites"));

    m_view.advanced_params->setMaximumHeight(QFontMetrics(font()).lineSpacing() * 5);

    m_view.optionsGroup->setVisible(m_view.options->isChecked());
    connect(m_view.options, SIGNAL(toggled(bool)), m_view.optionsGroup, SLOT(setVisible(bool)));
    m_view.videoLabel->setVisible(m_view.options->isChecked());
    connect(m_view.options, SIGNAL(toggled(bool)), m_view.videoLabel, SLOT(setVisible(bool)));
    m_view.video->setVisible(m_view.options->isChecked());
    connect(m_view.options, SIGNAL(toggled(bool)), m_view.video, SLOT(setVisible(bool)));
    m_view.audioLabel->setVisible(m_view.options->isChecked());
    connect(m_view.options, SIGNAL(toggled(bool)), m_view.audioLabel, SLOT(setVisible(bool)));
    m_view.audio->setVisible(m_view.options->isChecked());
    connect(m_view.options, SIGNAL(toggled(bool)), m_view.audio, SLOT(setVisible(bool)));
    connect(m_view.quality, SIGNAL(valueChanged(int)), this, SLOT(adjustAVQualities(int)));
    connect(m_view.video, SIGNAL(valueChanged(int)), this, SLOT(adjustQuality(int)));
    connect(m_view.speed, SIGNAL(valueChanged(int)), this, SLOT(adjustSpeed(int)));

    m_view.buttonRender->setEnabled(false);
    m_view.buttonGenerateScript->setEnabled(false);
    setRescaleEnabled(false);
    m_view.guides_box->setVisible(false);
    m_view.open_dvd->setVisible(false);
    m_view.create_chapter->setVisible(false);
    m_view.open_browser->setVisible(false);
    m_view.error_box->setVisible(false);
    m_view.tc_type->setEnabled(false);
    m_view.checkTwoPass->setEnabled(false);
    m_view.proxy_render->setHidden(!enableProxy);

    KColorScheme scheme(palette().currentColorGroup(), KColorScheme::Window, KSharedConfig::openConfig(KdenliveSettings::colortheme()));
    QColor bg = scheme.background(KColorScheme::NegativeBackground).color();
    m_view.errorBox->setStyleSheet(QStringLiteral("QGroupBox { background-color: rgb(%1, %2, %3); border-radius: 5px;}; ").arg(bg.red()).arg(bg.green()).arg(bg.blue()));
    int height = QFontInfo(font()).pixelSize();
    m_view.errorIcon->setPixmap(KoIconUtils::themedIcon(QStringLiteral("dialog-warning")).pixmap(height, height));
    m_view.errorBox->setHidden(true);

    m_infoMessage = new KMessageWidget;
    m_view.info->addWidget(m_infoMessage);
    m_infoMessage->setCloseButtonVisible(false);
    m_infoMessage->hide();

    m_view.encoder_threads->setMaximum(QThread::idealThreadCount());
    m_view.encoder_threads->setValue(KdenliveSettings::encodethreads());
    connect(m_view.encoder_threads, SIGNAL(valueChanged(int)), this, SLOT(slotUpdateEncodeThreads(int)));

    m_view.rescale_keep->setChecked(KdenliveSettings::rescalekeepratio());
    connect(m_view.rescale_width, SIGNAL(valueChanged(int)), this, SLOT(slotUpdateRescaleWidth(int)));
    connect(m_view.rescale_height, SIGNAL(valueChanged(int)), this, SLOT(slotUpdateRescaleHeight(int)));
    m_view.rescale_keep->setIcon(KoIconUtils::themedIcon(QStringLiteral("edit-link")));
    m_view.rescale_keep->setToolTip(i18n("Preserve aspect ratio"));
    connect(m_view.rescale_keep, SIGNAL(clicked()), this, SLOT(slotSwitchAspectRatio()));

    connect(m_view.buttonRender, SIGNAL(clicked()), this, SLOT(slotPrepareExport()));
    connect(m_view.buttonGenerateScript, SIGNAL(clicked()), this, SLOT(slotGenerateScript()));

    m_view.abort_job->setEnabled(false);
    m_view.start_script->setEnabled(false);
    m_view.delete_script->setEnabled(false);

    connect(m_view.export_audio, SIGNAL(stateChanged(int)), this, SLOT(slotUpdateAudioLabel(int)));
    m_view.export_audio->setCheckState(Qt::PartiallyChecked);

    parseProfiles();
    parseScriptFiles();
    m_view.running_jobs->setUniformRowHeights(false);
    m_view.scripts_list->setUniformRowHeights(false);
    connect(m_view.start_script, SIGNAL(clicked()), this, SLOT(slotStartScript()));
    connect(m_view.delete_script, SIGNAL(clicked()), this, SLOT(slotDeleteScript()));
    connect(m_view.scripts_list, SIGNAL(itemSelectionChanged()), this, SLOT(slotCheckScript()));
    connect(m_view.running_jobs, SIGNAL(itemSelectionChanged()), this, SLOT(slotCheckJob()));
    connect(m_view.running_jobs, SIGNAL(itemDoubleClicked(QTreeWidgetItem*,int)), this, SLOT(slotPlayRendering(QTreeWidgetItem*,int)));

    connect(m_view.buttonSave, SIGNAL(clicked()), this, SLOT(slotSaveProfile()));
    connect(m_view.buttonEdit, SIGNAL(clicked()), this, SLOT(slotEditProfile()));
    connect(m_view.buttonDelete, SIGNAL(clicked()), this, SLOT(slotDeleteProfile()));
    connect(m_view.buttonFavorite, SIGNAL(clicked()), this, SLOT(slotCopyToFavorites()));

    connect(m_view.abort_job, SIGNAL(clicked()), this, SLOT(slotAbortCurrentJob()));
    connect(m_view.start_job, SIGNAL(clicked()), this, SLOT(slotStartCurrentJob()));
    connect(m_view.clean_up, SIGNAL(clicked()), this, SLOT(slotCLeanUpJobs()));
    connect(m_view.hide_log, SIGNAL(clicked()), this, SLOT(slotHideLog()));

    connect(m_view.buttonClose, SIGNAL(clicked()), this, SLOT(hide()));
    connect(m_view.buttonClose2, SIGNAL(clicked()), this, SLOT(hide()));
    connect(m_view.buttonClose3, SIGNAL(clicked()), this, SLOT(hide()));
    connect(m_view.rescale, SIGNAL(toggled(bool)), this, SLOT(setRescaleEnabled(bool)));
    connect(m_view.out_file, SIGNAL(textChanged(QString)), this, SLOT(slotUpdateButtons()));
    connect(m_view.out_file, SIGNAL(urlSelected(QUrl)), this, SLOT(slotUpdateButtons(QUrl)));

    connect(m_view.formats, SIGNAL(currentItemChanged(QTreeWidgetItem*,QTreeWidgetItem*)), this, SLOT(refreshParams()));
    connect(m_view.formats, SIGNAL(itemDoubleClicked(QTreeWidgetItem*,int)), this, SLOT(slotEditItem(QTreeWidgetItem*)));

    connect(m_view.render_guide, SIGNAL(clicked(bool)), this, SLOT(slotUpdateGuideBox()));
    connect(m_view.render_zone, SIGNAL(clicked(bool)), this, SLOT(slotUpdateGuideBox()));
    connect(m_view.render_full, SIGNAL(clicked(bool)), this, SLOT(slotUpdateGuideBox()));

    connect(m_view.guide_end, SIGNAL(activated(int)), this, SLOT(slotCheckStartGuidePosition()));
    connect(m_view.guide_start, SIGNAL(activated(int)), this, SLOT(slotCheckEndGuidePosition()));

    connect(m_view.tc_overlay, SIGNAL(toggled(bool)), m_view.tc_type, SLOT(setEnabled(bool)));

    //m_view.splitter->setStretchFactor(1, 5);
    //m_view.splitter->setStretchFactor(0, 2);

    m_view.out_file->setMode(KFile::File);
    m_view.out_file->setFocusPolicy(Qt::ClickFocus);

    m_view.running_jobs->setHeaderLabels(QStringList() << QString() << i18n("File"));
    m_jobsDelegate = new RenderViewDelegate(this);
    m_view.running_jobs->setItemDelegate(m_jobsDelegate);

    QHeaderView *header = m_view.running_jobs->header();
    header->setSectionResizeMode(0, QHeaderView::Fixed);
    header->resizeSection(0, 30);
    header->setSectionResizeMode(1, QHeaderView::Interactive);

    m_view.scripts_list->setHeaderLabels(QStringList() << QString() << i18n("Script Files"));
    m_scriptsDelegate = new RenderViewDelegate(this);
    m_view.scripts_list->setItemDelegate(m_scriptsDelegate);
    header = m_view.scripts_list->header();
    header->setSectionResizeMode(0, QHeaderView::Fixed);
    header->resizeSection(0, 30);

    // Find path for Kdenlive renderer
    m_renderer = QCoreApplication::applicationDirPath() + QStringLiteral("/kdenlive_render");
    if (!QFile::exists(m_renderer)) {
        m_renderer = QStandardPaths::findExecutable(QStringLiteral("kdenlive_render"));
        if (m_renderer.isEmpty())
            m_renderer = QStringLiteral("kdenlive_render");
    }

    QDBusConnectionInterface* interface = QDBusConnection::sessionBus().interface();
    if (!interface || (!interface->isServiceRegistered(QStringLiteral("org.kde.ksmserver")) && !interface->isServiceRegistered(QStringLiteral("org.gnome.SessionManager"))))
        m_view.shutdown->setEnabled(false);

    focusFirstVisibleItem();
    adjustSize();
}

QSize RenderWidget::sizeHint() const
{
    // Make sure the widget has minimum size on opening
    return QSize(200, 200);
}

RenderWidget::~RenderWidget()
{
    m_view.running_jobs->blockSignals(true);
    m_view.scripts_list->blockSignals(true);
    m_view.running_jobs->clear();
    m_view.scripts_list->clear();
    delete m_jobsDelegate;
    delete m_scriptsDelegate;
    delete m_infoMessage;
}

void RenderWidget::slotEditItem(QTreeWidgetItem *item)
{
    const QString edit = item->data(0, EditableRole).toString();
    if (edit.isEmpty() || !edit.endsWith(QLatin1String("customprofiles.xml")))
        slotSaveProfile();
    else slotEditProfile();
}

void RenderWidget::showInfoPanel()
{
    if (m_view.advanced_params->isVisible()) {
        m_view.advanced_params->setVisible(false);
        KdenliveSettings::setShowrenderparams(false);
    } else {
        m_view.advanced_params->setVisible(true);
        KdenliveSettings::setShowrenderparams(true);
    }
}

void RenderWidget::setDocumentPath(const QString &path)
{
    if (m_view.out_file->url().adjusted(QUrl::RemoveFilename).path() == QUrl::fromLocalFile(m_projectFolder).adjusted(QUrl::RemoveFilename).path()) {
        const QString fileName = m_view.out_file->url().fileName();
        m_view.out_file->setUrl(QUrl(path + fileName));
    }
    m_projectFolder = path;
    parseScriptFiles();

}

void RenderWidget::slotUpdateGuideBox()
{
    m_view.guides_box->setVisible(m_view.render_guide->isChecked());
}

void RenderWidget::slotCheckStartGuidePosition()
{
    if (m_view.guide_start->currentIndex() > m_view.guide_end->currentIndex())
        m_view.guide_start->setCurrentIndex(m_view.guide_end->currentIndex());
}

void RenderWidget::slotCheckEndGuidePosition()
{
    if (m_view.guide_end->currentIndex() < m_view.guide_start->currentIndex())
        m_view.guide_end->setCurrentIndex(m_view.guide_start->currentIndex());
}

void RenderWidget::setGuides(QMap <double, QString> guidesData, double duration)
{
    m_view.guide_start->clear();
    m_view.guide_end->clear();
    if (!guidesData.isEmpty()) {
        m_view.guide_start->addItem(i18n("Beginning"), "0");
        m_view.render_guide->setEnabled(true);
        m_view.create_chapter->setEnabled(true);
    } else {
        m_view.render_guide->setEnabled(false);
        m_view.create_chapter->setEnabled(false);
    }
    double fps = (double) m_profile.frame_rate_num / m_profile.frame_rate_den;
    QMapIterator<double, QString> i(guidesData);
    while (i.hasNext()) {
        i.next();
        GenTime pos = GenTime(i.key());
        const QString guidePos = Timecode::getStringTimecode(pos.frames(fps), fps);
        m_view.guide_start->addItem(i.value() + '/' + guidePos, i.key());
        m_view.guide_end->addItem(i.value() + '/' + guidePos, i.key());
    }
    if (!guidesData.isEmpty())
        m_view.guide_end->addItem(i18n("End"), QString::number(duration));
}

/**
 * Will be called when the user selects an output file via the file dialog.
 * File extension will be added automatically.
 */
void RenderWidget::slotUpdateButtons(const QUrl &url)
{
    if (m_view.out_file->url().isEmpty()) {
        m_view.buttonGenerateScript->setEnabled(false);
        m_view.buttonRender->setEnabled(false);
    } else {
        updateButtons(); // This also checks whether the selected format is available
    }
    if (url.isValid()) {
        QTreeWidgetItem *item = m_view.formats->currentItem();
        if (!item || !item->parent()) { // categories have no parent
            m_view.buttonRender->setEnabled(false);
            m_view.buttonGenerateScript->setEnabled(false);
            return;
        }
        const QString extension = item->data(0, ExtensionRole).toString();
        m_view.out_file->setUrl(filenameWithExtension(url, extension));
    }
}

/**
 * Will be called when the user changes the output file path in the text line.
 * File extension must NOT be added, would make editing impossible!
 */
void RenderWidget::slotUpdateButtons()
{
    if (m_view.out_file->url().isEmpty()) {
        m_view.buttonRender->setEnabled(false);
        m_view.buttonGenerateScript->setEnabled(false);
    } else {
        updateButtons(); // This also checks whether the selected format is available
    }
}

void RenderWidget::slotSaveProfile()
{
    Ui::SaveProfile_UI ui;
    QPointer<QDialog> d = new QDialog(this);
    ui.setupUi(d);

    QString customGroup;
    QStringList arguments = m_view.advanced_params->toPlainText().split(' ', QString::SkipEmptyParts);
    if (!arguments.isEmpty()) {
        ui.parameters->setText(arguments.join(QStringLiteral(" ")));
    }
    ui.profile_name->setFocus();
    QTreeWidgetItem *item = m_view.formats->currentItem();
    if (item && item->parent()) { //not a category
        // Duplicate current item settings
        customGroup = item->parent()->text(0);
        ui.extension->setText(item->data(0, ExtensionRole).toString());
        if (ui.parameters->toPlainText().contains(QStringLiteral("%bitrate")) || ui.parameters->toPlainText().contains(QStringLiteral("%quality"))) {
            if (ui.parameters->toPlainText().contains(QStringLiteral("%quality"))) {
                ui.vbitrates_label->setText(i18n("Qualities"));
                ui.default_vbitrate_label->setText(i18n("Default quality"));
            } else {
                ui.vbitrates_label->setText(i18n("Bitrates"));
                ui.default_vbitrate_label->setText(i18n("Default bitrate"));
            }
            if (item->data(0, BitratesRole).canConvert(QVariant::StringList) && item->data(0, BitratesRole).toStringList().count()) {
                QStringList bitrates = item->data(0, BitratesRole).toStringList();
                ui.vbitrates_list->setText(bitrates.join(QStringLiteral(",")));
                if (item->data(0, DefaultBitrateRole).canConvert(QVariant::String))
                    ui.default_vbitrate->setValue(item->data(0, DefaultBitrateRole).toInt());
            }
        }
        else ui.vbitrates->setHidden(true);
        if (ui.parameters->toPlainText().contains(QStringLiteral("%audiobitrate")) || ui.parameters->toPlainText().contains(QStringLiteral("%audioquality"))) {
            if (ui.parameters->toPlainText().contains(QStringLiteral("%audioquality"))) {
                ui.abitrates_label->setText(i18n("Qualities"));
                ui.default_abitrate_label->setText(i18n("Default quality"));
            } else {
                ui.abitrates_label->setText(i18n("Bitrates"));
                ui.default_abitrate_label->setText(i18n("Default bitrate"));
            }
            if ( item && item->data(0, AudioBitratesRole).canConvert(QVariant::StringList) && item->data(0, AudioBitratesRole).toStringList().count()) {
                QStringList bitrates = item->data(0, AudioBitratesRole).toStringList();
                ui.abitrates_list->setText(bitrates.join(QStringLiteral(",")));
                if (item->data(0, DefaultAudioBitrateRole).canConvert(QVariant::String))
                    ui.default_abitrate->setValue(item->data(0, DefaultAudioBitrateRole).toInt());
            }
        }
        else ui.abitrates->setHidden(true);

        if (item->data(0, SpeedsRole).canConvert(QVariant::StringList) && item->data(0, SpeedsRole).toStringList().count()) {
            QStringList speeds = item->data(0, SpeedsRole).toStringList();
            ui.speeds_list->setText(speeds.join('\n'));
        }
    }

    if (customGroup.isEmpty()) customGroup = i18nc("Group Name", "Custom");
    ui.group_name->setText(customGroup);

    if (d->exec() == QDialog::Accepted && !ui.profile_name->text().simplified().isEmpty()) {
        QString newProfileName = ui.profile_name->text().simplified();
        QString newGroupName = ui.group_name->text().simplified();
        if (newGroupName.isEmpty()) newGroupName = i18nc("Group Name", "Custom");

        QDomDocument doc;
        QDomElement profileElement = doc.createElement(QStringLiteral("profile"));
        profileElement.setAttribute(QStringLiteral("name"), newProfileName);
        profileElement.setAttribute(QStringLiteral("category"), newGroupName);
        profileElement.setAttribute(QStringLiteral("extension"), ui.extension->text().simplified());
        QString args = ui.parameters->toPlainText().simplified();
        profileElement.setAttribute(QStringLiteral("args"), args);
        if (args.contains(QStringLiteral("%bitrate"))) {
            // profile has a variable bitrate
            profileElement.setAttribute(QStringLiteral("defaultbitrate"), QString::number(ui.default_vbitrate->value()));
            profileElement.setAttribute(QStringLiteral("bitrates"), ui.vbitrates_list->text());
        } else if (args.contains(QStringLiteral("%quality"))) {
            profileElement.setAttribute(QStringLiteral("defaultquality"), QString::number(ui.default_vbitrate->value()));
            profileElement.setAttribute(QStringLiteral("qualities"), ui.vbitrates_list->text());
        }

        if (args.contains(QStringLiteral("%audiobitrate"))) {
            // profile has a variable bitrate
            profileElement.setAttribute(QStringLiteral("defaultaudiobitrate"), QString::number(ui.default_abitrate->value()));
            profileElement.setAttribute(QStringLiteral("audiobitrates"), ui.abitrates_list->text());
        } else if (args.contains(QStringLiteral("%audioquality"))) {
            // profile has a variable bitrate
            profileElement.setAttribute(QStringLiteral("defaultaudioquality"), QString::number(ui.default_abitrate->value()));
            profileElement.setAttribute(QStringLiteral("audioqualities"), ui.abitrates_list->text());
        }
        if (!ui.speeds_list->toPlainText().isEmpty()) {
            profileElement.setAttribute(QStringLiteral("speeds"), ui.speeds_list->toPlainText().replace('\n', ';').simplified());
        }

        doc.appendChild(profileElement);
        saveProfile(doc.documentElement());

        parseProfiles();
    }
    delete d;
}


void RenderWidget::saveProfile(const QDomElement &newprofile)
{
    QDir dir(QStandardPaths::writableLocation(QStandardPaths::DataLocation) + "/export/");
    if (!dir.exists()) {
        dir.mkpath(QStringLiteral("."));
    }
    QString exportFile = QStandardPaths::writableLocation(QStandardPaths::DataLocation) + "/export/customprofiles.xml";
    QDomDocument doc;
    QFile file(dir.absoluteFilePath(QStringLiteral("customprofiles.xml")));
    doc.setContent(&file, false);
    file.close();
    QDomElement documentElement;
    QDomElement profiles = doc.documentElement();
    if (profiles.isNull() || profiles.tagName() != QLatin1String("profiles")) {
        doc.clear();
        profiles = doc.createElement(QStringLiteral("profiles"));
        profiles.setAttribute(QStringLiteral("version"), 1);
        doc.appendChild(profiles);
    }
    int version = profiles.attribute(QStringLiteral("version"), 0).toInt();
    if (version < 1) {
        doc.clear();
        profiles = doc.createElement(QStringLiteral("profiles"));
        profiles.setAttribute(QStringLiteral("version"), 1);
        doc.appendChild(profiles);
    }


    QDomNodeList profilelist = doc.elementsByTagName(QStringLiteral("profile"));
    int i = 0;
    while (!profilelist.item(i).isNull()) {
        // make sure a profile with same name doesn't exist
        documentElement = profilelist.item(i).toElement();
        QString profileName = documentElement.attribute(QStringLiteral("name"));
        if (profileName == newprofile.attribute(QStringLiteral("name"))) {
            // a profile with that same name already exists
            bool ok;
            QString newProfileName = QInputDialog::getText(this, i18n("Profile already exists"), i18n("This profile name already exists. Change the name if you don't want to overwrite it."), QLineEdit::Normal, profileName, &ok);
            if (!ok) return;
            if (profileName == newProfileName) {
                profiles.removeChild(profilelist.item(i));
                break;
            }
        }
        ++i;
    }

    profiles.appendChild(newprofile);

    //QCString save = doc.toString().utf8();

    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        KMessageBox::sorry(this, i18n("Unable to write to file %1", dir.absoluteFilePath("customprofiles.xml")));
        return;
    }
    QTextStream out(&file);
    out << doc.toString();
    if (file.error() != QFile::NoError) {
        KMessageBox::error(this, i18n("Cannot write to file %1", dir.absoluteFilePath("customprofiles.xml")));
        file.close();
        return;
    }
    file.close();
}

void RenderWidget::slotCopyToFavorites()
{
    QTreeWidgetItem *item = m_view.formats->currentItem();
    if (!item || !item->parent())
        return;
    QString currentGroup = item->parent()->text(0);

    QString params = item->data(0, ParamsRole).toString();
    QString extension = item->data(0, ExtensionRole).toString();
    QString currentProfile = item->text(0);
    QDomDocument doc;
    QDomElement profileElement = doc.createElement(QStringLiteral("profile"));
    profileElement.setAttribute(QStringLiteral("name"), currentProfile);
    profileElement.setAttribute(QStringLiteral("category"), i18nc("Category Name", "Custom"));
    profileElement.setAttribute(QStringLiteral("destinationid"), QStringLiteral("favorites"));
    profileElement.setAttribute(QStringLiteral("extension"), extension);
    profileElement.setAttribute(QStringLiteral("args"), params);
    if (params.contains(QStringLiteral("%bitrate"))) {
        // profile has a variable bitrate
        profileElement.setAttribute(QStringLiteral("defaultbitrate"), item->data(0, DefaultBitrateRole).toString());
        profileElement.setAttribute(QStringLiteral("bitrates"), item->data(0, BitratesRole).toStringList().join(QStringLiteral(",")));
    } else if (params.contains(QStringLiteral("%quality"))) {
        profileElement.setAttribute(QStringLiteral("defaultquality"), item->data(0, DefaultBitrateRole).toString());
        profileElement.setAttribute(QStringLiteral("qualities"), item->data(0, BitratesRole).toStringList().join(QStringLiteral(",")));
    }
    if (params.contains(QStringLiteral("%audiobitrate"))) {
        // profile has a variable bitrate
        profileElement.setAttribute(QStringLiteral("defaultaudiobitrate"), item->data(0, DefaultAudioBitrateRole).toString());
        profileElement.setAttribute(QStringLiteral("audiobitrates"), item->data(0, AudioBitratesRole).toStringList().join(QStringLiteral(",")));
    } else if (params.contains(QStringLiteral("%audioquality"))) {
        // profile has a variable bitrate
        profileElement.setAttribute(QStringLiteral("defaultaudioquality"), item->data(0, DefaultAudioBitrateRole).toString());
        profileElement.setAttribute(QStringLiteral("audioqualities"), item->data(0, AudioBitratesRole).toStringList().join(QStringLiteral(",")));
    }
    if (item->data(0, SpeedsRole).canConvert(QVariant::StringList) && item->data(0, SpeedsRole).toStringList().count()) {
        // profile has a variable speed
        profileElement.setAttribute(QStringLiteral("speeds"), item->data(0, SpeedsRole).toStringList().join(QStringLiteral(";")));
    }
    doc.appendChild(profileElement);
    saveProfile(doc.documentElement());
    parseProfiles();
}

void RenderWidget::slotEditProfile()
{
    QTreeWidgetItem *item = m_view.formats->currentItem();
    if (!item || !item->parent()) return;
    QString currentGroup = item->parent()->text(0);

    QString params = item->data(0, ParamsRole).toString();

    Ui::SaveProfile_UI ui;
    QPointer<QDialog> d = new QDialog(this);
    ui.setupUi(d);

    QString customGroup = item->parent()->text(0);
    if (customGroup.isEmpty()) customGroup = i18nc("Group Name", "Custom");
    ui.group_name->setText(customGroup);

    ui.profile_name->setText(item->text(0));
    ui.extension->setText(item->data(0, ExtensionRole).toString());
    ui.parameters->setText(params);
    ui.profile_name->setFocus();
    if (params.contains(QStringLiteral("%bitrate")) || ui.parameters->toPlainText().contains(QStringLiteral("%quality"))) {
        if (params.contains(QStringLiteral("%quality"))) {
            ui.vbitrates_label->setText(i18n("Qualities"));
            ui.default_vbitrate_label->setText(i18n("Default quality"));
        } else {
            ui.vbitrates_label->setText(i18n("Bitrates"));
            ui.default_vbitrate_label->setText(i18n("Default bitrate"));
        }
        if ( item->data(0, BitratesRole).canConvert(QVariant::StringList) && item->data(0, BitratesRole).toStringList().count()) {
            QStringList bitrates = item->data(0, BitratesRole).toStringList();
            ui.vbitrates_list->setText(bitrates.join(QStringLiteral(",")));
            if (item->data(0, DefaultBitrateRole).canConvert(QVariant::String))
                ui.default_vbitrate->setValue(item->data(0, DefaultBitrateRole).toInt());
        }
    } else {
        ui.vbitrates->setHidden(true);
    }

    if (params.contains(QStringLiteral("%audiobitrate")) || ui.parameters->toPlainText().contains(QStringLiteral("%audioquality"))) {
        if (params.contains(QStringLiteral("%audioquality"))) {
            ui.abitrates_label->setText(i18n("Qualities"));
            ui.default_abitrate_label->setText(i18n("Default quality"));
        } else {
            ui.abitrates_label->setText(i18n("Bitrates"));
            ui.default_abitrate_label->setText(i18n("Default bitrate"));
        }
        if ( item->data(0, AudioBitratesRole).canConvert(QVariant::StringList) && item->data(0, AudioBitratesRole).toStringList().count()) {
            QStringList bitrates = item->data(0, AudioBitratesRole).toStringList();
            ui.abitrates_list->setText(bitrates.join(QStringLiteral(",")));
            if (item->data(0, DefaultAudioBitrateRole).canConvert(QVariant::String))
                ui.default_abitrate->setValue(item->data(0, DefaultAudioBitrateRole).toInt());
        }
    }
    else ui.abitrates->setHidden(true);

    if ( item->data(0, SpeedsRole).canConvert(QVariant::StringList) && item->data(0, SpeedsRole).toStringList().count()) {
        QStringList speeds = item->data(0, SpeedsRole).toStringList();
        ui.speeds_list->setText(speeds.join('\n'));
    }

    d->setWindowTitle(i18n("Edit Profile"));

    if (d->exec() == QDialog::Accepted) {
        slotDeleteProfile(false);
        QString exportFile = QStandardPaths::writableLocation(QStandardPaths::DataLocation) + "/export/customprofiles.xml";
        QDomDocument doc;
        QFile file(exportFile);
        doc.setContent(&file, false);
        file.close();
        QDomElement documentElement;
        QDomElement profiles = doc.documentElement();

        if (profiles.isNull() || profiles.tagName() != QLatin1String("profiles")) {
            doc.clear();
            profiles = doc.createElement(QStringLiteral("profiles"));
            profiles.setAttribute(QStringLiteral("version"), 1);
            doc.appendChild(profiles);
        }

        int version = profiles.attribute(QStringLiteral("version"), 0).toInt();
        if (version < 1) {
            doc.clear();
            profiles = doc.createElement(QStringLiteral("profiles"));
            profiles.setAttribute(QStringLiteral("version"), 1);
            doc.appendChild(profiles);
        }

        QString newProfileName = ui.profile_name->text().simplified();
        QString newGroupName = ui.group_name->text().simplified();
        if (newGroupName.isEmpty()) newGroupName = i18nc("Group Name", "Custom");
        QDomNodeList profilelist = doc.elementsByTagName(QStringLiteral("profile"));
        int i = 0;
        while (!profilelist.item(i).isNull()) {
            // make sure a profile with same name doesn't exist
            documentElement = profilelist.item(i).toElement();
            QString profileName = documentElement.attribute(QStringLiteral("name"));
            if (profileName == newProfileName) {
                // a profile with that same name already exists
                bool ok;
                newProfileName = QInputDialog::getText(this, i18n("Profile already exists"), i18n("This profile name already exists. Change the name if you don't want to overwrite it."), QLineEdit::Normal, newProfileName, &ok);
                if (!ok) return;
                if (profileName == newProfileName) {
                    profiles.removeChild(profilelist.item(i));
                    break;
                }
            }
            ++i;
        }

        QDomElement profileElement = doc.createElement(QStringLiteral("profile"));
        profileElement.setAttribute(QStringLiteral("name"), newProfileName);
        profileElement.setAttribute(QStringLiteral("category"), newGroupName);
        profileElement.setAttribute(QStringLiteral("extension"), ui.extension->text().simplified());
        QString args = ui.parameters->toPlainText().simplified();
        profileElement.setAttribute(QStringLiteral("args"), args);
        if (args.contains(QStringLiteral("%bitrate"))) {
            // profile has a variable bitrate
            profileElement.setAttribute(QStringLiteral("defaultbitrate"), QString::number(ui.default_vbitrate->value()));
            profileElement.setAttribute(QStringLiteral("bitrates"), ui.vbitrates_list->text());
        } else if(args.contains(QStringLiteral("%quality"))) {
            profileElement.setAttribute(QStringLiteral("defaultquality"), QString::number(ui.default_vbitrate->value()));
            profileElement.setAttribute(QStringLiteral("qualities"), ui.vbitrates_list->text());
        }
        if (args.contains(QStringLiteral("%audiobitrate"))) {
            // profile has a variable bitrate
            profileElement.setAttribute(QStringLiteral("defaultaudiobitrate"), QString::number(ui.default_abitrate->value()));
            profileElement.setAttribute(QStringLiteral("audiobitrates"), ui.abitrates_list->text());
        } else if (args.contains(QStringLiteral("%audioquality"))) {
            profileElement.setAttribute(QStringLiteral("defaultaudioquality"), QString::number(ui.default_abitrate->value()));
            profileElement.setAttribute(QStringLiteral("audioqualities"), ui.abitrates_list->text());
        }

        if (!ui.speeds_list->toPlainText().isEmpty()) {
            // profile has a variable speed
            profileElement.setAttribute(QStringLiteral("speeds"), ui.speeds_list->toPlainText().replace('\n', ';').simplified());
        }

        profiles.appendChild(profileElement);

        //QCString save = doc.toString().utf8();
        delete d;
        if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            KMessageBox::error(this, i18n("Cannot write to file %1", exportFile));
            return;
        }
        QTextStream out(&file);
        out << doc.toString();
        if (file.error() != QFile::NoError) {
            KMessageBox::error(this, i18n("Cannot write to file %1", exportFile));
            file.close();
            return;
        }
        file.close();
        parseProfiles();
    } else delete d;
}

void RenderWidget::slotDeleteProfile(bool refresh)
{
    //TODO: delete a profile installed by KNewStuff the easy way
    /*
    QString edit = m_view.formats->currentItem()->data(EditableRole).toString();
    if (!edit.endsWith(QLatin1String("customprofiles.xml"))) {
        // This is a KNewStuff installed file, process through KNS
        KNS::Engine engine(0);
        if (engine.init("kdenlive_render.knsrc")) {
            KNS::Entry::List entries;
        }
        return;
    }*/
    QTreeWidgetItem *item = m_view.formats->currentItem();
    if (!item || !item->parent()) return;
    QString currentProfile = item->text(0);
    QString currentGroup = item->parent()->text(0);

    QString exportFile = QStandardPaths::writableLocation(QStandardPaths::DataLocation) + "/export/customprofiles.xml";
    QDomDocument doc;
    QFile file(exportFile);
    doc.setContent(&file, false);
    file.close();

    QDomElement documentElement;
    QDomNodeList profiles = doc.elementsByTagName(QStringLiteral("profile"));
    int i = 0;
    QString profileName;
    while (!profiles.item(i).isNull()) {
        documentElement = profiles.item(i).toElement();
        profileName = documentElement.attribute(QStringLiteral("name"));
        if (profileName == currentProfile) {
            doc.documentElement().removeChild(profiles.item(i));
            break;
        }
        ++i;
    }

    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        KMessageBox::sorry(this, i18n("Unable to write to file %1", exportFile));
        return;
    }
    QTextStream out(&file);
    out << doc.toString();
    if (file.error() != QFile::NoError) {
        KMessageBox::error(this, i18n("Cannot write to file %1", exportFile));
        file.close();
        return;
    }
    file.close();
    if (refresh) {
        parseProfiles();
        focusFirstVisibleItem();
    }
}

void RenderWidget::updateButtons()
{
    if (!m_view.formats->currentItem() || m_view.formats->currentItem()->isHidden()) {
        m_view.buttonSave->setEnabled(false);
        m_view.buttonDelete->setEnabled(false);
        m_view.buttonEdit->setEnabled(false);
        m_view.buttonRender->setEnabled(false);
        m_view.buttonGenerateScript->setEnabled(false);
    } else {
        m_view.buttonSave->setEnabled(true);
        m_view.buttonRender->setEnabled(m_view.formats->currentItem()->data(0, ErrorRole).isNull());
        m_view.buttonGenerateScript->setEnabled(m_view.formats->currentItem()->data(0, ErrorRole).isNull());
        QString edit = m_view.formats->currentItem()->data(0, EditableRole).toString();
        if (edit.isEmpty() || !edit.endsWith(QLatin1String("customprofiles.xml"))) {
            m_view.buttonDelete->setEnabled(false);
            m_view.buttonEdit->setEnabled(false);
        } else {
            m_view.buttonDelete->setEnabled(true);
            m_view.buttonEdit->setEnabled(true);
        }
    }
}


void RenderWidget::focusFirstVisibleItem(const QString &profile, const QString &category)
{
    QTreeWidgetItem *item = 0;
    if (!profile.isEmpty()) {
        QList <QTreeWidgetItem *> items = m_view.formats->findItems(profile, Qt::MatchExactly | Qt::MatchRecursive);
        if (!items.isEmpty())
            if (!category.isEmpty())
                foreach(item, items)
                    if (item->parent()->text(0) == category)
                        break;
    }
    if (!item) {
        // searched profile not found in any category, select 1st available profile
        for (int i = 0; i < m_view.formats->topLevelItemCount(); ++i) {
            item = m_view.formats->topLevelItem(i);
            if (item->childCount() > 0) {
                item = item->child(0);
                break;
            }
        }
    }
    if (item) {
        m_view.formats->setCurrentItem(item);
        item->parent()->setExpanded(true);
    }
    updateButtons();
}

void RenderWidget::slotPrepareExport(bool scriptExport)
{
    if (!QFile::exists(KdenliveSettings::rendererpath())) {
        KMessageBox::sorry(this, i18n("Cannot find the melt program required for rendering (part of Mlt)"));
        return;
    }
    if (m_view.play_after->isChecked() && KdenliveSettings::defaultplayerapp().isEmpty()) {
        KMessageBox::sorry(this, i18n("Cannot play video after rendering because the default video player application is not set.\nPlease define it in Kdenlive settings dialog."));
    }
    QString chapterFile;
    if (m_view.create_chapter->isChecked()) chapterFile = m_view.out_file->url().path() + ".dvdchapter";

    // mantisbt 1051
    QDir dir;
    if (!dir.mkpath(m_view.out_file->url().adjusted(QUrl::RemoveFilename).path())) {
        KMessageBox::sorry(this, i18n("The directory %1, could not be created.\nPlease make sure you have the required permissions.", m_view.out_file->url().adjusted(QUrl::RemoveFilename).path()));
        return;
    }

    emit prepareRenderingData(scriptExport, m_view.render_zone->isChecked(), chapterFile);
}

void RenderWidget::slotExport(bool scriptExport, int zoneIn, int zoneOut,
        const QMap<QString, QString> &metadata,
        const QList<QString> &playlistPaths, const QList<QString> &trackNames,
        const QString &scriptPath, bool exportAudio)
{
    QTreeWidgetItem *item = m_view.formats->currentItem();
    if (!item)
        return;

    QString destBase = m_view.out_file->url().path().trimmed();
    if (destBase.isEmpty())
        return;

    // script file
    QFile file(scriptPath);
    int stemCount = playlistPaths.count();
    bool stemExport = (trackNames.count() > 0);

    for (int stemIdx = 0; stemIdx < stemCount; stemIdx++) {
        QString dest(destBase);

        // on stem export append track name to each filename
        if (stemExport) {
            QFileInfo dfi(dest);
            QStringList filePath;
            // construct the full file path
            filePath << dfi.absolutePath() << QDir::separator() << dfi.completeBaseName() + "_" <<
                    QString(trackNames.at(stemIdx)).replace(QLatin1String(" "),QLatin1String("_")) << QStringLiteral(".") << dfi.suffix();
            dest = filePath.join(QLatin1String(""));
        }

        // Check whether target file has an extension.
        // If not, ask whether extension should be added or not.
        QString extension = item->data(0, ExtensionRole).toString();
        if (!dest.endsWith(extension, Qt::CaseInsensitive)) {
            if (KMessageBox::questionYesNo(this, i18n("File has no extension. Add extension (%1)?", extension)) == KMessageBox::Yes) {
                dest.append('.' + extension);
            }
        }
        // Checks for image sequence
        QStringList imageSequences;
        imageSequences << QStringLiteral("jpg") << QStringLiteral("png") << QStringLiteral("bmp") << QStringLiteral("dpx") << QStringLiteral("ppm") << QStringLiteral("tga") << QStringLiteral("tif");
        if (imageSequences.contains(extension)) {
            // format string for counter?
            if(!QRegExp(".*%[0-9]*d.*").exactMatch(dest)) {
                dest = dest.section('.',0,-2) + "_%05d." + extension;
            }
        }

        if (QFile::exists(dest)) {
            if (KMessageBox::warningYesNo(this, i18n("Output file already exists. Do you want to overwrite it?")) != KMessageBox::Yes) {
                foreach (const QString& playlistFilePath, playlistPaths) {
                    QFile playlistFile(playlistFilePath);
                    if (playlistFile.exists()) {
                        playlistFile.remove();
                    }
                }
                return;
            }
        }

        // Generate script file
        if (scriptExport && stemIdx == 0) {
            // Generate script file
            if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
                KMessageBox::error(this, i18n("Cannot write to file %1", scriptPath));
                return;
            }
            QTextStream outStream(&file);
            outStream << "#! /bin/sh" << '\n' << '\n';
            outStream << "RENDERER=" << '\"' + m_renderer + '\"' << '\n';
            outStream << "MELT=" << '\"' + KdenliveSettings::rendererpath() + '\"' << "\n\n";
        }

        QStringList overlayargs;
        if (m_view.tc_overlay->isChecked()) {
            QString filterFile = QStandardPaths::locate(QStandardPaths::DataLocation, QStringLiteral("metadata.properties"));
            overlayargs << QStringLiteral("meta.attr.timecode=1") << "meta.attr.timecode.markup=#" + QString(m_view.tc_type->currentIndex() ? "frame" : "timecode");
            overlayargs << QStringLiteral("-attach") << QStringLiteral("data_feed:attr_check") << QStringLiteral("-attach");
            overlayargs << "data_show:" + filterFile << QStringLiteral("_loader=1") << QStringLiteral("dynamic=1");
        }

        QStringList render_process_args;

        if (!scriptExport)
            render_process_args << QStringLiteral("-erase");
        if (KdenliveSettings::usekuiserver())
            render_process_args << QStringLiteral("-kuiserver");

        // get process id
        render_process_args << QStringLiteral("-pid:%1").arg(QCoreApplication::applicationPid());

        // Set locale for render process if required
        if (QLocale().decimalPoint() != QLocale::system().decimalPoint()) {;
#ifndef Q_OS_MAC
            const QString currentLocale = setlocale(LC_NUMERIC, NULL);
#else
            const QString currentLocale = setlocale(LC_NUMERIC_MASK, NULL);
#endif
            render_process_args << QStringLiteral("-locale:%1").arg(currentLocale);
        }

        QString renderArgs = m_view.advanced_params->toPlainText().simplified();
        QString std = renderArgs;
        // Check for fps change
        double forcedfps = 0;
        if (std.startsWith(QLatin1String("r="))) {
            QString sub = std.section(' ', 0, 0).toLower();
            sub = sub.section('=', 1, 1);
            forcedfps = sub.toDouble();
        } else if (std.contains(QStringLiteral(" r="))) {
            QString sub = std.section(QStringLiteral(" r="), 1, 1);
            sub = sub.section(' ', 0, 0).toLower();
            forcedfps = sub.toDouble();
        } else if (std.contains(QStringLiteral("mlt_profile="))) {
                QString sub = std.section(QStringLiteral("mlt_profile="), 1, 1);
                sub = sub.section(' ', 0, 0).toLower();
                MltVideoProfile destinationProfile = ProfilesDialog::getVideoProfile(sub);
                forcedfps = (double) destinationProfile.frame_rate_num / destinationProfile.frame_rate_den;
        }

        bool resizeProfile = false;
        if (renderArgs.contains("%dv_standard")) {
            QString std;
            if (fmod((double)m_profile.frame_rate_num / m_profile.frame_rate_den, 30.01) > 27) {
                std = "ntsc";
                if (!(m_profile.frame_rate_num == 30000 && m_profile.frame_rate_den == 1001))
                    forcedfps = 30000.0 / 1001;
                if (!(m_profile.width == 720 && m_profile.height == 480))
                    resizeProfile = true;
            } else {
                std = "pal";
                if (!(m_profile.frame_rate_num == 25 && m_profile.frame_rate_den == 1))
                    forcedfps = 25;
                if (!(m_profile.width == 720 && m_profile.height == 576))
                    resizeProfile = true;
            }

            if ((double) m_profile.display_aspect_num / m_profile.display_aspect_den > 1.5)
                std += "_wide";
            renderArgs.replace("%dv_standard", std);
        }

        // If there is an fps change, we need to use the producer consumer AND update the in/out points
        if (forcedfps > 0 && qAbs((int) 100 * forcedfps - ((int) 100 * m_profile.frame_rate_num / m_profile.frame_rate_den)) > 2) {
            resizeProfile = true;
            double ratio = m_profile.frame_rate_num / m_profile.frame_rate_den / forcedfps;
            if (ratio > 0) {
                zoneIn /= ratio;
                zoneOut /= ratio;
            }
        }

        if (m_view.render_zone->isChecked()) {
            render_process_args << "in=" + QString::number(zoneIn) << "out=" + QString::number(zoneOut);
        } else if (m_view.render_guide->isChecked()) {
            double fps = (double) m_profile.frame_rate_num / m_profile.frame_rate_den;
            double guideStart = m_view.guide_start->itemData(m_view.guide_start->currentIndex()).toDouble();
            double guideEnd = m_view.guide_end->itemData(m_view.guide_end->currentIndex()).toDouble();
            render_process_args << "in=" + QString::number((int) GenTime(guideStart).frames(fps)) << "out=" + QString::number((int) GenTime(guideEnd).frames(fps));
        }

        if (!overlayargs.isEmpty())
            render_process_args << "preargs=" + overlayargs.join(QStringLiteral(" "));

        if (scriptExport)
            render_process_args << QStringLiteral("$MELT");
        else
            render_process_args << KdenliveSettings::rendererpath();

        render_process_args << m_profile.path << item->data(0, RenderRole).toString();
        if (m_view.play_after->isChecked())
            render_process_args << KdenliveSettings::KdenliveSettings::defaultplayerapp();
        else
            render_process_args << QStringLiteral("-");

        if (m_view.speed->isEnabled()) {
            renderArgs.append(QChar(' ') + item->data(0, SpeedsRole).toStringList().at(m_view.speed->value()));
        }

        // Project metadata
        if (m_view.export_meta->isChecked()) {
            QMap<QString, QString>::const_iterator i = metadata.constBegin();
            while (i != metadata.constEnd()) {
                renderArgs.append(QStringLiteral(" %1=%2").arg(i.key()).arg(QString(QUrl::toPercentEncoding(i.value()))));
                ++i;
            }
        }

        // Adjust frame scale
        int width;
        int height;
        if (m_view.rescale->isChecked() && m_view.rescale->isEnabled()) {
            width = m_view.rescale_width->value();
            height = m_view.rescale_height->value();
        } else {
            width = m_profile.width;
            height = m_profile.height;
        }

        // Adjust scanning
        if (m_view.scanning_list->currentIndex() == 1)
            renderArgs.append(" progressive=1");
        else if (m_view.scanning_list->currentIndex() == 2)
            renderArgs.append(" progressive=0");

        // disable audio if requested
        if (!exportAudio)
            renderArgs.append(" an=1 ");

        // Set the thread counts
        if (!renderArgs.contains(QStringLiteral("threads="))) {
            renderArgs.append(QStringLiteral(" threads=%1").arg(KdenliveSettings::encodethreads()));
        }
        renderArgs.append(QStringLiteral(" real_time=-%1").arg(KdenliveSettings::mltthreads()));

        // Check if the rendering profile is different from project profile,
        // in which case we need to use the producer_comsumer from MLT
        const QString currentSize = QString::number(width) + 'x' + QString::number(height);
        QString subsize;
        if (std.startsWith(QLatin1String("s="))) {
            subsize = std.section(' ', 0, 0).toLower();
            subsize = subsize.section('=', 1, 1);
        } else if (std.contains(QStringLiteral(" s="))) {
            subsize = std.section(QStringLiteral(" s="), 1, 1);
            subsize = subsize.section(' ', 0, 0).toLower();
        } else if (m_view.rescale->isChecked() && m_view.rescale->isEnabled()) {
            subsize = QStringLiteral(" s=%1x%2").arg(width).arg(height);
            // Add current size parameter
            renderArgs.append(subsize);
        }
        // Check if we need to embed the playlist into the producer consumer
        // That is required if PAR != 1
        if (m_profile.sample_aspect_num != m_profile.sample_aspect_den && subsize.isEmpty()) {
            resizeProfile = true;
        }

        QStringList paramsList = renderArgs.split(' ', QString::SkipEmptyParts);

        QScriptEngine sEngine;
        sEngine.globalObject().setProperty(QStringLiteral("bitrate"), m_view.video->value());
        sEngine.globalObject().setProperty(QStringLiteral("quality"), m_view.video->value());
        sEngine.globalObject().setProperty(QStringLiteral("audiobitrate"), m_view.audio->value());
        sEngine.globalObject().setProperty(QStringLiteral("audioquality"), m_view.audio->value());
        sEngine.globalObject().setProperty(QStringLiteral("dar"), '@' + QString::number(m_profile.display_aspect_num) + '/' + QString::number(m_profile.display_aspect_den));
        sEngine.globalObject().setProperty(QStringLiteral("passes"), static_cast<int>(m_view.checkTwoPass->isChecked()) + 1);

        for (int i = 0; i < paramsList.count(); ++i) {
            QString paramName = paramsList.at(i).section('=', 0, -2);
            QString paramValue = paramsList.at(i).section('=', -1);
            // If the profiles do not match we need to use the consumer tag
            if (paramName == QLatin1String("mlt_profile") && paramValue != m_profile.path) {
                resizeProfile = true;
            }
            // evaluate expression
            if (paramValue.startsWith('%')) {
                paramValue = sEngine.evaluate(paramValue.remove(0, 1)).toString();
                paramsList[i] = paramName + '=' + paramValue;
            }
            sEngine.globalObject().setProperty(paramName.toUtf8().constData(), paramValue);
        }

        if (resizeProfile && !KdenliveSettings::gpu_accel())
            render_process_args << "consumer:" + (scriptExport ? "$SOURCE_" + QString::number(stemIdx) : playlistPaths.at(stemIdx));
        else
            render_process_args <<  (scriptExport ? "$SOURCE_" + QString::number(stemIdx) : playlistPaths.at(stemIdx));

        render_process_args << (scriptExport ? "$TARGET_" + QString::number(stemIdx) : QUrl::fromLocalFile(dest).url());
        if (KdenliveSettings::gpu_accel()) {
                render_process_args << QStringLiteral("glsl.=1");
        }
        render_process_args << paramsList;

        if (scriptExport) {
            QTextStream outStream(&file);
            QString stemIdxStr(QString::number(stemIdx));

            outStream << "SOURCE_" << stemIdxStr << "=" << '\"' + QUrl(playlistPaths.at(stemIdx)).toEncoded() + '\"' << '\n';
            outStream << "TARGET_" << stemIdxStr << "=" << '\"' + QUrl::fromLocalFile(dest).toEncoded() + '\"' << '\n';
            outStream << "PARAMETERS_" << stemIdxStr << "=" << '\"' + render_process_args.join(QStringLiteral(" ")) + '\"' << '\n';
            outStream << "$RENDERER $PARAMETERS_" << stemIdxStr << "\n\n";

            if (stemIdx == (stemCount - 1)) {
                if (file.error() != QFile::NoError) {
                    KMessageBox::error(this, i18n("Cannot write to file %1", scriptPath));
                    file.close();
                    return;
                }
                file.close();
                QFile::setPermissions(scriptPath,
                        file.permissions() | QFile::ExeUser);

                QTimer::singleShot(400, this, SLOT(parseScriptFiles()));
                m_view.tabWidget->setCurrentIndex(2);
                return;

            } else {
                continue;
            }
        }

        // Save rendering profile to document
        QMap <QString, QString> renderProps;
        renderProps.insert(QStringLiteral("rendercategory"), m_view.formats->currentItem()->parent()->text(0));
        renderProps.insert(QStringLiteral("renderprofile"), m_view.formats->currentItem()->text(0));
        renderProps.insert(QStringLiteral("renderurl"), destBase);
        renderProps.insert(QStringLiteral("renderzone"), QString::number(m_view.render_zone->isChecked()));
        renderProps.insert(QStringLiteral("renderguide"), QString::number(m_view.render_guide->isChecked()));
        renderProps.insert(QStringLiteral("renderstartguide"), QString::number(m_view.guide_start->currentIndex()));
        renderProps.insert(QStringLiteral("renderendguide"), QString::number(m_view.guide_end->currentIndex()));
        renderProps.insert(QStringLiteral("renderscanning"), QString::number(m_view.scanning_list->currentIndex()));
        int export_audio = 0;
        if (m_view.export_audio->checkState() == Qt::Checked)
            export_audio = 2;
        else if (m_view.export_audio->checkState() == Qt::Unchecked)
            export_audio = 1;

        renderProps.insert(QStringLiteral("renderexportaudio"), QString::number(export_audio));
        renderProps.insert(QStringLiteral("renderrescale"), QString::number(m_view.rescale->isChecked()));
        renderProps.insert(QStringLiteral("renderrescalewidth"), QString::number(m_view.rescale_width->value()));
        renderProps.insert(QStringLiteral("renderrescaleheight"), QString::number(m_view.rescale_height->value()));
        renderProps.insert(QStringLiteral("rendertcoverlay"), QString::number(m_view.tc_overlay->isChecked()));
        renderProps.insert(QStringLiteral("rendertctype"), QString::number(m_view.tc_type->currentIndex()));
        renderProps.insert(QStringLiteral("renderratio"), QString::number(m_view.rescale_keep->isChecked()));
        renderProps.insert(QStringLiteral("renderplay"), QString::number(m_view.play_after->isChecked()));
        renderProps.insert(QStringLiteral("rendertwopass"), QString::number(m_view.checkTwoPass->isChecked()));
        renderProps.insert(QStringLiteral("renderquality"), QString::number(m_view.video->value()));
        renderProps.insert(QStringLiteral("renderaudioquality"), QString::number(m_view.audio->value()));
        renderProps.insert(QStringLiteral("renderspeed"), QString::number(m_view.speed->value()));

        emit selectedRenderProfile(renderProps);

        // insert item in running jobs list
        RenderJobItem *renderItem = NULL;
        QList<QTreeWidgetItem *> existing = m_view.running_jobs->findItems(dest, Qt::MatchExactly, 1);
        if (!existing.isEmpty()) {
            renderItem = static_cast<RenderJobItem*> (existing.at(0));
            if (renderItem->status() == RUNNINGJOB || renderItem->status() == WAITINGJOB || renderItem->status() == STARTINGJOB) {
                KMessageBox::information(this, i18n("There is already a job writing file:<br /><b>%1</b><br />Abort the job if you want to overwrite it...", dest), i18n("Already running"));
                return;
            }
            if (renderItem->type() != DirectRenderType) {
                delete renderItem;
                renderItem = NULL;
            }
            else {
                renderItem->setData(1, ProgressRole, 0);
                renderItem->setStatus(WAITINGJOB);
                renderItem->setIcon(0, KoIconUtils::themedIcon(QStringLiteral("media-playback-pause")));
                renderItem->setData(1, Qt::UserRole, i18n("Waiting..."));
                renderItem->setData(1, ParametersRole, dest);
            }
        }
        if (!renderItem) {
            renderItem = new RenderJobItem(m_view.running_jobs, QStringList() << QString() << dest);
        }
        renderItem->setData(1, TimeRole, QDateTime::currentDateTime());

        // Set rendering type
        /*if (group == QLatin1String("dvd")) {
            if (m_view.open_dvd->isChecked()) {
                renderItem->setData(0, Qt::UserRole, group);
                if (renderArgs.contains(QStringLiteral("mlt_profile="))) {
                    //TODO: probably not valid anymore (no more MLT profiles in args)
                    // rendering profile contains an MLT profile, so pass it to the running jog item, useful for dvd
                    QString prof = renderArgs.section(QStringLiteral("mlt_profile="), 1, 1);
                    prof = prof.section(' ', 0, 0);
                    qDebug() << "// render profile: " << prof;
                    renderItem->setMetadata(prof);
                }
            }
        } else {
            if (group == QLatin1String("websites") && m_view.open_browser->isChecked()) {
                renderItem->setData(0, Qt::UserRole, group);
                // pass the url
                QString url = m_view.formats->currentItem()->data(ExtraRole).toString();
                renderItem->setMetadata(url);
            }
        }*/

        renderItem->setData(1, ParametersRole, render_process_args);
        if (exportAudio == false)
            renderItem->setData(1, ExtraInfoRole, i18n("Video without audio track"));
        else
            renderItem->setData(1, ExtraInfoRole, QString());

        m_view.running_jobs->setCurrentItem(renderItem);
        m_view.tabWidget->setCurrentIndex(1);
        // check render status
        checkRenderStatus();
    } // end loop
}

void RenderWidget::checkRenderStatus()
{
    // check if we have a job waiting to render
    if (m_blockProcessing)
        return;

    RenderJobItem* item = static_cast<RenderJobItem*> (m_view.running_jobs->topLevelItem(0));

    // Make sure no other rendering is running
    while (item) {
        if (item->status() == RUNNINGJOB)
            return;
        item = static_cast<RenderJobItem*> (m_view.running_jobs->itemBelow(item));
    }
    item = static_cast<RenderJobItem*> (m_view.running_jobs->topLevelItem(0));
    bool waitingJob = false;

    // Find first waiting job
    while (item) {
        if (item->status() == WAITINGJOB) {
            item->setData(1, TimeRole, QDateTime::currentDateTime());
            waitingJob = true;
            startRendering(item);
            item->setStatus(STARTINGJOB);
            break;
        }
        item = static_cast<RenderJobItem*> (m_view.running_jobs->itemBelow(item));
    }
    if (waitingJob == false && m_view.shutdown->isChecked())
        emit shutdown();
}

void RenderWidget::startRendering(RenderJobItem *item)
{
    if (item->type() == DirectRenderType) {
        // Normal render process
        if (QProcess::startDetached(m_renderer, item->data(1, ParametersRole).toStringList()) == false) {
            item->setStatus(FAILEDJOB);
        } else {
            KNotification::event(QStringLiteral("RenderStarted"), i18n("Rendering <i>%1</i> started", item->text(1)), QPixmap(), this);
        }
    } else if (item->type() == ScriptRenderType){
        // Script item
        if (QProcess::startDetached('"' + item->data(1, ParametersRole).toString() + '"') == false) {
            item->setStatus(FAILEDJOB);
        }
    }
}


int RenderWidget::waitingJobsCount() const
{
    int count = 0;
    RenderJobItem* item = static_cast<RenderJobItem*> (m_view.running_jobs->topLevelItem(0));
    while (item) {
        if (item->status() == WAITINGJOB || item->status() == STARTINGJOB) count++;
        item = static_cast<RenderJobItem*>(m_view.running_jobs->itemBelow(item));
    }
    return count;
}

void RenderWidget::setProfile(const MltVideoProfile &profile)
{
    m_view.scanning_list->setCurrentIndex(0);
    m_view.rescale_width->setValue(KdenliveSettings::defaultrescalewidth());
    if (!m_view.rescale_keep->isChecked()) {
        m_view.rescale_height->blockSignals(true);
        m_view.rescale_height->setValue(KdenliveSettings::defaultrescaleheight());
        m_view.rescale_height->blockSignals(false);
    }
    if (m_profile != profile) {
        m_profile = profile;
        refreshView();
    }
}

void RenderWidget::refreshView(const QString &profile)
{
    m_view.formats->blockSignals(true);
    m_view.formats->clear();
    QIcon brokenIcon = KoIconUtils::themedIcon(QStringLiteral("dialog-close"));
    QIcon warningIcon = KoIconUtils::themedIcon(QStringLiteral("dialog-warning"));

    QStringList formatsList;
    QStringList vcodecsList;
    QStringList acodecsList;
    if (!KdenliveSettings::bypasscodeccheck()) {
	formatsList= KdenliveSettings::supportedformats();
	vcodecsList = KdenliveSettings::videocodecs();
	acodecsList = KdenliveSettings::audiocodecs();
    }

    KColorScheme scheme(palette().currentColorGroup(), KColorScheme::Window);
    const QColor disabled = scheme.foreground(KColorScheme::InactiveText).color();
    const QColor disabledbg = scheme.background(KColorScheme::NegativeBackground).color();
    double project_framerate = (double) m_profile.frame_rate_num / m_profile.frame_rate_den;

    for (int i = 0; i < m_view.formats->topLevelItemCount(); ++i) {
        QTreeWidgetItem *group = m_view.formats->topLevelItem(i);
        for (int j = 0; j < group->childCount(); ++j) {
            QTreeWidgetItem *item = group->child(i);
            QString std = item->data(0, StandardRole).toString();
            if (std.isEmpty()
                || (std.contains(QStringLiteral("PAL"), Qt::CaseInsensitive) && m_profile.frame_rate_num == 25 && m_profile.frame_rate_den == 1)
                || (std.contains(QStringLiteral("NTSC"), Qt::CaseInsensitive) && m_profile.frame_rate_num == 30000 && m_profile.frame_rate_den == 1001)
                ) {
                // Standard OK
            } else {
                item->setData(0, ErrorRole, i18n("Standard (%1) not compatible with project profile (%2)", std, (double) m_profile.frame_rate_num / m_profile.frame_rate_den));
                item->setIcon(0, brokenIcon);
                item->setForeground(0, disabled);
                continue;
            }

            std = item->data(0, ParamsRole).toString();
            // Make sure the selected profile uses the same frame rate as project profile
            if (std.contains(QStringLiteral("mlt_profile="))) {
                QString profile = std.section(QStringLiteral("mlt_profile="), 1, 1).section(' ', 0, 0);
                MltVideoProfile p = ProfilesDialog::getVideoProfile(profile);
                if (p.frame_rate_den > 0) {
                    double profile_rate = (double) p.frame_rate_num / p.frame_rate_den;
                    if ((int) (1000.0 * profile_rate) != (int) (1000.0 * project_framerate)) {
                        item->setData(0, ErrorRole, i18n("Frame rate (%1) not compatible with project profile (%2)", profile_rate, project_framerate));
                        item->setIcon(0, brokenIcon);
                        item->setForeground(0, disabled);
                        continue;
                    }
                }
            }

            // Make sure the selected profile uses an installed avformat codec / format
            if (!formatsList.isEmpty()) {
                QString format;
                if (std.startsWith(QLatin1String("f="))) format = std.section(QStringLiteral("f="), 1, 1);
                else if (std.contains(QStringLiteral(" f="))) format = std.section(QStringLiteral(" f="), 1, 1);
                if (!format.isEmpty()) {
                    format = format.section(' ', 0, 0).toLower();
                    if (!formatsList.contains(format)) {
                        item->setData(0, ErrorRole, i18n("Unsupported video format: %1", format));
                        item->setIcon(0, brokenIcon);
                        item->setForeground(0, disabled);
                        continue;
                    }
                }
            }
            if (!acodecsList.isEmpty()) {
                QString format;
                if (std.startsWith(QLatin1String("acodec="))) format = std.section(QStringLiteral("acodec="), 1, 1);
                else if (std.contains(QStringLiteral(" acodec="))) format = std.section(QStringLiteral(" acodec="), 1, 1);
                if (!format.isEmpty()) {
                    format = format.section(' ', 0, 0).toLower();
                    if (!acodecsList.contains(format)) {
                        item->setData(0, ErrorRole, i18n("Unsupported audio codec: %1", format));
                        item->setIcon(0, brokenIcon);
                        item->setForeground(0, disabled);
                        item->setBackground(0, disabledbg);
                    }
                }
            }
            if (!vcodecsList.isEmpty()) {
                QString format;
                if (std.startsWith(QLatin1String("vcodec="))) format = std.section(QStringLiteral("vcodec="), 1, 1);
                else if (std.contains(QStringLiteral(" vcodec="))) format = std.section(QStringLiteral(" vcodec="), 1, 1);
                if (!format.isEmpty()) {
                    format = format.section(' ', 0, 0).toLower();
                    if (!vcodecsList.contains(format)) {
                        item->setData(0, ErrorRole, i18n("Unsupported video codec: %1", format));
                        item->setIcon(0, brokenIcon);
                        item->setForeground(0, disabled);
                        continue;
                    }
                }
            }
            if (std.contains(QStringLiteral(" profile=")) || std.startsWith(QLatin1String("profile="))) {
                // changed in MLT commit d8a3a5c9190646aae72048f71a39ee7446a3bd45
                // (http://www.mltframework.org/gitweb/mlt.git?p=mltframework.org/mlt.git;a=commit;h=d8a3a5c9190646aae72048f71a39ee7446a3bd45)
                item->setData(0, ErrorRole, i18n("This render profile uses a 'profile' parameter.<br />Unless you know what you are doing you will probably have to change it to 'mlt_profile'."));
                item->setIcon(0, warningIcon);
                continue;
            }
        }
    }
    focusFirstVisibleItem(profile);
    m_view.formats->blockSignals(false);
    refreshParams();
}

QUrl RenderWidget::filenameWithExtension(QUrl url, const QString &extension)
{
    if (!url.isValid()) url = QUrl::fromLocalFile(m_projectFolder);
    QString directory = url.adjusted(QUrl::RemoveFilename).path();
    QString filename = url.fileName();
    QString ext;

    if (extension.at(0) == '.') ext = extension;
    else ext = '.' + extension;

    if (filename.isEmpty()) filename = i18n("untitled");

    int pos = filename.lastIndexOf('.');
    if (pos == 0) filename.append(ext);
    else {
        if (!filename.endsWith(ext, Qt::CaseInsensitive)) {
            filename = filename.left(pos) + ext;
        }
    }

    return QUrl::fromLocalFile(directory + filename);
}

void RenderWidget::refreshParams()
{
    // Format not available (e.g. codec not installed); Disable start button
    QTreeWidgetItem *item = m_view.formats->currentItem();
    QString extension;
    if (item) {
        extension = item->data(0, ExtensionRole).toString();
    }
    if (!item || item->isHidden() || extension.isEmpty()) {
        if (!item)
            errorMessage(i18n("No matching profile"));
        else if (!item->parent()) // category
            ;
        else if (extension.isEmpty()) {
            errorMessage(i18n("Invalid profile"));
        }
        m_view.advanced_params->clear();
        m_view.buttonRender->setEnabled(false);
        m_view.buttonGenerateScript->setEnabled(false);
        return;
    }
    QString params = item->data(0, ParamsRole).toString();
    errorMessage(item->data(0, ErrorRole).toString());
    m_view.advanced_params->setPlainText(params);
    if (params.contains(QStringLiteral(" s=")) || params.startsWith(QLatin1String("s=")) || params.contains("%dv_standard")) {
        // profile has a fixed size, do not allow resize
        m_view.rescale->setEnabled(false);
        setRescaleEnabled(false);
    } else {
        m_view.rescale->setEnabled(true);
        setRescaleEnabled(m_view.rescale->isChecked());
    }
    QUrl url = filenameWithExtension(m_view.out_file->url(), extension);
    m_view.out_file->setUrl(url);
//     if (!url.isEmpty()) {
//         QString path = url.path();
//         int pos = path.lastIndexOf('.') + 1;
//  if (pos == 0) path.append('.' + extension);
//         else path = path.left(pos) + extension;
//         m_view.out_file->setUrl(QUrl(path));
//     } else {
//         m_view.out_file->setUrl(QUrl(QDir::homePath() + "/untitled." + extension));
//     }
    m_view.out_file->setFilter("*." + extension);
    QString edit = item->data(0, EditableRole).toString();
    if (edit.isEmpty() || !edit.endsWith(QLatin1String("customprofiles.xml"))) {
        m_view.buttonDelete->setEnabled(false);
        m_view.buttonEdit->setEnabled(false);
    } else {
        m_view.buttonDelete->setEnabled(true);
        m_view.buttonEdit->setEnabled(true);
    }

    // video quality control
    bool quality = false;
    if ((params.contains(QStringLiteral("%quality")) || params.contains(QStringLiteral("%bitrate")))
            && item->data(0, BitratesRole).canConvert(QVariant::StringList)) {
        // bitrates or quantizers list
        QStringList qs = item->data(0, BitratesRole).toStringList();
        if (qs.count() > 1) {
            quality = true;
            int qmax = qs.first().toInt();
            int qmin = qs.last().toInt();
            if (qmax < qmin) {
                // always show best quality on right
                m_view.video->setRange(qmax, qmin);
                m_view.video->setProperty("decreasing", true);
            } else {
                m_view.video->setRange(qmin, qmax);
                m_view.video->setProperty("decreasing", false);
            }
        }
    }
    m_view.video->setEnabled(quality);
    m_view.quality->setEnabled(quality);

    // audio quality control
    quality = false;
    if ((params.contains(QStringLiteral("%audioquality")) || params.contains(QStringLiteral("%audiobitrate")))
            && item->data(0, AudioBitratesRole).canConvert(QVariant::StringList)) {
        // bitrates or quantizers list
        QStringList qs = item->data(0, AudioBitratesRole).toStringList();
        if (qs.count() > 1) {
            quality = true;
            int qmax = qs.first().toInt();
            int qmin = qs.last().toInt();
            if (qmax < qmin) {
                m_view.audio->setRange(qmax, qmin);
                m_view.audio->setProperty("decreasing", true);
            } else {
                m_view.audio->setRange(qmin, qmax);
                m_view.audio->setProperty("decreasing", false);
            }
            if (params.contains(QStringLiteral("%audiobitrate")))
                m_view.audio->setSingleStep(32); // 32kbps step
            else
                m_view.audio->setSingleStep(1);
        }
    }
    else m_view.audio->setEnabled(quality);

    if (item->data(0, SpeedsRole).canConvert(QVariant::StringList) && item->data(0, SpeedsRole).toStringList().count()) {
        int speed = item->data(0, SpeedsRole).toStringList().count() - 1;
        m_view.speed->setEnabled(true);
        m_view.speed->setMaximum(speed);
        m_view.speed->setValue(speed * 3 / 4); // default to intermediate speed
    } else m_view.speed->setEnabled(false);

    m_view.checkTwoPass->setEnabled(params.contains(QStringLiteral("passes")));

    m_view.encoder_threads->setEnabled(!params.contains(QStringLiteral("threads=")));

    m_view.buttonRender->setEnabled(m_view.formats->currentItem()->data(0, ErrorRole).isNull());
    m_view.buttonGenerateScript->setEnabled(m_view.formats->currentItem()->data(0, ErrorRole).isNull());
}

void RenderWidget::reloadProfiles()
{
    parseProfiles();
}

void RenderWidget::parseProfiles()
{
    m_view.formats->clear();

    // Parse our xml profile
    QString exportFile = QStandardPaths::locate(QStandardPaths::DataLocation, QStringLiteral("export/profiles.xml"));
    parseFile(exportFile, false);

    // Parse some MLT's profiles
    parseMltPresets();

    QString exportFolder = QStandardPaths::writableLocation(QStandardPaths::DataLocation) + "/export/";
    QDir directory(exportFolder);
    QStringList filter;
    filter << QStringLiteral("*.xml");
    QStringList fileList = directory.entryList(filter, QDir::Files);
    // We should parse customprofiles.xml in last position, so that user profiles
    // can also override profiles installed by KNewStuff
    fileList.removeAll(QStringLiteral("customprofiles.xml"));
    foreach(const QString &filename, fileList)
        parseFile(directory.absoluteFilePath(filename), true);
    if (QFile::exists(exportFolder + "customprofiles.xml"))
        parseFile(exportFolder + "customprofiles.xml", true);
}

void RenderWidget::parseMltPresets()
{
    QDir root(KdenliveSettings::mltpath());
    if (!root.cd(QStringLiteral("../presets/consumer/avformat"))) {
        //Cannot find MLT's presets directory
        qWarning()<<" / / / WARNING, cannot find MLT's preset folder";
        return;
    }
    if (root.cd(QStringLiteral("lossless"))) {
        QString groupName = i18n("Lossless/HQ");
        QList<QTreeWidgetItem *> foundGroup = m_view.formats->findItems(groupName, Qt::MatchExactly);
        QTreeWidgetItem *groupItem;
        if (!foundGroup.isEmpty()) {
            groupItem = foundGroup.takeFirst();
        } else {
            groupItem = new QTreeWidgetItem(QStringList(groupName));
            m_view.formats->addTopLevelItem(groupItem);
            groupItem->setExpanded(true);
        }

        QStringList profiles = root.entryList(QDir::Files, QDir::Name);
        foreach(const QString &prof, profiles) {
            KConfig config(root.absoluteFilePath(prof), KConfig::SimpleConfig );
            KConfigGroup group = config.group(QByteArray());
            QString vcodec = group.readEntry("vcodec");
            QString acodec = group.readEntry("acodec");
            QString extension = group.readEntry("meta.preset.extension");
            QString note = group.readEntry("meta.preset.note");
            QString profileName = prof;
            if (!vcodec.isEmpty() || !acodec.isEmpty()) {
                profileName.append(" (");
                if (!vcodec.isEmpty()) {
                    profileName.append(vcodec);
                    if (!acodec.isEmpty()) {
                        profileName.append("+" + acodec);
                    }
                }
                else if (!acodec.isEmpty()) profileName.append(acodec);
                profileName.append(")");
            }
            QTreeWidgetItem *item = new QTreeWidgetItem(QStringList(profileName));
            item->setData(0, ExtensionRole, extension);
            item->setData(0, RenderRole, "avformat");
            item->setData(0, ParamsRole, QString("properties=lossless/" + prof));
            if (!note.isEmpty()) item->setToolTip(0, note);
            groupItem->addChild(item);
        }
    }
    if (root.cd(QStringLiteral("../stills"))) {
        QString groupName =i18nc("Category Name", "Images sequence");
        QList<QTreeWidgetItem *> foundGroup = m_view.formats->findItems(groupName, Qt::MatchExactly);
        QTreeWidgetItem *groupItem;
        if (!foundGroup.isEmpty()) {
            groupItem = foundGroup.takeFirst();
        } else {
            groupItem = new QTreeWidgetItem(QStringList(groupName));
            m_view.formats->addTopLevelItem(groupItem);
            groupItem->setExpanded(true);
        }
        QStringList profiles = root.entryList(QDir::Files, QDir::Name);
        foreach(const QString &prof, profiles) {
            KConfig config(root.absoluteFilePath(prof), KConfig::SimpleConfig );
            KConfigGroup group = config.group(QByteArray());
            QString extension = group.readEntry("meta.preset.extension");
            QString note = group.readEntry("meta.preset.note");
            QTreeWidgetItem *item = new QTreeWidgetItem(QStringList(prof));
            item->setData(0, GroupRole, groupName);
            item->setData(0, ExtensionRole, extension);
            item->setData(0, RenderRole, "avformat");
            item->setData(0, ParamsRole, QString("properties=stills/" + prof));
            if (!note.isEmpty()) item->setToolTip(0, note);
            groupItem->addChild(item);
        }
    }
}

void RenderWidget::parseFile(const QString &exportFile, bool editable)
{
    QDomDocument doc;
    QFile file(exportFile);
    doc.setContent(&file, false);
    file.close();
    QDomElement documentElement;
    QDomElement profileElement;
    QString extension;
    QDomNodeList groups = doc.elementsByTagName(QStringLiteral("group"));
    QTreeWidgetItem *item = NULL;
    const QStringList acodecsList = KdenliveSettings::audiocodecs();
    bool replaceVorbisCodec = false;
    if (acodecsList.contains(QStringLiteral("libvorbis"))) replaceVorbisCodec = true;
    bool replaceLibfaacCodec = false;
    if (acodecsList.contains(QStringLiteral("libfaac"))) replaceLibfaacCodec = true;

    if (editable || groups.count() == 0) {
        QDomElement profiles = doc.documentElement();
        if (editable && profiles.attribute(QStringLiteral("version"), 0).toInt() < 1) {
            // this is an old profile version, update it
            QDomDocument newdoc;
            QDomElement newprofiles = newdoc.createElement(QStringLiteral("profiles"));
            newprofiles.setAttribute(QStringLiteral("version"), 1);
            newdoc.appendChild(newprofiles);
            QDomNodeList profilelist = doc.elementsByTagName(QStringLiteral("profile"));
            for (int i = 0; i < profilelist.count(); ++i) {
                QString category = i18nc("Category Name", "Custom");
                QString extension;
                QDomNode parent = profilelist.at(i).parentNode();
                if (!parent.isNull()) {
                    QDomElement parentNode = parent.toElement();
                    if (parentNode.hasAttribute(QStringLiteral("name"))) category = parentNode.attribute(QStringLiteral("name"));
                    extension = parentNode.attribute(QStringLiteral("extension"));
                }
                if (!profilelist.at(i).toElement().hasAttribute(QStringLiteral("category"))) {
                  profilelist.at(i).toElement().setAttribute(QStringLiteral("category"), category);
                }
                if (!extension.isEmpty()) profilelist.at(i).toElement().setAttribute(QStringLiteral("extension"), extension);
                QDomNode n = profilelist.at(i).cloneNode();
                newprofiles.appendChild(newdoc.importNode(n, true));
            }
            if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
                KMessageBox::sorry(this, i18n("Unable to write to file %1", exportFile));
                return;
            }
            QTextStream out(&file);
            out << newdoc.toString();
            file.close();
            parseFile(exportFile, editable);
            return;
        }

        QDomNode node = doc.elementsByTagName(QStringLiteral("profile")).at(0);
        if (node.isNull()) {
            return;
        }
        int count = 1;
        while (!node.isNull()) {
            QDomElement profile = node.toElement();
            QString profileName = profile.attribute(QStringLiteral("name"));
            QString standard = profile.attribute(QStringLiteral("standard"));
	    QTextDocument docConvert;
	    docConvert.setHtml(profile.attribute(QStringLiteral("args")));
            QString params = docConvert.toPlainText().simplified();

            if (replaceVorbisCodec && params.contains(QStringLiteral("acodec=vorbis"))) {
                // replace vorbis with libvorbis
                params = params.replace(QLatin1String("vorbis"), QLatin1String("libvorbis"));
            }
            if (replaceLibfaacCodec && params.contains(QStringLiteral("acodec=aac"))) {
                // replace libfaac with aac
                params = params.replace(QLatin1String("aac"), QLatin1String("libfaac"));
            }

            QString prof_extension = profile.attribute(QStringLiteral("extension"));
            if (!prof_extension.isEmpty()) extension = prof_extension;
            QString groupName = profile.attribute(QStringLiteral("category"), i18nc("Category Name", "Custom"));
            QList<QTreeWidgetItem *> foundGroup = m_view.formats->findItems(groupName, Qt::MatchExactly);
            QTreeWidgetItem *groupItem;
            if (!foundGroup.isEmpty()) {
                groupItem = foundGroup.takeFirst();
            } else {
                groupItem = new QTreeWidgetItem(QStringList(groupName));
                if (editable)
                    m_view.formats->insertTopLevelItem(0, groupItem);
                else {
                    m_view.formats->addTopLevelItem(groupItem);
                    groupItem->setExpanded(true);
                }
            }
            
            // Check if item with same name already exists and replace it,
            // allowing to override default profiles
            QTreeWidgetItem *item  = 0;
            for (int j = 0; j < groupItem->childCount(); ++j) {
                if (groupItem->child(j)->text(0) == profileName) {
                    item = groupItem->child(j);
                    break;
                }
            }
            if (!item) item = new QTreeWidgetItem(QStringList(profileName));
            item->setData(0, GroupRole, groupName);
            item->setData(0, ExtensionRole, extension);
            item->setData(0, RenderRole, "avformat");
            item->setData(0, StandardRole, standard);
            item->setData(0, ParamsRole, params);
            if (params.contains("%quality"))
                item->setData(0, BitratesRole, profile.attribute(QStringLiteral("qualities")).split(',', QString::SkipEmptyParts));
            else if (params.contains("%bitrate"))
                item->setData(0, BitratesRole, profile.attribute(QStringLiteral("bitrates")).split(',', QString::SkipEmptyParts));
            if (params.contains("%audioquality"))
                item->setData(0, AudioBitratesRole, profile.attribute(QStringLiteral("audioqualities")).split(',', QString::SkipEmptyParts));
            else if (params.contains("%audiobitrate"))
                item->setData(0, AudioBitratesRole, profile.attribute(QStringLiteral("audiobitrates")).split(',', QString::SkipEmptyParts));
            if (profile.hasAttribute(QStringLiteral("speeds")))
                item->setData(0, SpeedsRole, profile.attribute(QStringLiteral("speeds")).split(';', QString::SkipEmptyParts));
            if (profile.hasAttribute(QStringLiteral("url")))
                item->setData(0, ExtraRole, profile.attribute(QStringLiteral("url")));
            if (editable) {
                item->setData(0, EditableRole, exportFile);
                if (exportFile.endsWith(QLatin1String("customprofiles.xml")))
                    item->setIcon(0, KoIconUtils::themedIcon(QStringLiteral("favorite")));
                else item->setIcon(0, QIcon::fromTheme(QStringLiteral("applications-internet")));
            }
            groupItem->addChild(item);
            node = doc.elementsByTagName(QStringLiteral("profile")).at(count);
            count++;
        }
        return;
    }

    int i = 0;
    QString groupName;
    QString profileName;

    QString prof_extension;
    QString renderer;
    QString params;
    QString standard;
    QIcon icon;

    while (!groups.item(i).isNull()) {
        documentElement = groups.item(i).toElement();
        QDomNode gname = documentElement.elementsByTagName(QStringLiteral("groupname")).at(0);
        groupName = documentElement.attribute(QStringLiteral("name"), i18nc("Attribute Name", "Custom"));
        extension = documentElement.attribute(QStringLiteral("extension"), QString());
        renderer = documentElement.attribute(QStringLiteral("renderer"), QString());
        QList<QTreeWidgetItem *> foundGroup = m_view.formats->findItems(groupName, Qt::MatchExactly);
        QTreeWidgetItem *groupItem;
        if (!foundGroup.isEmpty()) {
            groupItem = foundGroup.takeFirst();
        } else {
            groupItem = new QTreeWidgetItem(QStringList(groupName));
            m_view.formats->addTopLevelItem(groupItem);
            groupItem->setExpanded(true);
        }
        
        QDomNode n = groups.item(i).firstChild();
        while (!n.isNull()) {
            if (n.toElement().tagName() != QLatin1String("profile")) {
                n = n.nextSibling();
                continue;
            }
            profileElement = n.toElement();
            profileName = profileElement.attribute(QStringLiteral("name"));
            standard = profileElement.attribute(QStringLiteral("standard"));
            params = profileElement.attribute(QStringLiteral("args")).simplified();

            if (replaceVorbisCodec && params.contains(QStringLiteral("acodec=vorbis"))) {
                // replace vorbis with libvorbis
                params = params.replace(QLatin1String("vorbis"), QLatin1String("libvorbis"));
            }
            if (replaceLibfaacCodec && params.contains(QStringLiteral("acodec=aac"))) {
                // replace libfaac with aac
                params = params.replace(QLatin1String("aac"), QLatin1String("libfaac"));
            }

            prof_extension = profileElement.attribute(QStringLiteral("extension"));
            if (!prof_extension.isEmpty()) extension = prof_extension;
            item = new QTreeWidgetItem(QStringList(profileName));
            item->setData(0, GroupRole, groupName);
            item->setData(0, ExtensionRole, extension);
            item->setData(0, RenderRole, renderer);
            item->setData(0, StandardRole, standard);
            item->setData(0, ParamsRole, params);
            if (params.contains("%quality"))
                item->setData(0, BitratesRole, profileElement.attribute(QStringLiteral("qualities")).split(',', QString::SkipEmptyParts));
            else if (params.contains("%bitrate"))
                item->setData(0, BitratesRole, profileElement.attribute(QStringLiteral("bitrates")).split(',', QString::SkipEmptyParts));
            if (params.contains("%audioquality"))
                item->setData(0, AudioBitratesRole, profileElement.attribute(QStringLiteral("audioqualities")).split(',', QString::SkipEmptyParts));
            else if (params.contains("%audiobitrate"))
                item->setData(0, AudioBitratesRole, profileElement.attribute(QStringLiteral("audiobitrates")).split(',', QString::SkipEmptyParts));
            if (profileElement.hasAttribute(QStringLiteral("speeds")))
                item->setData(0, SpeedsRole, profileElement.attribute(QStringLiteral("speeds")).split(';', QString::SkipEmptyParts));
            if (profileElement.hasAttribute(QStringLiteral("url")))
                item->setData(0, ExtraRole, profileElement.attribute(QStringLiteral("url")));
            groupItem->addChild(item);
            n = n.nextSibling();
        }

        ++i;
    }
}



void RenderWidget::setRenderJob(const QString &dest, int progress)
{
    RenderJobItem *item;
    QList<QTreeWidgetItem *> existing = m_view.running_jobs->findItems(dest, Qt::MatchExactly, 1);
    if (!existing.isEmpty()) {
        item = static_cast<RenderJobItem*> (existing.at(0));
    } else {
        item = new RenderJobItem(m_view.running_jobs, QStringList() << QString() << dest);
        if (progress == 0) {
            item->setStatus(WAITINGJOB);
        }
    }
    item->setData(1, ProgressRole, progress);
    item->setStatus(RUNNINGJOB);
    if (progress == 0) {
        item->setIcon(0, KoIconUtils::themedIcon(QStringLiteral("media-record")));
        item->setData(1, TimeRole, QDateTime::currentDateTime());
        slotCheckJob();
    } else {
        QDateTime startTime = item->data(1, TimeRole).toDateTime();
        int days = startTime.daysTo (QDateTime::currentDateTime()) ;
        double elapsedTime = days * 86400 + startTime.addDays(days).secsTo( QDateTime::currentDateTime() );
        u_int32_t remaining = elapsedTime * (100.0 - progress) / progress;
        int remainingSecs = remaining % 86400;
        days = remaining / 86400;
        QTime when = QTime ( 0, 0, 0, 0 ) ;
        when = when.addSecs (remainingSecs) ;
        QString est = (days > 0) ? i18np("%1 day ", "%1 days ", days) : QString();
        est.append(when.toString(QStringLiteral("hh:mm:ss")));
        QString t = i18n("Remaining time %1", est);
        item->setData(1, Qt::UserRole, t);
    }
}

void RenderWidget::setRenderStatus(const QString &dest, int status, const QString &error)
{
    RenderJobItem *item;
    QList<QTreeWidgetItem *> existing = m_view.running_jobs->findItems(dest, Qt::MatchExactly, 1);
    if (!existing.isEmpty()) item = static_cast<RenderJobItem*> (existing.at(0));
    else {
        item = new RenderJobItem(m_view.running_jobs, QStringList() << QString() << dest);
    }
    if (status == -1) {
        // Job finished successfully
        item->setStatus(FINISHEDJOB);
        QDateTime startTime = item->data(1, TimeRole).toDateTime();
        int days = startTime.daysTo(QDateTime::currentDateTime()) ;
        int elapsedTime = startTime.addDays(days).secsTo( QDateTime::currentDateTime() );
        QTime when = QTime ( 0, 0, 0, 0 ) ;
        when = when.addSecs (elapsedTime) ;
        QString est = (days > 0) ? i18np("%1 day ", "%1 days ", days) : QString();
        est.append(when.toString(QStringLiteral("hh:mm:ss")));
        QString t = i18n("Rendering finished in %1", est);
        item->setData(1, Qt::UserRole, t);
        QString notif = i18n("Rendering of %1 finished in %2", item->text(1), est);
        //WARNING: notification below does not seem to work
        KNotification::event(QStringLiteral("RenderFinished"), notif, QPixmap(), this);
        QString itemGroup = item->data(0, Qt::UserRole).toString();
        if (itemGroup == QLatin1String("dvd")) {
            emit openDvdWizard(item->text(1));
        } else if (itemGroup == QLatin1String("websites")) {
            QString url = item->metadata();
            if (!url.isEmpty()) new KRun(QUrl::fromLocalFile(url), this);
        }
    } else if (status == -2) {
        // Rendering crashed
        item->setStatus(FAILEDJOB);
        m_view.error_log->append(i18n("<strong>Rendering of %1 crashed</strong><br />", dest));
        m_view.error_log->append(error);
        m_view.error_log->append(QStringLiteral("<hr />"));
        m_view.error_box->setVisible(true);
    } else if (status == -3) {
        // User aborted job
        item->setStatus(ABORTEDJOB);
    }
    slotCheckJob();
    checkRenderStatus();
}

void RenderWidget::slotAbortCurrentJob()
{
    RenderJobItem *current = static_cast<RenderJobItem*> (m_view.running_jobs->currentItem());
    if (current) {
        if (current->status() == RUNNINGJOB) {
            emit abortProcess(current->text(1));
        } else {
            delete current;
            slotCheckJob();
            checkRenderStatus();
        }
    }
}

void RenderWidget::slotStartCurrentJob()
{
    RenderJobItem *current = static_cast<RenderJobItem*> (m_view.running_jobs->currentItem());
    if (current && current->status() == WAITINGJOB)
        startRendering(current);
    m_view.start_job->setEnabled(false);
}

void RenderWidget::slotCheckJob()
{
    bool activate = false;
    RenderJobItem *current = static_cast<RenderJobItem*> (m_view.running_jobs->currentItem());
    if (current) {
        if (current->status() == RUNNINGJOB || current->status() == STARTINGJOB) {
            m_view.abort_job->setText(i18n("Abort Job"));
            m_view.start_job->setEnabled(false);
        } else {
            m_view.abort_job->setText(i18n("Remove Job"));
            m_view.start_job->setEnabled(current->status() == WAITINGJOB);
        }
        activate = true;
    }
    m_view.abort_job->setEnabled(activate);
    /*
    for (int i = 0; i < m_view.running_jobs->topLevelItemCount(); ++i) {
        current = static_cast<RenderJobItem*>(m_view.running_jobs->topLevelItem(i));
        if (current == static_cast<RenderJobItem*> (m_view.running_jobs->currentItem())) {
            current->setSizeHint(1, QSize(m_view.running_jobs->columnWidth(1), fontMetrics().height() * 3));
        } else current->setSizeHint(1, QSize(m_view.running_jobs->columnWidth(1), fontMetrics().height() * 2));
    }*/
}

void RenderWidget::slotCLeanUpJobs()
{
    int ix = 0;
    RenderJobItem *current = static_cast<RenderJobItem*> (m_view.running_jobs->topLevelItem(ix));
    while (current) {
        if (current->status() == FINISHEDJOB)
            delete current;
        else ix++;
        current = static_cast<RenderJobItem*>(m_view.running_jobs->topLevelItem(ix));
    }
    slotCheckJob();
}

void RenderWidget::parseScriptFiles()
{
    QStringList scriptsFilter;
    scriptsFilter << QStringLiteral("*.sh");
    m_view.scripts_list->clear();

    QTreeWidgetItem *item;
    // List the project scripts
    QDir directory(m_projectFolder);
    directory.cd(QStringLiteral("scripts"));
    QStringList scriptFiles = directory.entryList(scriptsFilter, QDir::Files);
    for (int i = 0; i < scriptFiles.size(); ++i) {
        QUrl scriptpath = QUrl::fromLocalFile(directory.absoluteFilePath(scriptFiles.at(i)));
        QString target;
        QString renderer;
        QString melt;
        QFile file(scriptpath.path());
        if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QTextStream stream(&file);
            while (!stream.atEnd()) {
                QString line = stream.readLine();
                if (line.startsWith(QLatin1String("TARGET_0="))) {
                    target = line.section(QStringLiteral("TARGET_0=\""), 1);
                    target = target.section('"', 0, 0);
                } else if (line.startsWith(QLatin1String("RENDERER="))) {
                    renderer = line.section(QStringLiteral("RENDERER=\""), 1);
                    renderer = renderer.section('"', 0, 0);
                } else if (line.startsWith(QLatin1String("MELT="))) {
                    melt = line.section(QStringLiteral("MELT=\""), 1);
                    melt = melt.section('"', 0, 0);
                }
            }
            file.close();
        }
        if (target.isEmpty()) continue;
        item = new QTreeWidgetItem(m_view.scripts_list, QStringList() << QString() << scriptpath.fileName());
        if (!renderer.isEmpty() && renderer.contains('/') && !QFile::exists(renderer)) {
            item->setIcon(0, QIcon::fromTheme(QStringLiteral("dialog-cancel")));
            item->setToolTip(1, i18n("Script contains wrong command: %1", renderer));
            item->setData(0, Qt::UserRole, '1');
        } else if (!melt.isEmpty() && melt.contains('/') && !QFile::exists(melt)) {
            item->setIcon(0, QIcon::fromTheme(QStringLiteral("dialog-cancel")));
            item->setToolTip(1, i18n("Script contains wrong command: %1", melt));
            item->setData(0, Qt::UserRole, '1');
        } else item->setIcon(0, QIcon::fromTheme(QStringLiteral("application-x-executable-script")));
        item->setSizeHint(0, QSize(m_view.scripts_list->columnWidth(0), fontMetrics().height() * 2));
        item->setData(1, Qt::UserRole, QUrl(QUrl::fromEncoded(target.toUtf8())).url(QUrl::PreferLocalFile));
        item->setData(1, Qt::UserRole + 1, scriptpath.path());
    }
    QTreeWidgetItem *script = m_view.scripts_list->topLevelItem(0);
    if (script) {
        m_view.scripts_list->setCurrentItem(script);
        script->setSelected(true);
    }
}

void RenderWidget::slotCheckScript()
{
    QTreeWidgetItem *current = m_view.scripts_list->currentItem();
    if (current == NULL)
        return;
    m_view.start_script->setEnabled(current->data(0, Qt::UserRole).toString().isEmpty());
    m_view.delete_script->setEnabled(true);
    for (int i = 0; i < m_view.scripts_list->topLevelItemCount(); ++i) {
        current = m_view.scripts_list->topLevelItem(i);
        if (current == m_view.scripts_list->currentItem()) {
            current->setSizeHint(1, QSize(m_view.scripts_list->columnWidth(1), fontMetrics().height() * 3));
        } else current->setSizeHint(1, QSize(m_view.scripts_list->columnWidth(1), fontMetrics().height() * 2));
    }
}

void RenderWidget::slotStartScript()
{
    RenderJobItem* item = static_cast<RenderJobItem*> (m_view.scripts_list->currentItem());
    if (item) {
        QString destination = item->data(1, Qt::UserRole).toString();
        QString path = item->data(1, Qt::UserRole + 1).toString();
        // Insert new job in queue
        RenderJobItem *renderItem = NULL;
        QList<QTreeWidgetItem *> existing = m_view.running_jobs->findItems(destination, Qt::MatchExactly, 1);
        if (!existing.isEmpty()) {
            renderItem = static_cast<RenderJobItem*> (existing.at(0));
            if (renderItem->status() == RUNNINGJOB || renderItem->status() == WAITINGJOB || renderItem->status() == STARTINGJOB) {
                KMessageBox::information(this, i18n("There is already a job writing file:<br /><b>%1</b><br />Abort the job if you want to overwrite it...", destination), i18n("Already running"));
                return;
            }
            else if (renderItem->type() != ScriptRenderType) {
                delete renderItem;
                renderItem = NULL;
            }
        }
        if (!renderItem) renderItem = new RenderJobItem(m_view.running_jobs, QStringList() << QString() << destination, ScriptRenderType);
        renderItem->setData(1, ProgressRole, 0);
        renderItem->setStatus(WAITINGJOB);
        renderItem->setIcon(0, QIcon::fromTheme(QStringLiteral("media-playback-pause")));
        renderItem->setData(1, Qt::UserRole, i18n("Waiting..."));
        renderItem->setData(1, TimeRole, QDateTime::currentDateTime());
        renderItem->setData(1, ParametersRole, path);
        checkRenderStatus();
        m_view.tabWidget->setCurrentIndex(1);
    }
}

void RenderWidget::slotDeleteScript()
{
    QTreeWidgetItem *item = m_view.scripts_list->currentItem();
    if (item) {
        QString path = item->data(1, Qt::UserRole + 1).toString();
        bool success = true;
        success |= QFile::remove(path + ".mlt");
        success |= QFile::remove(path);
        if (!success) qWarning()<<"// Error removing script or playlist: "<<path<<", "<<path<<".mlt";
        parseScriptFiles();
    }
}

void RenderWidget::slotGenerateScript()
{
    slotPrepareExport(true);
}

void RenderWidget::slotHideLog()
{
    m_view.error_box->setVisible(false);
}

void RenderWidget::setRenderProfile(const QMap<QString, QString> &props)
{
    m_view.scanning_list->setCurrentIndex(props.value(QStringLiteral("renderscanning")).toInt());
    int exportAudio = props.value(QStringLiteral("renderexportaudio")).toInt();
    switch (exportAudio) {
    case 1:
        m_view.export_audio->setCheckState(Qt::Unchecked);
        break;
    case 2:
        m_view.export_audio->setCheckState(Qt::Checked);
        break;
    default:
        m_view.export_audio->setCheckState(Qt::PartiallyChecked);
    }
    if (props.contains(QStringLiteral("renderrescale"))) m_view.rescale->setChecked(props.value(QStringLiteral("renderrescale")).toInt());
    if (props.contains(QStringLiteral("renderrescalewidth"))) m_view.rescale_width->setValue(props.value(QStringLiteral("renderrescalewidth")).toInt());
    if (props.contains(QStringLiteral("renderrescaleheight"))) m_view.rescale_height->setValue(props.value(QStringLiteral("renderrescaleheight")).toInt());
    if (props.contains(QStringLiteral("rendertcoverlay"))) m_view.tc_overlay->setChecked(props.value(QStringLiteral("rendertcoverlay")).toInt());
    if (props.contains(QStringLiteral("rendertctype"))) m_view.tc_type->setCurrentIndex(props.value(QStringLiteral("rendertctype")).toInt());
    if (props.contains(QStringLiteral("renderratio"))) m_view.rescale_keep->setChecked(props.value(QStringLiteral("renderratio")).toInt());
    if (props.contains(QStringLiteral("renderplay"))) m_view.play_after->setChecked(props.value(QStringLiteral("renderplay")).toInt());
    if (props.contains(QStringLiteral("rendertwopass"))) m_view.checkTwoPass->setChecked(props.value(QStringLiteral("rendertwopass")).toInt());

    if (props.value(QStringLiteral("renderzone")) == QLatin1String("1")) m_view.render_zone->setChecked(true);
    else if (props.value(QStringLiteral("renderguide")) == QLatin1String("1")) {
        m_view.render_guide->setChecked(true);
        m_view.guide_start->setCurrentIndex(props.value(QStringLiteral("renderstartguide")).toInt());
        m_view.guide_end->setCurrentIndex(props.value(QStringLiteral("renderendguide")).toInt());
    } else m_view.render_full->setChecked(true);
    slotUpdateGuideBox();

    QString url = props.value(QStringLiteral("renderurl"));
    if (!url.isEmpty())
        m_view.out_file->setUrl(QUrl(url));

    if (props.contains(QStringLiteral("renderprofile")) || props.contains(QStringLiteral("rendercategory")))
        focusFirstVisibleItem(props.value(QStringLiteral("renderprofile")), props.value(QStringLiteral("rendercategory")));

    if (props.contains(QStringLiteral("renderquality")))
        m_view.video->setValue(props.value(QStringLiteral("renderquality")).toInt());
    else if (props.contains(QStringLiteral("renderbitrate")))
        m_view.video->setValue(props.value(QStringLiteral("renderbitrate")).toInt());
    else
        m_view.quality->setValue(m_view.quality->maximum() * 3 / 4);
    if (props.contains(QStringLiteral("renderaudioquality")))
        m_view.audio->setValue(props.value(QStringLiteral("renderaudioquality")).toInt());
    if (props.contains(QStringLiteral("renderaudiobitrate")))
        m_view.audio->setValue(props.value(QStringLiteral("renderaudiobitrate")).toInt());

    if (props.contains(QStringLiteral("renderspeed"))) m_view.speed->setValue(props.value(QStringLiteral("renderspeed")).toInt());
}

bool RenderWidget::startWaitingRenderJobs()
{
    m_blockProcessing = true;
    QString autoscriptFile = getFreeScriptName(QUrl(), QStringLiteral("auto"));
    QFile file(autoscriptFile);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        qWarning() << "//////  ERROR writing to file: " << autoscriptFile;
        KMessageBox::error(0, i18n("Cannot write to file %1", autoscriptFile));
        return false;
    }

    QTextStream outStream(&file);
    outStream << "#! /bin/sh" << '\n' << '\n';
    RenderJobItem *item = static_cast<RenderJobItem*> (m_view.running_jobs->topLevelItem(0));
    while (item) {
        if (item->status() == WAITINGJOB) {
            if (item->type() == DirectRenderType) {
                // Add render process for item
                const QString params = item->data(1, ParametersRole).toStringList().join(QStringLiteral(" "));
                outStream << m_renderer << ' ' << params << '\n';
            } else if (item->type() == ScriptRenderType){
                // Script item
                outStream << item->data(1, ParametersRole).toString() << '\n';
            }
        }
        item = static_cast<RenderJobItem*>(m_view.running_jobs->itemBelow(item));
    }
    // erase itself when rendering is finished
    outStream << "rm " << autoscriptFile << '\n' << '\n';
    if (file.error() != QFile::NoError) {
        KMessageBox::error(0, i18n("Cannot write to file %1", autoscriptFile));
        file.close();
        m_blockProcessing = false;
        return false;
    }
    file.close();
    QFile::setPermissions(autoscriptFile, file.permissions() | QFile::ExeUser);
    QProcess::startDetached(autoscriptFile, QStringList());
    return true;
}

QString RenderWidget::getFreeScriptName(const QUrl &projectName, const QString &prefix)
{
    int ix = 0;
    QString scriptsFolder = m_projectFolder + "scripts/";
    QDir dir(m_projectFolder);
    dir.mkdir(QStringLiteral("scripts"));
    QString path;
    QString fileName;
    if (projectName.isEmpty()) fileName = i18n("script");
    else fileName = projectName.fileName().section('.', 0, -2) + '_';
    while (path.isEmpty() || QFile::exists(path)) {
        ++ix;
        path = scriptsFolder + prefix + fileName + QString::number(ix).rightJustified(3, '0', false) + ".sh";
    }
    return path;
}

void RenderWidget::slotPlayRendering(QTreeWidgetItem *item, int)
{
    RenderJobItem *renderItem = static_cast<RenderJobItem*> (item);
    if (KdenliveSettings::defaultplayerapp().isEmpty() || renderItem->status() != FINISHEDJOB) return;
    QList<QUrl> urls;
    urls.append(QUrl::fromLocalFile(item->text(1)));
    KRun::run(KdenliveSettings::defaultplayerapp(), urls, this);
}

void RenderWidget::missingClips(bool hasMissing)
{
    if (hasMissing) {
        m_view.errorLabel->setText(i18n("Check missing clips"));
        m_view.errorBox->setHidden(false);
    } else m_view.errorBox->setHidden(true);
}

void RenderWidget::errorMessage(const QString &message)
{
    if (!message.isEmpty()) {
        m_infoMessage->setMessageType(KMessageWidget::Warning);
        m_infoMessage->setText(message);
        m_infoMessage->animatedShow();
    }
    else {
        if (m_view.tabWidget->currentIndex() == 0 && m_infoMessage->isVisible())  {
            m_infoMessage->animatedHide();
        } else {
            // Seems like animated hide does not work when page is not visible
            m_infoMessage->hide();
        }
    }
}


void RenderWidget::slotUpdateEncodeThreads(int val)
{
	KdenliveSettings::setEncodethreads(val);
}

void RenderWidget::slotUpdateRescaleWidth(int val)
{
    KdenliveSettings::setDefaultrescalewidth(val);
    if (!m_view.rescale_keep->isChecked()) return;
    m_view.rescale_height->blockSignals(true);
    m_view.rescale_height->setValue(val * m_profile.height / m_profile.width);
    KdenliveSettings::setDefaultrescaleheight(m_view.rescale_height->value());
    m_view.rescale_height->blockSignals(false);
}

void RenderWidget::slotUpdateRescaleHeight(int val)
{
    KdenliveSettings::setDefaultrescaleheight(val);
    if (!m_view.rescale_keep->isChecked()) return;
    m_view.rescale_width->blockSignals(true);
    m_view.rescale_width->setValue(val * m_profile.width / m_profile.height);
    KdenliveSettings::setDefaultrescaleheight(m_view.rescale_width->value());
    m_view.rescale_width->blockSignals(false);
}

void RenderWidget::slotSwitchAspectRatio()
{
    KdenliveSettings::setRescalekeepratio(m_view.rescale_keep->isChecked());
    if (m_view.rescale_keep->isChecked()) slotUpdateRescaleWidth(m_view.rescale_width->value());
}

void RenderWidget::slotUpdateAudioLabel(int ix)
{
    if (ix == Qt::PartiallyChecked)
        m_view.export_audio->setText(i18n("Export audio (automatic)"));
    else
        m_view.export_audio->setText(i18n("Export audio"));
}

bool RenderWidget::automaticAudioExport() const
{
    return (m_view.export_audio->checkState() == Qt::PartiallyChecked);
}

bool RenderWidget::selectedAudioExport() const
{
    return (m_view.export_audio->checkState() != Qt::Unchecked);
}

void RenderWidget::updateProxyConfig(bool enable)
{
    m_view.proxy_render->setHidden(!enable);
}

bool RenderWidget::proxyRendering()
{
    return m_view.proxy_render->isChecked();
}

bool RenderWidget::isStemAudioExportEnabled() const
{
    return (m_view.stemAudioExport->isChecked()
            && m_view.stemAudioExport->isVisible());
}

void RenderWidget::setRescaleEnabled(bool enable)
{
    for (int i = 0; i < m_view.rescale_box->layout()->count(); ++i) {
        if (m_view.rescale_box->itemAt(i)->widget())
            m_view.rescale_box->itemAt(i)->widget()->setEnabled(enable);
    }
}

void RenderWidget::keyPressEvent(QKeyEvent *e) {
    if(e->key()==Qt::Key_Return || e->key()==Qt::Key_Enter) {
	switch (m_view.tabWidget->currentIndex()) {
	  case 1:
	    if (m_view.start_job->isEnabled()) slotStartCurrentJob();
	    break;
	  case 2:
	    if (m_view.start_script->isEnabled()) slotStartScript();
	    break;
	  default:
	    if (m_view.buttonRender->isEnabled()) slotPrepareExport();
	    break;
	}
    }
    else QDialog::keyPressEvent(e);
}

void RenderWidget::adjustAVQualities(int quality)
{
    // calculate video/audio quality indexes from the general quality cursor
    // taking into account decreasing/increasing video/audio quality parameter
    double q = (double)quality / m_view.quality->maximum();

    int dq = q * (m_view.video->maximum() - m_view.video->minimum());
    // prevent video spinbox to update quality cursor (loop)
    m_view.video->blockSignals(true);
    m_view.video->setValue(m_view.video->property("decreasing").toBool()
        ? m_view.video->maximum() - dq
        : m_view.video->minimum() + dq);
    m_view.video->blockSignals(false);
    dq = q * (m_view.audio->maximum() - m_view.audio->minimum());
    dq -= dq % m_view.audio->singleStep(); // keep a 32 pitch  for bitrates
    m_view.audio->setValue(m_view.audio->property("decreasing").toBool()
        ? m_view.audio->maximum() - dq
        : m_view.audio->minimum() + dq);
}

void RenderWidget::adjustQuality(int videoQuality)
{
    int dq = videoQuality * m_view.quality->maximum() / (m_view.video->maximum() - m_view.video->minimum());
    m_view.quality->blockSignals(true);
    m_view.quality->setValue(
        m_view.video->property("decreasing").toBool()
        ? m_view.video->maximum() - dq
        : m_view.video->minimum() + dq);
    m_view.quality->blockSignals(false);
}

void RenderWidget::adjustSpeed(int speedIndex)
{
    if (m_view.formats->currentItem()) {
        QStringList speeds = m_view.formats->currentItem()->data(0, SpeedsRole).toStringList();
        if (speedIndex < speeds.count()) {
            m_view.speed->setToolTip(i18n("Codec speed parameters:\n") + speeds.at(speedIndex));
        }
    }
}
