#include "mainwindow.h"
#include "ui_mainwindow.h"

#include <QCloseEvent>
#include <QScrollBar>
#include <QToolBar>
#include <QToolButton>
#include <QCheckBox>
#include <QTableWidgetItem>
#include <QShortcut>
#include <QFile>
#include <QClipboard>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QSettings>
#include <QCryptographicHash>
#include <QHostAddress>
#include <QFileDialog>
#include <QMessageBox>
#include <QRegularExpression>
#include <QProcess>
#include <QTimer>
#include <QScriptEngine>
#include <QDateTime>
#include <QProcessEnvironment>
#include <QtConcurrent>
#include <QProgressDialog>

#include "../misc/singleshortcut.h"
#include "layerwidget.h"
#include "layeritem.h"
#include "colorgrid.h"
#include "aboutdialog.h"
#include "configuredialog.h"
#include "brushsettingswidget.h"
#include "gradualbox.h"
#include "roomsharebar.h"
#include "developerconsole.h"
#include "networkindicator.h"
#include "../../common/network/clientsocket.h"
#include "../../common/network/localnetworkinterface.h"
#include "../paintingTools/brush/brushmanager.h"
#include "../../common/common.h"
#include "../misc/platformextend.h"
#include "../misc/singleton.h"
#include "../misc/shortcutmanager.h"
#include "../misc/errortable.h"
#include "../misc/archivefile.h"
#include "../misc/psdexport.h"

#define client_socket (Singleton<ClientSocket>::instance())
#define shortcut_manager (Singleton<ShortcutManager>::instance())

MainWindow::MainWindow(QWidget *parent) :
    QMainWindow(parent),
    ui(new Ui::MainWindow),
    lastBrushAction(nullptr),
    brushSettingControl_(nullptr),
    toolbar_(nullptr),
    brushActionGroup_(nullptr),
    colorPickerButton_(nullptr),
    moveToolButton_(nullptr),
    scriptEngine_(nullptr),
    console_(nullptr),
    networkIndicator_(nullptr)
{
    ui->setupUi(this);
    defaultView = saveState();
    init();
}

MainWindow::~MainWindow()
{
    delete ui;
}

void MainWindow::stylize()
{
    QFile stylesheet("./iconset/style.qss",this);
    stylesheet.open(QIODevice::ReadOnly);
    QTextStream stream(&stylesheet);
    QString string;
    string = stream.readAll();
    this->setStyleSheet(string);
    stylesheet.close();
}

void MainWindow::init()
{
    auto&& roomName = client_socket.roomName();
    setWindowTitle(roomName+tr(" - Mr.Paint"));
    ui->canvas->resize(client_socket.canvasSize());

    ui->centralWidget->setBackgroundRole(QPalette::Dark);
    ui->centralWidget->setCanvas(ui->canvas);

    connect(ui->canvas, &Canvas::contentMovedBy,
            [this](const QPoint& p){
        ui->centralWidget->moveBy(p * ui->centralWidget->currentScaleFactor());
    });

    connect(ui->panorama, &PanoramaWidget::scaled,
            ui->centralWidget, &CanvasContainer::setScaleFactor);
    connect(ui->centralWidget, &CanvasContainer::scaled,
            ui->panorama, &PanoramaWidget::setScaled);
    connect(ui->panorama, &PanoramaWidget::rotated,
            ui->centralWidget, &CanvasContainer::setRotation);
    connect(ui->centralWidget, &CanvasContainer::rotated,
            ui->panorama, &PanoramaWidget::setRotation);

    connect(ui->lineEdit,&QLineEdit::returnPressed,
            this,&MainWindow::onSendPressed);
    connect(ui->pushButton,&QPushButton::clicked,
            this,&MainWindow::onSendPressed);

    connect(ui->canvas, &Canvas::newBrushSettings,
            this, &MainWindow::onBrushSettingsChanged);

    connect(ui->layerWidget,&LayerWidget::itemHide,
            ui->canvas, &Canvas::hideLayer);
    connect(ui->layerWidget,&LayerWidget::itemShow,
            ui->canvas, &Canvas::showLayer);
    connect(ui->layerWidget,&LayerWidget::itemLock,
            ui->canvas, &Canvas::lockLayer);
    connect(ui->layerWidget,&LayerWidget::itemUnlock,
            ui->canvas,  &Canvas::unlockLayer);
    connect(ui->layerWidget,&LayerWidget::itemSelected,
            ui->canvas, &Canvas::layerSelected);

    connect(ui->colorBox, &ColorBox::colorChanged,
            this, &MainWindow::brushColorChange);
    connect(this, &MainWindow::brushColorChange,
            ui->canvas, &Canvas::setBrushColor);
    connect(ui->canvas, &Canvas::canvasToolComplete,
            this, &MainWindow::onCanvasToolComplete);

    connect(ui->colorGrid,
            static_cast<void (ColorGrid::*)(const int&)>
            (&ColorGrid::colorDroped),
            this, &MainWindow::onColorGridDroped);
    connect(ui->colorGrid, &ColorGrid::colorPicked,
            this, &MainWindow::onColorGridPicked);
    connect(ui->panorama, &PanoramaWidget::refresh,
            this, &MainWindow::onPanoramaRefresh);
    connect(ui->centralWidget, &CanvasContainer::rectChanged,
            ui->panorama, &PanoramaWidget::onRectChange);
    connect(ui->panorama, &PanoramaWidget::moveTo,
            ui->centralWidget,
            static_cast<void (CanvasContainer::*)(const QPointF&)>
            (&CanvasContainer::centerOn));

    connect(ui->memberList, &MemberListWidget::memberGetKicked,
            this, &MainWindow::requestKickUser);

    layerWidgetInit();
    colorGridInit();
    statusBarInit();
    toolbarInit();
    viewInit();
    shortcutInit();
    //    stylize();
    socketInit();
    scriptInit();
    // NOTE: turn off the pool once we are ready
    client_socket.setPoolEnabled(false);
}

void MainWindow::routerInit()
{

}

void MainWindow::scriptInit()
{
    scriptEngine_ = new QScriptEngine;

    QScriptValue scriptMainWindow = scriptEngine_->newQObject(this);
    scriptEngine_->globalObject().setProperty("mainwindow", scriptMainWindow);

    QScriptValue scriptCanvas = scriptEngine_->newQObject(this->ui->canvas);
    scriptEngine_->globalObject().setProperty("canvas", scriptCanvas);

    QScriptValue scriptClientSocket = scriptEngine_->newQObject(&Singleton<ClientSocket>::instance());
    scriptEngine_->globalObject().setProperty("clientsocket", scriptClientSocket);
}

void MainWindow::layerWidgetInit()
{
    for(int i=0;i<10;++i){
        addLayer();
    }
    ui->layerWidget->itemAt(0)->setSelect(true);
    ui->canvas->loadLayers();
}

void MainWindow::colorGridInit()
{
    QSettings settings(GlobalDef::SETTINGS_NAME,
                       QSettings::defaultFormat(),
                       qApp);
    QByteArray data = settings.value("colorgrid/pal")
            .toByteArray();
    if(data.isEmpty()){
        return;
    }else{
        ui->colorGrid->dataImport(data);
    }
}

void MainWindow::viewInit()
{
    QSettings settings(GlobalDef::SETTINGS_NAME,
                       QSettings::defaultFormat(),
                       qApp);
    QByteArray data = settings.value("mainwindow/view")
            .toByteArray();
    if(data.isEmpty()){
        return;
    }else{
        restoreState(data);
    }
}

void MainWindow::toolbarInit()
{
    toolbar_ = new QToolBar(tr("Brushes"), this);
    toolbar_->setObjectName("BrushToolbar");
    this->addToolBar(Qt::TopToolBarArea, toolbar_);
    brushActionGroup_ = new QActionGroup(this);

    // always remember last action
    auto restoreAction =  [this](){
        if(lastBrushAction){
            lastBrushAction->trigger();
        }
    };

    auto brushes = Singleton<BrushManager>::instance().allBrushes();

    for(auto &item: brushes){
        // create action on tool bar
        QAction * action = toolbar_->addAction(item->icon(),
                                               item->displayName());
        action->setObjectName(item->name());
        connect(action, &QAction::triggered,
                this, &MainWindow::onBrushTypeChange);
        action->setCheckable(true);
        action->setAutoRepeat(false);
        brushActionGroup_->addAction(action);

        // set shortcut for the brush
        // TODO: enable other type of shortcut
        //        ShT shortcut_type = (ShT)config["type"].toInt();

        //        switch(shortcut_type){
        //        case ShT::Single:
        //        {
        //            regShortcut<>(item->shortcut(),
        //                          [this, action](){
        //                      lastBrushAction = brushActionGroup_->checkedAction();
        //                      action->trigger();
        //                  },
        //            restoreAction);
        //        }
        //            break;
        //        case ShT::Multiple:
        //        default:
        //        {
        //            regShortcut<>(item->shortcut(),
        //                          [this, action](){
        //                      lastBrushAction = brushActionGroup_->checkedAction();
        //                      action->trigger();
        //                  });
        //        }
        //            break;
        //        }
        regShortcut<>(item->shortcut(),
                      [this, action](){
            lastBrushAction = brushActionGroup_->checkedAction();
            action->trigger();
        },
        restoreAction);

        action->setToolTip(
                    tr("%1\n"
                       "Shortcut: %2")
                    .arg(item->displayName())
                    .arg(item->shortcut().toString()));
        if(toolbar_->actions().count() < 2){
            action->trigger();
        }
    }


    // doing hacking to color picker
    QIcon colorpickerIcon(":/iconset/ui/brush/colorpicker.png");
    QAction *colorpicker = toolbar_->addAction(colorpickerIcon,
                                               tr("Color Picker"));
    colorpicker->setCheckable(true);
    colorpicker->setAutoRepeat(false);
    // we need the real QToolButton to know weather the picker is
    // canceled by hand
    auto l = colorpicker->associatedWidgets();
    if(l.count() > 1){
        QToolButton *b = qobject_cast<QToolButton *>(l[1]);
        if(b){
            colorPickerButton_ = b;
            connect(b, &QToolButton::clicked,
                    this, &MainWindow::onColorPickerPressed);

            auto colorpicker_key = Singleton<ShortcutManager>::instance()
                    .shortcut("colorpicker")["key"].toString();
            SingleShortcut *pickerShortcut = new SingleShortcut(this);
            pickerShortcut->setKey(colorpicker_key);
            connect(pickerShortcut, &SingleShortcut::activated,
                    b, &QToolButton::click);
            connect(pickerShortcut, &SingleShortcut::inactivated,
                    b, &QToolButton::click);
            colorpicker->setToolTip(
                        tr("%1\n"
                           "Shortcut: %2")
                        .arg(colorpicker->text())
                        .arg(colorpicker_key));
        }
    }

    // doing hacking for move tool
    QIcon moveIcon(":/iconset/ui/brush/move.png");
    QAction *moveTool = toolbar_->addAction(moveIcon,
                                            tr("Move Tool"));
    moveTool->setCheckable(true);
    moveTool->setAutoRepeat(false);
    // we need the real QToolButton to know weather the tool is
    // canceled by hand
    auto l2 = moveTool->associatedWidgets();
    if(l2.count() > 1){
        QToolButton *b = qobject_cast<QToolButton *>(l2[1]);
        if(b){
            moveToolButton_ = b;
            connect(b, &QToolButton::clicked,
                    this, &MainWindow::onMoveToolPressed);
            auto movetool_key = Singleton<ShortcutManager>::instance()
                    .shortcut("movetool")["key"].toString();
            SingleShortcut *moveToolShortcut = new SingleShortcut(this);
            moveToolShortcut->setKey(movetool_key);
            connect(moveToolShortcut, &SingleShortcut::activated,
                    b, &QToolButton::click);
            connect(moveToolShortcut, &SingleShortcut::inactivated,
                    b, &QToolButton::click);
            moveTool->setToolTip(
                        tr("%1\n"
                           "Shortcut: %2")
                        .arg(moveTool->text())
                        .arg(movetool_key));
        }
    }

    QToolBar *tabletEnableToolbar = new QToolBar(tr("Tablet"), this);
    tabletEnableToolbar->setObjectName("TabletEnableToolbar");
    QAction *tabletAction = tabletEnableToolbar->addAction(QIcon(":/iconset/ui/tablet.png"), tr("Draw with Tablet"));
    tabletAction->setCheckable(true);
    connect(tabletAction, &QAction::toggled, ui->canvas, &Canvas::setTabletEnabled);
    addToolBar(Qt::TopToolBarArea, tabletEnableToolbar);

    // for brush width
    QToolBar *brushSettingToolbar = new QToolBar(tr("Brush Settings"), this);
    brushSettingToolbar->setObjectName("BrushSettingToolbar");
    this->addToolBar(Qt::TopToolBarArea, brushSettingToolbar);
    BrushSettingsWidget * brushSettingWidget = new BrushSettingsWidget(this);
    connect(brushSettingWidget, &BrushSettingsWidget::widthChanged,
            ui->canvas, &Canvas::setBrushWidth);
    connect(brushSettingWidget, &BrushSettingsWidget::hardnessChanged,
            ui->canvas, &Canvas::setBrushHardness);
    connect(brushSettingWidget, &BrushSettingsWidget::thicknessChanged,
            ui->canvas, &Canvas::setBrushThickness);
    connect(brushSettingWidget, &BrushSettingsWidget::waterChanged,
            ui->canvas, &Canvas::setBrushWater);
    connect(brushSettingWidget, &BrushSettingsWidget::extendChanged,
            ui->canvas, &Canvas::setBrushExtend);
    connect(brushSettingWidget, &BrushSettingsWidget::mixinChanged,
            ui->canvas, &Canvas::setBrushMixin);
    connect(brushSettingToolbar, &QToolBar::orientationChanged,
            brushSettingWidget, &BrushSettingsWidget::setOrientation);


    //    ShortcutManager &stctmgr = Singleton<ShortcutManager>::instance();
    regShortcut<>("subwidth",
                  std::bind(&BrushSettingsWidget::widthDown, brushSettingWidget));
    regShortcut<>("addwidth",
                  std::bind(&BrushSettingsWidget::widthUp, brushSettingWidget));

    regShortcut<>("subhardness",
                  std::bind(&BrushSettingsWidget::hardnessDown, brushSettingWidget));
    regShortcut<>("addhardness",
                  std::bind(&BrushSettingsWidget::hardnessUp, brushSettingWidget));

    regShortcut<>("subthickness",
                  std::bind(&BrushSettingsWidget::thicknessDown, brushSettingWidget));
    regShortcut<>("addthickness",
                  std::bind(&BrushSettingsWidget::thicknessUp, brushSettingWidget));

    //    // shortcuts for water control
    //    QShortcut* waterActionSub = new QShortcut(this);
    //    waterActionSub->setKey(stctmgr.shortcut("subwater")["key"].toString());
    //    connect(waterActionSub, &QShortcut::activated,
    //            brushSettingWidget, &BrushSettingsWidget::waterDown);
    //    QShortcut* waterActionAdd = new QShortcut(this);
    //    waterActionAdd->setKey(stctmgr.shortcut("addwater")["key"].toString());
    //    connect(waterActionAdd, &QShortcut::activated,
    //            brushSettingWidget, &BrushSettingsWidget::waterUp);
    //    // shortcuts for extend control
    //    QShortcut* extendActionSub = new QShortcut(this);
    //    extendActionSub->setKey(stctmgr.shortcut("subextend")["key"].toString());
    //    connect(extendActionSub, &QShortcut::activated,
    //            brushSettingWidget, &BrushSettingsWidget::extendDown);
    //    QShortcut* extendActionAdd = new QShortcut(this);
    //    extendActionAdd->setKey(stctmgr.shortcut("addextend")["key"].toString());
    //    connect(extendActionAdd, &QShortcut::activated,
    //            brushSettingWidget, &BrushSettingsWidget::extendUp);
    //    // shortcuts for mixin control
    //    QShortcut* mixinActionSub = new QShortcut(this);
    //    mixinActionSub->setKey(stctmgr.shortcut("submixin")["key"].toString());
    //    connect(mixinActionSub, &QShortcut::activated,
    //            brushSettingWidget, &BrushSettingsWidget::mixinDown);
    //    QShortcut* mixinActionAdd = new QShortcut(this);
    //    mixinActionAdd->setKey(stctmgr.shortcut("addmixin")["key"].toString());
    //    connect(mixinActionAdd, &QShortcut::activated,
    //            brushSettingWidget, &BrushSettingsWidget::mixinUp);

    brushSettingControl_ = brushSettingWidget;
    brushSettingToolbar->addWidget(brushSettingWidget);

    changeToBrush("BasicBrush");

    // for room share
    QToolBar* roomShareToolbar = new QToolBar(tr("Room Share"), this);
    roomShareToolbar->setObjectName("RoomShareToolbar");
    this->addToolBar(Qt::TopToolBarArea, roomShareToolbar);
    RoomShareBar* rsb = new RoomShareBar(this);
    rsb->setAddress(client_socket.toUrl());
    roomShareToolbar->addWidget(rsb);

    if(client_socket.isIPv6Address()){
        auto f = [this](){
            GradualBox::showText(tr("Notice, we detected you're using IPv6 protocol"\
                                    " which may result in that your Room URL is not available"\
                                    " for IPv4 users."));
        };

        GlobalDef::delayJob(f, 5000);
    }
}

void MainWindow::statusBarInit()
{
    networkIndicator_ = new NetworkIndicator(this);
    this->statusBar()->addPermanentWidget(networkIndicator_);
}

QString MainWindow::getRoomKey()
{
    QCryptographicHash hash(QCryptographicHash::Md5);
    auto&& roomName = client_socket.roomName();
    hash.addData(roomName.toUtf8());
    QString hashed_name = hash.result().toHex();
    QSettings settings(GlobalDef::SETTINGS_NAME,
                       QSettings::defaultFormat(),
                       qApp);
    settings.sync();
    if( !settings.contains("rooms/"+hashed_name) ){
        // Tell user that he's not owner
        qDebug()<<"hashed_name"<<hashed_name
               <<" key cannot found!";
        return QString();
    }
    QVariant key = settings.value("rooms/"+hashed_name);
    return key.toString();
}

void MainWindow::requestCloseRoom()
{
    if(!client_socket.requestCloseRoom()){
        QMessageBox::warning(this,
                             tr("Sorry"),
                             tr("Only room owner is authorized "
                                "to close the room.\n"
                                "It seems you're not room manager."));
    }
}

void MainWindow::requestKickUser(const QString& id)
{
    if(!client_socket.requestKickUser(id)){
        QMessageBox::warning(this,
                             tr("Sorry"),
                             tr("Only room owner is authorized "
                                "to kick members.\n"
                                "It seems you're not room manager."));
    }
}

void MainWindow::shortcutInit()
{
    connect(ui->action_Quit, &QAction::triggered,
            this, &MainWindow::close);
    connect(ui->actionExport_All, &QAction::triggered,
            this, &MainWindow::exportAllToFile);
    connect(ui->actionExport_Visiable, &QAction::triggered,
            this, &MainWindow::exportVisibleToFile);
    connect(ui->actionExport_All_To_Clipboard, &QAction::triggered,
            this, &MainWindow::exportAllToClipboard);
    connect(ui->actionExport_Visible_To_ClipBorad, &QAction::triggered,
            this, &MainWindow::exportVisibleToClipboard);
    connect(ui->actionReset_View, &QAction::triggered,
            this, &MainWindow::resetView);
    connect(ui->action_About_Mr_Paint, &QAction::triggered,
            this, &MainWindow::about);
    connect(ui->actionAbout_Qt, &QAction::triggered,
            &QApplication::aboutQt);
    connect(ui->actionExport_to_PSD, &QAction::triggered,
            this, &MainWindow::exportToPSD);
    connect(ui->actionClose_Room, &QAction::triggered,
            this, &MainWindow::requestCloseRoom);
    connect(ui->actionAll_Layers, &QAction::triggered,
            this, &MainWindow::clearAllLayer);
    connect(ui->actionConfiguration, &QAction::triggered,
            [this](){
        ConfigureDialog conf_dialog;
        conf_dialog.exec();
    });

    regShortcut<>("zoomin", [this](){
        this->ui->centralWidget->scaleBy(1.2);
    });
    regShortcut<>("zoomout", [this](){
        this->ui->centralWidget->scaleBy(0.8);
    });
    regShortcut<>("rotateclock", [this](){
        this->ui->centralWidget->rotateBy(10);
    });
    regShortcut<>("rotateanticlock", [this](){
        this->ui->centralWidget->rotateBy(-10);
    });
    regShortcut<>("canvasreset", [this](){
        this->ui->centralWidget->setRotation(0);
        this->ui->centralWidget->setScaleFactor(1);
    });
    regShortcut<>(QKeySequence("F12"),
                  std::bind(&MainWindow::openConsole, this));
}

void MainWindow::socketInit()
{
    connect(&client_socket, &ClientSocket::newMessage,
            this, &MainWindow::onNewMessage);
    connect(this, &MainWindow::sendMessage,
            &client_socket, &ClientSocket::sendMessage);
    connect(&client_socket, &ClientSocket::clientSocketError,
            this, &MainWindow::onClientSocketError);

    connect(&client_socket, &ClientSocket::roomAboutToClose,
            this, &MainWindow::onAboutToClose);
    connect(&client_socket, &ClientSocket::layerAllCleared,
            this, &MainWindow::onAllLayerCleared);
    connect(&client_socket, &ClientSocket::memberListFetched,
            this, &MainWindow::onMemberlistFetched);
    connect(&client_socket, &ClientSocket::getNotified,
            this, &MainWindow::onNotify);
    connect(&client_socket, &ClientSocket::getKicked,
            this, &MainWindow::onKicked);
    connect(&client_socket, &ClientSocket::delayGet,
            this, &MainWindow::onDelayGet);
}

void MainWindow::onServerDisconnected()
{
    GradualBox::showText(tr("Server Connection Failed."));
    ui->canvas->setEnabled(false);
    client_socket.stopHeartbeat();
    // TODO: reconnect to room and request login
}

void MainWindow::onAboutToClose()
{
    QMessageBox::warning(this,
                         tr("Closing"),
                         tr("Warning, the room owner has "
                            "closed the room. This room will close"
                            " when everyone leaves.\n"
                            "Save your work if you like it!"));
}

void MainWindow::onAllLayerCleared()
{
    ui->canvas->clearAllLayer();
}

void MainWindow::onMemberlistFetched(const QHash<QString, QVariantList> &list)
{
    ui->memberList->setMemberList(list);
//    ui->statusBar->showMessage(tr("Online List Refreshed."),
//                               2000);
}

void MainWindow::onNotify(const QString &content)
{
    if(content.isEmpty()){
        return;
    }

    QTextCursor c = ui->textEdit->textCursor();
    c.movePosition(QTextCursor::End);
    ui->textEdit->setTextCursor(c);
    ui->textEdit->insertHtml(content);
    ui->textEdit->verticalScrollBar()
            ->setValue(ui->textEdit->verticalScrollBar()
                       ->maximum());
    ui->textEdit->insertPlainText("\n");
}

void MainWindow::onKicked()
{
    GradualBox::showText(tr("You've been kicked by room owner."), true, 3000);
}

void MainWindow::onDelayGet(const int delay)
{
    typedef NetworkIndicator::LEVEL NL;
    if(delay < 0){
        networkIndicator_->setLevel(NL::UNKNOWN);
        return;
    }
    if(delay > 60){
        networkIndicator_->setLevel(NL::NONE);
        return;
    }
    if(delay > 20){
        networkIndicator_->setLevel(NL::LOW);
        return;
    }
    if(delay > 10){
        networkIndicator_->setLevel(NL::MEDIUM);
        return;
    }
    if(delay < 10){
        networkIndicator_->setLevel(NL::GOOD);
        return;
    }
}

void MainWindow::onClientSocketError(const int code)
{
    QMessageBox::critical(this,
                          tr("Error"),
                          tr("Sorry, an error occurred.\n"
                             "Error: %1, %2").arg(code)
                          .arg(ErrorTable::toString(code)));
}

void MainWindow::onNewMessage(const QString &content)
{
    QTextCursor c = ui->textEdit->textCursor();
    c.movePosition(QTextCursor::End);
    ui->textEdit->setTextCursor(c);
    ui->textEdit->insertPlainText(content);
    ui->textEdit->verticalScrollBar()
            ->setValue(ui->textEdit->verticalScrollBar()
                       ->maximum());

    QSettings settings(GlobalDef::SETTINGS_NAME,
                       QSettings::defaultFormat(),
                       qApp);
    bool msg_notify = settings.value("chat/msg_notify", true).toBool();
    if(!this->isActiveWindow() && msg_notify)
        PlatformExtend::notify(this);
}

void MainWindow::onSendPressed()
{
    QString string(ui->lineEdit->text());
    if(string.isEmpty() || string.count()>256){
        qDebug()<<"Warnning: text too long or empty.";
        return;
    }
    string.prepend(client_socket.userName()
                   + ": ");
    string.append('\n');
    emit sendMessage(string);
    ui->lineEdit->commit();
}

void MainWindow::onColorGridDroped(int id)
{
    QColor c = ui->colorBox->color();
    ui->colorGrid->setColor(id, c);
}

void MainWindow::onColorGridPicked(int, const QColor &c)
{
    ui->colorBox->setColor(c);
}

void MainWindow::onBrushTypeChange()
{
    changeToBrush(sender()->objectName());
}

void MainWindow::onBrushSettingsChanged(const QVariantMap &m)
{
    int width = m["width"].toInt();
    int hardness = m["hardness"].toInt();
    int thickness = m["thickness"].toInt();
    int water = m["water"].toInt();
    int extend = m["extend"].toInt();
    int mixin = m["mixin"].toInt();
    QVariantMap colorMap = m["color"].toMap();
    QColor c(colorMap["red"].toInt(),
            colorMap["green"].toInt(),
            colorMap["blue"].toInt());

    // INFO: to prevent scaled to 1px, should always
    // change width first
    if(brushSettingControl_){
        if(brushSettingControl_->width() != width)
            brushSettingControl_->setWidth(width);
        if(brushSettingControl_->hardness() != hardness)
            brushSettingControl_->setHardness(hardness);
        if(brushSettingControl_->thickness() != thickness)
            brushSettingControl_->setThickness(thickness);
        if(brushSettingControl_->water() != water)
            brushSettingControl_->setWater(water);
        if(brushSettingControl_->extend() != extend)
            brushSettingControl_->setExtend(extend);
        if(brushSettingControl_->mixin() != mixin)
            brushSettingControl_->setMixin(mixin);
    }
    if(ui->colorBox->color() != c)
        ui->colorBox->setColor(c);

}

void MainWindow::onPanoramaRefresh()
{
    ui->panorama->onImageChange(ui->canvas->grab(),
                                ui->centralWidget->visualRect().toRect());
}

void MainWindow::onMoveToolPressed(bool c)
{
    ui->canvas->onMoveTool(c);
    if(brushActionGroup_){
        brushActionGroup_->setDisabled(c);
    }
    if(colorPickerButton_){
        colorPickerButton_->setDisabled(c);
    }
}

void MainWindow::onColorPickerPressed(bool c)
{
    ui->canvas->onColorPicker(c);
    if(brushActionGroup_){
        brushActionGroup_->setDisabled(c);
    }
    if(moveToolButton_){
        moveToolButton_->setDisabled(c);
    }
}

void MainWindow::onCanvasToolComplete()
{
    if(brushActionGroup_){
        brushActionGroup_->setDisabled(false);
    }
    if(colorPickerButton_){
        colorPickerButton_->setChecked(false);
    }
    if(moveToolButton_){
        moveToolButton_->setChecked(false);
    }
}

void MainWindow::openConsole()
{
    if(!console_){
        console_ = new DeveloperConsole(this);
        connect(this, &MainWindow::scriptResult,
                console_, &DeveloperConsole::append);
        connect(console_, &DeveloperConsole::evaluate,
                this, &MainWindow::evaluateScript);
    }
    console_->show();
}

void MainWindow::changeToBrush(const QString &brushName)
{
    ui->canvas->changeBrush(brushName);
    auto f = ui->canvas->brushFeatures();
    if(!this->brushSettingControl_){
        return;
    }
    this->brushSettingControl_->setHardnessEnabled(f.support(BrushFeature::HARDNESS));
    this->brushSettingControl_->setThicknessEnabled(f.support(BrushFeature::THICKNESS));
    this->brushSettingControl_->setWaterEnabled(f.support(BrushFeature::WATER));
    this->brushSettingControl_->setExtendEnabled(f.support(BrushFeature::EXTEND));
    this->brushSettingControl_->setMixinEnabled(f.support(BrushFeature::MIXIN));
    onBrushSettingsChanged(ui->canvas->brushSettings());
}

void MainWindow::remoteAddLayer(const QString &layerName)
{
    if( layerName.isEmpty() ){
        return;
    }

    LayerItem *item = new LayerItem;
    QIcon visibility(":/iconset/ui/visibility-on.png");
    visibility.addFile(":/iconset/ui/visibility-off.png",
                       QSize(),
                       QIcon::Selected,
                       QIcon::On);
    item->setVisibleIcon(visibility);
    QIcon lock(":/iconset/ui/lock.png");
    lock.addFile(":/iconset/ui/unlock.png",
                 QSize(),
                 QIcon::Selected,
                 QIcon::On);
    item->setLockIcon(lock);
    item->setLabel(layerName);
    ui->layerWidget->addItem(item);
}

void MainWindow::addLayer(const QString &layerName)
{
    QString name = layerName;
    if(name.isNull() || name.isEmpty())
        name = QString::number(ui->canvas->layerNum());

    LayerItem *item = new LayerItem;
    QIcon visibility(":/iconset/ui/visibility-on.png");
    visibility.addFile(":/iconset/ui/visibility-off.png",
                       QSize(),
                       QIcon::Selected,
                       QIcon::On);
    item->setVisibleIcon(visibility);
    QIcon lock(":/iconset/ui/lock.png");
    lock.addFile(":/iconset/ui/unlock.png",
                 QSize(),
                 QIcon::Selected,
                 QIcon::On);
    item->setLockIcon(lock);
    item->setLabel(name);
    ui->layerWidget->addItem(item);
    ui->canvas->addLayer(name);

    // NOTICE: disable single layer clear due to lack of
    // a way to store this action in server history
    //    QAction *clearOne = new QAction(this);
    //    ui->menuClear_Canvas->insertAction(ui->actionAll_Layers,
    //                                       clearOne);
    //    clearOne->setText(tr("Layer ")+name);
    //    connect(clearOne, &QAction::triggered,
    //            [this, name, clearOne](){
    //        this->clearLayer(name);
    //    });
}

void MainWindow::deleteLayer()
{
    LayerItem * item = ui->layerWidget->selected();
    QString text = item->label();
    bool sucess = ui->canvas->deleteLayer(text);
    if(sucess) ui->layerWidget->removeItem(item);
}

void MainWindow::clearLayer(const QString &name)
{
    auto result = QMessageBox::question(this,
                                        tr("OMG"),
                                        tr("You're going to clear layer %1. "
                                           "All the work of that layer"
                                           "will be deleted and CANNOT be undone.\n"
                                           "Do you really want to do so?").arg(name),
                                        QMessageBox::Yes|QMessageBox::No);
    if(result == QMessageBox::Yes){
        ui->canvas->clearLayer(name);
        QJsonObject map;
        map.insert("request", QString("clear"));
        map.insert("type", QString("command"));
        map.insert("key", getRoomKey());
        map.insert("layer", name);
        client_socket.sendCmdPack(map);
    }

}

void MainWindow::clearAllLayer()
{
    auto result = QMessageBox::question(this,
                                        tr("OMG"),
                                        tr("You're going to clear ALL LAYERS"
                                           ". All of work in this room"
                                           "will be deleted and CANNOT be undone.\n"
                                           "Do you really want to do so?"),
                                        QMessageBox::Yes|QMessageBox::No);
    if(result == QMessageBox::Yes){
        QJsonObject map;
        QString r_key = getRoomKey();
        if(r_key.isEmpty()){
            QMessageBox::warning(this,
                                 tr("Sorry"),
                                 tr("Only room owner is authorized "
                                    "to clear the canvas.\n"
                                    "It seems you're not room manager."));
            return;
        }
        map.insert("request", QString("clearall"));
        map.insert("type", QString("command"));
        map.insert("key", getRoomKey());
        client_socket.sendCmdPack(map);
    }
}

void MainWindow::evaluateScript(const QString &script)
{
    if(!scriptEngine_){
        qWarning()<<"Cannot evaluate script before script engine init!";
        return;
    }

    // pause event process
    if(scriptEngine_->processEventsInterval() > 0){
        scriptEngine_->setProcessEventsInterval(-1);
    }

    emit scriptResult(scriptEngine_->evaluate(script).toString());
}

void MainWindow::runScript(const QString &script)
{
    if(!scriptEngine_){
        qWarning()<<"Cannot run script before script engine init!";
        return;
    }
    scriptEngine_->setProcessEventsInterval(300);
    emit scriptResult(scriptEngine_->evaluate(script).toString());
}

void MainWindow::deleteLayer(const QString &name)
{
    bool sucess = ui->canvas->deleteLayer(name);
    if(sucess) ui->layerWidget->removeItem(name);
}

void MainWindow::closeEvent( QCloseEvent * event )
{
    disconnect(&client_socket, &ClientSocket::disconnected,
               this, &MainWindow::onServerDisconnected);
    ui->canvas->pause();
    client_socket.exitFromRoom();

    QProgressDialog dialog(tr("Waiting for sync, please do not close.\n"\
                              "This will cost you 1 minute at most."),
                           QString(),
                           0, 0, this);
    dialog.setWindowModality(Qt::ApplicationModal);
    dialog.show();

    // This is a workaround to make msgBox text shown
    QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);

    QSettings settings(GlobalDef::SETTINGS_NAME,
                       QSettings::defaultFormat(),
                       qApp);
    settings.setValue("colorgrid/pal",
                      ui->colorGrid->dataExport());
    settings.setValue("mainwindow/view",
                      saveState());
    bool skip_replay = settings.value("canvas/skip_replay", true).toBool();
    if(skip_replay){
        qDebug()<<"skip replay detected, save layers";
        ui->canvas->saveLayers();
    }
    settings.sync();

    dialog.close();

    event->accept();
}

void MainWindow::exportAllToFile()
{
    QString fileName =
            QFileDialog::getSaveFileName(this,
                                         tr("Export all to file"),
                                         this->windowTitle(),
                                         tr("Images (*.png)"));
    fileName = fileName.trimmed();
    if(fileName.isEmpty()){
        return;
    }
    if(!fileName.endsWith(".png", Qt::CaseInsensitive)){
        fileName = fileName + ".png";
    }
    QImage image = ui->canvas->allCanvas();
    image.save(fileName, "PNG");
}

void MainWindow::exportVisibleToFile()
{
    QString fileName =
            QFileDialog::getSaveFileName(this,
                                         tr("Export visible part to file"),
                                         this->windowTitle(),
                                         tr("Images (*.png)"));
    fileName = fileName.trimmed();
    if(fileName.isEmpty()){
        return;
    }
    if(!fileName.endsWith(".png", Qt::CaseInsensitive)){
        fileName = fileName + ".png";
    }
    QImage image = ui->canvas->currentCanvas();
    image.save(fileName, "PNG");
}

void MainWindow::exportToPSD()
{
    QString fileName =
            QFileDialog::getSaveFileName(this,
                                         tr("Export contents to psd file"),
                                         this->windowTitle(),
                                         tr("Photoshop Images (*.psd)"));
    fileName = fileName.trimmed();
    if(fileName.isEmpty()){
        return;
    }
    if(!fileName.endsWith(".psd", Qt::CaseInsensitive)){
        fileName = fileName + ".psd";
    }

    // save all layers into psd

    QProgressDialog *dialog = new QProgressDialog(tr("Exporting..."), QString(), 0, 0, this);
    dialog->setWindowModality(Qt::WindowModal);
    dialog->show();
    QFutureWatcher<QByteArray> *watcher = new QFutureWatcher<QByteArray>;
    QFuture<QByteArray> *future = new QFuture<QByteArray>(QtConcurrent::run(imagesToPSD,
                                                                            ui->canvas->layerImages(),
                                                                            ui->canvas->allCanvas()));
    watcher->setFuture(*future);
    connect(watcher, &QFutureWatcher<QByteArray>::finished, [watcher, dialog, future, fileName](){
        QByteArray data = future->result();
        QFile file(fileName);
        if(!file.open(QIODevice::Truncate|QIODevice::WriteOnly)) {
            return;
        }
        qDebug()<<data.length();
        file.write(data);
        file.close();
        dialog->close();
        dialog->deleteLater();
        watcher->deleteLater();
    });
}

void MainWindow::exportAllToClipboard()
{
    QClipboard *cb = qApp->clipboard();
    QImage image = ui->canvas->allCanvas();
    cb->setImage(image);
}

void MainWindow::exportVisibleToClipboard()
{
    QClipboard *cb = qApp->clipboard();
    QImage image = ui->canvas->currentCanvas();
    cb->setImage(image);
}

void MainWindow::resetView()
{
    restoreState(defaultView);
}

void MainWindow::about()
{
    AboutDialog dialog(this);
    dialog.exec();
}

template<typename T, typename U>
bool MainWindow::regShortcut(const QString& name, T func, U func2)
{
    //    auto shortcut_type = (ShT)config["type"].toInt();
    return regShortcut<>(QKeySequence(shortcut_manager.shortcut(name)["key"].toString()),
            func, func2);
}

template<typename T>
bool MainWindow::regShortcut(const QString& name, T func)
{
    return regShortcut<>(QKeySequence(shortcut_manager.shortcut(name)["key"].toString()), func);
}

template<typename T, typename U>
bool MainWindow::regShortcut(const QKeySequence& k, T func, U func2)
{
    if(keyMap_.contains(k.toString())){
        return false;
    }
    keyMap_.insert(k.toString(), true);

    SingleShortcut *shortcut = new SingleShortcut(this);
    shortcut->setKey(k);
    connect(shortcut, &SingleShortcut::activated,
            func);
    connect(shortcut, &SingleShortcut::inactivated,
            func2);
    return true;
}

template<typename T>
bool MainWindow::regShortcut(const QKeySequence& k, T func)
{
    if(keyMap_.contains(k.toString())){
        return false;
    }
    keyMap_.insert(k.toString(), true);
    QShortcut* shortcut = new QShortcut(k, this);
    connect(shortcut, &QShortcut::activated,
            func);
    return true;
}
