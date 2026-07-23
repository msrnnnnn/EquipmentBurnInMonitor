#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>

#include "homepage.h"
class QListWidget;
class QStackedWidget;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();
    void setSensor(EquipmentData *sensor);
private:
    void setupUI();
    QListWidget   *m_navList = nullptr;
    QStackedWidget *m_stackedWidget = nullptr;
    HomePage* m_home = nullptr;
};
#endif // MAINWINDOW_H
