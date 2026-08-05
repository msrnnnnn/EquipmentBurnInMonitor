#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>

#include "homepage.h"
#include "settingspage.h"
#include "videopage.h"

class QListWidget;
class QStackedWidget;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();
    void setSensor(EquipmentData *sensor);
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
