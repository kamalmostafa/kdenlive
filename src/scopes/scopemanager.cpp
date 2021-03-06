/***************************************************************************
 *   Copyright (C) 2011 by Simon Andreas Eugster (simon.eu@gmail.com)      *
 *   This file is part of kdenlive. See www.kdenlive.org.                  *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 ***************************************************************************/

#include "scopemanager.h"
#include "definitions.h"
#include "kdenlivesettings.h"
#include "core.h"
#include "mainwindow.h"
#include "monitor/monitormanager.h"
#include "colorscopes/vectorscope.h"
#include "colorscopes/waveform.h"
#include "colorscopes/rgbparade.h"
#include "colorscopes/histogram.h"
#include "audioscopes/audiosignal.h"
#include "audioscopes/audiospectrum.h"
#include "audioscopes/spectrogram.h"


#include <QDockWidget>
#include "klocalizedstring.h"

//#define DEBUG_SM
#ifdef DEBUG_SM
#include <QtCore/QDebug>
#endif

ScopeManager::ScopeManager(QObject *parent) :
    QObject(parent),
    m_lastConnectedRenderer(NULL)
{
    m_signalMapper = new QSignalMapper(this);

    connect(pCore->monitorManager(), SIGNAL(checkColorScopes()), SLOT(slotUpdateActiveRenderer()));
    connect(pCore->monitorManager(), SIGNAL(clearScopes()), SLOT(slotClearColorScopes()));
    connect(pCore->monitorManager(), SIGNAL(checkScopes()), SLOT(slotCheckActiveScopes()));
    connect(m_signalMapper, SIGNAL(mapped(QString)), SLOT(slotRequestFrame(QString)));

    slotUpdateActiveRenderer();

    createScopes();
}

bool ScopeManager::addScope(AbstractAudioScopeWidget *audioScope, QDockWidget *audioScopeWidget)
{
    bool added = false;
    int exists = false;
    // Only add the scope if it does not exist yet in the list
    for (int i = 0; i < m_audioScopes.size(); ++i) {
        if (m_audioScopes[i].scope == audioScope) {
            exists = true;
            break;
        }
    }
    if (!exists) {
        // Add scope to the list, set up signal/slot connections
#ifdef DEBUG_SM
        qDebug() << "Adding scope to scope manager: " << audioScope->widgetName();
#endif

        m_signalMapper->setMapping(audioScopeWidget, QString(audioScope->widgetName()));

        AudioScopeData asd;
        asd.scope = audioScope;
        m_audioScopes.append(asd);

        connect(audioScope, SIGNAL(requestAutoRefresh(bool)), this, SLOT(slotCheckActiveScopes()));
        if (audioScopeWidget != NULL) {
            connect(audioScopeWidget, SIGNAL(visibilityChanged(bool)), this, SLOT(slotCheckActiveScopes()));
            connect(audioScopeWidget, SIGNAL(visibilityChanged(bool)), m_signalMapper, SLOT(map()));
        }

        added = true;
    }
    return added;
}
bool ScopeManager::addScope(AbstractGfxScopeWidget *colorScope, QDockWidget *colorScopeWidget)
{
    bool added = false;
    int exists = false;
    for (int i = 0; i < m_colorScopes.size(); ++i) {
        if (m_colorScopes[i].scope == colorScope) {
            exists = true;
            break;
        }
    }
    if (!exists) {
#ifdef DEBUG_SM
        qDebug() << "Adding scope to scope manager: " << colorScope->widgetName();
#endif

        m_signalMapper->setMapping(colorScopeWidget, QString(colorScope->widgetName()));

        GfxScopeData gsd;
        gsd.scope = colorScope;
        m_colorScopes.append(gsd);

        connect(colorScope, SIGNAL(requestAutoRefresh(bool)), this, SLOT(slotCheckActiveScopes()));
        connect(colorScope, SIGNAL(signalFrameRequest(QString)), this, SLOT(slotRequestFrame(QString)));
        if (colorScopeWidget != NULL) {
            connect(colorScopeWidget, SIGNAL(visibilityChanged(bool)), this, SLOT(slotCheckActiveScopes()));
            connect(colorScopeWidget, SIGNAL(visibilityChanged(bool)), m_signalMapper, SLOT(map()));
        }

        added = true;
    }
    return added;
}


void ScopeManager::slotDistributeAudio(const audioShortVector &sampleData, int freq, int num_channels, int num_samples)
{
#ifdef DEBUG_SM
    qDebug() << "ScopeManager: Starting to distribute audio.";
#endif
    for (int i = 0; i < m_audioScopes.size(); ++i) {
        // Distribute audio to all scopes that are visible and want to be refreshed
        if (!m_audioScopes[i].scope->visibleRegion().isEmpty()) {
            if (m_audioScopes[i].scope->autoRefreshEnabled()) {
                m_audioScopes[i].scope->slotReceiveAudio(sampleData, freq, num_channels, num_samples);
#ifdef DEBUG_SM
                qDebug() << "ScopeManager: Distributed audio to " << m_audioScopes[i].scope->widgetName();
#endif
            }
        }
    }
}
void ScopeManager::slotDistributeFrame(const QImage &image)
{
#ifdef DEBUG_SM
    qDebug() << "ScopeManager: Starting to distribute frame.";
#endif
    for (int i = 0; i < m_colorScopes.size(); ++i) {
        if (!m_colorScopes[i].scope->visibleRegion().isEmpty()) {
            if (m_colorScopes[i].scope->autoRefreshEnabled()) {
                m_colorScopes[i].scope->slotRenderZoneUpdated(image);
#ifdef DEBUG_SM
                qDebug() << "ScopeManager: Distributed frame to " << m_colorScopes[i].scope->widgetName();
#endif
            } else if (m_colorScopes[i].singleFrameRequested) {
                // Special case: Auto refresh is disabled, but user requested an update (e.g. by clicking).
                // Force the scope to update.
                m_colorScopes[i].singleFrameRequested = false;
                m_colorScopes[i].scope->slotRenderZoneUpdated(image);
                m_colorScopes[i].scope->forceUpdateScope();
#ifdef DEBUG_SM
                qDebug() << "ScopeManager: Distributed forced frame to " << m_colorScopes[i].scope->widgetName();
#endif
            }
        }
    }

    checkActiveColourScopes();
}


void ScopeManager::slotRequestFrame(const QString &widgetName)
{
#ifdef DEBUG_SM
    qDebug() << "ScopeManager: New frame was requested by " << widgetName;
#endif

    // Search for the scope in the lists and tag it to trigger a forced update
    // in the distribution slots
    for (int i = 0; i < m_colorScopes.size(); ++i) {
        if (m_colorScopes[i].scope->widgetName() == widgetName) {
            m_colorScopes[i].singleFrameRequested = true;
            break;
        }
    }
    for (int i = 0; i < m_audioScopes.size(); ++i) {
        if (m_audioScopes[i].scope->widgetName() == widgetName) {
            m_audioScopes[i].singleFrameRequested = true;
            break;
        }
    }
    if (m_lastConnectedRenderer) m_lastConnectedRenderer->sendFrameUpdate();
}


void ScopeManager::slotClearColorScopes()
{
    m_lastConnectedRenderer = NULL;
}


void ScopeManager::slotUpdateActiveRenderer()
{
    // Disconnect old connections
    if (m_lastConnectedRenderer != NULL) {
#ifdef DEBUG_SM
        qDebug() << "Disconnected previous renderer: " << m_lastConnectedRenderer->id();
#endif
        m_lastConnectedRenderer->disconnect(this);
    }

    m_lastConnectedRenderer = pCore->monitorManager()->activeRenderer();
    // DVD monitor shouldn't be monitored or will cause crash on deletion
    if (pCore->monitorManager()->isActive(Kdenlive::DvdMonitor)) m_lastConnectedRenderer = NULL;

    // Connect new renderer
    if (m_lastConnectedRenderer != NULL) {
        connect(m_lastConnectedRenderer, SIGNAL(frameUpdated(QImage)),
                this, SLOT(slotDistributeFrame(QImage)), Qt::UniqueConnection);
        connect(m_lastConnectedRenderer, &AbstractRender::audioSamplesSignal,
                this, &ScopeManager::slotDistributeAudio, Qt::UniqueConnection);

#ifdef DEBUG_SM
        qDebug() << "Renderer connected to ScopeManager: " << m_lastConnectedRenderer->id();
#endif

        if (imagesAcceptedByScopes()) {
#ifdef DEBUG_SM
            qDebug() << "Some scopes accept images, triggering frame update.";
#endif
            m_lastConnectedRenderer->sendFrameUpdate();
        }
    }
}


void ScopeManager::slotCheckActiveScopes()
{
#ifdef DEBUG_SM
    qDebug() << "Checking active scopes ...";
#endif
    // Leave a small delay to make sure that scope widget has been shown or hidden
    QTimer::singleShot(500, this, SLOT(checkActiveAudioScopes()));
    QTimer::singleShot(500, this, SLOT(checkActiveColourScopes()));
}


bool ScopeManager::audioAcceptedByScopes() const
{
    bool accepted = false;
    for (int i = 0; i < m_audioScopes.size(); ++i) {
        if (m_audioScopes.at(i).scope->isVisible() && m_audioScopes.at(i).scope->autoRefreshEnabled()) {
            accepted = true;
            break;
        }
    }
#ifdef DEBUG_SM
    qDebug() << "Any scope accepting audio? " << accepted;
#endif
    return accepted;
}
bool ScopeManager::imagesAcceptedByScopes() const
{
    bool accepted = false;
    for (int i = 0; i < m_colorScopes.size(); ++i) {
        if (!m_colorScopes[i].scope->visibleRegion().isEmpty() && m_colorScopes[i].scope->autoRefreshEnabled()) {
            accepted = true;
            break;
        }
    }
#ifdef DEBUG_SM
    qDebug() << "Any scope accepting images? " << accepted;
#endif
    return accepted;
}



void ScopeManager::checkActiveAudioScopes()
{
    bool audioStillRequested = audioAcceptedByScopes();

#ifdef DEBUG_SM
    qDebug() << "ScopeManager: New audio data still requested? " << audioStillRequested;
#endif

    KdenliveSettings::setMonitor_audio(audioStillRequested);
    pCore->monitorManager()->slotUpdateAudioMonitoring();
}

void ScopeManager::checkActiveColourScopes()
{
    bool imageStillRequested = imagesAcceptedByScopes();

#ifdef DEBUG_SM
    qDebug() << "ScopeManager: New frames still requested? " << imageStillRequested;
#endif

    // Notify monitors whether frames are still required
    Monitor *monitor;
    monitor = static_cast<Monitor*>( pCore->monitorManager()->monitor(Kdenlive::ProjectMonitor) );
    if (monitor != NULL) {
	monitor->sendFrameForAnalysis(imageStillRequested);
    }

    monitor = static_cast<Monitor*>( pCore->monitorManager()->monitor(Kdenlive::ClipMonitor) );
    if (monitor != NULL) { monitor->sendFrameForAnalysis(imageStillRequested); }

    RecMonitor *recMonitor = static_cast<RecMonitor*>( pCore->monitorManager()->monitor(Kdenlive::RecordMonitor) );
    if (recMonitor != NULL) { recMonitor->analyseFrames(imageStillRequested); }
}

void ScopeManager::createScopes()
{
    createScopeDock(new Vectorscope(pCore->window()),   i18n("Vectorscope"));
    createScopeDock(new Waveform(pCore->window()),      i18n("Waveform"));
    createScopeDock(new RGBParade(pCore->window()),     i18n("RGB Parade"));
    createScopeDock(new Histogram(pCore->window()),     i18n("Histogram"));

    createScopeDock(new AudioSignal(pCore->window()),   i18n("Audio Signal"));
    createScopeDock(new AudioSpectrum(pCore->window()), i18n("AudioSpectrum"));
    createScopeDock(new Spectrogram(pCore->window()),   i18n("Spectrogram"));
}

template <class T> void ScopeManager::createScopeDock(T* scopeWidget, const QString& title)
{
    QDockWidget *dock = pCore->window()->addDock(title, scopeWidget->widgetName(), scopeWidget);
    addScope(scopeWidget, dock);

    // close for initial layout
    // actual state will be restored by session management
    dock->close();
}


