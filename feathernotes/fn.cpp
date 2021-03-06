/*
 * Copyright (C) Pedram Pourang (aka Tsu Jan) 2016-2019 <tsujan2000@gmail.com>
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

#include "fn.h"
#include "ui_fn.h"
#include "ui_about.h"
#include "dommodel.h"
#include "spinbox.h"
#include "simplecrypt.h"
#include "settings.h"
#include "help.h"
#include "filedialog.h"
#include "messagebox.h"
#include "svgicons.h"
#include "pref.h"

#include <QDir>
#include <QTextStream>
#include <QToolButton>
#include <QPrinter>
#include <QPrintDialog>
#include <QFontDialog>
#include <QColorDialog>
#include <QCheckBox>
#include <QGroupBox>
#include <QToolTip>
#include <QScreen>
#include <QWindow>
#include <QStyledItemDelegate>
#include <QTextTable>
#include <QTextBlock>
#include <QTextDocumentFragment>
#include <QTextDocumentWriter>
#include <QClipboard>
#include <QMimeDatabase>

#ifdef HAS_X11
#if defined Q_WS_X11 || defined Q_OS_LINUX || defined Q_OS_OPENBSD || defined Q_OS_NETBSD || defined Q_OS_HURD
#include <QX11Info>
#endif
#include "x11.h"
#endif

namespace FeatherNotes {

static QSize TOOLBAR_ICON_SIZE;

// Regex of an embedded image (should be checked for the image):
static const QRegularExpression EMBEDDED_IMG (R"(<\s*img(?=\s)[^<>]*(?<=\s)src\s*=\s*"data:[^<>]*;base64\s*,[a-zA-Z0-9+=/\s]+"[^<>]*/*>)");

FN::FN (const QStringList& message, QWidget *parent) : QMainWindow (parent), ui (new Ui::FN)
{
#ifdef HAS_X11
    // For now, the lack of x11 is seen as wayland.
#if defined Q_WS_X11 || defined Q_OS_LINUX || defined Q_OS_FREEBSD || defined Q_OS_OPENBSD || defined Q_OS_NETBSD || defined Q_OS_HURD
    isX11_ = QX11Info::isPlatformX11();
#else
    isX11_ = false;
#endif // defined Q_WS_X11
#else
    isX11_ = false;
#endif // HAS_X11

    ui->setupUi (this);
    imgScale_ = 100;

    TOOLBAR_ICON_SIZE = ui->mainToolBar->iconSize();

    QStyledItemDelegate *delegate = new QStyledItemDelegate (this);
    ui->treeView->setItemDelegate (delegate);
    ui->treeView->setContextMenuPolicy (Qt::CustomContextMenu);

    /* NOTE: The auto-saving timer starts only when a new note is created,
       a file is opened, or it is enabled in Preferences. It saves the doc
       only if it belongs to an existing file that needs saving. */
    autoSave_ = -1;
    saveNeeded_ = 0;
    timer_ = new QTimer (this);
    connect (timer_, &QTimer::timeout, this, &FN::autoSaving);

    /* appearance */
    setAttribute (Qt::WA_AlwaysShowToolTips);
    ui->statusBar->setVisible (false);

    defaultFont_ = QFont ("Monospace");
    defaultFont_.setPointSize (qMax (font().pointSize(), 9));
    nodeFont_ = font();

    /* search bar */
    ui->lineEdit->setVisible (false);
    ui->nextButton->setVisible (false);
    ui->prevButton->setVisible (false);
    ui->caseButton->setVisible (false);
    ui->wholeButton->setVisible (false);
    ui->everywhereButton->setVisible (false);
    ui->tagsButton->setVisible (false);
    ui->namesButton->setVisible (false);
    searchingOtherNode_ = false;
    rplOtherNode_ = false;
    replCount_ = 0;

    /* replace dock */
    ui->dockReplace->setVisible (false);

    model_ = new DomModel (QDomDocument(), this);
    ui->treeView->setModel (model_);

    /* get the default (customizable) shortcuts before any change */
    static const QStringList excluded = {"actionCut", "actionCopy", "actionPaste", "actionSelectAll"};
    const auto allMenus = ui->menuBar->findChildren<QMenu*>();
    for (auto thisMenu : allMenus)
    {
        const auto menuActions = thisMenu->actions();
        for (auto menuAction : menuActions)
        {
            QKeySequence seq = menuAction->shortcut();
            if (!seq.isEmpty() && !excluded.contains (menuAction->objectName()))
                defaultShortcuts_.insert (menuAction, seq);
        }
    }
    /* exceptions */
    defaultShortcuts_.insert (ui->actionPrintNodes, QKeySequence());
    defaultShortcuts_.insert (ui->actionPrintAll, QKeySequence());
    defaultShortcuts_.insert (ui->actionExportHTML, QKeySequence());
    defaultShortcuts_.insert (ui->actionPassword, QKeySequence());
    defaultShortcuts_.insert (ui->actionDocFont, QKeySequence());
    defaultShortcuts_.insert (ui->actionNodeFont, QKeySequence());

    reservedShortcuts_
    /* QTextEdit */
    << QKeySequence (Qt::CTRL + Qt::SHIFT + Qt::Key_Z).toString() << QKeySequence (Qt::CTRL + Qt::Key_Z).toString() << QKeySequence (Qt::CTRL + Qt::Key_X).toString() << QKeySequence (Qt::CTRL + Qt::Key_C).toString() << QKeySequence (Qt::CTRL + Qt::Key_V).toString() << QKeySequence (Qt::CTRL + Qt::Key_A).toString()
    << QKeySequence (Qt::SHIFT + Qt::Key_Insert).toString() << QKeySequence (Qt::SHIFT + Qt::Key_Delete).toString() << QKeySequence (Qt::CTRL + Qt::Key_Insert).toString()
    << QKeySequence (Qt::CTRL + Qt::Key_Left).toString() << QKeySequence (Qt::CTRL + Qt::Key_Right).toString() << QKeySequence (Qt::CTRL + Qt::Key_Up).toString() << QKeySequence (Qt::CTRL + Qt::Key_Down).toString()
    << QKeySequence (Qt::CTRL + Qt::Key_Home).toString() << QKeySequence (Qt::CTRL + Qt::Key_End).toString()
    << QKeySequence (Qt::CTRL + Qt::SHIFT + Qt::Key_Up).toString() << QKeySequence (Qt::CTRL + Qt::SHIFT + Qt::Key_Down).toString()
    << QKeySequence (Qt::META + Qt::Key_Up).toString() << QKeySequence (Qt::META + Qt::Key_Down).toString() << QKeySequence (Qt::META + Qt::SHIFT + Qt::Key_Up).toString() << QKeySequence (Qt::META + Qt::SHIFT + Qt::Key_Down).toString()
    /* search and replacement */
    << QKeySequence (Qt::Key_F3).toString() << QKeySequence (Qt::Key_F4).toString() << QKeySequence (Qt::Key_F5).toString() << QKeySequence (Qt::Key_F6).toString() << QKeySequence (Qt::Key_F7).toString()
    << QKeySequence (Qt::Key_F8).toString() << QKeySequence (Qt::Key_F9).toString() << QKeySequence (Qt::Key_F10).toString()
    << QKeySequence (Qt::Key_F11).toString() << QKeySequence (Qt::CTRL + Qt::SHIFT + Qt::Key_W).toString() << QKeySequence (Qt::SHIFT + Qt::Key_F7).toString() << QKeySequence (Qt::CTRL + Qt::SHIFT + Qt::Key_F7).toString()
    /* zooming */
    << QKeySequence (Qt::CTRL + Qt::Key_Equal).toString() << QKeySequence (Qt::CTRL + Qt::Key_Plus).toString() << QKeySequence (Qt::CTRL + Qt::Key_Minus).toString() << QKeySequence (Qt::CTRL + Qt::Key_0).toString()
    /* text tabulation */
    << QKeySequence (Qt::SHIFT + Qt::Key_Enter).toString() << QKeySequence (Qt::SHIFT + Qt::Key_Return).toString() << QKeySequence (Qt::CTRL + Qt::Key_Tab).toString() << QKeySequence (Qt::CTRL + Qt::META + Qt::Key_Tab).toString()
    /* used by LineEdit as well as QTextEdit */
    << QKeySequence (Qt::CTRL + Qt::Key_K).toString();
    readShortcuts();

    QHash<QString, QString>::const_iterator it = customActions_.constBegin();
    while (it != customActions_.constEnd())
    { // NOTE: Custom shortcuts are saved in the PortableText format.
        if (QAction *action = findChild<QAction*>(it.key()))
            action->setShortcut (QKeySequence (it.value(), QKeySequence::PortableText));
        ++it;
    }

    shownBefore_ = false;
    splitterSizes_ = QByteArray::fromBase64 ("AAAA/wAAAAEAAAACAAAAqgAAAhIB/////wEAAAABAA==");
    remSize_ = true;
    remSplitter_ = true;
    remPosition_ = true;
    wrapByDefault_ = true;
    indentByDefault_ = true;
    transparentTree_ = false;
    smallToolbarIcons_ = false;
    noToolbar_ = false;
    noMenubar_ = false;
    autoBracket_ = false;
    autoReplace_ = false;
    treeViewDND_ = false;
    readAndApplyConfig();

    QWidget* spacer = new QWidget();
    spacer->setSizePolicy (QSizePolicy::Expanding, QSizePolicy::Preferred);
    ui->mainToolBar->insertWidget (ui->actionMenu, spacer);
    QMenu *menu = new QMenu (ui->mainToolBar);
    menu->addMenu (ui->menuFile);
    menu->addMenu (ui->menuEdit);
    menu->addMenu (ui->menuFormat);
    menu->addMenu (ui->menuTree);
    menu->addMenu (ui->menuOptions);
    menu->addMenu (ui->menuSearch);
    menu->addMenu (ui->menuHelp);
    ui->actionMenu->setMenu(menu);
    QList<QToolButton*> tbList = ui->mainToolBar->findChildren<QToolButton*>();
    if (!tbList.isEmpty())
        tbList.at (tbList.count() - 1)->setPopupMode (QToolButton::InstantPopup);

    if (hasTray_)
        quitting_ = false;
    else
        quitting_ = true;

    QActionGroup *aGroup = new QActionGroup (this);
    ui->actionLeft->setActionGroup (aGroup);
    ui->actionCenter->setActionGroup (aGroup);
    ui->actionRight->setActionGroup (aGroup);
    ui->actionJust->setActionGroup (aGroup);

    QActionGroup *aGroup1 = new QActionGroup (this);
    ui->actionLTR->setActionGroup (aGroup1);
    ui->actionRTL->setActionGroup (aGroup1);

    /* signal connections */

    connect (ui->treeView, &QWidget::customContextMenuRequested, this, &FN::showContextMenu);
    connect (ui->treeView, &TreeView::FNDocDropped, this, &FN::openFNDoc);

    connect (ui->actionNew, &QAction::triggered, this, &FN::newNote);
    connect (ui->actionOpen, &QAction::triggered, this, &FN::openFile);
    connect (ui->actionSave, &QAction::triggered, this, &FN::saveFile);
    connect (ui->actionSaveAs, &QAction::triggered, this, &FN::saveFile);

    connect (ui->actionPassword, &QAction::triggered, this, &FN::setPswd);

    connect (ui->actionPrint, &QAction::triggered, this, &FN::txtPrint);
    connect (ui->actionPrintNodes, &QAction::triggered, this, &FN::txtPrint);
    connect (ui->actionPrintAll, &QAction::triggered, this, &FN::txtPrint);
    connect (ui->actionExportHTML, &QAction::triggered, this, &FN::exportHTML);

    connect (ui->actionUndo, &QAction::triggered, this, &FN::undoing);
    connect (ui->actionRedo, &QAction::triggered, this, &FN::redoing);

    connect (ui->actionCut, &QAction::triggered, this, &FN::cutText);
    connect (ui->actionCopy, &QAction::triggered, this, &FN::copyText);
    connect (ui->actionPaste, &QAction::triggered, this, &FN::pasteText);
    connect (ui->actionPasteHTML, &QAction::triggered, this, &FN::pasteHTML);
    connect (ui->actionDelete, &QAction::triggered, this, &FN::deleteText);
    connect (ui->actionSelectAll, &QAction::triggered, this, &FN::selectAllText);

    connect (ui->actionBold, &QAction::triggered, this, &FN::makeBold);
    connect (ui->actionItalic, &QAction::triggered, this, &FN::makeItalic);
    connect (ui->actionUnderline, &QAction::triggered, this, &FN::makeUnderlined);
    connect (ui->actionStrike, &QAction::triggered, this, &FN::makeStriked);
    connect (ui->actionSuper, &QAction::triggered, this, &FN::makeSuperscript);
    connect (ui->actionSub, &QAction::triggered, this, &FN::makeSubscript);
    connect (ui->actionTextColor, &QAction::triggered, this, &FN::textColor);
    connect (ui->actionBgColor, &QAction::triggered, this, &FN::bgColor);
    connect (ui->actionClear, &QAction::triggered, this, &FN::clearFormat);

    connect (ui->actionH3, &QAction::triggered, this, &FN::makeHeader);
    connect (ui->actionH2, &QAction::triggered, this, &FN::makeHeader);
    connect (ui->actionH1, &QAction::triggered, this, &FN::makeHeader);

    connect (ui->actionLink, &QAction::triggered, this, &FN::insertLink);
    connect (ui->actionCopyLink, &QAction::triggered, this, &FN::copyLink);

    connect (ui->actionEmbedImage, &QAction::triggered, this, &FN::embedImage);
    connect (ui->actionImageScale, &QAction::triggered, this, &FN::scaleImage);
    connect (ui->actionImageSave, &QAction::triggered, this, &FN::saveImage);

    connect (ui->actionTable, &QAction::triggered, this, &FN::addTable);
    connect (ui->actionTableMergeCells, &QAction::triggered, this, &FN::tableMergeCells);
    connect (ui->actionTablePrependRow, &QAction::triggered, this, &FN::tablePrependRow);
    connect (ui->actionTableAppendRow, &QAction::triggered, this, &FN::tableAppendRow);
    connect (ui->actionTablePrependCol, &QAction::triggered, this, &FN::tablePrependCol);
    connect (ui->actionTableAppendCol, &QAction::triggered, this, &FN::tableAppendCol);
    connect (ui->actionTableDeleteRow, &QAction::triggered, this, &FN::tableDeleteRow);
    connect (ui->actionTableDeleteCol, &QAction::triggered, this, &FN::tableDeleteCol);

    connect(aGroup, &QActionGroup::triggered, this,&FN::textAlign);
    connect(aGroup1, &QActionGroup::triggered, this, &FN::textDirection);

    connect (ui->actionExpandAll, &QAction::triggered, this, &FN::expandAll);
    connect (ui->actionCollapseAll, &QAction::triggered, this, &FN::collapseAll);

    connect (ui->actionNewSibling, &QAction::triggered, this, &FN::newNode);
    connect (ui->actionNewChild, &QAction::triggered, this, &FN::newNode);
    connect (ui->actionPrepSibling, &QAction::triggered, this, &FN::newNode);
    connect (ui->actionDeleteNode, &QAction::triggered, this, &FN::deleteNode);
    connect (ui->actionMoveUp, &QAction::triggered, this, &FN::moveUpNode);
    connect (ui->actionMoveDown, &QAction::triggered, this, &FN::moveDownNode);
    if (QApplication::layoutDirection() == Qt::RightToLeft)
    {
        connect (ui->actionMoveLeft, &QAction::triggered, this, &FN::moveRightNode);
        connect (ui->actionMoveRight, &QAction::triggered, this, &FN::moveLeftNode);
    }
    else
    {
        connect (ui->actionMoveLeft, &QAction::triggered, this, &FN::moveLeftNode);
        connect (ui->actionMoveRight, &QAction::triggered, this, &FN::moveRightNode);
    }

    connect (ui->actionTags, &QAction::triggered, this, &FN::handleTags);
    connect (ui->actionRenameNode, &QAction::triggered, this, &FN::renameNode);
    connect (ui->actionNodeIcon, &QAction::triggered, this, &FN::nodeIcon);
    connect (ui->actionProp, &QAction::triggered, this, &FN::toggleStatusBar);

    connect (ui->actionDocFont, &QAction::triggered, this, &FN::textFontDialog);
    connect (ui->actionNodeFont, &QAction::triggered, this, &FN::nodeFontDialog);

    connect (ui->actionWrap, &QAction::triggered, this, &FN::toggleWrapping);
    connect (ui->actionIndent, &QAction::triggered, this, &FN::toggleIndent);
    connect (ui->actionPref, &QAction::triggered, this, &FN::prefDialog);

    connect (ui->actionFind, &QAction::triggered, this, &FN::showHideSearch);
    connect (ui->nextButton, &QAbstractButton::clicked, this, &FN::find);
    connect (ui->prevButton, &QAbstractButton::clicked, this, &FN::find);
    connect (ui->lineEdit, &QLineEdit::returnPressed, this, &FN::find);
    connect (ui->wholeButton, &QAbstractButton::clicked, this, &FN::setSearchFlags);
    connect (ui->caseButton, &QAbstractButton::clicked, this, &FN::setSearchFlags);
    connect (ui->everywhereButton, &QAbstractButton::toggled, this, &FN::allBtn);
    connect (ui->tagsButton, &QAbstractButton::toggled, this, &FN::tagsAndNamesBtn);
    connect (ui->namesButton, &QAbstractButton::toggled, this, &FN::tagsAndNamesBtn );

    connect (ui->actionReplace, &QAction::triggered, this, &FN::replaceDock);
    connect (ui->dockReplace, &QDockWidget::visibilityChanged, this, &FN::closeReplaceDock);
    connect (ui->dockReplace, &QDockWidget::topLevelChanged, this, &FN::resizeDock);
    connect (ui->rplNextButton, &QAbstractButton::clicked, this, &FN::replace);
    connect (ui->rplPrevButton, &QAbstractButton::clicked, this, &FN::replace);
    connect (ui->allButton, &QAbstractButton::clicked, this, &FN::replaceAll);

    connect (ui->actionAbout, &QAction::triggered, this, &FN::aboutDialog);
    connect (ui->actionHelp, &QAction::triggered, this, &FN::showHelpDialog);

    /* Once the tray icon is created, it'll persist even if the systray
       disappears temporarily. But for the tray icon to be created, the
       systray should exist. Hence, we wait 1 min for the systray at startup. */
    tray_ = nullptr;
    trayCounter_ = 0;
    if (hasTray_)
    {
        if (QSystemTrayIcon::isSystemTrayAvailable())
            createTrayIcon();
        else
        {
            QTimer *trayTimer = new QTimer (this);
            trayTimer->setSingleShot (true);
            trayTimer->setInterval (5000);
            connect (trayTimer, &QTimer::timeout, this, &FN::checkTray);
            trayTimer->start();
            ++trayCounter_;
        }
    }

    QShortcut *focusSwitcher = new QShortcut (QKeySequence (Qt::Key_Escape), this);
    connect (focusSwitcher, &QShortcut::activated, [this] {
        if (QWidget *cw = ui->stackedWidget->currentWidget())
        {
            if (cw->hasFocus())
                ui->treeView->viewport()->setFocus();
            else
                cw->setFocus();
        }
    });

    QShortcut *fullscreen = new QShortcut (QKeySequence (Qt::Key_F11), this);
    connect (fullscreen, &QShortcut::activated, this, &FN::fullScreening);

    QShortcut *defaultsize = new QShortcut (QKeySequence (Qt::CTRL + Qt::SHIFT + Qt::Key_W), this);
    connect (defaultsize, &QShortcut::activated, this, &FN::defaultSize);

    QShortcut *zoomin = new QShortcut (QKeySequence (Qt::CTRL + Qt::Key_Equal), this);
    QShortcut *zoominPlus = new QShortcut (QKeySequence (Qt::CTRL + Qt::Key_Plus), this);
    QShortcut *zoomout = new QShortcut (QKeySequence (Qt::CTRL + Qt::Key_Minus), this);
    QShortcut *unzoom = new QShortcut (QKeySequence (Qt::CTRL + Qt::Key_0), this);
    connect (zoomin, &QShortcut::activated, this, &FN::zoomingIn);
    connect (zoominPlus, &QShortcut::activated, this, &FN::zoomingIn);
    connect (zoomout, &QShortcut::activated, this, &FN::zoomingOut);
    connect (unzoom, &QShortcut::activated, this, &FN::unZooming);

    /* parse the message */
    QString filePath;
    if (message.isEmpty())
    {
        if (!hasTray_ || !minToTray_)
            show();
    }
    else
    {
        if (message.at (0) != "--min" && message.at (0) != "-m"
            && message.at (0) != "--tray" && message.at (0) != "-t")
        {
            if (!hasTray_ || !minToTray_)
                show();
            filePath = message.at (0);
        }
        else
        {
            if (message.at (0) == "--min" || message.at (0) == "-m")
                showMinimized();
            else if (!hasTray_)
                show();
            if (message.count() > 1)
                filePath = message.at (1);
        }
    }

    /* always an absolute path */
    if (!filePath.isEmpty())
    {
        if (filePath.startsWith ("file://"))
            filePath = QUrl (filePath).toLocalFile();
        filePath = QDir::current().absoluteFilePath (filePath);
        filePath = QDir::cleanPath (filePath);
    }

    fileOpen (filePath);

    /*dummyWidget = nullptr;
    if (hasTray_)
        dummyWidget = new QWidget();*/

    setAcceptDrops (true);
}
/*************************/
FN::~FN()
{
    if (timer_)
    {
        if (timer_->isActive()) timer_->stop();
        delete timer_;
    }
    delete tray_; // also deleted at closeEvent() (this is for Ctrl+C in terminal)
    tray_ = nullptr;
    delete ui;
}
/*************************/
bool FN::close()
{
    QObject *sender = QObject::sender();
    if (sender != nullptr && sender->objectName() == "trayQuit" && findChildren<QDialog *>().count() > 0)
    { // don't respond to the tray icon when there's a dialog
        raise();
        activateWindow();
        return false;
    }

    quitting_ = true;
    return QWidget::close();
}
/*************************/
void FN::closeEvent (QCloseEvent *event)
{
    if (!quitting_)
    {
        event->ignore();
        if (!isMaximized() && !isFullScreen())
        {
            position_.setX (geometry().x());
            position_.setY (geometry().y());
        }
        if (tray_ && QSystemTrayIcon::isSystemTrayAvailable())
            QTimer::singleShot (0, this, &QWidget::hide);
        else
            QTimer::singleShot (0, this, &QWidget::showMinimized);
        return;
    }

    if (timer_->isActive()) timer_->stop();

    bool keep = false;
    if (ui->stackedWidget->currentIndex() > -1)
    {
        if (!xmlPath_.isEmpty() && (!QFile::exists (xmlPath_) || !QFileInfo (xmlPath_).isFile()))
        {
            if (tray_)
            {
                if (underE_ && QObject::sender() != nullptr && QObject::sender()->objectName() == "trayQuit")
                {
                    if (!isVisible())
                    {
                        activateTray();
                        QCoreApplication::processEvents();
                    }
                    else
                    {
                        raise();
                        activateWindow();
                    }
                }
                else if (!underE_ && (!isVisible() || !isActiveWindow()))
                {
                    activateTray();
                    QCoreApplication::processEvents();
                }
            }
            if (unSaved (false))
                keep = true;
        }
        else if (saveNeeded_)
        {
            if (tray_)
            {
                if (underE_ && QObject::sender() != nullptr && QObject::sender()->objectName() == "trayQuit")
                {
                    if (!isVisible())
                    {
                        activateTray();
                        QCoreApplication::processEvents();
                    }
                    else
                    {
                        raise();
                        activateWindow();
                    }
                }
                else if (!underE_ && (!isVisible() || !isActiveWindow()))
                {
                    activateTray();
                    QCoreApplication::processEvents();
                }
            }
            if (unSaved (true))
                keep = true;
        }
    }
    if (keep)
    {
        if (tray_)
            quitting_ = false;
        if (autoSave_ >= 1)
            timer_->start (autoSave_ * 1000 * 60);
        event->ignore();
    }
    else
    {
        writeGeometryConfig();
        delete tray_; // otherwise the app won't quit under KDE
        tray_ = nullptr;
        event->accept();
    }
}
/*************************/
void FN::checkTray()
{
    if (QTimer *trayTimer = qobject_cast<QTimer*>(sender()))
    {
        if (QSystemTrayIcon::isSystemTrayAvailable())
        {
            trayTimer->deleteLater();
            createTrayIcon();
            trayCounter_ = 0; // not needed
        }
        else if (trayCounter_ < 12)
        {
            trayTimer->start();
            ++trayCounter_;
        }
        else
        {
            trayTimer->deleteLater();
            show();
        }
    }
}
/*************************/
void FN::createTrayIcon()
{
    QIcon icn = QIcon::fromTheme ("feathernotes");
    if (icn.isNull())
        icn = QIcon (":icons/feathernotes.svg");
    tray_ = new QSystemTrayIcon (icn, this);
    if (xmlPath_.isEmpty())
        tray_->setToolTip ("FeatherNotes");
    else
    {
        QString shownName = QFileInfo (xmlPath_).fileName();
        if (shownName.endsWith (".fnx"))
            shownName.chop (4);
        tray_->setToolTip ("<p style='white-space:pre'>"
                           + shownName
                           + "</p>");
    }
    QMenu *trayMenu = new QMenu (this);
    /* we don't want shortcuts to be shown here */
    QAction *actionshowMainWindow = trayMenu->addAction (tr ("&Raise/Hide"));
    if (underE_)
        actionshowMainWindow->setText (tr ("&Raise"));
    connect (actionshowMainWindow, &QAction::triggered, this, &FN::activateTray);
    /* use system icons with the tray menu because it gets its style from the panel */
    QAction *actionNewTray = trayMenu->addAction (QIcon::fromTheme ("document-new"), tr ("&New Note"));
    QAction *actionOpenTray = trayMenu->addAction (QIcon::fromTheme ("document-open"), tr ("&Open"));
    trayMenu->addSeparator();
    QAction *antionQuitTray = trayMenu->addAction (QIcon::fromTheme ("application-exit"), tr ("&Quit"));
    connect (actionNewTray, &QAction::triggered, this, &FN::newNote);
    connect (actionOpenTray, &QAction::triggered, this, &FN::openFile);
    connect (antionQuitTray, &QAction::triggered, this, &FN::close);
    actionshowMainWindow->setObjectName ("raiseHide");
    actionNewTray->setObjectName ("trayNew");
    actionOpenTray->setObjectName ("trayOpen");
    antionQuitTray->setObjectName ("trayQuit");
    tray_->setContextMenu (trayMenu);
    tray_->setVisible (true);
    connect (tray_, &QSystemTrayIcon::activated, this, &FN::trayActivated );

    /*QShortcut *toggleTray = new QShortcut (QKeySequence (tr ("Ctrl+Shift+M", "Minimize to tray")), this);
    connect (toggleTray, &QShortcut::activated, this, &FN::activateTray);*/
}
/*************************/
void FN::showContextMenu (const QPoint &p)
{
    QModelIndex index = ui->treeView->indexAt (p);
    if (!index.isValid()) return;

    QMenu menu;
    menu.addAction (ui->actionPrepSibling);
    menu.addAction (ui->actionNewSibling);
    menu.addAction (ui->actionNewChild);
    menu.addAction (ui->actionDeleteNode);
    menu.addSeparator();
    menu.addAction (ui->actionTags);
    menu.addAction (ui->actionNodeIcon);
    menu.addAction (ui->actionRenameNode);
    menu.exec (ui->treeView->viewport()->mapToGlobal (p));
}
/*************************/
void FN::fullScreening()
{
    setWindowState (windowState() ^ Qt::WindowFullScreen);
}
/*************************/
void FN::defaultSize()
{
    if (isMaximized() || isFullScreen())
        return;
    /*if (isMaximized() && isFullScreen())
        showMaximized();
    if (isMaximized())
        showNormal();*/
    if (size() != startSize_)
    {
        //setVisible (false);
        resize (startSize_);
        //QTimer::singleShot (250, this, &QWidget::showNormal);
    }
    QList<int> sizes; sizes << 170 << 530;
    ui->splitter->setSizes (sizes);
}
/*************************/
void FN::zoomingIn()
{
    QWidget *cw = ui->stackedWidget->currentWidget();
    if (!cw) return;

    TextEdit *textEdit = qobject_cast< TextEdit *>(cw);
    textEdit->zooming (1.f);
}
/*************************/
void FN::zoomingOut()
{
    QWidget *cw = ui->stackedWidget->currentWidget();
    if (!cw) return;

    TextEdit *textEdit = qobject_cast< TextEdit *>(cw);
    textEdit->zooming (-1.f);

    rehighlight (textEdit);
}
/*************************/
void FN::unZooming()
{
    QWidget *cw = ui->stackedWidget->currentWidget();
    if (!cw) return;

    TextEdit *textEdit = qobject_cast< TextEdit *>(cw);
    textEdit->setFont (defaultFont_);
#if (QT_VERSION >= QT_VERSION_CHECK(5,11,0))
    QFontMetricsF metrics (defaultFont_);
    textEdit->setTabStopDistance (4 * metrics.horizontalAdvance (' '));
#elif (QT_VERSION >= QT_VERSION_CHECK(5,10,0))
    QFontMetricsF metrics (defaultFont_);
    textEdit->setTabStopDistance (4 * metrics.width (' '));
#else
    QFontMetrics metrics (defaultFont_);
    textEdit->setTabStopWidth (4 * metrics.width (' '));
#endif

    /* this may be a zoom-out */
    rehighlight (textEdit);
}
/*************************/
void FN::resizeEvent (QResizeEvent *event)
{
    if (remSize_ && windowState() == Qt::WindowNoState)
        winSize_ = event->size();
    QWidget::resizeEvent (event);
}
/*************************/
void FN::newNote()
{
    QObject *sender = QObject::sender();
    if (sender != nullptr && sender->objectName() == "trayNew" && findChildren<QDialog *>().count() > 0)
    { // don't respond to the tray icon when there's a dialog
        raise();
        activateWindow();
        return;
    }
    closeTagsDialog();

    if (timer_->isActive()) timer_->stop();

    if (tray_)
    {
        if (underE_ && sender != nullptr && sender->objectName() == "trayNew")
        {
            if (!isVisible())
            {
                activateTray();
                QCoreApplication::processEvents();
            }
            else
            {
                raise();
                activateWindow();
            }
        }
        else if (!underE_ && (!isVisible() || !isActiveWindow()))
        {
            activateTray();
            QCoreApplication::processEvents();
        }
    }

    if (!xmlPath_.isEmpty() && !QFile::exists (xmlPath_))
    {
        if (unSaved (false))
        {
            if (autoSave_ >= 1)
                timer_->start (autoSave_ * 1000 * 60);
            return;
        }
    }
    else if (saveNeeded_)
    {
        if (unSaved (true))
        {
            if (autoSave_ >= 1)
                timer_->start (autoSave_ * 1000 * 60);
            return;
        }
    }

    /* show user a prompt */
    if (!xmlPath_.isEmpty())
    {
        MessageBox msgBox;
        msgBox.setIcon (QMessageBox::Question);
        msgBox.setWindowTitle ("FeatherNotes"); // kwin sets an ugly title
        msgBox.setText (tr ("<center><b><big>New note?</big></b></center>"));
        msgBox.setInformativeText (tr ("<center><i>Do you really want to leave this document</i></center>\n"\
                                       "<center><i>and create an empty one?</i></center>"));
        msgBox.setStandardButtons (QMessageBox::Yes | QMessageBox::No);
        msgBox.changeButtonText (QMessageBox::Yes, tr ("Yes"));
        msgBox.changeButtonText (QMessageBox::No, tr ("No"));
        msgBox.setDefaultButton (QMessageBox::No);
        msgBox.setParent (this, Qt::Dialog);
        msgBox.setWindowModality (Qt::WindowModal);
        msgBox.show();
        msgBox.move (x() + width()/2 - msgBox.width()/2,
                     y() + height()/2 - msgBox.height()/ 2);
        switch (msgBox.exec())
        {
        case QMessageBox::Yes:
            break;
        case QMessageBox::No:
        default:
            if (autoSave_ >= 1)
                timer_->start (autoSave_ * 1000 * 60);
            return;
        }
    }

    QDomDocument doc;
    QDomProcessingInstruction inst = doc.createProcessingInstruction ("xml", "version=\'1.0\' encoding=\'utf-8\'");
    doc.insertBefore(inst, QDomNode());
    QDomElement root = doc.createElement ("feathernotes");
    root.setAttribute ("txtfont", defaultFont_.toString());
    root.setAttribute ("nodefont", nodeFont_.toString());
    doc.appendChild (root);
    QDomElement e = doc.createElement ("node");
    e.setAttribute ("name", tr ("New Node"));
    root.appendChild (e);

    showDoc (doc);
    xmlPath_ = QString();
    setTitle (xmlPath_);
    /* may be saved later */
    if (autoSave_ >= 1)
        timer_->start (autoSave_ * 1000 * 60);
    docProp();
}
/*************************/
void FN::setTitle (const QString& fname)
{
    QFileInfo fileInfo;
    if (fname.isEmpty()
        || !(fileInfo = QFileInfo (fname)).exists())
    {
        setWindowTitle (QString ("[*]%1").arg ("FeatherNotes"));
        if (tray_)
            tray_->setToolTip ("FeatherNotes");
        return;
    }

    QString shownName = fileInfo.fileName();
    if (shownName.endsWith (".fnx"))
        shownName.chop (4);

    QString path (fileInfo.dir().path());
    setWindowTitle (QString ("[*]%1 (%2)").arg (shownName).arg (path));

    if (tray_)
    {
        tray_->setToolTip ("<p style='white-space:pre'>"
                           + shownName
                           + "</p>");
    }
}
/*************************/
bool FN::unSaved (bool modified)
{
    int unsaved = false;
    MessageBox msgBox;
    msgBox.setIcon (QMessageBox::Warning);
    msgBox.setText (tr ("<center><b><big>Save changes?</big></b></center>"));
    if (modified)
        msgBox.setInformativeText (tr ("<center><i>The document has been modified.</i></center>"));
    else
        msgBox.setInformativeText (tr ("<center><i>The document has been removed.</i></center>"));
    msgBox.setStandardButtons (QMessageBox::Save
                               | QMessageBox::Discard
                               | QMessageBox::Cancel);
    msgBox.changeButtonText (QMessageBox::Save, tr ("Save"));
    msgBox.changeButtonText (QMessageBox::Discard, tr ("Discard changes"));
    msgBox.changeButtonText (QMessageBox::Cancel, tr ("Cancel"));
    msgBox.setDefaultButton (QMessageBox::Save);
    msgBox.setParent (this, Qt::Dialog);
    msgBox.setWindowModality (Qt::WindowModal);
    /* enforce a central position */
    msgBox.show();
    msgBox.move (x() + width()/2 - msgBox.width()/2,
                 y() + height()/2 - msgBox.height()/ 2);
    switch (msgBox.exec())
    {
    case QMessageBox::Save:
        if (!saveFile())
            unsaved = true;
        break;
    case QMessageBox::Discard: // false
        break;
    case QMessageBox::Cancel: // true
    default:
        unsaved = true;
        break;
    }

    return unsaved;
}
/*************************/
void FN::enableActions (bool enable)
{
    ui->actionSaveAs->setEnabled (enable);
    ui->actionPrint->setEnabled (enable);
    ui->actionPrintNodes->setEnabled (enable);
    ui->actionPrintAll->setEnabled (enable);
    ui->actionExportHTML->setEnabled (enable);
    ui->actionPassword->setEnabled (enable);

    ui->actionPaste->setEnabled (enable);
    ui->actionPasteHTML->setEnabled (enable);
    ui->actionSelectAll->setEnabled (enable);

    ui->actionClear->setEnabled (enable);
    ui->actionBold->setEnabled (enable);
    ui->actionItalic->setEnabled (enable);
    ui->actionUnderline->setEnabled (enable);
    ui->actionStrike->setEnabled (enable);
    ui->actionSuper->setEnabled (enable);
    ui->actionSub->setEnabled (enable);
    ui->actionTextColor->setEnabled (enable);
    ui->actionBgColor->setEnabled (enable);
    ui->actionLeft->setEnabled (enable);
    ui->actionCenter->setEnabled (enable);
    ui->actionRight->setEnabled (enable);
    ui->actionJust->setEnabled (enable);

    ui->actionLTR->setEnabled (enable);
    ui->actionRTL->setEnabled (enable);

    ui->actionH3->setEnabled (enable);
    ui->actionH2->setEnabled (enable);
    ui->actionH1->setEnabled (enable);

    ui->actionEmbedImage->setEnabled (enable);
    ui->actionTable->setEnabled (enable);

    ui->actionExpandAll->setEnabled (enable);
    ui->actionCollapseAll->setEnabled (enable);
    ui->actionPrepSibling->setEnabled (enable);
    ui->actionNewSibling->setEnabled (enable);
    ui->actionNewChild->setEnabled (enable);
    ui->actionDeleteNode->setEnabled (enable);
    ui->actionMoveUp->setEnabled (enable);
    ui->actionMoveDown->setEnabled (enable);
    ui->actionMoveLeft->setEnabled (enable);
    ui->actionMoveRight->setEnabled (enable);

    ui->actionTags->setEnabled (enable);
    ui->actionRenameNode->setEnabled (enable);
    ui->actionNodeIcon->setEnabled (enable);

    ui->actionDocFont->setEnabled (enable);
    ui->actionNodeFont->setEnabled (enable);
    ui->actionWrap->setEnabled (enable);
    ui->actionIndent->setEnabled (enable);

    ui->actionFind->setEnabled (enable);
    ui->actionReplace->setEnabled (enable);

    if (!enable)
    {
        ui->actionUndo->setEnabled (false);
        ui->actionRedo->setEnabled (false);
    }
}
/*************************/
void FN::showDoc (QDomDocument &doc)
{
    if (saveNeeded_)
    {
        saveNeeded_ = 0;
        ui->actionSave->setEnabled (false);
        setWindowModified (false);
    }

    while (ui->stackedWidget->count() > 0)
    {
        widgets_.clear();
        searchEntries_.clear();
        greenSels_.clear();
        QWidget *cw = ui->stackedWidget->currentWidget();
        TextEdit *textEdit = qobject_cast< TextEdit *>(cw);
        ui->stackedWidget->removeWidget (cw);
        delete textEdit; textEdit = nullptr;
    }

    QDomElement root = doc.firstChildElement ("feathernotes");
    QString fontStr = root.attribute ("txtfont");
    if (!fontStr.isEmpty())
        defaultFont_.fromString (fontStr);
    else // defaultFont_ may have changed by the user
    {
        defaultFont_ = QFont ("Monospace");
        defaultFont_.setPointSize (qMax (font().pointSize(), 9));
    }
    fontStr = root.attribute ("nodefont");
    if (!fontStr.isEmpty())
        nodeFont_.fromString (fontStr);
    else // nodeFont_ may have changed by the user
        nodeFont_ = font();

    DomModel *newModel = new DomModel (doc, this);
    QItemSelectionModel *m = ui->treeView->selectionModel();
    ui->treeView->setModel (newModel);
    ui->treeView->setFont (nodeFont_);
    delete m;
    /* first connect to selectionChanged()... */
    connect (ui->treeView->selectionModel(), &QItemSelectionModel::selectionChanged, this, &FN::selChanged);
    /* ... and then, select the first row */
    ui->treeView->setCurrentIndex (newModel->index(0, 0));
    ui->treeView->expandAll();
    delete model_;
    model_ = newModel;
    connect (model_, &QAbstractItemModel::dataChanged, this, &FN::nodeChanged);
    connect (model_, &DomModel::treeChanged, this, &FN::noteModified);
    connect (model_, &DomModel::treeChanged, this, &FN::docProp);
    connect (model_, &DomModel::treeChanged, this, &FN::closeTagsDialog);

    connect (model_, &DomModel::dragStarted, [this] (const QModelIndex &draggedIndex) {
        if (draggedIndex == ui->treeView->currentIndex())
        {
            treeViewDND_ = true;
            ui->treeView->setAutoScroll (false);
        }
    });
    connect (model_, &DomModel::droppedAtIndex, [this] (const QModelIndex &droppedIndex) {
        ui->treeView->setAutoScroll (true);
        ui->treeView->setCurrentIndex (droppedIndex);
        treeViewDND_ = false;
    });

    /* enable widgets */
    if (!ui->actionSaveAs->isEnabled())
        enableActions (true);
}
/*************************/
void FN::fileOpen (const QString &filePath)
{
    if (!filePath.isEmpty())
    {
        QFile file (filePath);
        if (file.open (QIODevice::ReadOnly))
        {
            QTextStream in (&file);
            QString cntnt = in.readAll();
            file.close();
            SimpleCrypt crypto (Q_UINT64_C(0xc9a25eb1610eb104));
            QString decrypted = crypto.decryptToString (cntnt);
            if (decrypted.isEmpty())
                decrypted = cntnt;
            QDomDocument document;
            if (document.setContent (decrypted))
            {
                QDomElement root = document.firstChildElement ("feathernotes");
                if (root.isNull()) return;
                pswrd_ = root.attribute ("pswrd");
                if (!pswrd_.isEmpty() && !isPswrdCorrect())
                    return;
                showDoc (document);
                xmlPath_ = filePath;
                setTitle (xmlPath_);
                docProp();
            }
        }
    }

    /* start the timer (again) if file
       opening is done or canceled */
    if (!xmlPath_.isEmpty() && autoSave_ >= 1)
        timer_->start (autoSave_ * 1000 * 60);
}
/*************************/
void FN::openFile()
{
    QObject *sender = QObject::sender();
    if (sender != nullptr && sender->objectName() == "trayOpen" && findChildren<QDialog *>().count() > 0)
    { // don't respond to the tray icon when there's a dialog
        raise();
        activateWindow();
        return;
    }
    closeTagsDialog();

    if (timer_->isActive()) timer_->stop();

    if (tray_)
    {
        if (underE_ && sender != nullptr && sender->objectName() == "trayOpen")
        {
            if (!isVisible())
            {
                activateTray();
                QCoreApplication::processEvents();
            }
            else
            {
                raise();
                activateWindow();
            }
        }
        else if (!underE_ && (!isVisible() || !isActiveWindow()))
        {
            activateTray();
            QCoreApplication::processEvents();
        }
    }

    if (!xmlPath_.isEmpty() && !QFile::exists (xmlPath_))
    {
        if (unSaved (false))
        {
            if (autoSave_ >= 1)
                timer_->start (autoSave_ * 1000 * 60);
            return;
        }
    }
    else if (saveNeeded_)
    {
        if (unSaved (true))
        {
            if (autoSave_ >= 1)
                timer_->start (autoSave_ * 1000 * 60);
            return;
        }
    }

    QString path;
    if (!xmlPath_.isEmpty())
    {
        if (QFile::exists (xmlPath_))
            path = xmlPath_;
        else
        {
            QDir dir = QFileInfo (xmlPath_).absoluteDir();
            if (!dir.exists())
                dir = QDir::home();
            path = dir.path();
        }
    }
    else
    {
        QDir dir = QDir::home();
        path = dir.path();
    }

    QString filePath;
    FileDialog dialog (this);
    dialog.setAcceptMode (QFileDialog::AcceptOpen);
    dialog.setWindowTitle (tr ("Open file..."));
    dialog.setFileMode (QFileDialog::ExistingFiles);
    dialog.setNameFilter (tr ("FeatherNotes documents (*.fnx);;All Files (*)"));
    if (QFileInfo (path).isDir())
        dialog.setDirectory (path);
    else
    {
        dialog.setDirectory (path.section ("/", 0, -2)); // workaround for KDE
        dialog.selectFile (path);
        dialog.autoScroll();
    }
    if (dialog.exec())
        filePath = dialog.selectedFiles().at (0);

    /* fileOpen() restarts auto-saving even when opening is canceled */
    fileOpen (filePath);
}
/*************************/
void FN::openFNDoc (const QString &filePath)
{
    if (filePath.isEmpty()) return; // impossible

    closeTagsDialog();

    if (timer_->isActive()) timer_->stop();

    if (!xmlPath_.isEmpty() && !QFile::exists (xmlPath_))
    {
        if (unSaved (false))
        {
            if (autoSave_ >= 1)
                timer_->start (autoSave_ * 1000 * 60);
            return;
        }
    }
    else if (saveNeeded_)
    {
        if (unSaved (true))
        {
            if (autoSave_ >= 1)
                timer_->start (autoSave_ * 1000 * 60);
            return;
        }
    }

    /* TextEdit::insertFromMimeData() should first return */
    QTimer::singleShot (0, this, [this, filePath] () {
        fileOpen (filePath);
        raise();
        activateWindow();
    });
}
/*************************/
void FN::dragMoveEvent (QDragMoveEvent *event)
{
    if (event->mimeData()->hasUrls())
    {
        const auto urls = event->mimeData()->urls();
        for (const QUrl &url : urls)
        {
            if (url.fileName().endsWith (".fnx"))
            {
                event->acceptProposedAction();
                return;
            }
            QMimeDatabase mimeDatabase;
            QMimeType mimeType = mimeDatabase.mimeTypeForFile (QFileInfo (url.toLocalFile()));
            if (mimeType.name() == "text/feathernotes-fnx")
            {
                event->acceptProposedAction();
                return;
            }
        }
    }
    event->ignore();
}
/*************************/
void FN::dragEnterEvent (QDragEnterEvent *event)
{
    if (event->mimeData()->hasUrls())
    {
        const auto urls = event->mimeData()->urls();
        for (const QUrl &url : urls)
        {
            if (url.fileName().endsWith (".fnx"))
            {
                event->acceptProposedAction();
                return;
            }
            QMimeDatabase mimeDatabase;
            QMimeType mimeType = mimeDatabase.mimeTypeForFile (QFileInfo (url.toLocalFile()));
            if (mimeType.name() == "text/feathernotes-fnx")
            {
                event->acceptProposedAction();
                return;
            }
        }
    }
    event->ignore();
}
/*************************/
void FN::dropEvent (QDropEvent *event)
{
    if (event->mimeData()->hasUrls())
    {
        const auto urls = event->mimeData()->urls();
        for (const QUrl &url : urls)
        {
            if (url.fileName().endsWith (".fnx"))
            {
                openFNDoc (url.path());
                break;
            }
            QMimeDatabase mimeDatabase;
            QMimeType mimeType = mimeDatabase.mimeTypeForFile (QFileInfo (url.toLocalFile()));
            if (mimeType.name() == "text/feathernotes-fnx")
            {
                openFNDoc (url.path());
                break;
            }
        }
    }

    event->acceptProposedAction();
}
/*************************/
void FN::autoSaving()
{
    if (xmlPath_.isEmpty() || saveNeeded_ == 0
        || !QFile::exists (xmlPath_)) // the file is deleted (but might be restored later)
    {
        return;
    }
    fileSave (xmlPath_);
}
/*************************/
void FN::notSaved()
{
    MessageBox msgBox (QMessageBox::Warning,
                       tr ("FeatherNotes"),
                       tr ("<center><b><big>Cannot be saved!</big></b></center>"),
                       QMessageBox::Close,
                       this);
    msgBox.changeButtonText (QMessageBox::Close, tr ("Close"));
    msgBox.exec();
}
/*************************/
void FN::setNodesTexts()
{
    /* first set the default font */
    QDomElement root = model_->domDocument.firstChildElement ("feathernotes");
    root.setAttribute ("txtfont", defaultFont_.toString());
    root.setAttribute ("nodefont", nodeFont_.toString());
    if (!pswrd_.isEmpty())
        root.setAttribute ("pswrd", pswrd_);
    else
        root.removeAttribute ("pswrd");

    QHash<DomItem*, TextEdit*>::iterator it;
    for (it = widgets_.begin(); it != widgets_.end(); ++it)
    {
        if (!it.value()->document()->isModified())
            continue;

        QString txt;
        /* don't write useless HTML code */
        if (!it.value()->toPlainText().isEmpty())
        {
            /* unzoom the text if it's zoomed */
            if (it.value()->document()->defaultFont() != defaultFont_)
            {
                QTextDocument *tempDoc = it.value()->document()->clone();
                tempDoc->setDefaultFont (defaultFont_);
                txt = tempDoc->toHtml();
                delete tempDoc;
            }
            else
                txt = it.value()->toHtml();
        }
        DomItem *item = it.key();
        QDomNodeList list = item->node().childNodes();

        if (list.isEmpty())
        {
            /* if this node doesn't have any child,
               append a text child node to it... */
            QDomText t = model_->domDocument.createTextNode (txt);
            item->node().appendChild (t);
        }
        else if (list.item (0).isElement())
        {
            /* ... but if its first child is an element node,
               insert the text node before that node... */
            QDomText t = model_->domDocument.createTextNode (txt);
            item->node().insertBefore (t, list.item (0));
        }
        else if (list.item (0).isText())
            /* ... finally, if this node's first child
               is a text node, replace its text */
            list.item (0).setNodeValue (txt);
    }
}
/*************************/
bool FN::saveFile()
{
    int index = ui->stackedWidget->currentIndex();
    if (index == -1) return false;

    QString fname = xmlPath_;

    if (fname.isEmpty() || !QFile::exists (fname))
    {
        if (fname.isEmpty())
        {
            QDir dir = QDir::home();
            fname = dir.filePath (tr ("Untitled") + ".fnx");
        }
        else
        {
            QDir dir = QFileInfo (fname).absoluteDir();
            if (!dir.exists())
                dir = QDir::home();
            /* add the file name */
            fname = dir.filePath (QFileInfo (fname).fileName());
        }

        /* use Save-As for Save or saving */
        if (QObject::sender() != ui->actionSaveAs)
        {
            FileDialog dialog (this);
            dialog.setAcceptMode (QFileDialog::AcceptSave);
            dialog.setWindowTitle (tr ("Save As..."));
            dialog.setFileMode (QFileDialog::AnyFile);
            dialog.setNameFilter (tr ("FeatherNotes documents (*.fnx);;All Files (*)"));
            dialog.setDirectory (fname.section ("/", 0, -2)); // workaround for KDE
            dialog.selectFile (fname);
            dialog.autoScroll();
            if (dialog.exec())
            {
                fname = dialog.selectedFiles().at (0);
                if (fname.isEmpty() || QFileInfo (fname).isDir())
                    return false;
            }
            else
                return false;
        }
    }

    if (QObject::sender() == ui->actionSaveAs)
    {
        FileDialog dialog (this);
        dialog.setAcceptMode (QFileDialog::AcceptSave);
        dialog.setWindowTitle (tr ("Save As..."));
        dialog.setFileMode (QFileDialog::AnyFile);
        dialog.setNameFilter (tr ("FeatherNotes documents (*.fnx);;All Files (*)"));
        dialog.setDirectory (fname.section ("/", 0, -2)); // workaround for KDE
        dialog.selectFile (fname);
        dialog.autoScroll();
        if (dialog.exec())
        {
            fname = dialog.selectedFiles().at (0);
            if (fname.isEmpty() || QFileInfo (fname).isDir())
                return false;
        }
        else
            return false;
    }

    if (!fileSave (fname))
    {
        notSaved();
        return false;
    }

    return true;
}
/*************************/
bool FN::fileSave (const QString &filePath)
{
    QFile outputFile (filePath);
    if (pswrd_.isEmpty())
    {
        if (outputFile.open (QIODevice::WriteOnly))
        {
            /* now, it's the time to set the nodes' texts */
            setNodesTexts();

            QTextStream outStream (&outputFile);
            model_->domDocument.save (outStream, 1);
            outputFile.close();

            xmlPath_ = filePath;
            setTitle (xmlPath_);
            QHash<DomItem*, TextEdit*>::iterator it;
            for (it = widgets_.begin(); it != widgets_.end(); ++it)
                it.value()->document()->setModified (false);
            if (saveNeeded_)
            {
                saveNeeded_ = 0;
                ui->actionSave->setEnabled (false);
                setWindowModified (false);
            }
            docProp();
        }
        else return false;
    }
    else
    {
        if (outputFile.open (QFile::WriteOnly))
        {
            setNodesTexts();
            SimpleCrypt crypto (Q_UINT64_C (0xc9a25eb1610eb104));
            QString encrypted = crypto.encryptToString (model_->domDocument.toString());

            QTextStream out (&outputFile);
            out << encrypted;
            outputFile.close();

            xmlPath_ = filePath;
            setTitle (xmlPath_);
            QHash<DomItem*, TextEdit*>::iterator it;
            for (it = widgets_.begin(); it != widgets_.end(); ++it)
                it.value()->document()->setModified (false);
            if (saveNeeded_)
            {
                saveNeeded_ = 0;
                ui->actionSave->setEnabled (false);
                setWindowModified (false);
            }
            docProp();
        }
        else return false;
    }

    return true;
}
/*************************/
void FN::undoing()
{
    QWidget *cw = ui->stackedWidget->currentWidget();
    if (!cw) return;

    /* remove green highlights */
    QHash<TextEdit*,QList<QTextEdit::ExtraSelection> >::iterator it;
    for (it = greenSels_.begin(); it != greenSels_.end(); ++it)
    {
        QList<QTextEdit::ExtraSelection> extraSelectionsIth;
        greenSels_[it.key()] = extraSelectionsIth;
        it.key()->setExtraSelections (QList<QTextEdit::ExtraSelection>());
    }

    qobject_cast< TextEdit *>(cw)->undo();
}
/*************************/
void FN::redoing()
{
    QWidget *cw = ui->stackedWidget->currentWidget();
    if (!cw) return;

    qobject_cast< TextEdit *>(cw)->redo();
}
/*************************/
void FN::cutText()
{
    QWidget *cw = ui->stackedWidget->currentWidget();
    if (!cw) return;

    qobject_cast< TextEdit *>(cw)->cut();
}
/*************************/
void FN::copyText()
{
    QWidget *cw = ui->stackedWidget->currentWidget();
    if (!cw) return;

    qobject_cast< TextEdit *>(cw)->copy();
}
/*************************/
void FN::pasteText()
{
    QWidget *cw = ui->stackedWidget->currentWidget();
    if (!cw) return;

    qobject_cast< TextEdit *>(cw)->paste();
}
/*************************/
void FN::pasteHTML()
{
    QWidget *cw = ui->stackedWidget->currentWidget();
    if (!cw) return;

    TextEdit *textEdit = qobject_cast< TextEdit *>(cw);
    textEdit->setAcceptRichText (true);
    textEdit->paste();
    textEdit->setAcceptRichText (false);
}
/*************************/
void FN::deleteText()
{
    QWidget *cw = ui->stackedWidget->currentWidget();
    if (!cw) return;

    TextEdit *textEdit = qobject_cast< TextEdit *>(cw);
    if (!textEdit->isReadOnly())
        textEdit->insertPlainText ("");
}
/*************************/
void FN::selectAllText()
{
    if (QWidget *cw = ui->stackedWidget->currentWidget())
        qobject_cast< TextEdit *>(cw)->selectAll();
}
/*************************/
TextEdit *FN::newWidget()
{
    TextEdit *textEdit = new TextEdit;
    textEdit->setScrollJumpWorkaround (scrollJumpWorkaround_);
    //textEdit->autoIndentation = true; // auto-indentation is enabled by default
    textEdit->autoBracket = autoBracket_;
    textEdit->autoReplace = autoReplace_;
    QPalette p = QApplication::palette();
    QColor hCol = p.color(QPalette::Active, QPalette::Highlight);
    QBrush brush = p.window();
    if (brush.color().value() <= 120)
    {
        if (236 - qGray (hCol.rgb()) < 30)
            textEdit->setStyleSheet ("QTextEdit {"
                                     "color: black;"
                                     "selection-color: black;"
                                     "selection-background-color: rgb(200, 200, 200);}");
        else
            textEdit->setStyleSheet ("QTextEdit {"
                                     "color: black;}");
        textEdit->viewport()->setStyleSheet (".QWidget {"
                                             "color: black;"
                                             "background-color: rgb(236, 236, 236);}");
    }
    else
    {
        if (255 - qGray (hCol.rgb()) < 30)
            textEdit->setStyleSheet ("QTextEdit {"
                                     "color: black;"
                                     "selection-color: black;"
                                     "selection-background-color: rgb(200, 200, 200);}");
        else
            textEdit->setStyleSheet ("QTextEdit {"
                                     "color: black;}");
        textEdit->viewport()->setStyleSheet (".QWidget {"
                                             "color: black;"
                                             "background-color: rgb(255, 255, 255);}");
    }
    textEdit->setAcceptRichText (false);
    textEdit->viewport()->setMouseTracking (true);
    textEdit->setContextMenuPolicy (Qt::CustomContextMenu);
    /* we want consistent widgets */
    textEdit->document()->setDefaultFont (defaultFont_);
#if (QT_VERSION >= QT_VERSION_CHECK(5,11,0))
    QFontMetricsF metrics (defaultFont_);
    textEdit->setTabStopDistance (4 * metrics.horizontalAdvance (' '));
#elif (QT_VERSION >= QT_VERSION_CHECK(5,10,0))
    QFontMetricsF metrics (defaultFont_);
    textEdit->setTabStopDistance (4 * metrics.width (' '));
#else
    QFontMetrics metrics (defaultFont_);
    textEdit->setTabStopWidth (4 * metrics.width (' '));
#endif

    int index = ui->stackedWidget->currentIndex();
    ui->stackedWidget->insertWidget (index + 1, textEdit);
    ui->stackedWidget->setCurrentWidget (textEdit);

    if (!ui->actionWrap->isChecked())
        textEdit->setLineWrapMode (QTextEdit::NoWrap);
    if (!ui->actionIndent->isChecked())
        textEdit->autoIndentation = false;

    connect (textEdit, &QTextEdit::copyAvailable, ui->actionCut, &QAction::setEnabled);
    connect (textEdit, &QTextEdit::copyAvailable, ui->actionCopy, &QAction::setEnabled);
    connect (textEdit, &QTextEdit::copyAvailable, ui->actionDelete, &QAction::setEnabled);
    connect (textEdit, &QTextEdit::copyAvailable, ui->actionLink, &QAction::setEnabled);
    connect (textEdit, &QTextEdit::copyAvailable, this, &FN::setCursorInsideSelection);
    connect (textEdit, &TextEdit::imageDropped, this, &FN::imageEmbed);
    connect (textEdit, &TextEdit::FNDocDropped, this, &FN::openFNDoc);
    connect (textEdit, &TextEdit::zoomedOut, this, &FN::rehighlight);
    connect (textEdit, &QWidget::customContextMenuRequested, this, &FN::txtContextMenu);
    /* The remaining connections to QTextEdit signals are in selChanged(). */

    /* I don't know why, under KDE, when a text is selected for the first time,
       it may not be copied to the selection clipboard. Perhaps it has something
       to do with Klipper. I neither know why the following line is a workaround
       but it can cause a long delay when FeatherNotes is started. */
    //QApplication::clipboard()->text (QClipboard::Selection);

    return textEdit;
}
/*************************/
// If some text is selected and the cursor is put somewhere inside
// the selection with mouse, Qt may not emit currentCharFormatChanged()
// when it should, as if the cursor isn't really set. The following method
// really sets the text cursor and can be used as a workaround for this bug.
void FN::setCursorInsideSelection (bool sel)
{
    if (!sel)
    {
        if (QWidget *cw = ui->stackedWidget->currentWidget())
        {
            TextEdit *textEdit = qobject_cast< TextEdit *>(cw);
            /* WARNING: Qt4 didn't need disconnecting. Why?! */
            disconnect (textEdit, &QTextEdit::copyAvailable, this, &FN::setCursorInsideSelection);
            QTextCursor cur = textEdit->textCursor();
            textEdit->setTextCursor (cur);
            connect (textEdit, &QTextEdit::copyAvailable, this, &FN::setCursorInsideSelection);
        }
    }
}
/*************************/
void FN::txtContextMenu (const QPoint &p)
{
    QWidget *cw = ui->stackedWidget->currentWidget();
    if (!cw) return;

    TextEdit *textEdit = qobject_cast< TextEdit *>(cw);
    QTextCursor cur = textEdit->textCursor();
    bool hasSel = cur.hasSelection();
    /* set the text cursor at the position of
       right clicking if there's no selection */
    if (!hasSel)
    {
        cur = textEdit->cursorForPosition (p);
        textEdit->setTextCursor (cur);
    }
    linkAtPos_ = textEdit->anchorAt (p);
    QMenu *menu = textEdit->createStandardContextMenu (p);
    bool sepAdded (false);

    const QList<QAction*> list = menu->actions();
    int copyIndx = -1, pasteIndx = -1;
    for (int i = 0; i < list.count(); ++i)
    {
        const auto thisAction = list.at (i);
        /* remove the shortcut strings because shortcuts may change */
        QString txt = thisAction->text();
        if (!txt.isEmpty())
            txt = txt.split ('\t').first();
        if (!txt.isEmpty())
            thisAction->setText (txt);
        /* find appropriate places for actionCopyLink and actionPasteHTML */
        if (thisAction->objectName() == "edit-copy")
            copyIndx = i;
        else if (thisAction->objectName() == "edit-paste")
            pasteIndx = i;
    }
    if (!linkAtPos_.isEmpty())
    {
        if (copyIndx > -1 && copyIndx + 1 < list.count())
            menu->insertAction (list.at (copyIndx + 1), ui->actionCopyLink);
        else
        {
            menu->addSeparator();
            menu->addAction (ui->actionCopyLink);
        }
    }
    if (pasteIndx > -1 && pasteIndx + 1 < list.count())
        menu->insertAction (list.at (pasteIndx + 1), ui->actionPasteHTML);
    else
    {
        menu->addAction (ui->actionPasteHTML);
        menu->addSeparator();
        sepAdded = true;
    }

    if (hasSel)
    {
        if (!sepAdded)
        {
            menu->addSeparator();
            sepAdded = true;
        }
        menu->addAction (ui->actionLink);
        if (isImageSelected())
        {
            menu->addSeparator();
            menu->addAction (ui->actionImageScale);
            menu->addAction (ui->actionImageSave);
        }
        menu->addSeparator();
    }
    if (!sepAdded) menu->addSeparator();
    menu->addAction (ui->actionEmbedImage);
    menu->addAction (ui->actionTable);
    txtTable_ = cur.currentTable();
    if (txtTable_)
    {
        menu->addSeparator();
        if (cur.hasComplexSelection())
            menu->addAction (ui->actionTableMergeCells);
        else
        {
            menu->addAction (ui->actionTablePrependRow);
            menu->addAction (ui->actionTableAppendRow);
            menu->addAction (ui->actionTablePrependCol);
            menu->addAction (ui->actionTableAppendCol);
            menu->addAction (ui->actionTableDeleteRow);
            menu->addAction (ui->actionTableDeleteCol);
        }
    }

    menu->exec (textEdit->viewport()->mapToGlobal (p));
    delete menu;
    txtTable_ = nullptr;
}
/*************************/
void FN::copyLink()
{
    QClipboard *clipboard = QApplication::clipboard();
    clipboard->setText (linkAtPos_);
}
/*************************/
void FN::selChanged (const QItemSelection &selected, const QItemSelection& /*deselected*/)
{
    if (selected.isEmpty()) // if the last node is closed
    {
        if (ui->lineEdit->isVisible())
            showHideSearch();
        if (ui->dockReplace->isVisible())
            replaceDock();
        enableActions (false);
        /*if (QWidget *widget = ui->stackedWidget->currentWidget())
            widget->setVisible (false);*/
        return;
    }
    /*else if (deselected.isEmpty()) // a row is selected after Ctrl + left click
    {
        if (QWidget *widget = ui->stackedWidget->currentWidget())
            widget->setVisible (true);
        enableActions (true);
    }*/

    if (treeViewDND_) return;

    /* if a widget is paired with this DOM item, show it;
       otherwise create a widget and pair it with the item */
    QModelIndex index = selected.indexes().at (0);
    TextEdit *textEdit = nullptr;
    bool found = false;
    QHash<DomItem*, TextEdit*>::iterator it;
    for (it = widgets_.begin(); it != widgets_.end(); ++it)
    {
        if (it.key() == static_cast<DomItem*>(index.internalPointer()))
        {
            found = true;
            break;
        }
    }
    if (found)
    {
        textEdit = it.value();
        ui->stackedWidget->setCurrentWidget (textEdit);
        QString txt = searchEntries_[textEdit];
        /* change the search entry's text only
           if the search isn't done in tags or names */
        if (!ui->tagsButton->isChecked()
            && !ui->namesButton->isChecked())
        {
            ui->lineEdit->setText (txt);
            if (!txt.isEmpty()) hlight();
        }
    }
    else
    {
        QString text;
        DomItem *item = static_cast<DomItem*>(index.internalPointer());
        QDomNodeList list = item->node().childNodes();
        text = list.item (0).nodeValue();
        /* this is needed for text zooming */
        QRegularExpressionMatch match;
        QRegularExpression regex (R"(^<!DOCTYPE[A-Za-z0-9/<>,;.:\-={}\s"]+</style></head><body\sstyle=[A-Za-z0-9/<>;:\-\s"']+>)");
        if (text.indexOf (regex, 0, &match) > -1)
        {
            QString str = "<!DOCTYPE HTML PUBLIC \"-//W3C//DTD HTML 4.0//EN\" \"http://www.w3.org/TR/REC-html40/strict.dtd\">\n"
                          "<html><head><meta name=\"qrichtext\" content=\"1\" /><style type=\"text/css\">\n"
                          "p, li { white-space: pre-wrap; }\n"
                          "</style></head><body>";
            text.replace (0, match.capturedLength(), str);
        }
        textEdit = newWidget();
        textEdit->setHtml (text);

        connect (textEdit->document(), &QTextDocument::modificationChanged, this, &FN::setSaveEnabled);
        connect (textEdit->document(), &QTextDocument::undoAvailable, this, &FN::setUndoEnabled);
        connect (textEdit->document(), &QTextDocument::redoAvailable, this, &FN::setRedoEnabled);
        connect (textEdit, &QTextEdit::currentCharFormatChanged, this, &FN::formatChanged);
        connect (textEdit, &QTextEdit::cursorPositionChanged, this, &FN::alignmentChanged);
        connect (textEdit, &QTextEdit::cursorPositionChanged, this, &FN::directionChanged);

        /* focus the text widget only if
           a document is opened just now */
        if (widgets_.isEmpty())
            textEdit->setFocus();

        widgets_[static_cast<DomItem*>(index.internalPointer())] = textEdit;
        searchEntries_[textEdit] = QString();
        greenSels_[textEdit] = QList<QTextEdit::ExtraSelection>();
        if (!ui->tagsButton->isChecked()
            && !ui->namesButton->isChecked())
        {
            ui->lineEdit->setText (QString());
        }
    }

    ui->actionUndo->setEnabled (textEdit->document()->isUndoAvailable());
    ui->actionRedo->setEnabled (textEdit->document()->isRedoAvailable());

    bool textIsSelected = textEdit->textCursor().hasSelection();
    ui->actionCopy->setEnabled (textIsSelected);
    ui->actionCut->setEnabled (textIsSelected);
    ui->actionDelete->setEnabled (textIsSelected);
    ui->actionLink->setEnabled (textIsSelected);

    formatChanged (textEdit->currentCharFormat());
    alignmentChanged();
    directionChanged();
}
/*************************/
void FN::setSaveEnabled (bool modified)
{
    if (modified)
        noteModified();
    else
    {
        if (saveNeeded_)
            --saveNeeded_;
        if (saveNeeded_ == 0)
        {
            ui->actionSave->setEnabled (false);
            setWindowModified (false);
        }
    }
}
/*************************/
void FN::setUndoEnabled (bool enabled)
{
    ui->actionUndo->setEnabled (enabled);
}
/*************************/
void FN::setRedoEnabled (bool enabled)
{
    ui->actionRedo->setEnabled (enabled);
}
/*************************/
void FN::formatChanged (const QTextCharFormat &format)
{
    ui->actionSuper->setChecked (format.verticalAlignment() == QTextCharFormat::AlignSuperScript ?
                                 true : false);
    ui->actionSub->setChecked (format.verticalAlignment() == QTextCharFormat::AlignSubScript ?
                               true : false);

    if (format.fontWeight() == QFont::Bold)
        ui->actionBold->setChecked (true);
    else
        ui->actionBold->setChecked (false);
    ui->actionItalic->setChecked (format.fontItalic());
    ui->actionUnderline->setChecked (format.fontUnderline());
    ui->actionStrike->setChecked (format.fontStrikeOut());
}
/*************************/
void FN::alignmentChanged()
{
    QWidget *cw = ui->stackedWidget->currentWidget();
    if (!cw) return;

    Qt::Alignment a = qobject_cast< TextEdit *>(cw)->alignment();
    if (a & Qt::AlignLeft)
    {
        if (a & Qt::AlignAbsolute)
            ui->actionLeft->setChecked (true);
        else // get alignment from text layout direction
        {
            QTextCursor cur = qobject_cast< TextEdit *>(cw)->textCursor();
            QTextBlockFormat fmt = cur.blockFormat();
            if (fmt.layoutDirection() == Qt::LeftToRight)
                ui->actionLeft->setChecked (true);
            else if (fmt.layoutDirection() == Qt::RightToLeft)
                ui->actionRight->setChecked (true);
            else // Qt::LayoutDirectionAuto
            {
                /* textDirection() returns either Qt::LeftToRight
                   or Qt::RightToLeft (-> qtextobject.cpp) */
                QTextBlock blk = cur.block();
                if (blk.textDirection() == Qt::LeftToRight)
                    ui->actionLeft->setChecked (true);
                else
                    ui->actionRight->setChecked (true);
            }
        }
    }
    else if (a & Qt::AlignHCenter)
        ui->actionCenter->setChecked (true);
    else if (a & Qt::AlignRight)
    {
        if (a & Qt::AlignAbsolute)
            ui->actionRight->setChecked (true);
        else // get alignment from text layout direction
        {
            QTextCursor cur = qobject_cast< TextEdit *>(cw)->textCursor();
            QTextBlockFormat fmt = cur.blockFormat();
            if (fmt.layoutDirection() == Qt::RightToLeft)
                ui->actionRight->setChecked (true);
            else if (fmt.layoutDirection() == Qt::LeftToRight)
                ui->actionLeft->setChecked (true);
            else // Qt::LayoutDirectionAuto
            {
                QTextBlock blk = cur.block();
                if (blk.textDirection() == Qt::LeftToRight)
                    ui->actionLeft->setChecked (true);
                else
                    ui->actionRight->setChecked (true);
            }
        }
    }
    else if (a & Qt::AlignJustify)
        ui->actionJust->setChecked (true);
}
/*************************/
void FN::directionChanged()
{
    QWidget *cw = ui->stackedWidget->currentWidget();
    if (!cw) return;

    QTextCursor cur = qobject_cast< TextEdit *>(cw)->textCursor();
    QTextBlockFormat fmt = cur.blockFormat();
    if (fmt.layoutDirection() == Qt::LeftToRight)
        ui->actionLTR->setChecked (true);
    else if (fmt.layoutDirection() == Qt::RightToLeft)
        ui->actionRTL->setChecked (true);
    else // Qt::LayoutDirectionAuto
    {
        QTextBlock blk = cur.block();
        if (blk.textDirection() == Qt::LeftToRight)
            ui->actionLTR->setChecked (true);
        else
            ui->actionRTL->setChecked (true);
    }
}
/*************************/
void FN::mergeFormatOnWordOrSelection (const QTextCharFormat &format)
{
    QWidget *cw = ui->stackedWidget->currentWidget();
    if (!cw) return;

    TextEdit *textEdit = qobject_cast< TextEdit *>(cw);
    QTextCursor cursor = textEdit->textCursor();
    if (!cursor.hasSelection())
        cursor.select (QTextCursor::WordUnderCursor);
    cursor.mergeCharFormat (format);
    /* correct the pressed states of the format buttons if necessary */
    formatChanged (textEdit->currentCharFormat());
}
/*************************/
void FN::makeBold()
{
    QTextCharFormat fmt;
    fmt.setFontWeight (ui->actionBold->isChecked() ? QFont::Bold : QFont::Normal);
    mergeFormatOnWordOrSelection (fmt);
}
/*************************/
void FN::makeItalic()
{
    QTextCharFormat fmt;
    fmt.setFontItalic (ui->actionItalic->isChecked());
    mergeFormatOnWordOrSelection (fmt);
}
/*************************/
void FN::makeUnderlined()
{
    QTextCharFormat fmt;
    fmt.setFontUnderline (ui->actionUnderline->isChecked());
    mergeFormatOnWordOrSelection (fmt);
}
/*************************/
void FN::makeStriked()
{
    QTextCharFormat fmt;
    fmt.setFontStrikeOut (ui->actionStrike->isChecked());
    mergeFormatOnWordOrSelection (fmt);
}
/*************************/
void FN::makeSuperscript()
{
    QTextCharFormat fmt;
    fmt.setVerticalAlignment (ui->actionSuper->isChecked() ?
                              QTextCharFormat::AlignSuperScript :
                              QTextCharFormat::AlignNormal);
    mergeFormatOnWordOrSelection (fmt);
}
/*************************/
void FN::makeSubscript()
{
    QTextCharFormat fmt;
    fmt.setVerticalAlignment (ui->actionSub->isChecked() ?
                              QTextCharFormat::AlignSubScript :
                              QTextCharFormat::AlignNormal);
    mergeFormatOnWordOrSelection (fmt);
}
/*************************/
void FN::textColor()
{
    QWidget *cw = ui->stackedWidget->currentWidget();
    if (!cw) return;

    QColor color;
    if ((color = qobject_cast< TextEdit *>(cw)->textColor())
        == QColor (Qt::black))
    {
        if (lastTxtColor_.isValid())
            color = lastTxtColor_;
    }
    color = QColorDialog::getColor (color,
                                    this,
                                    tr ("Select Text Color"));
    if (!color.isValid()) return;
    lastTxtColor_ = color;
    QTextCharFormat fmt;
    fmt.setForeground (color);
    mergeFormatOnWordOrSelection (fmt);
}
/*************************/
void FN::bgColor()
{
    QWidget *cw = ui->stackedWidget->currentWidget();
    if (!cw) return;

    QColor color;
    if ((color = qobject_cast< TextEdit *>(cw)->textBackgroundColor())
        == QColor (Qt::black))
    {
        if (lastBgColor_.isValid())
            color = lastBgColor_;
    }
    color = QColorDialog::getColor (color,
                                    this,
                                    tr ("Select Background Color"));
    if (!color.isValid()) return;
    lastBgColor_ = color;
    QTextCharFormat fmt;
    fmt.setBackground (color);
    mergeFormatOnWordOrSelection (fmt);
}
/*************************/
void FN::clearFormat()
{
    QWidget *cw = ui->stackedWidget->currentWidget();
    if (!cw) return;

    QTextCursor cur = qobject_cast< TextEdit *>(cw)->textCursor();
    if (!cur.hasSelection())
        cur.select (QTextCursor::WordUnderCursor);
    cur.setCharFormat (QTextCharFormat());
}
/*************************/
void FN::textAlign (QAction *a)
{
    QWidget *cw = ui->stackedWidget->currentWidget();
    if (!cw) return;

    TextEdit *textEdit = qobject_cast< TextEdit *>(cw);
    if (a == ui->actionLeft)
        textEdit->setAlignment (Qt::Alignment (Qt::AlignLeft | Qt::AlignAbsolute));
    else if (a == ui->actionCenter)
        textEdit->setAlignment (Qt::AlignHCenter);
    else if (a == ui->actionRight)
        textEdit->setAlignment (Qt::Alignment (Qt::AlignRight | Qt::AlignAbsolute));
    else if (a == ui->actionJust)
        textEdit->setAlignment (Qt::AlignJustify);
}
/*************************/
void FN::textDirection (QAction *a)
{
    QWidget *cw = ui->stackedWidget->currentWidget();
    if (!cw) return;

    QTextBlockFormat fmt;
    if (a == ui->actionLTR)
        fmt.setLayoutDirection (Qt::LeftToRight);
    else if (a == ui->actionRTL)
        fmt.setLayoutDirection (Qt::RightToLeft);

    QTextCursor cur = qobject_cast< TextEdit *>(cw)->textCursor();
    if (!cur.hasSelection())
        cur.select (QTextCursor::WordUnderCursor);
    cur.mergeBlockFormat (fmt);

    alignmentChanged();
}
/*************************/
void FN::makeHeader()
{
    int index = ui->stackedWidget->currentIndex();
    if (index == -1) return;

    QTextCharFormat fmt;
    if (QObject::sender() == ui->actionH3)
        fmt.setProperty (QTextFormat::FontSizeAdjustment, 1);
    else if (QObject::sender() == ui->actionH2)
        fmt.setProperty (QTextFormat::FontSizeAdjustment, 2);
    else// if (QObject::sender() == ui->actionH1)
        fmt.setProperty (QTextFormat::FontSizeAdjustment, 3);
    mergeFormatOnWordOrSelection (fmt);
}
/*************************/
void FN::expandAll()
{
    ui->treeView->expandAll();
}
/*************************/
void FN::collapseAll()
{
    ui->treeView->collapseAll();
}
/*************************/
void FN::newNode()
{
    closeTagsDialog();

    QModelIndex index = ui->treeView->currentIndex();
    if (QObject::sender() == ui->actionNewSibling)
    {
        QModelIndex pIndex = model_->parent (index);
        model_->insertRow (index.row() + 1, pIndex);
    }
    else if (QObject::sender() == ui->actionPrepSibling)
    {
        QModelIndex pIndex = model_->parent (index);
        model_->insertRow (index.row(), pIndex);
    }
    else //if (QObject::sender() == ui->actionNewChild)
    {
        model_->insertRow (model_->rowCount (index), index);
        ui->treeView->expand (index);
    }
}
/*************************/
void FN::deleteNode()
{
    closeTagsDialog();

    MessageBox msgBox;
    msgBox.setIcon (QMessageBox::Question);
    msgBox.setWindowTitle (tr ("Deletion"));
    msgBox.setText (tr ("<center><b><big>Delete this node?</big></b></center>"));
    msgBox.setInformativeText (tr ("<center><b><i>Warning!</i></b></center>\n<center>This action cannot be undone.</center>"));
    msgBox.setStandardButtons (QMessageBox::Yes | QMessageBox::No);
    msgBox.changeButtonText (QMessageBox::Yes, tr ("Yes"));
    msgBox.changeButtonText (QMessageBox::No, tr ("No"));
    msgBox.setDefaultButton (QMessageBox::No);
    msgBox.show();
    msgBox.move (x() + width()/2 - msgBox.width()/2,
                 y() + height()/2 - msgBox.height()/ 2);
    switch (msgBox.exec()) {
    case QMessageBox::Yes:
        break;
    case QMessageBox::No:
    default:
        return;
    }

    QModelIndex index = ui->treeView->currentIndex();

    /* remove all widgets paired with
       this node or its descendants */
    QModelIndexList list = model_->allDescendants (index);
    list << index;
    for (int i = 0; i < list.count(); ++i)
    {
        QHash<DomItem*, TextEdit*>::iterator it = widgets_.find (static_cast<DomItem*>(list.at (i).internalPointer()));
        if (it != widgets_.end())
        {
            TextEdit *textEdit = it.value();
            if (saveNeeded_ && textEdit->document()->isModified())
                --saveNeeded_;
            searchEntries_.remove (textEdit);
            greenSels_.remove (textEdit);
            ui->stackedWidget->removeWidget (textEdit);
            delete textEdit;
            widgets_.remove (it.key());
        }
    }

    /* now, really remove the node */
    QModelIndex pIndex = model_->parent (index);
    model_->removeRow (index.row(), pIndex);
}
/*************************/
void FN::moveUpNode()
{
    closeTagsDialog();

    QModelIndex index = ui->treeView->currentIndex();
    QModelIndex pIndex = model_->parent (index);

    if (index.row() == 0) return;

    model_->moveUpRow (index.row(), pIndex);
}
/*************************/
void FN::moveLeftNode()
{
    closeTagsDialog();

    QModelIndex index = ui->treeView->currentIndex();
    QModelIndex pIndex = model_->parent (index);

    if (!pIndex.isValid()) return;

    model_->moveLeftRow (index.row(), pIndex);
}
/*************************/
void FN::moveDownNode()
{
    closeTagsDialog();

    QModelIndex index = ui->treeView->currentIndex();
    QModelIndex pIndex = model_->parent (index);

    if (index.row() == model_->rowCount (pIndex) - 1) return;

    model_->moveDownRow (index.row(), pIndex);
}
/*************************/
void FN::moveRightNode()
{
    closeTagsDialog();

    QModelIndex index = ui->treeView->currentIndex();
    QModelIndex pIndex = model_->parent (index);

    if (index.row() == 0) return;

    model_->moveRightRow (index.row(), pIndex);
}
/*************************/
// Add or edit tags.
void FN::handleTags()
{
    QModelIndex index = ui->treeView->currentIndex();
    DomItem *item = static_cast<DomItem*>(index.internalPointer());
    QDomNode node = item->node();
    QDomNamedNodeMap attributeMap = node.attributes();
    QString tags = attributeMap.namedItem ("tag").nodeValue();

    QDialog *dialog = new QDialog (this);
    dialog->setWindowTitle (tr ("Tags"));
    QGridLayout *grid = new QGridLayout;
    grid->setSpacing (5);
    grid->setContentsMargins (5, 5, 5, 5);

    LineEdit *lineEdit = new LineEdit();
    lineEdit->returnOnClear = false;
    lineEdit->setMinimumWidth (250);
    lineEdit->setText (tags);
    lineEdit->setToolTip ("<p style='white-space:pre'>"
                          + tr ("Tag(s) for this node")
                          + "</p>");
    connect (lineEdit, &QLineEdit::returnPressed, dialog, &QDialog::accept);
    QSpacerItem *spacer = new QSpacerItem (1, 5);
    QPushButton *cancelButton = new QPushButton (symbolicIcon::icon (":icons/dialog-cancel.svg"), tr ("Cancel"));
    QPushButton *okButton = new QPushButton (symbolicIcon::icon (":icons/dialog-ok.svg"), tr ("OK"));
    connect (cancelButton, &QAbstractButton::clicked, dialog, &QDialog::reject);
    connect (okButton, &QAbstractButton::clicked, dialog, &QDialog::accept);

    grid->addWidget (lineEdit, 0, 0, 1, 3);
    grid->addItem (spacer, 1, 0);
    grid->addWidget (cancelButton, 2, 1, Qt::AlignRight);
    grid->addWidget (okButton, 2, 2, Qt::AlignCenter);
    grid->setColumnStretch (0, 1);
    grid->setRowStretch (1, 1);

    dialog->setLayout (grid);
    /*dialog->resize (dialog->minimumWidth(),
                    dialog->minimumHeight());*/
    /*dialog->show();
    dialog->move (x() + width()/2 - dialog->width(),
                  y() + height()/2 - dialog->height());*/

    QString newTags;
    switch (dialog->exec()) {
    case QDialog::Accepted:
        newTags = lineEdit->text();
        delete dialog;
        break;
    case QDialog::Rejected:
    default:
        delete dialog;
        return;
    }

    if (newTags != tags)
    {
        closeTagsDialog();

        if (newTags.isEmpty())
            node.toElement().removeAttribute ("tag");
        else
            node.toElement().setAttribute ("tag", newTags);

        noteModified();
    }
}
/*************************/
void FN::renameNode()
{
    ui->treeView->edit (ui->treeView->currentIndex());
}
/*************************/
void FN::nodeIcon()
{
    QDialog *dlg = new QDialog (this);
    dlg->setWindowTitle (tr ("Node Icon"));
    QGridLayout *grid = new QGridLayout;
    grid->setSpacing (5);
    dlg->setContentsMargins (5, 5, 5, 5);

    LineEdit *ImagePathEntry = new LineEdit();
    ImagePathEntry->returnOnClear = false;
    ImagePathEntry->setMinimumWidth (200);
    ImagePathEntry->setToolTip (tr ("Image path"));
    connect (ImagePathEntry, &QLineEdit::returnPressed, dlg, &QDialog::accept);
    QToolButton *openBtn = new QToolButton();
    openBtn->setIcon (symbolicIcon::icon (":icons/document-open.svg"));
    openBtn->setToolTip (tr ("Open image"));
    connect (openBtn, &QAbstractButton::clicked, dlg, [=] {
        QString path;
        if (!xmlPath_.isEmpty())
        {
            QDir dir = QFileInfo (xmlPath_).absoluteDir();
            if (!dir.exists())
                dir = QDir::home();
            path = dir.path();
        }
        else
        {
            QDir dir = QDir::home();
            path = dir.path();
        }
        QString file;
        FileDialog dialog (this);
        dialog.setAcceptMode (QFileDialog::AcceptOpen);
        dialog.setWindowTitle (tr ("Open Image..."));
        dialog.setFileMode (QFileDialog::ExistingFiles);
        dialog.setNameFilter (tr ("Image Files (*.svg *.png *.jpg *.jpeg *.bmp *.gif);;All Files (*)"));
        dialog.setDirectory (path);
        if (dialog.exec())
        {
            QStringList files = dialog.selectedFiles();
            if (files.count() > 0)
                file = files.at (0);
        }
        ImagePathEntry->setText (file);
    });
    QSpacerItem *spacer = new QSpacerItem (1, 10, QSizePolicy::Minimum, QSizePolicy::MinimumExpanding);
    QPushButton *cancelButton = new QPushButton (symbolicIcon::icon (":icons/dialog-cancel.svg"), tr ("Cancel"));
    QPushButton *okButton = new QPushButton (symbolicIcon::icon (":icons/dialog-ok.svg"), tr ("OK"));
    connect (cancelButton, &QAbstractButton::clicked, dlg, &QDialog::reject);
    connect (okButton, &QAbstractButton::clicked, dlg, &QDialog::accept);

    grid->addWidget (ImagePathEntry, 0, 0, 1, 4);
    grid->addWidget (openBtn, 0, 4, Qt::AlignCenter);
    grid->addItem (spacer, 1, 0);
    grid->addWidget (cancelButton, 2, 2, Qt::AlignRight);
    grid->addWidget (okButton, 2, 3, 1, 2, Qt::AlignCenter);
    grid->setColumnStretch (1, 1);

    dlg->setLayout (grid);
    dlg->resize (dlg->sizeHint());

    QString imagePath;
    switch (dlg->exec()) {
    case QDialog::Accepted:
        imagePath = ImagePathEntry->text();
        delete dlg;
        break;
    case QDialog::Rejected:
    default:
        delete dlg;
        return;
    }

    QModelIndex index = ui->treeView->currentIndex();
    DomItem *item = static_cast<DomItem*>(index.internalPointer());
    QDomNode node = item->node();
    QString curIcn = node.toElement().attribute ("icon");

    if (imagePath.isEmpty())
    {
        if (!curIcn.isEmpty())
        {
            node.toElement().removeAttribute ("icon");
            emit ui->treeView->dataChanged (index, index);
            noteModified();
        }
    }
    else
    {
        QFile file (imagePath);
        if (file.open (QIODevice::ReadOnly))
        {
            QDataStream in (&file);
            QByteArray rawarray;
            QDataStream datastream (&rawarray, QIODevice::WriteOnly);
            char a;
            while (in.readRawData (&a, 1) != 0)
                datastream.writeRawData (&a, 1);
            file.close();
            QByteArray base64array = rawarray.toBase64();
            const QString icn = QString (base64array);

            if (curIcn != icn)
            {
                node.toElement().setAttribute ("icon", icn);
                emit ui->treeView->dataChanged (index, index);
                noteModified();
            }
        }
    }
}
/*************************/
void FN::toggleStatusBar()
{
    if (ui->statusBar->isVisible())
    {
        QLabel *statusLabel = ui->statusBar->findChild<QLabel *>();
        ui->statusBar->removeWidget (statusLabel);
        if (statusLabel)
        {
            delete statusLabel;
            statusLabel = nullptr;
        }
        ui->statusBar->setVisible (false);
        return;
    }

    int rows = model_->rowCount();
    int allNodes = 0;
    if (rows > 0)
    {
        QModelIndex indx = model_->index (0, 0, QModelIndex());
        while ((indx = model_->adjacentIndex (indx, true)).isValid())
            ++allNodes;
        ++allNodes;
    }
    QLabel *statusLabel = new QLabel();
    statusLabel->setTextInteractionFlags (Qt::TextSelectableByMouse);
    if (xmlPath_.isEmpty())
    {
        statusLabel->setText (tr ("<b>Main nodes:</b> <i>%1</i>"
                                  "&nbsp;&nbsp;&nbsp;&nbsp;<b>All nodes:</b> <i>%2</i>")
                              .arg (rows).arg (allNodes));
    }
    else
    {
        statusLabel->setText (tr ("<b>Note:</b> <i>%1</i><br>"
                                  "<b>Main nodes:</b> <i>%2</i>"
                                  "&nbsp;&nbsp;&nbsp;&nbsp;<b>All nodes:</b> <i>%3</i>")
                              .arg (xmlPath_).arg (rows).arg (allNodes));
    }
    ui->statusBar->addWidget (statusLabel);
    ui->statusBar->setVisible (true);
}
/*************************/
void FN::docProp()
{
    if (!ui->statusBar->isVisible()) return;

    QList<QLabel *> list = ui->statusBar->findChildren<QLabel*>();
    if (list.isEmpty()) return;
    QLabel *statusLabel = list.at (0);
    int rows = model_->rowCount();
    int allNodes = 0;
    if (rows > 0)
    {
        QModelIndex indx = model_->index (0, 0, QModelIndex());
        while ((indx = model_->adjacentIndex (indx, true)).isValid())
            ++allNodes;
        ++allNodes;
    }
    if (xmlPath_.isEmpty())
    {
        statusLabel->setText (tr ("<b>Main nodes:</b> <i>%1</i>"
                                  "&nbsp;&nbsp;&nbsp;&nbsp;<b>All nodes:</b> <i>%2</i>")
                              .arg (rows).arg (allNodes));
    }
    else
    {
        statusLabel->setText (tr ("<b>Note:</b> <i>%1</i><br>"
                                  "<b>Main nodes:</b> <i>%2</i>"
                                  "&nbsp;&nbsp;&nbsp;&nbsp;<b>All nodes:</b> <i>%3</i>")
                              .arg (xmlPath_).arg (rows).arg (allNodes));
    }
}
/*************************/
void FN::setNewFont (DomItem *item, QTextCharFormat &fmt)
{
    QString text;
    QDomNodeList list = item->node().childNodes();
    if (!list.item (0).isText()) return;
    text = list.item (0).nodeValue();
    if (!text.startsWith ("<!DOCTYPE HTML PUBLIC"))
        return;

    TextEdit *textEdit = new TextEdit;
    /* body font */
    textEdit->document()->setDefaultFont (defaultFont_);
    textEdit->setHtml (text);

    /* paragraph font, merged with body font */
    QTextCursor cursor = textEdit->textCursor();
    cursor.select (QTextCursor::Document);
    cursor.mergeCharFormat (fmt);

    text = textEdit->toHtml();
    list.item (0).setNodeValue (text);

    delete textEdit;
}
/*************************/
void FN::textFontDialog()
{
    bool ok;
    QFont newFont = QFontDialog::getFont (&ok, defaultFont_, this,
                                          tr ("Select Document Font"));
    if (ok)
    {
        QFont font (newFont.family(), newFont.pointSize());
        defaultFont_ = font;

        noteModified();

        QTextCharFormat fmt;
        fmt.setFontFamily (defaultFont_.family());
        fmt.setFontPointSize (defaultFont_.pointSize());

        /* change the font for all shown nodes */
        QHash<DomItem*, TextEdit*>::iterator it;
        for (it = widgets_.begin(); it != widgets_.end(); ++it)
        {
            it.value()->document()->setDefaultFont (defaultFont_);
#if (QT_VERSION >= QT_VERSION_CHECK(5,11,0))
            QFontMetricsF metrics (defaultFont_);
            it.value()->setTabStopDistance (4 * metrics.horizontalAdvance (' '));
#elif (QT_VERSION >= QT_VERSION_CHECK(5,10,0))
            QFontMetricsF metrics (defaultFont_);
            it.value()->setTabStopDistance (4 * metrics.width (' '));
#else
            QFontMetrics metrics (defaultFont_);
            it.value()->setTabStopWidth (4 * metrics.width (' '));
#endif
        }

        /* also change the font for all nodes,
           that aren't shown yet */
        for (int i = 0; i < model_->rowCount (QModelIndex()); ++i)
        {
            QModelIndex index = model_->index (i, 0, QModelIndex());
            DomItem *item = static_cast<DomItem*>(index.internalPointer());
            if (!widgets_.contains (item))
                setNewFont (item, fmt);
            QModelIndexList list = model_->allDescendants (index);
            for (int j = 0; j < list.count(); ++j)
            {
                item = static_cast<DomItem*>(list.at (j).internalPointer());
                if (!widgets_.contains (item))
                    setNewFont (item, fmt);
            }
        }

        /* rehighlight found matches for this node
           because the font may be smaller now */
        if (QWidget *cw = ui->stackedWidget->currentWidget())
        {
            TextEdit *textEdit = qobject_cast< TextEdit *>(cw);
            rehighlight (textEdit);
        }
    }
}
/*************************/
void FN::nodeFontDialog()
{
    bool ok;
    QFont newFont = QFontDialog::getFont (&ok, nodeFont_, this,
                                          tr ("Select Node Font"));
    if (ok)
    {
        nodeFont_ = newFont;
        noteModified();
        ui->treeView->setFont (nodeFont_);
    }
}
/*************************/
void FN::noteModified()
{
    if (model_->rowCount() == 0) // if the last node is closed
    {
        ui->actionSave->setEnabled (false);
        setWindowModified (false);
    }
    else
    {
        if (saveNeeded_ == 0)
        {
            ui->actionSave->setEnabled (true);
            setWindowModified (true);
        }
        ++saveNeeded_;
    }
}
/*************************/
void FN::nodeChanged (const QModelIndex&, const QModelIndex&)
{
    noteModified();
}
/*************************/
void FN::showHideSearch()
{
    bool visibility = ui->lineEdit->isVisible();

    if (QObject::sender() == ui->actionFind
        && visibility && !ui->lineEdit->hasFocus())
    {
        ui->lineEdit->setFocus();
        ui->lineEdit->selectAll();
        return;
    }

    ui->lineEdit->setVisible (!visibility);
    ui->nextButton->setVisible (!visibility);
    ui->prevButton->setVisible (!visibility);
    ui->caseButton->setVisible (!visibility);
    ui->wholeButton->setVisible (!visibility);
    ui->everywhereButton->setVisible (!visibility);
    ui->tagsButton->setVisible (!visibility);
    ui->namesButton->setVisible (!visibility);

    if (!visibility)
        ui->lineEdit->setFocus();
    else
    {
        ui->dockReplace->setVisible (false); // no replace dock without searchbar
        if (QWidget *cw = ui->stackedWidget->currentWidget())
        {
            /* return focus to the document */
            qobject_cast< TextEdit *>(cw)->setFocus();
            /* cancel search */
            QHash<TextEdit*,QString>::iterator it;
            for (it = searchEntries_.begin(); it != searchEntries_.end(); ++it)
            {
                ui->lineEdit->setText (QString());
                it.value() = QString();
                disconnect (it.key()->verticalScrollBar(), &QAbstractSlider::valueChanged, this, &FN::scrolled);
                disconnect (it.key()->horizontalScrollBar(), &QAbstractSlider::valueChanged, this, &FN::scrolled);
                disconnect (it.key(), &TextEdit::resized, this, &FN::hlight);
                disconnect (it.key(), &QTextEdit::textChanged, this, &FN::hlight);
                QList<QTextEdit::ExtraSelection> extraSelections;
                greenSels_[it.key()] = extraSelections;
                it.key()->setExtraSelections (extraSelections);
            }
            ui->everywhereButton->setChecked (false);
            ui->tagsButton->setChecked (false);
            ui->namesButton->setChecked (false);
        }
    }
}
/*************************/
void FN::findInNames()
{
    QString txt = ui->lineEdit->text();
    if (txt.isEmpty()) return;

    QModelIndex indx = ui->treeView->currentIndex();
    bool down = true;
    bool found = false;
    if (QObject::sender() == ui->prevButton)
        down = false;
    Qt::CaseSensitivity cs = Qt::CaseInsensitive;
    if (ui->caseButton->isChecked())
        cs = Qt::CaseSensitive;
    QRegularExpression regex;
    if (ui->wholeButton->isChecked())
    {
        if (cs == Qt::CaseInsensitive)
            regex.setPatternOptions (QRegularExpression::CaseInsensitiveOption);
        regex.setPattern (QString ("\\b%1\\b").arg (QRegularExpression::escape (txt)));
        while ((indx = model_->adjacentIndex (indx, down)).isValid())
        {
            if (model_->data (indx, Qt::DisplayRole).toString().indexOf (regex) != -1)
            {
                found = true;
                break;
            }
        }
    }
    else
    {
        while ((indx = model_->adjacentIndex (indx, down)).isValid())
        {
            if (model_->data (indx, Qt::DisplayRole).toString().contains (txt, cs))
            {
                found = true;
                break;
            }
        }
    }

    /* if nothing is found, search again from the first/last index to the current index */
    if (!indx.isValid())
    {
        if (down)
            indx = model_->index (0, 0);
        else
            indx = model_->index (model_->rowCount() - 1, 0);
        if (indx == ui->treeView->currentIndex())
            return;

        if (ui->wholeButton->isChecked())
        {
            if (model_->data (indx, Qt::DisplayRole).toString().indexOf (regex) != -1)
                found = true;
            else
            {
                while ((indx = model_->adjacentIndex (indx, down)).isValid())
                {
                    if (indx == ui->treeView->currentIndex())
                        return;
                    if (model_->data (indx, Qt::DisplayRole).toString().indexOf (regex) != -1)
                    {
                        found = true;
                        break;
                    }
                }
            }
        }
        else
        {
            if (model_->data (indx, Qt::DisplayRole).toString().contains (txt, cs))
                found = true;
            else
            {
                while ((indx = model_->adjacentIndex (indx, down)).isValid())
                {
                    if (indx == ui->treeView->currentIndex())
                        return;
                    if (model_->data (indx, Qt::DisplayRole).toString().contains (txt, cs))
                    {
                        found = true;
                        break;
                    }
                }
            }
        }
    }

    if (found)
        ui->treeView->setCurrentIndex (indx);
}
/*************************/
void FN::clearTagsList (int)
{
    tagsList_.clear();
}
/*************************/
void FN::selectRow (QListWidgetItem *item)
{
    QList<QDialog *> list = findChildren<QDialog*>();
    if (list.isEmpty()) return;
    QList<QListWidget *> list1;
    for (int i = 0; i < list.count(); ++i)
    {
        list1 = list.at (i)->findChildren<QListWidget *>();
        if (!list1.isEmpty()) break;
    }
    if (list1.isEmpty()) return;
    QListWidget *listWidget = list1.at (0);
    ui->treeView->setCurrentIndex (tagsList_.at (listWidget->row (item)));
}
/*************************/
void FN::chooseRow (int row)
{
    ui->treeView->setCurrentIndex (tagsList_.at (row));
}
/*************************/
void FN::findInTags()
{
    QString txt = ui->lineEdit->text();
    if (txt.isEmpty()) return;

    /* close any existing tag matches dialog */
    QList<QDialog *> list = findChildren<QDialog*>();
    for (int i = 0; i < list.count(); ++i)
    {
        QList<QListWidget *> list1 = list.at (i)->findChildren<QListWidget *>();
        if (!list1.isEmpty())
        {
            /* this also deletes the dialog */
            list.at (i)->done (QDialog::Rejected);
            break;
        }
    }

    QModelIndex nxtIndx = model_->index (0, 0, QModelIndex());
    DomItem *item = static_cast<DomItem*>(nxtIndx.internalPointer());
    QDomNode node = item->node();
    QDomNamedNodeMap attributeMap = node.attributes();
    QString tags = attributeMap.namedItem ("tag").nodeValue();

    while (nxtIndx.isValid())
    {
        item = static_cast<DomItem*>(nxtIndx.internalPointer());
        node = item->node();
        attributeMap = node.attributes();
        tags = attributeMap.namedItem ("tag").nodeValue();
        if (tags.contains (txt, Qt::CaseInsensitive))
            tagsList_.append (nxtIndx);
        nxtIndx = model_->adjacentIndex (nxtIndx, true);
    }

    int matches = tagsList_.count();

    QDialog *TagsDialog = new QDialog (this);
    TagsDialog->setAttribute (Qt::WA_DeleteOnClose, true);
    if (matches > 1)
        TagsDialog->setWindowTitle (tr ("%1 Matches").arg (matches));
    else if (matches == 1)
        TagsDialog->setWindowTitle (tr ("One Match"));
    else
        TagsDialog->setWindowTitle (tr ("No Match"));
    QGridLayout *grid = new QGridLayout;
    grid->setSpacing (5);
    grid->setContentsMargins (5, 5, 5, 5);

    QListWidget *listWidget = new QListWidget();
    listWidget->setSelectionMode (QAbstractItemView::SingleSelection);
    connect (listWidget, &QListWidget::itemActivated, this, &FN::selectRow);
    connect (listWidget, &QListWidget::currentRowChanged, this, &FN::chooseRow);
    QPushButton *closeButton = new QPushButton (symbolicIcon::icon (":icons/dialog-cancel.svg"), tr ("Close"));
    connect (closeButton, &QAbstractButton::clicked, TagsDialog, &QDialog::reject);
    connect (TagsDialog, &QDialog::finished, this, &FN::clearTagsList);

    for (int i = 0; i < matches; ++i)
        new QListWidgetItem (model_->data (tagsList_.at (i), Qt::DisplayRole).toString(), listWidget);

    grid->addWidget (listWidget, 0, 0);
    grid->addWidget (closeButton, 1, 0, Qt::AlignRight);

    TagsDialog->setLayout (grid);
    /*TagsDialog->resize (TagsDialog->minimumWidth(),
                        TagsDialog->minimumHeight());*/
    TagsDialog->show();
    /*TagsDialog->move (x() + width()/2 - TagsDialog->width(),
                      y() + height()/2 - TagsDialog->height());*/
    TagsDialog->raise();
    TagsDialog->raise();
    TagsDialog->activateWindow();
}
/*************************/
// Closes tag matches dialog.
void FN::closeTagsDialog()
{
    QList<QDialog *> list = findChildren<QDialog*>();
    for (int i = 0; i < list.count(); ++i)
    {
        QList<QListWidget *> list1 = list.at (i)->findChildren<QListWidget *>();
        if (!list1.isEmpty()) // the only non-modal dialog (tag dialog) has a list widget
        {
            list.at (i)->done (QDialog::Rejected);
            break;
        }
    }
}
/*************************/
void FN::scrolled (int) const
{
    hlight();
}
/*************************/
void FN::allBtn (bool checked)
{
    if (checked)
    {
        ui->tagsButton->setChecked (false);
        ui->namesButton->setChecked (false);
    }
}
/*************************/
void FN::tagsAndNamesBtn (bool checked)
{
    int index = ui->stackedWidget->currentIndex();
    if (index == -1) return;

    if (checked)
    {
        /* first clear all search info except the search
           entry's text but don't do redundant operations */
        if (!ui->tagsButton->isChecked() || !ui->namesButton->isChecked())
        {
            QHash<TextEdit*,QString>::iterator it;
            for (it = searchEntries_.begin(); it != searchEntries_.end(); ++it)
            {
                it.value() = QString();
                disconnect (it.key()->verticalScrollBar(), &QAbstractSlider::valueChanged, this, &FN::scrolled);
                disconnect (it.key()->horizontalScrollBar(), &QAbstractSlider::valueChanged, this, &FN::scrolled);
                disconnect (it.key(), &TextEdit::resized, this, &FN::hlight);
                disconnect (it.key(), &QTextEdit::textChanged, this, &FN::hlight);
                QList<QTextEdit::ExtraSelection> extraSelections;
                greenSels_[it.key()] = extraSelections;
                it.key()->setExtraSelections (extraSelections);
            }
        }
        else // then uncheck the other radio buttons
        {
            if (QObject::sender() == ui->tagsButton)
                ui->namesButton->setChecked (false);
            else
                ui->tagsButton->setChecked (false);
        }
        ui->everywhereButton->setChecked (false);
    }
    if (QObject::sender() == ui->tagsButton)
    {
        ui->prevButton->setEnabled (!checked);
        ui->wholeButton->setEnabled (!checked);
        ui->caseButton->setEnabled (!checked);
    }
}
/*************************/
void FN::replaceDock()
{
    if (!ui->dockReplace->isVisible())
    {
        if (!ui->lineEdit->isVisible()) // replace dock needs searchbar
        {
            ui->lineEdit->setVisible (true);
            ui->nextButton->setVisible (true);
            ui->prevButton->setVisible (true);
            ui->caseButton->setVisible (true);
            ui->wholeButton->setVisible (true);
            ui->everywhereButton->setVisible (true);
            ui->tagsButton->setVisible (true);
            ui->namesButton->setVisible (true);
        }
        ui->dockReplace->setWindowTitle (tr ("Replacement"));
        ui->dockReplace->setVisible (true);
        ui->dockReplace->setTabOrder (ui->lineEditFind, ui->lineEditReplace);
        ui->dockReplace->setTabOrder (ui->lineEditReplace, ui->rplNextButton);
        ui->dockReplace->raise();
        ui->dockReplace->activateWindow();
        if (!ui->lineEditFind->hasFocus())
            ui->lineEditFind->setFocus();
        return;
    }

    ui->dockReplace->setVisible (false);
    // closeReplaceDock(false) is automatically called here
}
/*************************/
// When the dock is closed with its titlebar button,
// clear the replacing text and remove green highlights.
void FN::closeReplaceDock (bool visible)
{
    if (visible) return;

    txtReplace_.clear();
    /* remove green highlights */
    QHash<TextEdit*,QList<QTextEdit::ExtraSelection> >::iterator it;
    for (it = greenSels_.begin(); it != greenSels_.end(); ++it)
    {
        QList<QTextEdit::ExtraSelection> extraSelectionsIth;
        greenSels_[it.key()] = extraSelectionsIth;
        it.key()->setExtraSelections (QList<QTextEdit::ExtraSelection>());
    }
    hlight();

    /* return focus to the document */
    if (ui->stackedWidget->count() > 0)
        qobject_cast< TextEdit *>(ui->stackedWidget->currentWidget())->setFocus();
}
/*************************/
// Resize the floating dock widget to its minimum size.
void FN::resizeDock (bool topLevel)
{
    if (topLevel)
        ui->dockReplace->resize (ui->dockReplace->minimumWidth(),
                                 ui->dockReplace->minimumHeight());
}
/*************************/
void FN::replace()
{
    QWidget *cw = ui->stackedWidget->currentWidget();
    if (!cw) return;

    TextEdit *textEdit = qobject_cast< TextEdit *>(cw);

    ui->dockReplace->setWindowTitle (tr ("Replacement"));

    QString txtFind = ui->lineEditFind->text();
    if (txtFind.isEmpty()) return;

    if (txtReplace_ != ui->lineEditReplace->text())
    {
        txtReplace_ = ui->lineEditReplace->text();
        /* remove previous green highlights
           if the replacing text is changed */
        QHash<TextEdit*,QList<QTextEdit::ExtraSelection> >::iterator it;
        for (it = greenSels_.begin(); it != greenSels_.end(); ++it)
        {
            QList<QTextEdit::ExtraSelection> extraSelectionsIth;
            greenSels_[it.key()] = extraSelectionsIth;
            it.key()->setExtraSelections (QList<QTextEdit::ExtraSelection>());
        }
        hlight();
    }

    /* remember all previous (yellow and) green highlights */
    QList<QTextEdit::ExtraSelection> extraSelections;
    extraSelections.append (textEdit->extraSelections());

    bool backwardSearch = false;
    QTextCursor start = textEdit->textCursor();
    if (QObject::sender() == ui->rplNextButton)
    {
        if (rplOtherNode_)
            start.movePosition (QTextCursor::Start, QTextCursor::MoveAnchor);
    }
    else// if (QObject::sender() == ui->rplPrevButton)
    {
        backwardSearch = true;
        if (rplOtherNode_)
            start.movePosition (QTextCursor::End, QTextCursor::MoveAnchor);
    }

    QTextCursor found;
    if (!backwardSearch)
        found = finding (txtFind, start, searchFlags_);
    else
        found = finding (txtFind, start, searchFlags_ | QTextDocument::FindBackward);

    QList<QTextEdit::ExtraSelection> gsel = greenSels_[textEdit];
    QModelIndex nxtIndx;
    if (found.isNull())
    {
        if (ui->everywhereButton->isChecked())
        {
            nxtIndx = ui->treeView->currentIndex();
            Qt::CaseSensitivity cs = Qt::CaseInsensitive;
            if (ui->caseButton->isChecked()) cs = Qt::CaseSensitive;
            QString text;
            while (!text.contains (txtFind, cs))
            {
                nxtIndx = model_->adjacentIndex (nxtIndx, !backwardSearch);
                if (!nxtIndx.isValid()) break;
                DomItem *item = static_cast<DomItem*>(nxtIndx.internalPointer());
                if (TextEdit *thisTextEdit = widgets_.value (item))
                    text = thisTextEdit->toPlainText(); // the node text may have been edited
                else
                {
                    QDomNodeList list = item->node().childNodes();
                    text = list.item (0).nodeValue();
                }
            }
        }
        rplOtherNode_ = false;
    }
    else
    {
        QColor green = QColor (Qt::green);
        QColor black = QColor (Qt::black);
        int pos;
        QTextCursor tmp = start;

        start.setPosition (found.anchor());
        pos = found.anchor();
        start.setPosition (found.position(), QTextCursor::KeepAnchor);
        textEdit->setTextCursor (start);
        textEdit->insertPlainText (txtReplace_);

        if (rplOtherNode_)
        {
            /* the value of this boolean should be set to false but, in addition,
               we shake the splitter as a workaround for what seems to be a bug
               that makes ending parts of texts disappear after a text insertion */
            QList<int> sizes = ui->splitter->sizes();
            QList<int> newSizes; newSizes << sizes.first() + 1 << sizes.last() - 1;
            ui->splitter->setSizes (newSizes);
            ui->splitter->setSizes (sizes);
            rplOtherNode_ = false;
        }

        start = textEdit->textCursor(); // at the end of txtReplace_
        tmp.setPosition (pos);
        tmp.setPosition (start.position(), QTextCursor::KeepAnchor);
        QTextEdit::ExtraSelection extra;
        extra.format.setBackground (green);
        extra.format.setUnderlineStyle (QTextCharFormat::WaveUnderline);
        extra.format.setUnderlineColor (black);
        extra.cursor = tmp;
        extraSelections.prepend (extra);
        gsel.append (extra);

        if (QObject::sender() != ui->rplNextButton)
        {
            /* With the cursor at the end of the replacing text, if the backward replacement
               is repeated and the text is matched again (which is possible when the replacing
               and replaced texts are the same, case-insensitively), the replacement won't proceed.
               So, the cursor should be moved. */
            start.setPosition (start.position() - txtReplace_.length());
            textEdit->setTextCursor (start);
        }
    }

    greenSels_[textEdit] = gsel;
    textEdit->setExtraSelections (extraSelections);
    hlight();

    if (nxtIndx.isValid())
    {
        rplOtherNode_ = true;
        ui->treeView->setCurrentIndex (nxtIndx);
        replace();
    }
}
/*************************/
void FN::replaceAll()
{
    QString txtFind = ui->lineEditFind->text();
    if (txtFind.isEmpty()) return;

    Qt::CaseSensitivity cs = Qt::CaseInsensitive;
    if (ui->caseButton->isChecked()) cs = Qt::CaseSensitive;

    QModelIndex nxtIndx;
    /* start with the first node when replacing everywhere */
    if (!rplOtherNode_ && ui->everywhereButton->isChecked())
    {
        nxtIndx = model_->index (0, 0);
        DomItem *item = static_cast<DomItem*>(nxtIndx.internalPointer());
        QString text;
        if (TextEdit *thisTextEdit = widgets_.value (item))
            text = thisTextEdit->toPlainText(); // the node text may have been edited
        else
        {
            QDomNodeList list = item->node().childNodes();
            text = list.item (0).nodeValue();
        }
        while (!text.contains (txtFind, cs))
        {
            nxtIndx = model_->adjacentIndex (nxtIndx, true);
            if (!nxtIndx.isValid())
            {
                ui->dockReplace->setWindowTitle (tr ("No Match"));
                return;
            }
            item = static_cast<DomItem*>(nxtIndx.internalPointer());
            if (TextEdit *thisTextEdit = widgets_.value (item))
                text = thisTextEdit->toPlainText();
            else
            {
                QDomNodeList list = item->node().childNodes();
                text = list.item (0).nodeValue();
            }
        }
        rplOtherNode_ = true;
        ui->treeView->setCurrentIndex (nxtIndx);
        nxtIndx = QModelIndex();
    }

    QWidget *cw = ui->stackedWidget->currentWidget();
    if (!cw) return;
    TextEdit *textEdit = qobject_cast< TextEdit *>(cw);

    if (txtReplace_ != ui->lineEditReplace->text())
    {
        txtReplace_ = ui->lineEditReplace->text();
        QHash<TextEdit*,QList<QTextEdit::ExtraSelection> >::iterator it;
        for (it = greenSels_.begin(); it != greenSels_.end(); ++it)
        {
            QList<QTextEdit::ExtraSelection> extraSelectionsIth;
            greenSels_[it.key()] = extraSelectionsIth;
            it.key()->setExtraSelections (QList<QTextEdit::ExtraSelection>());
        }
        hlight();
    }

    QTextCursor orig = textEdit->textCursor();
    QTextCursor start = orig;
    QColor green = QColor (Qt::green);
    QColor black = QColor (Qt::black);
    int pos; QTextCursor found;
    start.beginEditBlock();
    start.setPosition (0);
    QTextCursor tmp = start;
    QList<QTextEdit::ExtraSelection> gsel = greenSels_[textEdit];
    QList<QTextEdit::ExtraSelection> extraSelections;
    while (!(found = finding (txtFind, start, searchFlags_)).isNull())
    {
        start.setPosition (found.anchor());
        pos = found.anchor();
        start.setPosition (found.position(), QTextCursor::KeepAnchor);
        start.insertText (txtReplace_);

        if (rplOtherNode_)
        {
            QList<int> sizes = ui->splitter->sizes();
            QList<int> newSizes; newSizes << sizes.first() + 1 << sizes.last() - 1;
            ui->splitter->setSizes (newSizes);
            ui->splitter->setSizes (sizes);
            rplOtherNode_ = false;
        }

        tmp.setPosition (pos);
        tmp.setPosition (start.position(), QTextCursor::KeepAnchor);
        start.setPosition (start.position());
        QTextEdit::ExtraSelection extra;
        extra.format.setBackground (green);
        extra.format.setUnderlineStyle (QTextCharFormat::WaveUnderline);
        extra.format.setUnderlineColor (black);
        extra.cursor = tmp;
        extraSelections.prepend (extra);
        gsel.append (extra);
        ++replCount_;
    }
    rplOtherNode_ = false;
    greenSels_[textEdit] = gsel;
    start.endEditBlock();
    textEdit->setExtraSelections (extraSelections);
    hlight();
    /* restore the original cursor without selection */
    orig.setPosition (orig.anchor());
    textEdit->setTextCursor (orig);

    if (ui->everywhereButton->isChecked() && model_->rowCount() > 1)
    {
        nxtIndx = ui->treeView->currentIndex();
        QString text;
        while (!text.contains (txtFind, cs))
        {
            nxtIndx = model_->adjacentIndex (nxtIndx, true);
            if (!nxtIndx.isValid()) break;
            DomItem *item = static_cast<DomItem*>(nxtIndx.internalPointer());
            if (TextEdit *thisTextEdit = widgets_.value (item))
                text = thisTextEdit->toPlainText();
            else
            {
                QDomNodeList list = item->node().childNodes();
                text = list.item (0).nodeValue();
            }
        }
    }

    if (nxtIndx.isValid())
    {
        rplOtherNode_ = true;
        ui->treeView->setCurrentIndex (nxtIndx);
        replaceAll();
    }
    else
    {
        if (replCount_ == 0)
            ui->dockReplace->setWindowTitle (tr ("No Replacement"));
        else if (replCount_ == 1)
            ui->dockReplace->setWindowTitle (tr ("One Replacement"));
        else
            ui->dockReplace->setWindowTitle (tr ("%1 Replacements").arg (replCount_));
        replCount_ = 0;
    }
}
/*************************/
void FN::showEvent (QShowEvent *event)
{
    /* To position the main window correctly with translucency when it's
       shown for the first time, we use setGeometry() inside showEvent(). */
    if (!shownBefore_ && !event->spontaneous())
    {
        shownBefore_ = true;
        if (remPosition_)
        {
            QSize theSize = (remSize_ ? winSize_ : startSize_);
            setGeometry (position_.x() - (underE_ ? EShift_.width() : 0), position_.y() - (underE_ ? EShift_.height() : 0),
                         theSize.width(), theSize.height());
        }
    }
    QWidget::showEvent (event);
}
/*************************/
void FN::showAndFocus()
{
    show();
    raise();
    activateWindow();
    if (ui->stackedWidget->count() > 0)
        qobject_cast< TextEdit *>(ui->stackedWidget->currentWidget())->setFocus();
    // to bypass focus stealing prevention
    QTimer::singleShot (0, this, &FN::stealFocus);
}
/*************************/
void FN::stealFocus()
{
    if (QWindow *win = windowHandle())
        win->requestActivate();
}
/*************************/
void FN::trayActivated (QSystemTrayIcon::ActivationReason r)
{
    if (!tray_) return;
    if (r != QSystemTrayIcon::Trigger) return;

    if (QObject::sender() == tray_ && findChildren<QDialog *>().count() > 0)
    { // don't respond to the tray icon when there's a dialog
        raise();
        activateWindow();
        return;
    }

    if (!isVisible())
    {
        /* make the widget an independent window again */
        /*if (parent() != nullptr)
            setParent (nullptr, Qt::Window);
        QTimer::singleShot (0, this, SLOT (show()));*/
        show();

#ifdef HAS_X11
        if (isX11_ && onWhichDesktop (winId()) != fromDesktop())
            moveToCurrentDesktop (winId());
#endif
        showAndFocus();
    }
#ifdef HAS_X11
    else if (!isX11_ || onWhichDesktop (winId()) == fromDesktop())
    {
        if (isX11_ && underE_)
        {
            hide();
            QTimer::singleShot (250, this, &QWidget::show);
            return;
        }
        QRect sr;
        if (QWindow *win = windowHandle())
        {
            if (QScreen *sc = win->screen())
                sr = sc->virtualGeometry();
        }
        if (sr.isNull())
        {
            if (QScreen *pScreen = QApplication::primaryScreen())
                sr = pScreen->virtualGeometry();
        }
        QRect g = geometry();
        if (g.x() >= sr.left() && g.x() + g.width() <= sr.left() + sr.width()
            && g.y() >= sr.top() && g.y() + g.height() <= sr.top() + sr.height())
        {
            if (isActiveWindow())
            {
                if (!isMaximized() && !isFullScreen())
                {
                    position_.setX (g.x());
                    position_.setY (g.y());
                }
                /* instead of hiding the window in the ususal way,
                   reparent it to preserve its state info */
                //setParent (dummyWidget, Qt::SubWindow);
                QTimer::singleShot (0, this, &QWidget::hide);
            }
            else
            {
                if (isMinimized())
                    showNormal();
                showAndFocus();
            }
        }
        else
        {
            hide();
            setGeometry (position_.x(), position_.y(), g.width(), g.height());
            QTimer::singleShot (0, this, &FN::showAndFocus);
        }
    }
    else
    {
        if (isX11_)
            moveToCurrentDesktop (winId());
        if (isMinimized())
            showNormal();
        showAndFocus();
    }
#else
    /* without X11, just iconify the window */
    else
        QTimer::singleShot (0, this, &QWidget::hide);
#endif
}
/*************************/
void FN::activateTray()
{
    QObject *sender = QObject::sender();
    if (sender != nullptr && sender->objectName() == "raiseHide" && findChildren<QDialog *>().count() > 0)
    { // don't respond to the tray icon when there's a dialog
        raise();
        activateWindow();
        return;
    }

    trayActivated (QSystemTrayIcon::Trigger);
}
/*************************/
void FN::insertLink()
{
    QWidget *cw = ui->stackedWidget->currentWidget();
    if (!cw) return;

    TextEdit *textEdit = qobject_cast< TextEdit *>(cw);
    QTextCursor cur = textEdit->textCursor();
    if (!cur.hasSelection()) return;
    /* only if the position is after the anchor,
       the format will be detected correctly */
    int pos, anch;
    QTextCursor cursor = cur;
    if ((pos = cur.position()) < (anch = cur.anchor()))
    {
        cursor.setPosition (pos);
        cursor.setPosition (anch, QTextCursor::KeepAnchor);
    }
    QTextCharFormat format = cursor.charFormat();
    QString href = format.anchorHref();

    QDialog *dialog = new QDialog (this);
    dialog->setWindowTitle (tr ("Insert Link"));
    QGridLayout *grid = new QGridLayout;
    grid->setSpacing (5);
    grid->setContentsMargins (5, 5, 5, 5);

    /* needed widgets */
    LineEdit *linkEntry = new LineEdit();
    linkEntry->returnOnClear = false;
    linkEntry->setMinimumWidth (250);
    linkEntry->setText (href);
    connect (linkEntry, &QLineEdit::returnPressed, dialog, &QDialog::accept);
    QSpacerItem *spacer = new QSpacerItem (1, 5);
    QPushButton *cancelButton = new QPushButton (symbolicIcon::icon (":icons/dialog-cancel.svg"), tr ("Cancel"));
    connect (cancelButton, &QAbstractButton::clicked, dialog, &QDialog::reject);
    QPushButton *okButton = new QPushButton (symbolicIcon::icon (":icons/dialog-ok.svg"), tr ("OK"));
    connect (okButton, &QAbstractButton::clicked, dialog,  &QDialog::accept);

    /* fit in the grid */
    grid->addWidget (linkEntry, 0, 0, 1, 3);
    grid->addItem (spacer, 1, 0);
    grid->addWidget (cancelButton, 2, 1, Qt::AlignRight);
    grid->addWidget (okButton, 2, 2, Qt::AlignRight);
    grid->setColumnStretch (0, 1);
    grid->setRowStretch (1, 1);

    /* show the dialog */
    dialog->setLayout (grid);
    /*dialog->resize (dialog->minimumWidth(),
                    dialog->minimumHeight());*/
    /*dialog->show();
    dialog->move (x() + width()/2 - dialog->width(),
                  y() + height()/2 - dialog->height());*/

    QString address;
    switch (dialog->exec()) {
    case QDialog::Accepted:
        address = linkEntry->text();
        delete dialog;
        break;
    case QDialog::Rejected:
    default:
        delete dialog;
        return;
    }

    if (!address.isEmpty())
    {
        format.setAnchor (true);
        format.setFontUnderline (true);
        format.setFontItalic (true);
        format.setForeground (QColor (0, 0, 255));
    }
    else
    {
        format.setAnchor (false);
        format.setFontUnderline (false);
        format.setFontItalic (false);
        format.setForeground (QBrush());
    }
    format.setAnchorHref (address);
    cur.mergeCharFormat (format);
}
/*************************/
void FN::embedImage()
{
    int index = ui->stackedWidget->currentIndex();
    if (index == -1) return;

    QDialog *dialog = new QDialog (this);
    dialog->setWindowTitle (tr ("Embed Image"));
    QGridLayout *grid = new QGridLayout;
    grid->setSpacing (5);
    grid->setContentsMargins (5, 5, 5, 5);

    /* create the needed widgets */
    ImagePathEntry_ = new LineEdit();
    ImagePathEntry_->returnOnClear = false;
    ImagePathEntry_->setMinimumWidth (200);
    ImagePathEntry_->setToolTip (tr ("Image path"));
    connect (ImagePathEntry_, &QLineEdit::returnPressed, dialog, &QDialog::accept);
    QToolButton *openBtn = new QToolButton();
    openBtn->setIcon (symbolicIcon::icon (":icons/document-open.svg"));
    openBtn->setToolTip (tr ("Open image"));
    connect (openBtn, &QAbstractButton::clicked, this, &FN::setImagePath);
    QLabel *label = new QLabel();
    label->setText (tr ("Scale to"));
    SpinBox *spinBox = new SpinBox();
    spinBox->setRange (1, 200);
    spinBox->setValue (imgScale_);
    spinBox->setSuffix (tr ("%"));
    spinBox->setToolTip (tr ("Scaling percentage"));
    connect (spinBox, &QAbstractSpinBox::editingFinished, dialog, &QDialog::accept);
    QSpacerItem *spacer = new QSpacerItem (1, 10);
    QPushButton *cancelButton = new QPushButton (symbolicIcon::icon (":icons/dialog-cancel.svg"), tr ("Cancel"));
    QPushButton *okButton = new QPushButton (symbolicIcon::icon (":icons/dialog-ok.svg"), tr ("OK"));
    connect (cancelButton, &QAbstractButton::clicked, dialog, &QDialog::reject);
    connect (okButton, &QAbstractButton::clicked, dialog, &QDialog::accept);

    /* make the widgets fit in the grid */
    grid->addWidget (ImagePathEntry_, 0, 0, 1, 4);
    grid->addWidget (openBtn, 0, 4, Qt::AlignCenter);
    grid->addWidget (label, 1, 0, Qt::AlignRight);
    grid->addWidget (spinBox, 1, 1, Qt::AlignLeft);
    grid->addItem (spacer, 2, 0);
    grid->addWidget (cancelButton, 3, 2, Qt::AlignRight);
    grid->addWidget (okButton, 3, 3, 1, 2, Qt::AlignCenter);
    grid->setColumnStretch (1, 1);
    grid->setRowStretch (2, 1);

    /* show the dialog */
    dialog->setLayout (grid);
    /*dialog->resize (dialog->minimumWidth(),
                    dialog->minimumHeight());*/
    //dialog->setFixedHeight (dialog->sizeHint().height());
    /*dialog->show();
    dialog->move (x() + width()/2 - dialog->width(),
                  y() + height()/2 - dialog->height());*/

    switch (dialog->exec()) {
    case QDialog::Accepted:
        lastImgPath_ = ImagePathEntry_->text();
        imgScale_ = spinBox->value();
        delete dialog;
        ImagePathEntry_ = nullptr;
        break;
    case QDialog::Rejected:
    default:
        lastImgPath_ = ImagePathEntry_->text();
        delete dialog;
        ImagePathEntry_ = nullptr;
        return;
    }

    imageEmbed (lastImgPath_);
}
/*************************/
void FN::imageEmbed (const QString &path)
{
    if (path.isEmpty()) return;

    QFile file (path);
    if (!file.open (QIODevice::ReadOnly))
        return;
    /* read the data serialized from the file */
    QDataStream in (&file);
    QByteArray rawarray;
    QDataStream datastream (&rawarray, QIODevice::WriteOnly);
    char a;
    /* copy between the two data streams */
    while (in.readRawData (&a, 1) != 0)
        datastream.writeRawData (&a, 1);
    file.close();
    QByteArray base64array = rawarray.toBase64();

    QImage img = QImage (path);
    QSize imgSize = img.size();
    int w, h;
    if (QObject::sender() == ui->actionEmbedImage)
    {
        w = imgSize.width() * imgScale_ / 100;
        h = imgSize.height() * imgScale_ / 100;
    }
    else
    {
        w = imgSize.width();
        h = imgSize.height();
    }
    TextEdit *textEdit = qobject_cast< TextEdit *>(ui->stackedWidget->currentWidget());
    //QString ("<img src=\"data:image/png;base64,%1\">")
    textEdit->insertHtml (QString ("<img src=\"data:image;base64,%1\" width=\"%2\" height=\"%3\" />")
                          .arg (QString (base64array))
                          .arg (w)
                          .arg (h));

    raise();
    activateWindow();
}
/*************************/
void FN::setImagePath (bool)
{
    QString path;
    if (!lastImgPath_.isEmpty())
    {
        if (QFile::exists (lastImgPath_))
            path = lastImgPath_;
        else
        {
            QDir dir = QFileInfo (lastImgPath_).absoluteDir();
            if (!dir.exists())
                dir = QDir::home();
            path = dir.path();
        }
    }
    else
        path = QDir::home().path();

    QString imagePath;
    FileDialog dialog (this);
    dialog.setAcceptMode (QFileDialog::AcceptOpen);
    dialog.setWindowTitle (tr ("Open Image..."));
    dialog.setFileMode (QFileDialog::ExistingFiles);
    dialog.setNameFilter (tr ("Image Files (*.svg *.png *.jpg *.jpeg *.bmp *.gif);;All Files (*)"));
    if (QFileInfo (path).isDir())
        dialog.setDirectory (path);
    else
    {
        dialog.setDirectory (path.section ("/", 0, -2)); // workaround for KDE
        dialog.selectFile (path);
        dialog.autoScroll();
    }
    if (dialog.exec())
    {
        QStringList files = dialog.selectedFiles();
        if (files.count() > 0)
            imagePath = files.at (0);
    }

    if (!imagePath.isEmpty())
        ImagePathEntry_->setText (imagePath);
}
/*************************/
bool FN::isImageSelected()
{
    QWidget *cw = ui->stackedWidget->currentWidget();
    if (!cw) return false;

    TextEdit *textEdit = qobject_cast< TextEdit *>(cw);
    QTextCursor cur = textEdit->textCursor();
    if (!cur.hasSelection()) return false;

    QTextDocumentFragment docFrag = cur.selection();
    QString txt = docFrag.toHtml();
    if (txt.contains (QRegularExpression (EMBEDDED_IMG)))
        return true;

    return false;
}
/*************************/
void FN::scaleImage()
{
    QDialog *dialog = new QDialog (this);
    dialog->setWindowTitle (tr ("Scale Image(s)"));
    QGridLayout *grid = new QGridLayout;
    grid->setSpacing (5);
    grid->setContentsMargins (5, 5, 5, 5);

    QLabel *label = new QLabel();
    label->setText (tr ("Scale to"));
    SpinBox *spinBox = new SpinBox();
    spinBox->setRange (1, 200);
    spinBox->setSuffix (tr ("%"));
    spinBox->setToolTip (tr ("Scaling percentage"));
    connect (spinBox, &QAbstractSpinBox::editingFinished, dialog, &QDialog::accept);
    QSpacerItem *spacer = new QSpacerItem (1, 10);
    QPushButton *cancelButton = new QPushButton (symbolicIcon::icon (":icons/dialog-cancel.svg"), tr ("Cancel"));
    QPushButton *okButton = new QPushButton (symbolicIcon::icon (":icons/dialog-ok.svg"), tr ("OK"));
    connect (cancelButton, &QAbstractButton::clicked, dialog, &QDialog::reject);
    connect (okButton, &QAbstractButton::clicked, dialog, &QDialog::accept);

    grid->addWidget (label, 0, 0, Qt::AlignRight);
    grid->addWidget (spinBox, 0, 1, 1, 2, Qt::AlignLeft);
    grid->addItem (spacer, 1, 0);
    grid->addWidget (cancelButton, 2, 1, 1, 2, Qt::AlignRight);
    grid->addWidget (okButton, 2, 3, Qt::AlignCenter);
    grid->setColumnStretch (1, 1);
    grid->setRowStretch (1, 1);

    TextEdit *textEdit = qobject_cast< TextEdit *>(ui->stackedWidget->currentWidget());
    QTextCursor cur = textEdit->textCursor();
    QTextDocumentFragment docFrag = cur.selection();
    if (docFrag.isEmpty()) return;
    QString txt = docFrag.toHtml();

    QRegularExpression imageExp (R"((?<=\s)src\s*=\s*"data:[^<>]*;base64\s*,[a-zA-Z0-9+=/\s]+)");
    QRegularExpressionMatch match;
    QSize imageSize;
    int W = 0, H = 0;

    int startIndex = txt.indexOf (EMBEDDED_IMG, 0, &match);
    if (startIndex == -1) return;

    QString str = txt.mid (startIndex, match.capturedLength());
    int indx = str.lastIndexOf (imageExp, -1, &match);
    QString imgStr = str.mid (indx, match.capturedLength());
    imgStr.remove (QRegularExpression (R"(src\s*=\s*"data:[^<>]*;base64\s*,)"));
    QImage image;
    if (image.loadFromData (QByteArray::fromBase64 (imgStr.toUtf8())))
        imageSize = image.size();
    if (imageSize.isEmpty()) return;

    int scale = 100;

    /* first, check the (last) width */
    if ((indx = str.lastIndexOf (QRegularExpression (R"(width\s*=\s*"\s*(\+|-){0,1}[0-9]+\s*")"), -1, &match)) != -1)
    {
        bool ok;
        str = str.mid (indx, match.capturedLength());
        str.remove (QRegularExpression (R"(width\s*=\s*"\s*)"));
        str.remove (QRegularExpression (R"(\s*")"));
        W = str.toInt(&ok);
        if (!ok) W = 0;
        W = qMax (W, 0);
        scale = 100 * W / imageSize.width();
    }
    /* if there's no width, check the (last) height */
    else if ((indx = str.lastIndexOf (QRegularExpression (R"(height\s*=\s*"\s*(\+|-){0,1}[0-9]+\s*")"), -1, &match)) != -1)
    {
        bool ok;
        str = str.mid (indx, match.capturedLength());
        str.remove (QRegularExpression (R"(height\s*=\s*"\s*)"));
        str.remove (QRegularExpression (R"(\s*")"));
        H = str.toInt(&ok);
        if (!ok) H = 0;
        H = qMax (H, 0);
        scale = 100 * H / imageSize.height();
    }

    spinBox->setValue (scale);
    /* show the dialog */
    dialog->setLayout (grid);
    /*dialog->resize (dialog->minimumWidth(),
                    dialog->minimumHeight());*/
    /*dialog->show();
    dialog->move (x() + width()/2 - dialog->width(),
                  y() + height()/2 - dialog->height());*/

    switch (dialog->exec()) {
    case QDialog::Accepted:
        scale = spinBox->value();
        delete dialog;
        break;
    case QDialog::Rejected:
    default:
        delete dialog;
        return;
    }

    while ((indx = txt.indexOf (EMBEDDED_IMG, startIndex, &match)) != -1)
    {
        str = txt.mid (indx, match.capturedLength());

        if (imageSize.isEmpty()) // already calculated for the first image
        {
            QRegularExpressionMatch imageMatch;
            int pos = str.lastIndexOf (imageExp, -1, &imageMatch);

            if (pos == -1)
            {
                startIndex = indx + match.capturedLength();
                continue;
            }
            imgStr = str.mid (pos, imageMatch.capturedLength());
            imgStr.remove (QRegularExpression (R"(src\s*=\s*"data:[^<>]*;base64\s*,)"));
            QImage image;
            if (!image.loadFromData (QByteArray::fromBase64 (imgStr.toUtf8())))
            {
                startIndex = indx + match.capturedLength();
                continue;
            }
            imageSize = image.size();
            if (imageSize.isEmpty()) return;
        }

        W = imageSize.width() * scale / 100;
        H = imageSize.height() * scale / 100;
        txt.replace (indx, match.capturedLength(), "<img src=\"data:image;base64," + imgStr + QString ("\" width=\"%1\" height=\"%2\">").arg (W).arg (H));
        imageSize = QSize(); // for the next image

        /* since the text is changed, startIndex should be found again */
        indx = txt.indexOf (EMBEDDED_IMG, startIndex, &match);
        startIndex = indx + match.capturedLength();
    }
    cur.insertHtml (txt);
}
/*************************/
void FN::saveImage()
{
    TextEdit *textEdit = qobject_cast< TextEdit *>(ui->stackedWidget->currentWidget());
    QTextCursor cur = textEdit->textCursor();
    QTextDocumentFragment docFrag = cur.selection();
    if (docFrag.isEmpty()) return;
    QString txt = docFrag.toHtml();

    QString path;
    if (!xmlPath_.isEmpty())
    {
        QDir dir = QFileInfo (xmlPath_).absoluteDir();
        if (!dir.exists())
            dir = QDir::home();
        path = dir.path();

        QString shownName = QFileInfo (xmlPath_).fileName();
        if (shownName.endsWith (".fnx"))
            shownName.chop (4);
        path += "/" + shownName;
    }
    else
    {
        QDir dir = QDir::home();
        path = dir.path();
        path += "/" + tr ("untitled");
    }

    QRegularExpression imageExp (R"((?<=\s)src\s*=\s*"data:[^<>]*;base64\s*,[a-zA-Z0-9+=/\s]+)");
    int indx;
    int startIndex = 0;
    int n = 1;
    QString extension = "png";
    QRegularExpressionMatch match;
    while ((indx = txt.indexOf (EMBEDDED_IMG, startIndex, &match)) != -1)
    {
        QString str = txt.mid (indx, match.capturedLength());
        startIndex = indx + match.capturedLength();

        indx = str.lastIndexOf (imageExp, -1, &match);

        if (indx == -1) continue;
        str = str.mid (indx, match.capturedLength());
        str.remove (QRegularExpression (R"(src\s*=\s*"data:[^<>]*;base64\s*,)"));
        QImage image;
        if (!image.loadFromData (QByteArray::fromBase64 (str.toUtf8())))
            continue;

        bool retry (true);
        bool err (false);
        while (retry)
        {
            if (err)
            {
                MessageBox msgBox;
                msgBox.setIcon (QMessageBox::Question);
                msgBox.setWindowTitle (tr ("Error"));
                msgBox.setText (tr ("<center><b><big>Image cannot be saved! Retry?</big></b></center>"));
                msgBox.setInformativeText (tr ("<center>Maybe you did not choose a proper extension</center>\n"\
                                               "<center>or do not have write permission.</center><p></p>"));
                msgBox.setStandardButtons (QMessageBox::Yes | QMessageBox::No);
                msgBox.changeButtonText (QMessageBox::Yes, tr ("Yes"));
                msgBox.changeButtonText (QMessageBox::No, tr ("No"));
                msgBox.setDefaultButton (QMessageBox::No);
                msgBox.setParent (this, Qt::Dialog);
                msgBox.setWindowModality (Qt::WindowModal);
                msgBox.show();
                msgBox.move (x() + width()/2 - msgBox.width()/2,
                             y() + height()/2 - msgBox.height()/ 2);
                switch (msgBox.exec())
                {
                case QMessageBox::Yes:
                    break;
                case QMessageBox::No:
                default:
                    retry = false; // next image without saving this one
                    break;
                }
            }

            if (retry)
            {
                QString fname;
                FileDialog dialog (this);
                dialog.setAcceptMode (QFileDialog::AcceptSave);
                dialog.setWindowTitle (tr ("Save Image As..."));
                dialog.setFileMode (QFileDialog::AnyFile);
                dialog.setNameFilter (tr ("Image Files (*.png *.jpg *.jpeg *.bmp);;All Files (*)"));
                dialog.setDirectory (path.section ("/", 0, -2)); // workaround for KDE
                dialog.selectFile (QString ("%1-%2.%3").arg (path).arg (n).arg (extension));
                dialog.autoScroll();
                if (dialog.exec())
                {
                    fname = dialog.selectedFiles().at (0);
                    if (fname.isEmpty() || QFileInfo (fname).isDir())
                    {
                        err = true;
                        continue;
                    }
                }
                else return;

                if (image.save (fname))
                {
                    lastImgPath_ = fname;
                    QFileInfo info = QFileInfo (lastImgPath_);
                    QString shownName = info.fileName();
                    extension = shownName.split (".").last();
                    shownName.chop (extension.count() + 1);
                    /* if the name ends with a number following a dash,
                       use it; otherwise, increase the number by one */
                    int m = 0;
                    QRegularExpression exp ("-[1-9]+[0-9]*");
                    indx = shownName.lastIndexOf (QRegularExpression ("-[1-9]+[0-9]*"), -1, &match);
                    if (indx > -1 && indx == shownName.count() - match.capturedLength())
                    {
                        QString number = shownName.split ("-").last();
                        shownName.chop (number.count() + 1);
                        m = number.toInt() + 1;
                    }
                    n = m > n ? m : n + 1;
                    path = info.dir().path() + "/" + shownName;
                    retry = false; // next image after saving this one
                }
                else
                    err = true;
            }
        }
    }
}
/*************************/
void FN::addTable()
{
    QWidget *cw = ui->stackedWidget->currentWidget();
    if (!cw) return;

    QDialog *dialog = new QDialog (this);
    dialog->setWindowTitle (tr ("Insert Table"));
    QGridLayout *grid = new QGridLayout;
    grid->setSpacing (5);
    grid->setContentsMargins (5, 5, 5, 5);

    QLabel *labelRow = new QLabel();
    labelRow->setText (tr ("Rows:"));
    SpinBox *spinBoxRow = new SpinBox();
    spinBoxRow->setRange (1, 100);
    spinBoxRow->setValue (1);
    connect (spinBoxRow, &QAbstractSpinBox::editingFinished, dialog, &QDialog::accept);
    QLabel *labelCol = new QLabel();
    labelCol->setText (tr ("Columns:"));
    SpinBox *spinBoxCol = new SpinBox();
    spinBoxCol->setRange (1, 100);
    spinBoxCol->setValue (1);
    connect (spinBoxCol, &QAbstractSpinBox::editingFinished, dialog, &QDialog::accept);
    QSpacerItem *spacer = new QSpacerItem (1, 10);
    QPushButton *cancelButton = new QPushButton (symbolicIcon::icon (":icons/dialog-cancel.svg"), tr ("Cancel"));
    QPushButton *okButton = new QPushButton (symbolicIcon::icon (":icons/dialog-ok.svg"), tr ("OK"));
    connect (cancelButton, &QAbstractButton::clicked, dialog, &QDialog::reject);
    connect (okButton, &QAbstractButton::clicked, dialog, &QDialog::accept);

    grid->addWidget (labelRow, 0, 0, Qt::AlignRight);
    grid->addWidget (spinBoxRow, 0, 1, 1, 2, Qt::AlignLeft);
    grid->addWidget (labelCol, 1, 0, Qt::AlignRight);
    grid->addWidget (spinBoxCol, 1, 1, 1, 2, Qt::AlignLeft);
    grid->addItem (spacer, 2, 0);
    grid->addWidget (cancelButton, 3, 0, 1, 2, Qt::AlignRight);
    grid->addWidget (okButton, 3, 2, Qt::AlignLeft);
    grid->setColumnStretch (1, 2);
    grid->setRowStretch (2, 1);

    dialog->setLayout (grid);
    /*dialog->resize (dialog->minimumWidth(),
                    dialog->minimumHeight());*/
    /*dialog->show();
    dialog->move (x() + width()/2 - dialog->width(),
                  y() + height()/2 - dialog->height());*/

    int rows = 0;
    int columns = 0;
    switch (dialog->exec()) {
    case QDialog::Accepted:
        rows = spinBoxRow->value();
        columns = spinBoxCol->value();
        delete dialog;
        break;
    case QDialog::Rejected:
    default:
        delete dialog;
        return;
    }

    TextEdit *textEdit = qobject_cast< TextEdit *>(cw);
    QTextCursor cur = textEdit->textCursor();
    QTextTableFormat tf;
    tf.setCellPadding (3);
    QTextTable *table = cur.insertTable (rows, columns);
    table->setFormat (tf);
}
/*************************/
void FN::tableMergeCells()
{
    QWidget *cw = ui->stackedWidget->currentWidget();
    if (!cw) return;

    TextEdit *textEdit = qobject_cast< TextEdit *>(cw);
    QTextCursor cur = textEdit->textCursor();
    txtTable_->mergeCells (cur);
}
/*************************/
void FN::tablePrependRow()
{
    QWidget *cw = ui->stackedWidget->currentWidget();
    if (!cw) return;

    TextEdit *textEdit = qobject_cast< TextEdit *>(cw);
    QTextCursor cur = textEdit->textCursor();
    QTextTableCell tableCell = txtTable_->cellAt (cur);
    txtTable_->insertRows (tableCell.row(), 1);
}
/*************************/
void FN::tableAppendRow()
{
    QWidget *cw = ui->stackedWidget->currentWidget();
    if (!cw) return;

    TextEdit *textEdit = qobject_cast< TextEdit *>(cw);
    QTextCursor cur = textEdit->textCursor();
    QTextTableCell tableCell = txtTable_->cellAt (cur);
    txtTable_->insertRows (tableCell.row() + 1, 1);
}
/*************************/
void FN::tablePrependCol()
{
    QWidget *cw = ui->stackedWidget->currentWidget();
    if (!cw) return;

    TextEdit *textEdit = qobject_cast< TextEdit *>(cw);
    QTextCursor cur = textEdit->textCursor();
    QTextTableCell tableCell = txtTable_->cellAt (cur);
    txtTable_->insertColumns (tableCell.column(), 1);
}
/*************************/
void FN::tableAppendCol()
{
    QWidget *cw = ui->stackedWidget->currentWidget();
    if (!cw) return;

    TextEdit *textEdit = qobject_cast< TextEdit *>(cw);
    QTextCursor cur = textEdit->textCursor();
    QTextTableCell tableCell = txtTable_->cellAt (cur);
    txtTable_->insertColumns (tableCell.column() + 1, 1);
}
/*************************/
void FN::tableDeleteRow()
{
    QWidget *cw = ui->stackedWidget->currentWidget();
    if (!cw) return;

    TextEdit *textEdit = qobject_cast< TextEdit *>(cw);
    QTextCursor cur = textEdit->textCursor();
    QTextTableCell tableCell = txtTable_->cellAt (cur);
    txtTable_->removeRows (tableCell.row(), 1);
}
/*************************/
void FN::tableDeleteCol()
{
    QWidget *cw = ui->stackedWidget->currentWidget();
    if (!cw) return;

    TextEdit *textEdit = qobject_cast< TextEdit *>(cw);
    QTextCursor cur = textEdit->textCursor();
    QTextTableCell tableCell = txtTable_->cellAt (cur);
    txtTable_->removeColumns (tableCell.column(), 1);
}
/*************************/
void FN::toggleWrapping()
{
    int count = ui->stackedWidget->count();
    if (count == 0) return;

    if (ui->actionWrap->isChecked())
    {
        for (int i = 0; i < count; ++i)
            qobject_cast< TextEdit *>(ui->stackedWidget->widget (i))->setLineWrapMode (QTextEdit::WidgetWidth);
    }
    else
    {
        for (int i = 0; i < count; ++i)
            qobject_cast< TextEdit *>(ui->stackedWidget->widget (i))->setLineWrapMode (QTextEdit::NoWrap);
    }
    hlight();
}
/*************************/
void FN::toggleIndent()
{
    int count = ui->stackedWidget->count();
    if (count == 0) return;

    if (ui->actionIndent->isChecked())
    {
        for (int i = 0; i < count; ++i)
            qobject_cast< TextEdit *>(ui->stackedWidget->widget (i))->autoIndentation = true;
    }
    else
    {
        for (int i = 0; i < count; ++i)
            qobject_cast< TextEdit *>(ui->stackedWidget->widget (i))->autoIndentation = false;
    }
}
/*************************/
void FN::prefDialog()
{
    /* first, update settings because another
       FeatherNotes window may have changed them  */
    readAndApplyConfig (false);

    PrefDialog dlg (this);
    dlg.exec();
}
/*************************/
QByteArray FN::getSpltiterState() const
{
    return ui->splitter->saveState();
}
/*************************/
void FN::makeTreeTransparent (bool trans)
{
    if (trans)
    {
        if (!transparentTree_)
        {
            transparentTree_ = true;
            ui->treeView->setFrameShape (QFrame::NoFrame);
            if (ui->treeView->viewport())
            {
                QPalette p = ui->treeView->palette();
                p.setColor (QPalette::Base, QColor (Qt::transparent));
                ui->treeView->setPalette (p);
                ui->treeView->viewport()->setAutoFillBackground (false);
            }
        }
    }
    else
    {
        if (transparentTree_)
        {
            transparentTree_ = false;
            ui->treeView->setFrameShape (QFrame::StyledPanel);
            if (ui->treeView->viewport())
            {
                QPalette p = ui->treeView->palette();
                p.setColor (QPalette::Active, QPalette::Base,
                            QApplication::palette().color (QPalette::Active, QPalette::Base));
                p.setColor (QPalette::Inactive, QPalette::Base,
                            QApplication::palette().color (QPalette::Inactive, QPalette::Base));
                ui->treeView->setPalette (p);
                ui->treeView->viewport()->setAutoFillBackground (true);
            }
        }
    }
}
/*************************/
void FN::setToolBarIconSize (bool small)
{
    if (small)
    {
        if (!smallToolbarIcons_)
        {
            smallToolbarIcons_ = true;
            ui->mainToolBar->setIconSize (QSize (16, 16));
        }
    }
    else
    {
        if (smallToolbarIcons_)
        {
            smallToolbarIcons_ = false;
            ui->mainToolBar->setIconSize (TOOLBAR_ICON_SIZE);
        }
    }
}
/*************************/
void FN::showToolbar (bool show)
{
    ui->mainToolBar->setVisible (show);
    noToolbar_ = !show;
}
/*************************/
void FN::showMenubar (bool show)
{
    ui->menuBar->setVisible (show);
    ui->actionMenu->setVisible (!show);
    noMenubar_ = !show;
}
/*************************/
void FN::setUnderE (bool yes)
{
    if (yes)
    {
        if (!underE_)
        {
            underE_ = true;
            if (tray_)
            {
                if (QAction *actionshowMainWindow = tray_->contextMenu()->findChild<QAction *>("raiseHide"))
                    actionshowMainWindow->setText (tr ("&Raise"));
            }
        }
    }
    else
    {
        if (underE_)
        {
            underE_ = false;
            if (tray_)
            {
                if (QAction *actionshowMainWindow = tray_->contextMenu()->findChild<QAction *>("raiseHide"))
                    actionshowMainWindow->setText (tr ("&Raise/Hide"));
            }
        }
    }
}
/*************************/
void FN::enableScrollJumpWorkaround (bool enable)
{
    if (enable)
    {
        if (!scrollJumpWorkaround_)
        {
            scrollJumpWorkaround_ = true;
            for (int i = 0; i < ui->stackedWidget->count(); ++i)
                qobject_cast< TextEdit *>(ui->stackedWidget->widget (i))->setScrollJumpWorkaround (true);
        }
    }
    else
    {
        if (scrollJumpWorkaround_)
        {
            scrollJumpWorkaround_ = false;
            for (int i = 0; i < ui->stackedWidget->count(); ++i)
                qobject_cast< TextEdit *>(ui->stackedWidget->widget (i))->setScrollJumpWorkaround (false);
        }
    }
}
/*************************/
void FN::updateCustomizableShortcuts()
{
    QHash<QAction*, QKeySequence>::const_iterator iter = defaultShortcuts_.constBegin();
    QList<QString> cn = customActions_.keys();

    while (iter != defaultShortcuts_.constEnd())
    {
        const QString name = iter.key()->objectName();
        iter.key()->setShortcut (cn.contains (name)
                                 ? QKeySequence (customActions_.value (name), QKeySequence::PortableText)
                                 : iter.value());
        ++ iter;
    }
}
/*************************/
void FN::readShortcuts()
{
    /* NOTE: We don't read the custom shortcuts from global config files
             because we want the user to be able to restore their default values. */
    Settings tmp ("feathernotes", "fn");
    Settings settings (tmp.fileName(), QSettings::NativeFormat);

    settings.beginGroup ("shortcuts");
    QStringList actions = settings.childKeys();
    for (int i = 0; i < actions.size(); ++i)
    {
        QVariant v = settings.value (actions.at (i));
        bool isValid;
        QString vs = validatedShortcut (v, &isValid);
        if (isValid)
            customActions_.insert (actions.at (i), vs);
        else // remove the key on writing config
            uncustomizedActions_ << actions.at (i);
    }
    settings.endGroup();
}
/*************************/
QString FN::validatedShortcut (const QVariant v, bool *isValid)
{
    static QStringList added;
    if (v.isValid())
    {
        QString str = v.toString();
        if (str.isEmpty())
        { // it means the removal of a shortcut
            *isValid = true;
            return QString();
        }

        if (!QKeySequence (str, QKeySequence::PortableText).toString().isEmpty())
        {
            if (!reservedShortcuts_.contains (str)
                // prevent ambiguous shortcuts at startup as far as possible
                && !added.contains (str))
            {
                *isValid = true;
                added << str;
                return str;
            }
        }
    }
    *isValid = false;
    return QString();
}
/*************************/
void FN::readAndApplyConfig (bool startup)
{
    QSettings settings ("feathernotes", "fn");

    /**************
     *** Window ***
     **************/

    settings.beginGroup ("window");

    startSize_ = settings.value ("startSize", QSize (700, 500)).toSize();
    if (startSize_.isEmpty())
        startSize_ = QSize (700, 500);
    if (settings.value ("size").toString() == "none")
    {
        remSize_ = false; // true by default
        if (startup)
            resize (startSize_);
    }
    else
    {
        remSize_ = true;
        winSize_ = settings.value ("size", QSize (700, 500)).toSize();
        if (winSize_.isEmpty())
            winSize_ = QSize (700, 500);
        if (startup)
            resize (winSize_);
    }

    if (settings.value ("splitterSizes").toString() == "none")
        remSplitter_ = false; // true by default
    else
    {
        remSplitter_ = true;
        splitterSizes_ = settings.value ("splitterSizes", splitterSizes_).toByteArray();
    }
    if (startup)
        ui->splitter->restoreState (splitterSizes_);

    if (settings.value ("position").toString() == "none")
        remPosition_ = false; // true by default
    else
    {
        remPosition_ = true;
        position_ = settings.value ("position", QPoint (0, 0)).toPoint();
    }

    prefSize_ = settings.value ("prefSize").toSize();

    hasTray_ = settings.value ("hasTray").toBool(); // false by default

    minToTray_ = settings.value ("minToTray").toBool(); // false by default

    bool bv = settings.value ("underE").toBool(); // false by default
    if (startup)
        underE_ = bv;
    else
        setUnderE (bv);

    if (settings.contains ("Shift"))
        EShift_ = settings.value ("Shift").toSize();
    else
        EShift_ = QSize (0, 0);

    if (settings.value ("transparentTree").toBool()) // false by default
        makeTreeTransparent (true);
    else if (!startup)
        makeTreeTransparent (false);

    if (settings.value ("smallToolbarIcons").toBool()) // false by default
        setToolBarIconSize (true);
    else if (!startup)
        setToolBarIconSize (false);

    noToolbar_ = settings.value ("noToolbar").toBool(); // false by default
    noMenubar_ = settings.value ("noMenubar").toBool(); // false by default
    if (noToolbar_ && noMenubar_)
    { // we don't want to hide all actions
        noToolbar_ = false;
        noMenubar_ = true;
    }
    ui->mainToolBar->setVisible (!noToolbar_);
    ui->menuBar->setVisible (!noMenubar_);
    ui->actionMenu->setVisible (noMenubar_);

    if (startup)
    {
        QIcon icn;
        icn = symbolicIcon::icon (":icons/go-down.svg");
        ui->nextButton->setIcon (icn);
        ui->rplNextButton->setIcon (icn);
        ui->actionMoveDown->setIcon (icn);
        icn = symbolicIcon::icon (":icons/go-up.svg");
        ui->prevButton->setIcon (icn);
        ui->rplPrevButton->setIcon (icn);
        ui->actionMoveUp->setIcon (icn);
        ui->allButton->setIcon (symbolicIcon::icon (":icons/arrow-down-double.svg"));
        icn = symbolicIcon::icon (":icons/document-save.svg");
        ui->actionSave->setIcon (icn);
        ui->actionImageSave->setIcon (icn);
        ui->actionOpen->setIcon (symbolicIcon::icon (":icons/document-open.svg"));
        ui->actionUndo->setIcon (symbolicIcon::icon (":icons/edit-undo.svg"));
        ui->actionRedo->setIcon (symbolicIcon::icon (":icons/edit-redo.svg"));
        ui->actionFind->setIcon (symbolicIcon::icon (":icons/edit-find.svg"));
        ui->actionClear->setIcon (symbolicIcon::icon (":icons/edit-clear.svg"));
        ui->actionBold->setIcon (symbolicIcon::icon (":icons/format-text-bold.svg"));
        ui->actionItalic->setIcon (symbolicIcon::icon (":icons/format-text-italic.svg"));
        ui->actionUnderline->setIcon (symbolicIcon::icon (":icons/format-text-underline.svg"));
        ui->actionStrike->setIcon (symbolicIcon::icon (":icons/format-text-strikethrough.svg"));
        ui->actionTextColor->setIcon (symbolicIcon::icon (":icons/format-text-color.svg"));
        ui->actionBgColor->setIcon (symbolicIcon::icon (":icons/format-fill-color.svg"));
        ui->actionNew->setIcon (symbolicIcon::icon (":icons/document-new.svg"));
        ui->actionSaveAs->setIcon (symbolicIcon::icon (":icons/document-save-as.svg"));
        icn = symbolicIcon::icon (":icons/document-print.svg");
        ui->actionPrint->setIcon (icn);
        ui->actionPrintNodes->setIcon (icn);
        ui->actionPrintAll->setIcon (icn);
        ui->actionPassword->setIcon (symbolicIcon::icon (":icons/document-encrypt.svg"));
        ui->actionQuit->setIcon (symbolicIcon::icon (":icons/application-exit.svg"));
        ui->actionCut->setIcon (symbolicIcon::icon (":icons/edit-cut.svg"));
        ui->actionCopy->setIcon (symbolicIcon::icon (":icons/edit-copy.svg"));
        icn = symbolicIcon::icon (":icons/edit-paste.svg");
        ui->actionPaste->setIcon (icn);
        ui->actionPasteHTML->setIcon (icn);
        ui->actionDelete->setIcon (symbolicIcon::icon (":icons/edit-delete.svg"));
        ui->actionSelectAll->setIcon (symbolicIcon::icon (":icons/edit-select-all.svg"));
        icn = symbolicIcon::icon (":icons/image-x-generic.svg");
        ui->actionEmbedImage->setIcon (icn);
        ui->actionImageScale->setIcon (icn);
        ui->actionNodeIcon->setIcon (icn);
        ui->actionExpandAll->setIcon (symbolicIcon::icon (":icons/expand.svg"));
        ui->actionCollapseAll->setIcon (symbolicIcon::icon (":icons/collapse.svg"));
        ui->actionDeleteNode->setIcon (symbolicIcon::icon (":icons/user-trash.svg"));
        icn = symbolicIcon::icon (":icons/edit-rename.svg");
        ui->actionRenameNode->setIcon (icn);
        ui->namesButton->setIcon (icn);
        ui->actionProp->setIcon (symbolicIcon::icon (":icons/document-properties.svg"));
        icn = symbolicIcon::icon (":icons/preferences-desktop-font.svg");
        ui->actionDocFont->setIcon (icn);
        ui->actionNodeFont->setIcon (icn);
        ui->actionPref->setIcon (symbolicIcon::icon (":icons/preferences-system.svg"));
        ui->actionReplace->setIcon (symbolicIcon::icon (":icons/edit-find-replace.svg"));
        ui->actionHelp->setIcon (symbolicIcon::icon (":icons/help-contents.svg"));
        ui->actionAbout->setIcon (symbolicIcon::icon (":icons/help-about.svg"));
        ui->actionSuper->setIcon (symbolicIcon::icon (":icons/format-text-superscript.svg"));
        ui->actionSub->setIcon (symbolicIcon::icon (":icons/format-text-subscript.svg"));
        ui->actionCenter->setIcon (symbolicIcon::icon (":icons/format-justify-center.svg"));
        ui->actionRight->setIcon (symbolicIcon::icon (":icons/format-justify-right.svg"));
        ui->actionLeft->setIcon (symbolicIcon::icon (":icons/format-justify-left.svg"));
        ui->actionJust->setIcon (symbolicIcon::icon (":icons/format-justify-fill.svg"));
        ui->actionMoveLeft->setIcon (symbolicIcon::icon (":icons/go-previous.svg"));
        ui->actionMoveRight->setIcon (symbolicIcon::icon (":icons/go-next.svg"));
        icn = symbolicIcon::icon (":icons/zoom-in.svg");
        ui->actionH1->setIcon (icn);
        ui->actionH2->setIcon (icn);
        ui->actionH3->setIcon (icn);
        icn = symbolicIcon::icon (":icons/tag.svg");
        ui->actionTags->setIcon (icn);
        ui->tagsButton->setIcon (icn);
        ui->actionLink->setIcon (symbolicIcon::icon (":icons/insert-link.svg"));
        ui->actionCopyLink->setIcon (symbolicIcon::icon (":icons/link.svg"));
        ui->actionTable->setIcon (symbolicIcon::icon (":icons/insert-table.svg"));
        ui->actionTableAppendRow->setIcon (symbolicIcon::icon (":icons/edit-table-insert-row-below.svg"));
        ui->actionTableAppendCol->setIcon (symbolicIcon::icon (":icons/edit-table-insert-column-right.svg"));
        ui->actionTableDeleteRow->setIcon (symbolicIcon::icon (":icons/edit-table-delete-row.svg"));
        ui->actionTableDeleteCol->setIcon (symbolicIcon::icon (":icons/edit-table-delete-column.svg"));
        ui->actionTableMergeCells->setIcon (symbolicIcon::icon (":icons/edit-table-cell-merge.svg"));
        ui->actionTablePrependRow->setIcon (symbolicIcon::icon (":icons/edit-table-insert-row-above.svg"));
        ui->actionTablePrependCol->setIcon (symbolicIcon::icon (":icons/edit-table-insert-column-left.svg"));
        ui->actionRTL->setIcon (symbolicIcon::icon (":icons/format-text-direction-rtl.svg"));
        ui->actionLTR->setIcon (symbolicIcon::icon (":icons/format-text-direction-ltr.svg"));
        ui->actionMenu->setIcon (symbolicIcon::icon (":icons/application-menu.svg"));

        ui->actionPrepSibling->setIcon (symbolicIcon::icon (":icons/sibling-above.svg"));
        ui->actionNewSibling->setIcon (symbolicIcon::icon (":icons/sibling-below.svg"));
        ui->actionNewChild->setIcon (symbolicIcon::icon (":icons/child.svg"));

        ui->everywhereButton->setIcon (symbolicIcon::icon (":icons/all.svg"));
        ui->wholeButton->setIcon (symbolicIcon::icon (":icons/whole.svg"));
        ui->caseButton->setIcon (symbolicIcon::icon (":icons/case.svg"));

        icn = QIcon::fromTheme ("feathernotes");
        if (icn.isNull())
            icn = QIcon (":icons/feathernotes.svg");
        setWindowIcon (icn);
    }

    settings.endGroup();

    /************
     *** Text ***
     ************/

    settings.beginGroup ("text");

    if (settings.value ("noWrap").toBool())
    {
        wrapByDefault_ = false; // true by default
        if (startup)
            ui->actionWrap->setChecked (false);
    }
    else
        wrapByDefault_ = true;

    if (settings.value ("noIndent").toBool())
    {
        indentByDefault_ = false; // true by default
        if (startup)
            ui->actionIndent->setChecked (false);
    }
    else
        indentByDefault_ = true;

    autoBracket_ = settings.value ("autoBracket").toBool(); // false by default

    autoReplace_ = settings.value ("autoReplace").toBool(); // false by default

    int as = settings.value ("autoSave", -1).toInt();
    if (startup)
        autoSave_ = as;
    else if (autoSave_ != as)
    {
        autoSave_ = as;
        if (autoSave_ >= 1)
            timer_->start (autoSave_ * 1000 * 60);
        else if (timer_->isActive())
            timer_->stop();
    }

    scrollJumpWorkaround_ = settings.value ("scrollJumpWorkaround").toBool(); // false by default
    if (!startup)
        enableScrollJumpWorkaround (scrollJumpWorkaround_);

    settings.endGroup();
}
/*************************/
void FN::writeGeometryConfig()
{
    Settings settings ("feathernotes", "fn");
    settings.beginGroup ("window");

    if (remSize_)
        settings.setValue ("size", winSize_);
    else
        settings.setValue ("size", "none");
    settings.setValue ("startSize", startSize_);

    if (remSplitter_)
        settings.setValue ("splitterSizes", ui->splitter->saveState());
    else
        settings.setValue ("splitterSizes", "none");

    if (remPosition_)
    {
        QPoint CurrPos;
        if (isVisible() && !isMaximized() && !isFullScreen())
        {
            CurrPos.setX (geometry().x());
            CurrPos.setY (geometry().y());
        }
        else
            CurrPos = position_;
        settings.setValue ("position", CurrPos);
    }
    else
        settings.setValue ("position", "none");

    settings.setValue ("prefSize", prefSize_);

    settings.endGroup();
}
/*************************/
void FN::writeConfig()
{
    Settings settings ("feathernotes", "fn");
    if (!settings.isWritable()) return;

    /**************
     *** Window ***
     **************/

    settings.beginGroup ("window");

    settings.setValue ("hasTray", hasTray_);
    settings.setValue ("minToTray", minToTray_);
    settings.setValue ("underE", underE_);
    settings.setValue ("Shift", EShift_);
    settings.setValue ("transparentTree", transparentTree_);
    settings.setValue ("smallToolbarIcons", smallToolbarIcons_);
    settings.setValue ("noToolbar", noToolbar_);
    settings.setValue ("noMenubar", noMenubar_);

    settings.endGroup();

    /************
     *** Text ***
     ************/

    settings.beginGroup ("text");

    settings.setValue ("noWrap", !wrapByDefault_);
    settings.setValue ("noIndent", !indentByDefault_);
    settings.setValue ("autoBracket", autoBracket_);
    settings.setValue ("autoReplace", autoReplace_);

    settings.setValue ("autoSave", autoSave_);
    if (autoSave_ >= 1)
        timer_->start (autoSave_ * 1000 * 60);
    else if (timer_->isActive())
        timer_->stop();

    settings.setValue ("scrollJumpWorkaround", scrollJumpWorkaround_);

    settings.endGroup();

    /*****************
     *** Shortcuts ***
     *****************/

    settings.beginGroup ("shortcuts");

    for (int i = 0; i < uncustomizedActions_.size(); ++i)
        settings.remove (uncustomizedActions_.at (i));

    QHash<QString, QString>::const_iterator it = customActions_.constBegin();
    while (it != customActions_.constEnd())
    {
        settings.setValue (it.key(), it.value());
        ++it;
    }

    settings.endGroup();
}
/*************************/
void FN::txtPrint()
{
    QWidget *cw = ui->stackedWidget->currentWidget();
    if (!cw) return;

    /* choose an appropriate name and directory */
    QDir dir = QDir::home();
    if (!xmlPath_.isEmpty())
        dir = QFileInfo (xmlPath_).absoluteDir();
    QModelIndex indx = ui->treeView->currentIndex();
    QString fname;
    if (QObject::sender() == ui->actionPrintAll)
    {
        if (xmlPath_.isEmpty()) fname = tr ("Untitled");
        else
        {
            fname = QFileInfo (xmlPath_).fileName();
            if (fname.endsWith (".fnx"))
                fname.chop (4);
        }
    }
    else if ((fname = model_->data (indx, Qt::DisplayRole).toString()).isEmpty())
            fname = tr ("Untitled");
    fname = dir.filePath (fname);

    QPrinter printer (QPrinter::HighResolution);
    if (printer.outputFormat() == QPrinter::PdfFormat)
        printer.setOutputFileName (fname.append (".pdf"));
    /*else if (printer.outputFormat() == QPrinter::PostScriptFormat)
        printer.setOutputFileName (fname.append (".ps"));*/

    QPrintDialog *dlg = new QPrintDialog (&printer, this);
    dlg->setWindowTitle (tr ("Print Document"));

    QTextDocument *doc = nullptr;
    bool newDocCreated = false;
    if (QObject::sender() == ui->actionPrint)
    {
        doc = qobject_cast< TextEdit *>(cw)
              ->document();
    }
    else
    {
        QString text;
        if (QObject::sender() == ui->actionPrintNodes)
        {
            indx = ui->treeView->currentIndex();
            QModelIndex sibling = model_->sibling (indx.row() + 1, 0, indx);
            while (indx != sibling)
            {
                text.append (nodeAddress (indx));
                DomItem *item = static_cast<DomItem*>(indx.internalPointer());
                if (TextEdit *thisTextEdit = widgets_.value (item))
                    text.append (thisTextEdit->toHtml()); // the node text may have been edited
                else
                {
                    QDomNodeList lst = item->node().childNodes();
                    text.append (lst.item (0).nodeValue());
                }
                indx = model_->adjacentIndex (indx, true);
            }
        }
        else// if (QObject::sender() == ui->actionPrintAll)
        {
            indx = model_->index (0, 0, QModelIndex());
            while (indx.isValid())
            {
                text.append (nodeAddress (indx));
                DomItem *item = static_cast<DomItem*>(indx.internalPointer());
                if (TextEdit *thisTextEdit = widgets_.value (item))
                    text.append (thisTextEdit->toHtml());
                else
                {
                    QDomNodeList lst = item->node().childNodes();
                    text.append (lst.item (0).nodeValue());
                }
                indx = model_->adjacentIndex (indx, true);
            }
        }
        doc = new QTextDocument();
        newDocCreated = true;
        doc->setHtml (text);
    }

    if (dlg->exec() == QDialog::Accepted)
        doc->print (&printer);
    delete dlg;
    if (newDocCreated) delete doc;
}
/*************************/
void FN::exportHTML()
{
    QWidget *cw = ui->stackedWidget->currentWidget();
    if (!cw) return;

    QDialog *dialog = new QDialog (this);
    dialog->setWindowTitle (tr ("Export HTML"));
    QGridLayout *grid = new QGridLayout;
    grid->setSpacing (5);
    grid->setContentsMargins (5, 5, 5, 5);

    QGroupBox *groupBox = new QGroupBox (tr ("Export:"));
    QRadioButton *radio1 = new QRadioButton (tr ("&Current node"));
    radio1->setChecked (true);
    QRadioButton *radio2 = new QRadioButton (tr ("With all &sub-nodes"));
    QRadioButton *radio3 = new QRadioButton (tr ("&All nodes"));
    QVBoxLayout *vbox = new QVBoxLayout;
    vbox->addWidget (radio1);
    vbox->addWidget (radio2);
    vbox->addWidget (radio3);
    connect (radio1, &QAbstractButton::toggled, this, &FN::setHTMLName);
    connect (radio2, &QAbstractButton::toggled, this, &FN::setHTMLName);
    connect (radio3, &QAbstractButton::toggled, this, &FN::setHTMLName);
    vbox->addStretch (1);
    groupBox->setLayout (vbox);

    QLabel *label = new QLabel();
    label->setText (tr ("Output file:"));

    htmlPahEntry_ = new LineEdit();
    htmlPahEntry_->returnOnClear = false;
    htmlPahEntry_->setMinimumWidth (150);
    QModelIndex indx = ui->treeView->currentIndex();
    QDir dir = QDir::home();
    if (!xmlPath_.isEmpty())
        dir = QFileInfo (xmlPath_).absoluteDir();
    QString fname;
    if ((fname = model_->data (indx, Qt::DisplayRole).toString()).isEmpty())
        fname = tr ("Untitled");
    fname.append (".html");
    fname = dir.filePath (fname);
    htmlPahEntry_->setText (fname);
    connect (htmlPahEntry_, &QLineEdit::returnPressed, dialog, &QDialog::accept);

    QToolButton *openBtn = new QToolButton();
    openBtn->setIcon (symbolicIcon::icon (":icons/document-open.svg"));
    openBtn->setToolTip (tr ("Select path"));
    connect (openBtn, &QAbstractButton::clicked, this, &FN::setHTMLPath);
    QSpacerItem *spacer = new QSpacerItem (1, 5);
    QPushButton *cancelButton = new QPushButton (symbolicIcon::icon (":icons/dialog-cancel.svg"), tr ("Cancel"));
    connect (cancelButton, &QAbstractButton::clicked, dialog, &QDialog::reject);
    QPushButton *okButton = new QPushButton (symbolicIcon::icon (":icons/dialog-ok.svg"), tr ("OK"));
    connect (okButton, &QAbstractButton::clicked, dialog, &QDialog::accept);


    grid->addWidget (groupBox, 0, 0, 1, 2);
    grid->addWidget (label, 1, 0);
    grid->addWidget (htmlPahEntry_, 1, 1, 1, 3);
    grid->addWidget (openBtn, 1, 4, Qt::AlignLeft);
    grid->addItem (spacer, 2, 0);
    grid->addWidget (cancelButton, 3, 1, Qt::AlignRight);
    grid->addWidget (okButton, 3, 2, 1, 3, Qt::AlignRight);
    grid->setColumnStretch (1, 1);
    grid->setRowStretch (2, 1);

    dialog->setLayout (grid);
    /*dialog->resize (dialog->minimumWidth(),
                    dialog->minimumHeight());*/
    /*dialog->show();
    dialog->move (x() + width()/2 - dialog->width(),
                  y() + height()/2 - dialog->height());*/

    int sel = 0;
    switch (dialog->exec()) {
    case QDialog::Accepted:
        if (radio2->isChecked())
            sel = 1;
        else if (radio3->isChecked())
            sel = 2;
        fname = htmlPahEntry_->text();
        delete dialog;
        htmlPahEntry_ = nullptr;
        break;
    case QDialog::Rejected:
    default:
        delete dialog;
        htmlPahEntry_ = nullptr;
        return;
    }

    QTextDocument *doc = nullptr;
    bool newDocCreated = false;
    if (sel == 0)
        doc = qobject_cast< TextEdit *>(cw)->document();
    else
    {
        QString text;
        if (sel == 1)
        {
            indx = ui->treeView->currentIndex();
            QModelIndex sibling = model_->sibling (indx.row() + 1, 0, indx);
            while (indx != sibling)
            {
                text.append (nodeAddress (indx));
                DomItem *item = static_cast<DomItem*>(indx.internalPointer());
                if (TextEdit *thisTextEdit = widgets_.value (item))
                    text.append (thisTextEdit->toHtml()); // the node text may have been edited
                else
                {
                    QDomNodeList lst = item->node().childNodes();
                    text.append (lst.item (0).nodeValue());
                }
                indx = model_->adjacentIndex (indx, true);
            }
        }
        else// if (sel == 2)
        {
            indx = model_->index (0, 0, QModelIndex());
            while (indx.isValid())
            {
                text.append (nodeAddress (indx));
                DomItem *item = static_cast<DomItem*>(indx.internalPointer());
                if (TextEdit *thisTextEdit = widgets_.value (item))
                    text.append (thisTextEdit->toHtml());
                else
                {
                    QDomNodeList lst = item->node().childNodes();
                    text.append (lst.item (0).nodeValue());
                }
                indx = model_->adjacentIndex (indx, true);
            }
        }
        doc = new QTextDocument();
        newDocCreated = true;
        doc->setHtml (text);
    }

    QTextDocumentWriter writer (fname, "html");
    bool success = false;
    success = writer.write (doc);
    if (newDocCreated) delete doc;
    if (!success)
    {
        QString str = writer.device()->errorString ();
        MessageBox msgBox (QMessageBox::Warning,
                           tr ("FeatherNotes"),
                           tr ("<center><b><big>Cannot be saved!</big></b></center>"),
                           QMessageBox::Close,
                           this);
        msgBox.changeButtonText (QMessageBox::Close, tr ("Close"));
        msgBox.setInformativeText (QString ("<center><i>%1.</i></center>").arg (str));
        msgBox.setParent (this, Qt::Dialog);
        msgBox.setWindowModality (Qt::WindowModal);
        msgBox.exec();
    }
}
/*************************/
QString FN::nodeAddress (QModelIndex index)
{
    QString res;
    if (!index.isValid()) return res;

    res.prepend (model_->data (index, Qt::DisplayRole).toString());
    QModelIndex indx = model_->parent (index);
    while (indx.isValid())
    {
        res.prepend (QString ("%1 > ").arg (model_->data (indx, Qt::DisplayRole).toString()));
        indx = model_->parent (indx);
    }
    res = QString ("<br><center><h2>%1</h2></center><br>").arg (res);

    return res;
}
/*************************/
void FN::setHTMLName (bool checked)
{
    if (!checked) return;
    QList<QDialog *> list = findChildren<QDialog*>();
    if (list.isEmpty()) return;
    QList<QRadioButton *> list1;
    int i = 0;
    for (i = 0; i < list.count(); ++i)
    {
        list1 = list.at (i)->findChildren<QRadioButton *>();
        if (!list1.isEmpty()) break;
    }
    if (list1.isEmpty()) return;
    int index = list1.indexOf (qobject_cast< QRadioButton *>(QObject::sender()));

    /* choose an appropriate name */
    QModelIndex indx = ui->treeView->currentIndex();
    QString fname;
    if (index == 2)
    {
        if (xmlPath_.isEmpty()) fname = tr ("Untitled");
        else
        {
            fname = QFileInfo (xmlPath_).fileName();
            if (fname.endsWith (".fnx"))
                fname.chop (4);
        }
    }
    else if ((fname = model_->data (indx, Qt::DisplayRole).toString()).isEmpty())
        fname = tr ("Untitled");
    fname.append (".html");

    QString str = htmlPahEntry_->text();
    QStringList strList = str.split ("/");
    if (strList.count() == 1)
    {
        QDir dir = QDir::home();
        if (!xmlPath_.isEmpty())
            dir = QFileInfo (xmlPath_).absoluteDir();
        fname = dir.filePath (fname);
    }
    else
    {
        QString last = strList.last();
        int lstIndex = str.lastIndexOf (last);
        fname = str.replace (lstIndex, last.count(), fname);
    }

    htmlPahEntry_->setText (fname);
}
/*************************/
void FN::setHTMLPath (bool)
{
    QString path;
    if ((path = htmlPahEntry_->text()).isEmpty())
        path = QDir::home().filePath (tr ("Untitled") + ".html");

    QString HTMLPath;
    FileDialog dialog (this);
    dialog.setAcceptMode (QFileDialog::AcceptSave);
    dialog.setWindowTitle (tr ("Save HTML As..."));
    dialog.setFileMode (QFileDialog::AnyFile);
    dialog.setNameFilter (tr ("HTML Files (*.html *.htm)"));
    dialog.setDirectory (path.section ("/", 0, -2)); // workaround for KDE
    dialog.selectFile (path);
    dialog.autoScroll();
    if (dialog.exec())
    {
        HTMLPath = dialog.selectedFiles().at (0);
        if (HTMLPath.isEmpty() || QFileInfo (HTMLPath).isDir())
            return;
    }
    else return;

    htmlPahEntry_->setText (HTMLPath);
}
/*************************/
void FN::setPswd()
{
    int index = ui->stackedWidget->currentIndex();
    if (index == -1) return;

    QDialog *dialog = new QDialog (this);
    dialog->setWindowTitle (tr ("Set Password"));
    QGridLayout *grid = new QGridLayout;
    grid->setSpacing (5);
    grid->setContentsMargins (5, 5, 5, 5);

    LineEdit *lineEdit1 = new LineEdit();
    lineEdit1->setMinimumWidth (200);
    lineEdit1->setEchoMode (QLineEdit::Password);
    lineEdit1->setPlaceholderText (tr ("Type password"));
    connect (lineEdit1, &QLineEdit::returnPressed, this, &FN::reallySetPswrd);
    LineEdit *lineEdit2 = new LineEdit();
    lineEdit2->returnOnClear = false;
    lineEdit2->setEchoMode (QLineEdit::Password);
    lineEdit2->setPlaceholderText (tr ("Retype password"));
    connect (lineEdit2, &QLineEdit::returnPressed, this, &FN::reallySetPswrd);
    QLabel *label = new QLabel();
    QSpacerItem *spacer = new QSpacerItem (1, 10);
    QPushButton *cancelButton = new QPushButton (symbolicIcon::icon (":icons/dialog-cancel.svg"), tr ("Cancel"));
    QPushButton *okButton = new QPushButton (symbolicIcon::icon (":icons/dialog-ok.svg"), tr ("OK"));
    connect (cancelButton, &QAbstractButton::clicked, dialog, &QDialog::reject);
    connect (okButton, &QAbstractButton::clicked, this, &FN::reallySetPswrd);

    grid->addWidget (lineEdit1, 0, 0, 1, 3);
    grid->addWidget (lineEdit2, 1, 0, 1, 3);
    grid->addWidget (label, 2, 0, 1, 3);
    grid->addItem (spacer, 3, 0);
    grid->addWidget (cancelButton, 4, 0, 1, 2, Qt::AlignRight);
    grid->addWidget (okButton, 4, 2, Qt::AlignCenter);
    grid->setColumnStretch (1, 1);
    grid->setRowStretch (3, 1);
    label->setVisible (false);

    dialog->setLayout (grid);
    /*dialog->resize (dialog->minimumWidth(),
                    dialog->minimumHeight());*/
    /*dialog->show();
    dialog->move (x() + width()/2 - dialog->width(),
                  y() + height()/2 - dialog->height());*/

    switch (dialog->exec()) {
    case QDialog::Accepted:
        if (pswrd_ != lineEdit1->text())
        {
            pswrd_ = lineEdit1->text();
            noteModified();
        }
        delete dialog;
        break;
    case QDialog::Rejected:
    default:
        delete dialog;
        break;
    }
}
/*************************/
void FN::reallySetPswrd()
{
    QList<QDialog *> list = findChildren<QDialog*>();
    if (list.isEmpty()) return;

    QList<LineEdit *> listEdit;
    int i = 0;
    for (i = 0; i < list.count(); ++i)
    {
        listEdit = list.at (i)->findChildren<LineEdit *>();
        if (!listEdit.isEmpty()) break;
    }
    if (listEdit.isEmpty()) return;
    if (listEdit.count() < 2) return;

    QList<QLabel *> listLabel = list.at (i)->findChildren<QLabel *>();
    if (listLabel.isEmpty()) return;

    QList<QPushButton *> listBtn = list.at (i)->findChildren<QPushButton *>();
    if (listBtn.isEmpty()) return;

    listBtn.at (0)->setDefault (false); // don't interfere
    LineEdit *lineEdit1 = listEdit.at (0);
    LineEdit *lineEdit2 = listEdit.at (1);
    if (QObject::sender() == lineEdit1)
    {
        listLabel.at (0)->setVisible (false);
        lineEdit2->setFocus();
        return;
    }

    if (lineEdit1->text() != lineEdit2->text())
    {
        listLabel.at (0)->setText (tr ("<center>Passwords were different. Retry!</center>"));
        listLabel.at (0)->setVisible (true);
    }
    else
        list.at (i)->accept();
}
/*************************/
bool FN::isPswrdCorrect()
{
    if (tray_)
    {
        if (underE_ && QObject::sender() == nullptr) // opened by command line
        {
            if (!isVisible())
            {
                activateTray();
                QCoreApplication::processEvents();
            }
            else // not needed really
            {
                raise();
                activateWindow();
            }
        }
        else if (!underE_&& (!isVisible() || !isActiveWindow()))
        {
            activateTray();
            QCoreApplication::processEvents();
        }
    }

    QDialog *dialog = new QDialog (this);
    dialog->setWindowTitle (tr ("Enter Password"));
    QGridLayout *grid = new QGridLayout;
    grid->setSpacing (5);
    grid->setContentsMargins (5, 5, 5, 5);

    LineEdit *lineEdit = new LineEdit();
    lineEdit->setMinimumWidth (200);
    lineEdit->setEchoMode (QLineEdit::Password);
    lineEdit->setPlaceholderText (tr ("Enter Password"));
    connect (lineEdit, &QLineEdit::returnPressed, this, &FN::checkPswrd);
    QLabel *label = new QLabel();
    QSpacerItem *spacer = new QSpacerItem (1, 5);
    QPushButton *cancelButton = new QPushButton (symbolicIcon::icon (":icons/dialog-cancel.svg"), tr ("Cancel"));
    QPushButton *okButton = new QPushButton (symbolicIcon::icon (":icons/dialog-ok.svg"), tr ("OK"));
    connect (cancelButton, &QAbstractButton::clicked, dialog, &QDialog::reject);
    connect (okButton, &QAbstractButton::clicked, this, &FN::checkPswrd);

    grid->addWidget (lineEdit, 0, 0, 1, 3);
    grid->addWidget (label, 1, 0, 1, 3);
    grid->addItem (spacer, 2, 0);
    grid->addWidget (cancelButton, 3, 0, 1, 2, Qt::AlignRight);
    grid->addWidget (okButton, 3, 2, Qt::AlignCenter);
    grid->setColumnStretch (1, 1);
    grid->setRowStretch (2, 1);
    label->setVisible (false);

    dialog->setLayout (grid);
    /*dialog->resize (dialog->minimumWidth(),
                    dialog->minimumHeight());*/
    /*dialog->show();
    dialog->move (x() + width()/2 - dialog->width(),
                  y() + height()/2 - dialog->height());*/

    bool res = true;
    switch (dialog->exec()) {
    case QDialog::Accepted:
        if (pswrd_ != lineEdit->text())
            res = false;
        delete dialog;
        break;
    case QDialog::Rejected:
    default:
        delete dialog;
        res = false;
        break;
    }

    return res;
}
/*************************/
void FN::checkPswrd()
{
    QList<QDialog *> list = findChildren<QDialog*>();
    if (list.isEmpty()) return;

    QList<LineEdit *> listEdit;
    int i = 0;
    for (i = 0; i < list.count(); ++i)
    {
        listEdit = list.at (i)->findChildren<LineEdit *>();
        if (!listEdit.isEmpty()) break;
    }
    if (listEdit.isEmpty()) return;

    QList<QLabel *> listLabel = list.at (i)->findChildren<QLabel *>();
    if (listLabel.isEmpty()) return;

    QList<QPushButton *> listBtn = list.at (i)->findChildren<QPushButton *>();
    if (listBtn.isEmpty()) return;

    listBtn.at (0)->setDefault (false); // don't interfere

    if (listEdit.at (0)->text() != pswrd_)
    {
        listLabel.at (0)->setText (tr ("<center>Wrong password. Retry!</center>"));
        listLabel.at (0)->setVisible (true);
    }
    else
        list.at (i)->accept();
}
/*************************/
void FN::aboutDialog()
{
    class AboutDialog : public QDialog {
    public:
        explicit AboutDialog (QWidget* parent = nullptr, Qt::WindowFlags f = nullptr) : QDialog (parent, f) {
            aboutUi.setupUi (this);
            aboutUi.textLabel->setOpenExternalLinks (true);
        }
        void setTabTexts (const QString& first, const QString& sec) {
            aboutUi.tabWidget->setTabText (0, first);
            aboutUi.tabWidget->setTabText (1, sec);
        }
        void setMainIcon (const QIcon& icn) {
            aboutUi.iconLabel->setPixmap (icn.pixmap (64, 64));
        }
        void settMainTitle (const QString& title) {
            aboutUi.titleLabel->setText (title);
        }
        void setMainText (const QString& txt) {
            aboutUi.textLabel->setText (txt);
        }
    private:
        Ui::AboutDialog aboutUi;
    };

    AboutDialog dialog (this);

    QIcon FPIcon = QIcon::fromTheme ("feathernotes");
    if (FPIcon.isNull())
        FPIcon = QIcon (":icons/feathernotes.svg");
    dialog.setMainIcon (FPIcon);
    dialog.settMainTitle (QString ("<center><b><big>%1 %2</big></b></center><br>").arg (qApp->applicationName()).arg (qApp->applicationVersion()));
    dialog.setMainText ("<center> " + tr ("A lightweight notes manager") + " </center>\n<center> "
                        + tr ("based on Qt5") + " </center><br><center> "
                        + tr ("Author")+": <a href='mailto:tsujan2000@gmail.com?Subject=My%20Subject'>Pedram Pourang ("
                        + tr ("aka.") + " Tsu Jan)</a> </center><p></p>");
    dialog.setTabTexts (tr ("About FeatherNotes"), tr ("Translators"));
    dialog.setWindowTitle (tr ("About FeatherNotes"));
    dialog.setWindowModality (Qt::WindowModal);
    dialog.exec();
}
/*************************/
void FN::showHelpDialog()
{
    FHelp *dlg = new FHelp (this);
    dlg->resize (ui->stackedWidget->size().expandedTo (ui->treeView->size()));
    switch (dlg->exec()) {
    case QDialog::Rejected:
    default:
        delete dlg;
        break;
    }
}
/*************************/
bool FN::event (QEvent *event)
{
    /* NOTE: This is a workaround for an old Qt bug, because of which,
             QTimer may not work after resuming from suspend or hibernation. */
    if (event->type() == QEvent::WindowActivate
        && timer_->isActive() && timer_->remainingTime() <= 0)
    {
        if (autoSave_ >= 1)
        {
            autoSaving();
            timer_->start (autoSave_ * 1000 * 60);
        }
    }
    return QMainWindow::event (event);
}

}
