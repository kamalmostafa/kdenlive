/***************************************************************************
 *   Copyright (C) 2015 by Jean-Baptiste Mardelle (jb@kdenlive.org)        *
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


#ifndef CLIPITEM_H
#define CLIPITEM_H

#include "abstractclipitem.h"
#include "gentime.h"
#include "effectslist/effectslist.h"
#include "mltcontroller/effectscontroller.h"

#include <QTimeLine>
#include <QGraphicsRectItem>
#include <QDomElement>
#include <QFutureSynchronizer>
#include <QGraphicsSceneMouseEvent>
#include <QTimer>

class Transition;
class ProjectClip;

namespace Mlt
{
    class Producer;
    class Profile;
}

class ClipItem : public AbstractClipItem
{
    Q_OBJECT

public:
    ClipItem(ProjectClip *clip, const ItemInfo &info, double fps, double speed, int strobe, int frame_width, bool generateThumbs = true);
    virtual ~ ClipItem();
    virtual void paint(QPainter *painter,
                       const QStyleOptionGraphicsItem *option,
                       QWidget *);
    virtual int type() const;
    void resizeStart(int posx, bool size = true, bool emitChange = true);
    void resizeEnd(int posx, bool emitChange = true);
    OperationType operationMode(const QPointF &pos);
    void updateKeyframes(QDomElement effect);
    static int itemHeight();
    int clipType() const;
    const QString &getBinId() const;
    const QString getBinHash() const;
    ProjectClip *binClip() const;
    QString clipName() const;
    QDomElement xml() const;
    QDomElement itemXml() const;
    ClipItem *clone(const ItemInfo &info) const;
    const EffectsList effectList() const;
    void setFadeOut(int pos);
    void setFadeIn(int pos);
    void setFades(int in, int out);

    /** @brief Gets the clip's effect names.
    * @return The names of the effects in a string list */
    QStringList effectNames();

    /** @brief Adds an effect to the clip.
    * @return The parameters that will be passed to Mlt */
    EffectsParameterList addEffect(ProfileInfo info, QDomElement effect, bool animate = true);

    /** @brief Deletes the effect with id @param ix. */
    void deleteEffect(int ix);

    /** @brief Gets the number of effects in this clip. */
    int effectsCount();

    /** @brief Gets a unique (?) effect id. */
    int effectsCounter();
    
        /** @brief Gets a copy of the xml of an effect.
    * @param ix The effect's list index (starting from 0)
    * @return A copy of the effect's xml */
    QDomElement effect(int ix) const;

    /** @brief Gets a copy of the xml of an effect.
    * @param ix The effect's index in effectlist (starting from 1)
    * @return A copy of the effect's xml */
    QDomElement effectAtIndex(int ix) const;

    /** @brief Gets the xml of an effect.
    * @param ix The effect's index in effectlist (starting from 1)
    * @return The effect's xml */
    QDomElement getEffectAtIndex(int ix) const;

    /** @brief Replaces an effect.
    * @param ix The effect's index in effectlist
    * @param effect The new effect */
    void updateEffect(QDomElement effect);
    /** @brief Enable / disable a list of effect from their indexes. */
    void enableEffects(QList <int> indexes, bool disable);
    bool moveEffect(QDomElement effect, int ix);
    void flashClip();
    void addTransition(Transition*);

    /** @brief Regenerates audio and video thumbnails.
    * @param clearExistingThumbs true = regenerate all thumbs, false = only create missing thumbs. */
    void resetThumbs(bool clearExistingThumbs);

    /** @brief Updates clip properties from base clip.
    * @param checkDuration whether or not to check for a valid duration. 
    * @param resetThumbs whether or not to recreate the image thumbnails. */
    void refreshClip(bool checkDuration, bool resetThumbs);

    /** @brief Gets clip's marker times.
    * @return A list of the times. */
    QList <GenTime> snapMarkers(const QList < GenTime > markers ) const;
    QList <CommentedTime> commentedSnapMarkers() const;

    /** @brief Gets the position of the fade in effect. */
    int fadeIn() const;

    /** @brief Gets the position of the fade out effect. */
    int fadeOut() const;
    void setSelectedEffect(const int ix);
    QDomElement selectedEffect();
    int selectedEffectIndex() const;
    void initEffect(ProfileInfo info, QDomElement effect, int diff = 0, int offset = 0);

    /** @brief Gets all keyframes.
    * @param index Index of the effect
    * @return a list of strings of keyframes (one string per param) */
    QStringList keyframes(const int index);

    /** @brief Adjust all geometry keyframes.
    * @param index Index of the effect */
    bool resizeGeometries(const int index, int width, int height, int previousDuration, int start, int duration, int cropStart);
    QString resizeAnimations(const int index, int width, int height, int previousDuration, int start, int duration, int cropStart);

    /** @brief Sets params with keyframes and updates the visible keyframes.
    * @param ix Number of the effect
    * @param keyframes a list of strings of keyframes (one string per param), which should be used */
    void setKeyframes(const int ix, const QStringList &keyframes = QStringList());
    void setEffectList(const EffectsList &effectList);
    void setSpeed(const double speed, int strobe);
    double speed() const;
    int strobe() const;
    GenTime maxDuration() const;
    GenTime speedIndependantCropStart() const;
    GenTime speedIndependantCropDuration() const;
    const ItemInfo speedIndependantInfo() const;
    int hasEffect(const QString &tag, const QString &id) const;

    /** @brief Makes sure all keyframes are in the clip's cropped duration.
     * @param cutPos the frame number where the new clip starts
    * @return Whether or not changes were made */
    bool checkKeyFrames(int width, int height, int previousDuration, int cutPos = -1);
    QPixmap startThumb() const;
    QPixmap endThumb() const;
    void setState(PlaylistState::ClipState state);
    void updateState(const QString &id);

    void updateGeometryKeyframes(QDomElement effect, int paramIndex, int width, int height, ItemInfo oldInfo);
    bool updateNormalKeyframes(QDomElement parameter, ItemInfo oldInfo);

    /** @brief Adjusts effects after a clip duration change. */
    QMap<int, QDomElement> adjustEffectsToDuration(int width, int height, const ItemInfo &oldInfo);

    /** Returns the necessary (audio, video, general) producer.
     * @param track Track of the requested producer
     * @param trackSpecific (default = true) Whether to return general producer for a specific track.
     * @return Fitting producer
     * Which producer is returned depends on the type of this clip (audioonly, videoonly, normal) */
    //Mlt::Producer *getProducer(int track, bool trackSpecific = true);
    void resetFrameWidth(int width);
    /** @brief Clip is about to be deleted, block thumbs. */
    void stopThumbs();
    
    /** @brief Get a free index value for effect group. */
    int nextFreeEffectGroupIndex() const;
    
    /** @brief Returns true of this clip needs a duplicate (MLT requires duplicate for clips with audio or we get clicks. */
    bool needsDuplicate() const;

    /** @brief Returns some info useful for recreating this clip. */
    PlaylistState::ClipState clipState() const;

protected:
    //virtual void mouseMoveEvent(QGraphicsSceneMouseEvent * event);
    void dragEnterEvent(QGraphicsSceneDragDropEvent *event);
    void dragLeaveEvent(QGraphicsSceneDragDropEvent *event);
    void dropEvent(QGraphicsSceneDragDropEvent *event);
    //virtual void hoverEnterEvent(QGraphicsSceneHoverEvent *);
    //virtual void hoverLeaveEvent(QGraphicsSceneHoverEvent *);
    QVariant itemChange(GraphicsItemChange change, const QVariant &value);

private:
    ProjectClip *m_binClip;
    ItemInfo m_speedIndependantInfo;
    ClipType m_clipType;
    QString m_effectNames;
    int m_startFade;
    int m_endFade;
    PlaylistState::ClipState m_clipState;
    QColor m_baseColor;
    QColor m_paintColor;

    QPixmap m_startPix;
    QPixmap m_endPix;

    bool m_hasThumbs;
    QTimer m_startThumbTimer;
    QTimer m_endThumbTimer;

    QTimeLine *m_timeLine;
    bool m_startThumbRequested;
    bool m_endThumbRequested;
    //bool m_hover;
    double m_speed;
    int m_strobe;

    EffectsList m_effectList;
    QList <Transition*> m_transitionsList;
    QMap<int, QPixmap> m_audioThumbCachePic;
    bool m_audioThumbReady;
    double m_framePixelWidth;
    QPixmap m_videoPix;
    QPixmap m_audioPix;

private slots:
    void slotGetStartThumb();
    void slotGetEndThumb();
    void slotGotAudioData();
    void animate(qreal value);
    void slotSetStartThumb(const QImage &img);
    void slotSetEndThumb(const QImage &img);
    void slotThumbReady(int frame, const QImage &img);
    /** @brief For fixed thumbnail clip (image / titles), update thumb to reflect bin thumbnail. */
    void slotUpdateThumb(QImage);
    /** @brief Something changed a detail in clip (thumbs, markers,...), repaint. */
    void slotRefreshClip();

public slots:
    void slotFetchThumbs();
    void slotSetStartThumb(const QPixmap &pix);
    void slotSetEndThumb(const QPixmap &pix);
    void slotUpdateRange();

signals:
    void updateRange();
};

#endif
