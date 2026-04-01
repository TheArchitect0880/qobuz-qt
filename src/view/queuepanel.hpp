#pragma once

#include "../playqueue.hpp"

#include <QDockWidget>
#include <QListWidget>
#include <QPushButton>
#include <QLabel>

class QueuePanel : public QDockWidget
{
    Q_OBJECT

public:
    explicit QueuePanel(PlayQueue *queue, QWidget *parent = nullptr);

signals:
    void skipToTrackRequested(qint64 trackId);

private slots:
    void refresh();
    void onItemDoubleClicked(QListWidgetItem *item);
    void onContextMenu(const QPoint &pos);
    void onRowsMoved();

private:
    PlayQueue   *m_queue      = nullptr;
    QLabel      *m_countLabel = nullptr;
    QListWidget *m_list       = nullptr;
    QPushButton *m_clearBtn   = nullptr;
    bool         m_refreshing = false;
};
