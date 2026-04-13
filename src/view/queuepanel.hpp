#pragma once

#include "../playqueue.hpp"
#include "../backend/qobuzbackend.hpp"
#include "../util/albumqueuehelper.hpp"

#include <QDockWidget>
#include <QListWidget>
#include <QPushButton>
#include <QLabel>
#include <QVector>
#include <QSet>
#include <QPair>

class QueuePanel : public QDockWidget
{
    Q_OBJECT

public:
    explicit QueuePanel(QobuzBackend *backend, PlayQueue *queue, QWidget *parent = nullptr);

    void setUserPlaylists(const QVector<QPair<qint64, QString>> &playlists) { m_userPlaylists = playlists; }
    void setFavTrackIds(const QSet<qint64> &ids) { m_favTrackIds = ids; }
    void addFavTrackId(qint64 id) { m_favTrackIds.insert(id); }
    void removeFavTrackId(qint64 id) { m_favTrackIds.remove(id); }

signals:
    void skipToTrackRequested(qint64 trackId);
    void addToPlaylistRequested(qint64 trackId, qint64 playlistId);
    void favTrackRequested(qint64 trackId);
    void unfavTrackRequested(qint64 trackId);

private slots:
    void refresh();
    void onItemDoubleClicked(QListWidgetItem *item);
    void onContextMenu(const QPoint &pos);
    void onRowsMoved();

private:
    QobuzBackend     *m_backend  = nullptr;
    PlayQueue        *m_queue    = nullptr;
    AlbumQueueHelper *m_albumQueueHelper = nullptr;
    QLabel           *m_countLabel = nullptr;
    QListWidget      *m_list       = nullptr;
    QPushButton      *m_clearBtn   = nullptr;
    bool              m_refreshing = false;
    QVector<QPair<qint64, QString>> m_userPlaylists;
    QSet<qint64>      m_favTrackIds;
};
