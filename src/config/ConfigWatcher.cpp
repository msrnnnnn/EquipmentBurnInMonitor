#include "ConfigWatcher.h"

ConfigWatcher::ConfigWatcher(QObject *parent)
    : QObject{parent}
{
    m_watcher = new QFileSystemWatcher(this);
    connect(m_watcher, &QFileSystemWatcher::fileChanged, this, &ConfigWatcher::onFileChanged);
}

void ConfigWatcher::watch(const QString &path)
{
    if(!path.isEmpty()) m_watcher->addPath(path);
}

void ConfigWatcher::onFileChanged(const QString &path)
{
    Config cfg = m_loader.loadFromFile(path);
    emit updated(cfg);

    if(!m_watcher->files().contains(path))
    {
        m_watcher->addPath(path);
    }
}
