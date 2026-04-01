#pragma once

#include "../model/tracklistmodel.hpp"
#include "../backend/qobuzbackend.hpp"
#include "../playqueue.hpp"

#include <QTreeView>
#include <QJsonArray>
#include <QJsonObject>
#include <QVector>
#include <QPair>
#include <QString>
#include <QSet>

namespace List
{
    class Tracks : public QTreeView
    {
        Q_OBJECT

    public:
        explicit Tracks(QobuzBackend *backend, PlayQueue *queue, QWidget *parent = nullptr);

        void loadTracks(const QJsonArray &tracks);
        void loadAlbum(const QJsonObject &album);
        void loadPlaylist(const QJsonObject &playlist);
        void loadSearchTracks(const QJsonArray &tracks);

        /// Called when the backend fires EV_TRACK_CHANGED so the playing row is highlighted.
        void setPlayingTrackId(qint64 id);

        /// Populate favorite track IDs so the star indicator and context menu reflect fav status.
        void setFavTrackIds(const QSet<qint64> &ids);
        void addFavTrackId(qint64 id);
        void removeFavTrackId(qint64 id);

        /// Start playing all tracks in the current view from the beginning.
        /// If shuffle is true, enables shuffle mode before starting.
        void playAll(bool shuffle = false);

        /// Set which playlist is currently displayed (0 = none).
        /// isOwned controls whether "Remove from this playlist" is shown.
        void setPlaylistContext(qint64 playlistId, bool isOwned = false);
        qint64 playlistId() const { return m_playlistId; }
        /// Provide the user's playlist list for the "Add to playlist" submenu.
        void setUserPlaylists(const QVector<QPair<qint64, QString>> &playlists);

    signals:
        void playTrackRequested(qint64 trackId);
        void addToPlaylistRequested(qint64 trackId, qint64 playlistId);
        void removeFromPlaylistRequested(qint64 playlistId, qint64 playlistTrackId);

    private:
        TrackListModel *m_model   = nullptr;
        QobuzBackend   *m_backend = nullptr;
        PlayQueue      *m_queue   = nullptr;
        qint64          m_playlistId = 0;
        bool            m_playlistIsOwned = false;
        QVector<QPair<qint64, QString>> m_userPlaylists;

        void onDoubleClicked(const QModelIndex &index);
        void onContextMenu(const QPoint &pos);
    };
}
