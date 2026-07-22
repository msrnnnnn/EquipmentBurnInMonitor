#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>

class QListWidget;
class QStackedWidget;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private:
    void setupUI();

    QListWidget   *m_navList = nullptr;
    QStackedWidget *m_stackedWidget = nullptr;
};
#endif // MAINWINDOW_H
