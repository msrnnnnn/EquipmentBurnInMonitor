#ifndef VIDEOPAGE_H
#define VIDEOPAGE_H

#include <QWidget>

class QLabel;

class VideoPage : public QWidget
{
    Q_OBJECT
public:
    explicit VideoPage(QWidget *parent = nullptr);

signals:

private:
    QLabel *m_placeholderLabel = nullptr;
};

#endif // VIDEOPAGE_H
