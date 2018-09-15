/*
 * Copyright (C) Pedram Pourang (aka Tsu Jan) 2016 <tsujan2000@gmail.com>
 *
 * FeatherNotes is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * FeatherNotes is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef TEXTEDIT_H
#define TEXTEDIT_H

#include <QTextEdit>
#include <QKeyEvent>
#include <QUrl>
#include <QFileInfo>
#include <QImageReader>
#include <QMimeData>
#include "vscrollbar.h"

namespace FeatherNotes {

/* Here, I subclassed QTextEdit to gain control
   over pressing Enter and have auto-indentation.
   I also replaced its vertical scrollbar for faster
   wheel scrolling when the mouse cursor is on the scrollbar. */
class TextEdit : public QTextEdit
{
    Q_OBJECT

public:
    TextEdit (QWidget *parent = nullptr) : QTextEdit (parent)
    {
        autoIndentation = true;
        autoBracket = false;
        textTab_ = "    "; // the default text tab is four spaces
        pressPoint = QPoint();
        scrollJumpWorkaround = false;
        VScrollBar *vScrollBar = new VScrollBar;
        setVerticalScrollBar (vScrollBar);
    }
    void setScrollJumpWorkaround (bool apply)
    {
        scrollJumpWorkaround = apply;
    }

    void zooming (float range);

    bool autoIndentation;
    bool autoBracket;

signals:
    void resized();
    void imageDropped (QString path);
    void zoomedOut (TextEdit *textEdit); // needed for reformatting text

protected:
    void keyPressEvent (QKeyEvent *event);
    bool canInsertFromMimeData (const QMimeData *source) const;
    void insertFromMimeData (const QMimeData *source);
    void mouseMoveEvent (QMouseEvent *e);
    void mousePressEvent (QMouseEvent *e);
    void mouseReleaseEvent (QMouseEvent *e);
    void resizeEvent (QResizeEvent *e);
    bool event (QEvent *e);
    virtual void wheelEvent (QWheelEvent *e);

private:
    QString computeIndentation (const QTextCursor& cur) const;
    QString remainingSpaces (const QString& spaceTab, const QTextCursor& cursor) const;
    QTextCursor backTabCursor(const QTextCursor& cursor) const;

    QString textTab_; // text tab in terms of spaces
    QPoint pressPoint;
    bool scrollJumpWorkaround; // for working around Qt5's scroll jump bug
};

}


#endif // TEXTEDIT_H
