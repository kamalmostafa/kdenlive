/***************************************************************************
 *   Copyright (C) 2009 by Jean-Baptiste Mardelle (jb@kdenlive.org)        *
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


#ifndef DVDWIZARDMENU_H
#define DVDWIZARDMENU_H

#include "dvdwizardvob.h"
#include "ui_dvdwizardmenu_ui.h"

#include <QWizardPage>
#include <QGraphicsScene>
#include <QGraphicsTextItem>
#include <QGraphicsPixmapItem>
#include <QGraphicsRectItem>
#include <QDomElement>

#include <QDebug>
#include <KMessageWidget>

class DvdScene : public QGraphicsScene
{
    Q_OBJECT
public:
    explicit DvdScene(QObject * parent = 0): QGraphicsScene(parent) {
        m_width = 0; m_height = 0; m_gridSize = 1;
    }
    void setProfile(int width, int height) {
        m_width = width;
        m_height = height;
        setSceneRect(0, 0, m_width, m_height);
    }
    int sceneWidth() const {
        return m_width;
    }
    int sceneHeight() const {
        return m_height;
    }
    int gridSize() const {
       return m_gridSize;
    }
    void setGridSize(int gridSize) {
       m_gridSize = gridSize;
    }
private:
    int m_width;
    int m_height;
    int m_gridSize;
    
protected:
    void mouseReleaseEvent( QGraphicsSceneMouseEvent * mouseEvent ) {
        QGraphicsScene::mouseReleaseEvent(mouseEvent);
        emit sceneChanged();
    }
    void drawForeground(QPainter *painter, const QRectF &rect) {
       // draw the grid if needed
       if (gridSize() <= 1)
          return;

       QPen pen;
       painter->setPen(pen);

       qreal left = int(rect.left()) - (int(rect.left()) % m_gridSize);
       qreal top = int(rect.top()) - (int(rect.top()) % m_gridSize);
       QVector<QPointF> points;
       for (qreal x = left; x < rect.right(); x += m_gridSize){
          for (qreal y = top; y < rect.bottom(); y += m_gridSize){
             points.append(QPointF(x,y));
          }
       }
       painter->drawPoints(points.data(), points.size());
    }
signals:
    void sceneChanged();
};

class DvdButtonUnderline : public QGraphicsRectItem
{

public:
    explicit DvdButtonUnderline( const QRectF & rect, QGraphicsItem * parent = 0 ) : QGraphicsRectItem(rect, parent) {}

    int type() const {
        // Enable the use of qgraphicsitem_cast with this item.
        return UserType + 2;
    }
};

class DvdButton : public QGraphicsTextItem
{

public:
    explicit DvdButton(const QString & text): QGraphicsTextItem(text), m_target(0), m_command(QStringLiteral("jump title 1")), m_backToMenu(false) {
        setFlags(QGraphicsItem::ItemIsMovable | QGraphicsItem::ItemIsSelectable);
#if QT_VERSION >= 0x040600
        setFlag(QGraphicsItem::ItemSendsGeometryChanges, true);
#endif
    }
    void setTarget(int t, const QString &c) {
        m_target = t;
        m_command = c;
    }
    int target() const {
        return m_target;
    }
    QString command() const {
        return m_command;
    }
    bool backMenu() const {
        return m_backToMenu;
    }
    int type() const {
        // Enable the use of qgraphicsitem_cast with this item.
        return UserType + 1;
    }
    void setBackMenu(bool back) {
        m_backToMenu = back;
    }

private:
    int m_target;
    QString m_command;
    bool m_backToMenu;

protected:

    virtual QVariant itemChange(GraphicsItemChange change, const QVariant &value) {
        if (change == ItemPositionChange && scene()) {
            QPoint newPos = value.toPoint();

            if(QApplication::mouseButtons() == Qt::LeftButton && qobject_cast<DvdScene*> (scene())){
               DvdScene* customScene = qobject_cast<DvdScene*> (scene());
               int gridSize = customScene->gridSize();
               qreal xV = round(newPos.x()/gridSize)*gridSize;
               qreal yV = round(newPos.y()/gridSize)*gridSize;
               newPos = QPoint(xV, yV);
            }

            QRectF sceneShape = sceneBoundingRect();
            DvdScene *sc = static_cast < DvdScene * >(scene());
            newPos.setX(qMax(newPos.x(), 0));
            newPos.setY(qMax(newPos.y(), 0));
            if (newPos.x() + sceneShape.width() > sc->width())
                newPos.setX(sc->width() - sceneShape.width());
            if (newPos.y() + sceneShape.height() > sc->height())
                newPos.setY(sc->height() - sceneShape.height());

            sceneShape.translate(newPos - pos());
            QList<QGraphicsItem*> list = scene()->items(sceneShape, Qt::IntersectsItemShape);
            list.removeAll(this);
            if (!list.isEmpty()) {
                for (int i = 0; i < list.count(); ++i) {
                    if (list.at(i)->type() == Type)
                        return pos();
                }
            }
            return newPos;
        }
        return QGraphicsItem::itemChange(change, value);
    }

};


class DvdWizardMenu : public QWizardPage
{
    Q_OBJECT

public:
    explicit DvdWizardMenu(DVDFORMAT format, QWidget * parent = 0);
    virtual ~DvdWizardMenu();
    bool createMenu() const;
    void createBackgroundImage(const QString &img1, bool letterbox);
    void createButtonImages(const QString &selected_image, const QString &highlighted_image, bool letterbox);
    void setTargets(const QStringList &list, const QStringList &targetlist);
    QMap <QString, QRect> buttonsInfo(bool letterbox = false);
    bool loopMovie() const;
    bool menuMovie() const;
    QString menuMoviePath() const;
    int menuMovieLength() const;
    void changeProfile(DVDFORMAT format);
    QDomElement toXml() const;
    void loadXml(DVDFORMAT format, const QDomElement &xml);
    void prepareUnderLines();
    void resetUnderLines();

private:
    Ui::DvdWizardMenu_UI m_view;
    DVDFORMAT m_format;
    DvdScene *m_scene;
    QGraphicsPixmapItem *m_background;
    QGraphicsRectItem *m_color;
    QGraphicsRectItem *m_safeRect;
    int m_width;
    int m_height;
    QSize m_finalSize;
    int m_movieLength;
    KMessageWidget *m_menuMessage;

private slots:
    void buildButton();
    void buildColor();
    void buildImage();
    void checkBackgroundType(int ix);
    void updatePreview();
    void buttonChanged();
    void addButton();
    void setButtonTarget(int ix);
    void deleteButton();
    void updateColor();
    void updateColor(const QColor &c);
    void updateUnderlineColor(QColor c);
    void setBackToMenu(bool backToMenu);
    void slotZoom();
    void slotUnZoom();
    void slotEnableShadows(int enable);
    void slotUseGrid(bool useGrid);
};

#endif

