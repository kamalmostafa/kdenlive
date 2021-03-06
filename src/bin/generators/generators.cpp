/***************************************************************************
 *   Copyright (C) 2016 by Jean-Baptiste Mardelle (jb@kdenlive.org)        *
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

#include "generators.h"
#include "doc/kthumb.h"
#include "monitor/monitor.h"
#include "effectstack/parametercontainer.h"
#include "kdenlivesettings.h"

#include <QStandardPaths>
#include <QDomDocument>
#include <QDir>
#include <QVBoxLayout>
#include <QLabel>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <KRecentDirs>
#include <KMessageBox>

Generators::Generators(Monitor *monitor, const QString &path, QWidget *parent) : QDialog(parent)
{
    QFile file(path);
    QDomDocument doc;
    doc.setContent(&file, false);
    file.close();
    QDomElement base = doc.documentElement();
    if (base.tagName() == QLatin1String("generator")) {
        QString generatorTag = base.attribute(QStringLiteral("tag"));
        setWindowTitle(base.firstChildElement(QStringLiteral("name")).text());
        QVBoxLayout *lay = new QVBoxLayout(this);
        m_preview = new QLabel;
        m_preview->setMinimumSize(1, 1);
        lay->addWidget(m_preview);
        m_producer = new Mlt::Producer(*monitor->profile(), generatorTag.toUtf8().constData());
        m_pixmap = QPixmap::fromImage(KThumb::getFrame(m_producer, 0, monitor->profile()->width(), monitor->profile()->height()));
        m_preview->setPixmap(m_pixmap.scaledToWidth(m_preview->width()));
        QHBoxLayout *hlay = new QHBoxLayout;
        hlay->addWidget(new QLabel(i18n("Duration")));
        m_timePos = new TimecodeDisplay(monitor->timecode(), this);
        if (base.hasAttribute(QStringLiteral("updateonduration"))) {
            connect(m_timePos, SIGNAL(timeCodeEditingFinished(int)), this, SLOT(updateDuration(int)));
        }
        hlay->addWidget(m_timePos);
        lay->addLayout(hlay);
        QWidget *frameWidget = new QWidget;
        lay->addWidget(frameWidget);
        ItemInfo info;
        EffectMetaInfo metaInfo;
        metaInfo.monitor = monitor;
        m_container = new ParameterContainer(base, info, &metaInfo, frameWidget);
        connect(m_container, &ParameterContainer::parameterChanged, this, &Generators::updateProducer);
        lay->addStretch(10);
        QDialogButtonBox *buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel);
        connect(buttonBox, SIGNAL(accepted()), this, SLOT(accept()));
        connect(buttonBox, SIGNAL(rejected()), this, SLOT(reject()));
        lay->addWidget(buttonBox);
        m_timePos->setValue(KdenliveSettings::color_duration());
    }
}

void Generators::updateProducer(QDomElement,QDomElement effect,int)
{
    QDomNodeList params = effect.elementsByTagName(QStringLiteral("parameter"));
    for (int i = 0; i < params.count() ; ++i) {
        QDomElement pa = params.item(i).toElement();
        QString paramName = pa.attribute(QStringLiteral("name"));
        QString paramValue = pa.attribute(QStringLiteral("value"));
        m_producer->set(paramName.toUtf8().constData(), paramValue.toUtf8().constData());
    }
    int w = m_pixmap.width();
    int h = m_pixmap.height();
    m_pixmap = QPixmap::fromImage(KThumb::getFrame(m_producer, 0, w, h));
    m_preview->setPixmap(m_pixmap.scaledToWidth(m_preview->width()));
}

void Generators::resizeEvent(QResizeEvent * event)
{
    QDialog::resizeEvent(event);
    m_preview->setPixmap(m_pixmap.scaledToWidth(m_preview->width()));
}

Generators::~Generators()
{
    delete m_producer;
    delete m_timePos;
}

//static
void Generators::getGenerators(QStringList producers, QMenu *menu)
{
    QStringList generatorFolders = QStandardPaths::locateAll(QStandardPaths::DataLocation, QStringLiteral("generators"), QStandardPaths::LocateDirectory);
    QStringList filters;
    filters << QStringLiteral("*.xml");
    foreach(const QString & folder, generatorFolders) {
        QDir directory(folder);
        QStringList filesnames = directory.entryList(filters, QDir::Files);
        foreach(const QString & fname, filesnames) {
            QPair <QString, QString> result = parseGenerator(directory.absoluteFilePath(fname), producers);
            if (!result.first.isEmpty()) {
                QAction *action = menu->addAction(result.first);
                action->setData(result.second);
            }
        }
    }
}

//static
QPair <QString, QString> Generators::parseGenerator(const QString &path, QStringList producers)
{
    QPair <QString, QString>  result;
    QDomDocument doc;
    QFile file(path);
    doc.setContent(&file, false);
    file.close();
    QDomElement base = doc.documentElement();
    if (base.tagName() == QLatin1String("generator")) {
        QString generatorTag = base.attribute(QStringLiteral("tag"));
        if (producers.contains(generatorTag)) {
            result.first = base.firstChildElement(QStringLiteral("name")).text();
            result.second = path;
        }
    }
    return result;
}

void Generators::updateDuration(int duration)
{
    m_producer->set("length", duration);
    m_producer->set_in_and_out(0, duration - 1);
    updateProducer();
}

QUrl Generators::getSavedClip(QString clipFolder)
{
    if (clipFolder.isEmpty()) {
        clipFolder = KRecentDirs::dir(QStringLiteral(":KdenliveClipFolder"));
    }
    if (clipFolder.isEmpty()) {
        clipFolder = QDir::homePath();
    }
    QUrl url = QFileDialog::getSaveFileUrl(this, i18n("Save clip"), QUrl::fromLocalFile(clipFolder), i18n("MLT playlist (*.mlt)"));
    if (url.isValid()) {
        if (QFile::exists(url.path())) {
            if (KMessageBox::warningYesNo(this, i18n("Output file already exists. Do you want to overwrite it?")) != KMessageBox::Yes) {
                return getSavedClip(url.path());
            }
        }
        m_producer->set("length", m_timePos->getValue());
        m_producer->set_in_and_out(0, m_timePos->getValue() - 1);
        Mlt::Consumer c(*m_producer->profile(), "xml", url.path().toUtf8().constData());
        c.connect(*m_producer);
        c.run();
        return url;
    }
    return QUrl();
}