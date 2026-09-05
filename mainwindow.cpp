#include "mainwindow.h"

#include <QApplication>
#include <QListWidget>
#include <QStackedWidget>
#include <QSplitter>

#include "homepage.h"
#include "videopage.h"
#include "settingspage.h"
#include "equipmentdata.h"

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setupUI();
}

MainWindow::~MainWindow() {}

void MainWindow::setupUI()
{
    // ── 窗口基本设置 ──
    setWindowTitle("Equipment Burn-In Monitor");
    resize(1280, 720);

    // ── 暗色工业风格调色板 ──
    QPalette darkPal;
    darkPal.setColor(QPalette::Window,          QColor(0x0f, 0x17, 0x2a));
    darkPal.setColor(QPalette::WindowText,      QColor(0xcb, 0xd5, 0xe1));
    darkPal.setColor(QPalette::Base,            QColor(0x1e, 0x29, 0x3b));
    darkPal.setColor(QPalette::AlternateBase,   QColor(0x0f, 0x17, 0x2a));
    darkPal.setColor(QPalette::Text,            QColor(0xe2, 0xe8, 0xf0));
    darkPal.setColor(QPalette::Button,          QColor(0x1e, 0x29, 0x3b));
    darkPal.setColor(QPalette::ButtonText,      QColor(0xcb, 0xd5, 0xe1));
    darkPal.setColor(QPalette::Highlight,       QColor(0x25, 0x63, 0xeb));
    darkPal.setColor(QPalette::HighlightedText, QColor(0xff, 0xff, 0xff));
    qApp->setPalette(darkPal);

    qApp->setStyleSheet(R"(
        QMainWindow { background-color: #0f172a; }
        QListWidget {
            background-color: #1e293b;
            border: none;
            outline: none;
            font-size: 14px;
        }
        QListWidget::item {
            padding: 12px 16px;
            color: #94a3b8;
            border-left: 3px solid transparent;
        }
        QListWidget::item:selected {
            background-color: #2563eb;
            color: #ffffff;
            border-left: 3px solid #60a5fa;
        }
        QListWidget::item:hover {
            background-color: #334155;
            color: #e2e8f0;
        }
        QGroupBox {
            color: #60a5fa;
            border: 1px solid #334155;
            border-radius: 6px;
            margin-top: 8px;
            padding-top: 16px;
            font-weight: bold;
        }
        QGroupBox::title {
            subcontrol-origin: margin;
            left: 12px;
            padding: 0 6px;
        }
        QLineEdit {
            background-color: #1e293b;
            color: #e2e8f0;
            border: 1px solid #334155;
            border-radius: 4px;
            padding: 6px;
        }
        QLineEdit:focus { border: 1px solid #2563eb; }
        QLineEdit[echoMode="2"] { /* password */ }
        QPushButton {
            background-color: #2563eb;
            color: #ffffff;
            border: none;
            border-radius: 4px;
            padding: 8px 16px;
            font-weight: bold;
        }
        QPushButton:hover { background-color: #3b82f6; }
        QPushButton:pressed { background-color: #1d4ed8; }
    )");

    // ── 左侧导航栏 ──
    m_navList = new QListWidget;
    m_navList->setFixedWidth(220);
    m_navList->addItem("  🏠  Home");
    m_navList->addItem("  🎥  Video");
    m_navList->addItem("  ⚙  Settings");

    // ── 右侧页面区 ──
    m_stackedWidget = new QStackedWidget;
    m_home = new HomePage(this);
    m_videoPage = new VideoPage(this);
    m_setting = new SettingsPage(this);
    m_stackedWidget->addWidget(m_home);
    m_stackedWidget->addWidget(m_videoPage);
    m_stackedWidget->addWidget(m_setting);

    // ── 连接导航信号 ──
    connect(m_navList, &QListWidget::currentRowChanged,
            m_stackedWidget, &QStackedWidget::setCurrentIndex);

    // ── 分栏布局 ──
    auto *splitter = new QSplitter(Qt::Horizontal);
    splitter->addWidget(m_navList);
    splitter->addWidget(m_stackedWidget);

    setCentralWidget(splitter);

    // 默认选中首页
    m_navList->setCurrentRow(0);
}

// 传递者模式：MainWindow只负责把sensor转交给HomePage，自己不处理数据
// 保持MainWindow简洁——它只管导航布局，不插手数据流
void MainWindow::setSensor(EquipmentData *sensor)
{
    if (m_home && sensor) m_home->bindSensor(sensor);
    if (m_setting && sensor) m_setting->bindSensor(sensor);
}

// S6：属性注入。ServiceFacade 是主线程对象，直接传指针即可；
// 页面内部用信号槽订阅 NOTIFY，避免任何轮询。
void MainWindow::setServiceFacade(ServiceFacade *facade)
{
    if (m_home && facade) m_home->bindServiceFacade(facade);
    if (m_setting && facade) m_setting->bindServiceFacade(facade);
}
