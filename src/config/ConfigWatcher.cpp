#include "ConfigWatcher.h"
#include "logging/logger.h"

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

    // B1：热更新第一原则 —— 新配置验证通过才切换，否则保持旧配置。
    // 旧实现：loadFromFile 失败返回空 Config 照发 updated()，onConfigUpdated 里
    // stop 后 rebuildTasks 遇空 items 直接 return —— 规则清空 + 采集停止 + 永不自愈，
    // 改坏一个逗号就得重启程序。现在：无效配置只记日志，系统继续跑旧配置。
    if (cfg.items.isEmpty() || cfg.endpoint.host.isEmpty()) {
        burninsys::Logger::instance().warn(
            "Config reload rejected (invalid config), keeping current config running");
    } else {
        emit updated(cfg);
    }

    // 编辑器保存可能"删除并重建"文件，QFileSystemWatcher 会丢监听，重新挂上
    if(!m_watcher->files().contains(path))
    {
        m_watcher->addPath(path);
    }
}
