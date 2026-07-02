#pragma once

#include "../backend/qobuzbackend.hpp"
#include "../util/settings.hpp"

#include <QTreeWidget>
#include <QVector>
#include <QPair>
#include <QString>
#include <QSet>

namespace List
{
    /// Left-sidebar library tree — mirrors List::Library from spotify-qt.
    /// Shows: Favorites (tracks, albums, artists) and Playlists.
    class Library : public QTreeWidget
    {
        Q_OBJECT

    public:
        explicit Library(QobuzBackend *backend, QWidget *parent = nullptr);

        void refresh();

    signals:
        /// Emitted when the user selects a node that should load tracks.
        void favTracksRequested();
        void favAlbumsRequested();
        void favArtistsRequested();
        void browseGenresRequested();
        void browsePlaylistsRequested();
        void playlistRequested(qint64 playlistId, const QString &name);
        void playlistDownloadRequested(qint64 playlistId, const QString &name);
        /// Emitted after playlists are loaded so others can cache the list.
        void userPlaylistsChanged(const QVector<QPair<qint64, QString>> &playlists);
        /// Emitted with all user playlist IDs (owned + subscribed).
        void userPlaylistIdsChanged(const QSet<qint64> &playlistIds);
        /// Emitted when the currently open playlist was deleted.
        void openPlaylistDeleted();

    private slots:
        void onUserPlaylistsLoaded(const QJsonObject &result);
        void onItemClicked(QTreeWidgetItem *item, int column);
        void onItemDoubleClicked(QTreeWidgetItem *item, int column);
        void onContextMenuRequested(const QPoint &pos);

    private:
        QobuzBackend *m_backend = nullptr;

        QTreeWidgetItem *m_myLibNode     = nullptr;
        QTreeWidgetItem *m_playlistsNode = nullptr;
        QTreeWidgetItem *m_browseNode    = nullptr;
        qint64           m_openPlaylistId = 0;

        void buildStaticNodes();
    };
}
