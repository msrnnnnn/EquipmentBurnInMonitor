#ifndef CONFIGWATCHER_H
#define CONFIGWATCHER_H

#include <QObject>
#include <qfilesystemwatcher.h>
#include "config/Config.h"
class ConfigWatcher : public QObject
{
    Q_OBJECT
public:
    explicit ConfigWatcher(QObject *parent = nullptr);
    void watch(const QString &path);
signals:
    void updated(const Config &cfg);
private slots:
    void onFileChanged(const QString &path);
private:
    QFileSystemWatcher *m_watcher = nullptr;
    ConfigLoader m_loader;
};

#endif // CONFIGWATCHER_H
