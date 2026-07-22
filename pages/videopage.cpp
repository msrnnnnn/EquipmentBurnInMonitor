#include "videopage.h"

#include <QFrame>
#include <QLabel>
#include <QHBoxLayout>
#include <QVBoxLayout>

VideoPage::VideoPage(QWidget *parent)
    : QWidget{parent}
{
    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(16, 16, 16, 16);
    layout->setSpacing(16);

    // ── 左侧：视频显示区域（后续接 FFmpeg） ──
    m_placeholderLabel = new QLabel("Video Feed Placeholder");
    m_placeholderLabel->setAlignment(Qt::AlignCenter);
    m_placeholderLabel->setStyleSheet(
        "background-color: #000000;"
        "color: #64748b;"
        "font-size: 16px;"
        "border: 1px solid #334155;"
        "border-radius: 8px;"
    );
    layout->addWidget(m_placeholderLabel, 1);

    // ── 右侧：控制侧栏 ──
    auto *sidebar = new QFrame;
    sidebar->setFixedWidth(320);
    sidebar->setStyleSheet(
        "QFrame {"
        "  background-color: #1e293b;"
        "  border: 1px solid #334155;"
        "  border-radius: 8px;"
        "}"
    );

    auto *sidebarLayout = new QVBoxLayout(sidebar);
    sidebarLayout->setContentsMargins(16, 16, 16, 16);
    sidebarLayout->setSpacing(12);

    auto *title = new QLabel("Video Controls");
    title->setStyleSheet("color: white; font-size: 16px; font-weight: bold;");

    auto *hint = new QLabel("Video module not enabled yet");
    hint->setStyleSheet("color: #cbd5e1; font-size: 13px;");
    hint->setWordWrap(true);

    sidebarLayout->addWidget(title);
    sidebarLayout->addWidget(hint);
    sidebarLayout->addStretch(1);

    layout->addWidget(sidebar, 0);
}
