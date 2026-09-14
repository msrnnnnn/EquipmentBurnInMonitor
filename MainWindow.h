#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>

#include "HomePage.h"
#include "SettingsPage.h"
#include "VideoPage.h"

class QListWidget;
class QStackedWidget;
class ServiceFacade;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();
    void setSensor(EquipmentData *sensor);
    // S6：与 setSensor 同款传递者模式 —— MainWindow 只负责转交，不插手数据流
    void setServiceFacade(ServiceFacade *facade);
    HomePage* homePage() { return m_home; }
private:
    void setupUI();
    QListWidget   *m_navList = nullptr;
    QStackedWidget *m_stackedWidget = nullptr;
    HomePage* m_home = nullptr;
    SettingsPage* m_setting = nullptr;
    VideoPage* m_videoPage = nullptr;
};
#endif // MAINWINDOW_H
