/*
    This code is released under the terms of the MIT license.
    See COPYING.txt for details.
*/

#include <QPixmap>
#include <QPainter>
#include <algorithm>
#include <stdexcept>
#include <cstdlib>
#include <cmath>
#include "kirby.h"
#include "level.h"
#include "mainwindow.h"
#include "mapscene.h"
#include "mapchange.h"
#include "tileeditwindow.h"
#include "graphics.h"

#define MAP_TEXT_PAD_H 2
#define MAP_TEXT_PAD_V 1

const QFont MapScene::infoFont("Consolas", 8);
const QFontMetrics MapScene::infoFontMetrics(MapScene::infoFont);

const QColor MapScene::infoColor(255, 192, 192, 192);
const QColor MapScene::infoBackColor(255, 192, 192, 64);

const QColor MapScene::selectionColor(255, 192, 192, 128);
const QColor MapScene::selectionBorder(255, 192, 192, 255);

const QColor MapScene::layerColor(0, 192, 224, 192);

/*
  Overridden constructor which inits some scene info
 */
MapScene::MapScene(QWidget *parent, leveldata_t *currentLevel)
    : QWidget(parent),

      tileX(-1), tileY(-1),
      selLength(0), selWidth(0), selecting(false),
      copyWidth(0), copyLength(0),
      stack(this),
      level(currentLevel)
{
    bounce.load  (":images/bounce.png");
    bumpers.load (":images/bumpers.png");
    conveyor.load(":images/conveyor.png");
    dedede.load  (":images/dedede.png");
    enemies.load (":images/enemies.png");
    gordo.load   (":images/gordo.png");
    kirby.load   (":images/kirby.png");
    movers.load  (":images/movers.png");
    rotate.load  (":images/rotate.png");
    tiles.load   (":images/terrain.png");
    traps.load   (":images/traps.png");
    warps.load   (":images/warps.png");
    water.load   (":images/water.png");
    switches.load(":images/switches.png");
    unknown.load (":images/unknown.png");

    this->setMouseTracking(true);

    QObject::connect(this, SIGNAL(edited()),
                     this, SLOT(refresh()));
}

/*
  Edit the currently selected tiles (if any)
*/
void MapScene::editTiles() {
    if (selWidth == 0 || selLength == 0)
        return;

    MapChange *edit = new MapChange(level, selX, selY, selWidth, selLength);

    // send the level and selection info to a new tile edit window instance
    TileEditWindow win;
    if (win.startEdit(level, QRect(selX, selY, selWidth, selLength)))
        stack.push(edit);
    else delete edit;

    // redraw the map scene with the new properties
    emit edited();
}

/*
  Redraw the scene
*/
void MapScene::refresh() {
    tileX = -1;
    tileY = -1;
    setMinimumSize(level->header.width * TILE_SIZE + 1, level->header.length * TILE_SIZE + 1);
    setMaximumSize(this->minimumSize());
    updateGeometry();
    update();
}

/*
  Handle when the mouse is pressed on the scene
*/
void MapScene::mousePressEvent(QMouseEvent *event) {
    if (level->header.width == 0 || level->header.length == 0)
        return;

    // left button: start or continue selection
    // right button: cancel selection
    if (event->buttons() & Qt::LeftButton) {
        beginSelection(event);

    } else if (event->buttons() & Qt::RightButton) {
        cancelSelection();
    }
    update();
}

/*
  Handle when a double-click occurs (used to start the tile edit window)
*/
void MapScene::mouseDoubleClickEvent(QMouseEvent *event) {
    editTiles();
    emit doubleClicked();

    event->accept();
    update();
}

/*
  Handle when the left mouse button is released
*/
void MapScene::mouseReleaseEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton) {
        selecting = false;

        // normalize selection dimensions (i.e. handle negative height/width)
        // so that selections made down and/or to the left are handled appropriately
        if (selWidth < 0) {
            selX += selWidth + 1;
            selWidth *= -1;
        }
        if (selLength < 0) {
            selY += selLength + 1;
            selLength *= -1;
        }

        event->accept();
        update();
    }
}

/*
  Handle when the mouse is moved over the scene
 */
void MapScene::mouseMoveEvent(QMouseEvent *event) {
    if (selecting && event->buttons() & Qt::LeftButton) {
        // left button down: generate/show selection
        updateSelection(event);
    } else {
        showTileInfo(event);
    }

    event->accept();
}

/*
 *Undo/redo functions
 */
bool MapScene::canUndo() const {
    return stack.canUndo();
}

bool MapScene::canRedo() const {
    return stack.canRedo();
}

bool MapScene::isClean() const {
    return stack.isClean();
}

void MapScene::undo() {
    if (stack.canUndo()) {
        emit statusMessage(QString("Undoing ").append(stack.undoText()));
        stack.undo();
        emit edited();

        level->modified = true;
        level->modifiedRecently = !isClean();
    }
}

void MapScene::redo() {
    if (stack.canRedo()) {
        emit statusMessage(QString("Redoing ").append(stack.redoText()));
        stack.redo();
        emit edited();

        level->modified = true;
        level->modifiedRecently = !isClean();
    }
}

void MapScene::setClean() {
    stack.setClean();
}

void MapScene::clearStack() {
    stack.clear();
}

/*
  Cut/copy/paste functions
*/
void MapScene::cut() {
    copyTiles(true);
}

void MapScene::copy() {
    copyTiles(false);
}

void MapScene::copyTiles(bool cut = false) {
    // if there is no selection, don't do anything
    if (selWidth == 0 || selLength == 0) return;

    MapChange *edit = NULL;
    if (cut) {
        edit = new MapChange(level, selX, selY, selWidth, selLength);
        edit->setText("cut");
    }

    // otherwise, move stuff into the buffer
    for (int i = 0; i < selLength; i++) {
        for (int j = 0; j < selWidth; j++) {
            copyBuffer[i][j] = level->tiles[selY + i][selX + j];
            if (cut)
                level->tiles[selY + i][selX + j] = noTile;
        }
    }

    copyWidth = selWidth;
    copyLength = selLength;

    if (cut) {
        stack.push(edit);
        emit edited();
    }

    emit statusMessage(QString("%1 (%2, %3) to (%4, %5)")
                       .arg(cut ? "Cut" : "Copied")
                       .arg(selX).arg(selY)
                       .arg(selX + selWidth - 1)
                       .arg(selY + selLength - 1));
}

void MapScene::paste() {
    // if there is no selection or copy buffer, don't do anything
    if (selWidth == 0 || selLength == 0
            || copyWidth == 0 || copyLength == 0) return;

    MapChange *edit = new MapChange(level, selX, selY, copyWidth, copyLength);
    edit->setText("paste");

    // otherwise, move stuff into the level from the buffer
    for (uint i = 0; i < copyLength && selY + i < MAX_2D_SIZE; i++) {
        for (uint j = 0; j < copyWidth && selX + j < MAX_2D_SIZE; j++) {
            level->tiles[selY + i][selX + j] = copyBuffer[i][j];
        }
    }

    stack.push(edit);
    emit edited();

    emit statusMessage(QString("Pasted (%1, %2) to (%3, %4)")
                       .arg(selX).arg(selY)
                       .arg(selX + copyWidth - 1)
                       .arg(selY + copyLength - 1));
}

void MapScene::deleteTiles() {
    // if there is no selection, don't do anything
    if (selWidth == 0 || selLength == 0) return;

    MapChange *edit = new MapChange(level, selX, selY, selWidth, selLength);
    edit->setText("delete");

    // otherwise, delete stuff
    for (int i = 0; i < selLength && selY + i < MAX_2D_SIZE; i++) {
        for (int j = 0; j < selWidth && selX + j < MAX_2D_SIZE; j++) {
            level->tiles[selY + i][selX + j] = noTile;
        }
    }

    stack.push(edit);
    emit edited();

    emit statusMessage(QString("Deleted (%1, %2) to (%3, %4)")
                       .arg(selX).arg(selY)
                       .arg(selX + selWidth - 1)
                       .arg(selY + selLength - 1));
}

/*
  Start a new selection on the map scene.
  Called when the mouse is clicked outside of any current selection.
*/
void MapScene::beginSelection(QMouseEvent *event) {
    QPointF pos = event->pos();

    int x = floor(pos.x() / TILE_SIZE);
    int y = floor(pos.y() / TILE_SIZE);

    // ignore invalid click positions
    // (use the floating point X coord to avoid roundoff stupidness)
    if (x >= level->header.width || y >= level->header.length
            || pos.x() < 0 || y < 0)
        return;

    // is the click position outside of the current selection?
    if (x < selX || x >= selX + selWidth || y < selY || y >= selY + selLength) {
        selecting = true;
        selX = x;
        selY = y;
        updateSelection(event);
    }
}

/*
  Update the selected range of map tiles.
  Called when the mouse is over the MapScene with the left button held down.
*/
void MapScene::updateSelection(QMouseEvent *event) {
    int x = selX;
    int y = selY;

    QPointF pos = event->pos();

    x = floor(pos.x() / TILE_SIZE);
    y = floor(pos.y() / TILE_SIZE);

    // ignore invalid mouseover/click positions
    // (use the floating point X coord to avoid roundoff stupidness)
    if (x >= level->header.width || y >= level->header.length
            || x < 0 || y < 0)
        return;

    // update the selection size
    if (x >= selX)
        selWidth = x - selX + 1;
    else
        selWidth = x - selX - 1;
    if (y >= selY)
        selLength = y - selY + 1;
    else
        selLength = y - selY - 1;

    int top = std::min(y, selY);
    int left = std::min(x, selX);

    if (event)
        emit statusMessage(QString("Selected (%1, %2) to (%3, %4)")
                           .arg(left).arg(top)
                           .arg(left + abs(selWidth) - 1)
                           .arg(top + abs(selLength) - 1));

    // also, pass the mouseover coords to the main window
    emit mouseOverTile(x, y);

    update();
}

/*
  Display information about a map tile being hovered over.
  Called when the mouse is over the MapScene without the left button held down.
*/
void MapScene::showTileInfo(QMouseEvent *event) {
    if (level->header.length == 0 || level->header.width == 0)
        return;

    QPointF pos = event->pos();
    // if hte mouse is moved onto a different tile, erase the old one
    // and draw the new one
    if (floor(pos.x() / TILE_SIZE) != tileX || floor(pos.y() / TILE_SIZE) != tileY) {
        tileX = floor(pos.x() / TILE_SIZE);
        tileY = floor(pos.y() / TILE_SIZE);

        maptile_t tile = level->tiles[tileY][tileX];
        // show tile contents on the status bar
        QString stat(QString("(%1,%2,%3)").arg(tileX).arg(tileY).arg(tile.height));
        try {
            stat.append(QString(" %1").arg(kirbyGeometry.at(tile.geometry)));

            if (tile.obstacle)
                stat.append(QString(" / %1").arg(kirbyObstacles.at(tile.obstacle)));
        } catch (const std::out_of_range &dummy) {}

        emit statusMessage(stat);

        // also, pass the mouseover coords to the main window
        emit mouseOverTile(tileX, tileY);
    }
    update();
}

void MapScene::cancelSelection() {
    selWidth = 0;
    selLength = 0;
    selX = 0;
    selY = 0;
    update();
}

void MapScene::paintEvent(QPaintEvent *event) {
    int width = level->header.width;
    int height = level->header.length;

    // no width/height = don't draw anything
    if (width + height == 0) {
        return;
    }

    // assign a painter to the target pixmap
    QPainter painter(this);
    QString infoText;
    QRect infoRect;

    QRect rect = event->rect();

    // slowly blit shit from the tile resource onto the pixmap
    for (int h = rect.top() / TILE_SIZE; h < MAX_2D_SIZE && h <= rect.bottom() / TILE_SIZE; h++) {
        for (int w = rect.left() / TILE_SIZE; w < MAX_2D_SIZE && w <= rect.right() / TILE_SIZE; w++) {
            maptile_t *tile = &level->tiles[h][w];
            int geo = tile->geometry;
            if (geo) {
                painter.drawPixmap(w * TILE_SIZE, h * TILE_SIZE,
                                   tiles,
                                   (geo - 1) * TILE_SIZE, 0,
                                   TILE_SIZE, TILE_SIZE);
            }

            // include obstacles and all other stuff in the same pass
            int obs = tile->obstacle;

            QPixmap gfx;
            int frame = 0;

            /*
             *
             * START OF KIRBY OBSTACLE CHECK
             * (TODO: move the enum from metatile.h and use it instead of magic numbers
             */

            // whispy woods (index 0x00 in enemies.png)
            if (obs == 0x02) {
                gfx = enemies;
                frame = 0;

            // sand trap (index 0 in traps.png)
            } else if (obs == 0x04) {
                gfx = traps;
                frame = 0;

            // spike pit (index 1 in traps.png)
            } else if (obs == 0x05) {
                gfx = traps;
                frame = 1;

            // kirby's start pos (kirby.png)
            } else if (obs == 0x0c) {
                gfx = kirby;
                frame = 0;

            // dedede (frame 0 in dedede.png)
            } else if (obs == 0x0d) {
                gfx = dedede;
                frame = 0;

            // current, arrows, boosters, vents
            // (ind. 00 to 0d in movers.png)
            } else if (obs >= 0x10 && obs <= 0x1d) {
                gfx = movers;
                frame = obs - 0x10;

            // bouncy pads (ind. 0 to 4 in bounce.png)
            } else if (obs >= 0x20 && obs <= 0x24) {
                gfx = bounce;
                frame = obs - 0x20;

            // bumpers (start at index 4 in bumpers.png)
            } else if (obs >= 0x28 && obs <= 0x2d) {
                gfx = bumpers;
                frame = obs - 0x28 + 4;

            // conveyor belts (ind. 0 to b in conveyor.png)
            } else if (obs >= 0x30 && obs <= 0x3b) {
                gfx = conveyor;
                frame = obs - 0x30;

            // most enemies (ind. 01 to 13 in enemies.png)
            } else if (obs >= 0x40 && obs <= 0x52) {
                gfx = enemies;
                frame = obs - 0x40 + 1;

            // transformer (ind. 14 in enemies.png
            } else if (obs == 0x57) {
                gfx = enemies;
                frame = 0x14;

            // switches (ind. 0 to 5 in switches.png)
            } else if (obs >= 0x58 && obs <= 0x5d) {
                gfx = switches;
                frame = obs - 0x58;

            // water hazards (ind. 0 to e in water.png)
            // (note types 62 & 63 seem unused)
            } else if (obs >= 0x61 && obs <= 0x6f) {
                gfx = water;
                frame = obs - 0x61;

            // rotating spaces (ind. 0-b in rotate.png)
            } else if (obs >= 0x70 && obs <= 0x7b) {
                gfx = rotate;
                frame = obs & 0x01;

            // gordo (ind. 00 to 21 in gordo.png)
            } else if (obs >= 0x80 && obs <= 0xa1) {
                gfx = gordo;
                frame = obs - 0x80;

            // kracko (index 15-17 in enemies.png)
            } else if (obs >= 0xac && obs <= 0xae) {
                gfx = enemies;
                frame = obs - 0xac + 0x15;

            // warps (ind. 0 to 9 in warps.png)
            } else if (obs >= 0xb0 && obs <= 0xb9) {
                gfx = warps;
                frame = obs - 0xb0;

            // starting line (ind. 1 to 4 in dedede.png)
            } else if (obs >= 0xc0 && obs <= 0xc3) {
                gfx = dedede;
                frame = obs - 0xc0 + 1;

            // anything else - question mark (or don't draw)
            } else {
#ifdef QT_DEBUG
                gfx = unknown;
                frame = 0;
#else
                obs = 0;
#endif
            }

            /*
             *
             * END OF KIRBY OBSTACLE CHECK
             *
             */

            // draw the selected obstacle
            if (obs) {
                painter.drawPixmap(w * TILE_SIZE,
                                   (h + 1) * TILE_SIZE - gfx.height(),
                                   gfx, frame * TILE_SIZE, 0,
                                   TILE_SIZE, gfx.height());
            }

            // render side bumpers (ind. 0 - 3 in bumpers.png)
            if (tile->flags.bumperSouth)
                painter.drawPixmap(w * TILE_SIZE, h * TILE_SIZE,
                                   bumpers, 0 * TILE_SIZE, 0,
                                   TILE_SIZE, TILE_SIZE);
            if (tile->flags.bumperEast)
                painter.drawPixmap(w * TILE_SIZE, h * TILE_SIZE,
                                   bumpers, 1 * TILE_SIZE, 0,
                                   TILE_SIZE, TILE_SIZE);
            if (tile->flags.bumperNorth)
                painter.drawPixmap(w * TILE_SIZE, h * TILE_SIZE,
                                   bumpers, 2 * TILE_SIZE, 0,
                                   TILE_SIZE, TILE_SIZE);
            if (tile->flags.bumperWest)
                painter.drawPixmap(w * TILE_SIZE, h * TILE_SIZE,
                                   bumpers, 3 * TILE_SIZE, 0,
                                   TILE_SIZE, TILE_SIZE);

            painter.setFont(MapScene::infoFont);

            if (geo) {
                infoText = QString("%1").arg(tile->height, 2);
                infoRect = MapScene::infoFontMetrics.boundingRect(infoText);

                painter.fillRect((w+1) * TILE_SIZE - infoRect.width() - 2 * MAP_TEXT_PAD_H,
                                 (h+1) * TILE_SIZE - infoRect.height() - MAP_TEXT_PAD_V,
                                 infoRect.width() + 2 * MAP_TEXT_PAD_H, infoRect.height() + MAP_TEXT_PAD_V,
                                 MapScene::infoColor);
                painter.drawText(w * TILE_SIZE + MAP_TEXT_PAD_H - 1, h * TILE_SIZE + MAP_TEXT_PAD_V,
                                 TILE_SIZE - MAP_TEXT_PAD_H, TILE_SIZE - MAP_TEXT_PAD_V,
                                 Qt::AlignRight | Qt::AlignBottom,
                                 infoText);
            }

#ifdef QT_DEBUG
            if (tile->flags.layer || tile->flags.dummy) {
                infoText.sprintf("%02X", tile->flags);
#else
            if (tile->flags.layer) {
                infoText = "L2";
#endif
                infoRect = MapScene::infoFontMetrics.boundingRect(infoText);

                painter.fillRect((w+1) * TILE_SIZE - infoRect.width() - 2 * MAP_TEXT_PAD_H,
                                 h * TILE_SIZE,
                                 infoRect.width() + 2 * MAP_TEXT_PAD_H, infoRect.height() + MAP_TEXT_PAD_V,
                                 MapScene::layerColor);
                painter.drawText(w * TILE_SIZE + MAP_TEXT_PAD_H - 1, h * TILE_SIZE,
                                 TILE_SIZE - MAP_TEXT_PAD_H, TILE_SIZE - MAP_TEXT_PAD_V,
                                 Qt::AlignRight | Qt::AlignTop,
                                 infoText);
            }
        }
    }

    // draw tile grid
    for (int h = TILE_SIZE; h < height * TILE_SIZE; h += TILE_SIZE)
        painter.drawLine(0, h, width * TILE_SIZE, h);
    for (int w = TILE_SIZE; w < width * TILE_SIZE; w += TILE_SIZE)
        painter.drawLine(w, 0, w, height * TILE_SIZE);
    painter.setPen(Qt::black);
    painter.drawRect(0, 0, width * TILE_SIZE, height * TILE_SIZE);

    // ignore invalid mouseover positions
    // (use the floating point X coord to avoid roundoff stupidness)
    if (tileX < level->header.width && tileY < level->header.length
           && tileX >= 0 && tileY >= 0) {

        uint infoX = tileX * TILE_SIZE;
        uint infoY = tileY * TILE_SIZE;

        // render background
        painter.fillRect(infoX, infoY, TILE_SIZE, TILE_SIZE,
                         MapScene::infoBackColor);

        // render tile info
        painter.setFont(MapScene::infoFont);
        maptile_t tile = level->tiles[tileY][tileX];

        // only draw bottom part if terrain != 0 (i.e. not empty space)
        if (tile.geometry) {
            // bottom corner: terrain + obstacle
            infoText = QString("%1 %2")
                               .arg((uint)tile.geometry, 2, 16, QLatin1Char('0'))
                               .arg((uint)tile.obstacle, 2, 16, QLatin1Char('0'))
                               .toUpper();
            infoRect = MapScene::infoFontMetrics.boundingRect(infoText);

            // for some stupid reason QFontMetrics is making this bounding box 6px too narrow
            // so i'll just add it back manually (and this willprobably look stupid for any other font settings)
            painter.fillRect(infoX + 1, infoY + TILE_SIZE - infoRect.height() - MAP_TEXT_PAD_V,
                             infoRect.width() + 2 * MAP_TEXT_PAD_H + 6, infoRect.height() + MAP_TEXT_PAD_V,
                             MapScene::infoColor);
            painter.drawText(infoX + MAP_TEXT_PAD_H, infoY + MAP_TEXT_PAD_V,
                             TILE_SIZE - MAP_TEXT_PAD_H, TILE_SIZE - MAP_TEXT_PAD_V,
                             Qt::AlignLeft | Qt::AlignBottom,
                             infoText);

        }
    }

    // draw selection
    if (selWidth != 0 && selLength != 0) {
        // account for selections in either negative direction
        int selLeft = qMin(selX, selX + selWidth + 1);
        int selTop  = qMin(selY, selY + selLength + 1);
        QRect selArea(selLeft * TILE_SIZE, selTop * TILE_SIZE, abs(selWidth) * TILE_SIZE, abs(selLength) * TILE_SIZE);
        painter.fillRect(selArea, MapScene::selectionColor);
    }
}
