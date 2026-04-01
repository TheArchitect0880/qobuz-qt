#include "tracks.hpp"
#include "../util/settings.hpp"
#include "../util/trackinfo.hpp"

#include <QHeaderView>
#include <QMenu>
#include <QAction>

namespace List
{

Tracks::Tracks(QobuzBackend *backend, PlayQueue *queue, QWidget *parent)
    : QTreeView(parent)
    , m_backend(backend)
    , m_queue(queue)
{
    m_model = new TrackListModel(this);
    setModel(m_model);

    setRootIsDecorated(false);
    setAlternatingRowColors(true);
    setSelectionMode(QAbstractItemView::ExtendedSelection);
    setSelectionBehavior(QAbstractItemView::SelectRows);
    setSortingEnabled(true);
    setContextMenuPolicy(Qt::CustomContextMenu);
    sortByColumn(TrackListModel::ColNumber, Qt::AscendingOrder);

    header()->setStretchLastSection(false);
    header()->setSectionResizeMode(TrackListModel::ColTitle,    QHeaderView::Stretch);
    header()->setSectionResizeMode(TrackListModel::ColArtist,   QHeaderView::Stretch);
    header()->setSectionResizeMode(TrackListModel::ColAlbum,    QHeaderView::Stretch);
    header()->setSectionResizeMode(TrackListModel::ColNumber,   QHeaderView::ResizeToContents);
    header()->setSectionResizeMode(TrackListModel::ColDuration, QHeaderView::ResizeToContents);

    connect(this, &QTreeView::doubleClicked,
            this, &Tracks::onDoubleClicked);
    connect(this, &QTreeView::customContextMenuRequested,
            this, &Tracks::onContextMenu);
    connect(m_model, &QAbstractItemModel::modelReset, this, [this] {
        for (int row : m_model->discHeaderRows())
            setFirstColumnSpanned(row, {}, true);
        setSortingEnabled(!m_model->hasMultipleDiscs());
    });

}

void Tracks::loadTracks(const QJsonArray &tracks)
{
    setPlaylistContext(0);
    setColumnHidden(TrackListModel::ColAlbum, false);
    m_model->setTracks(tracks, false, /*useSequential=*/true);
}

void Tracks::loadAlbum(const QJsonObject &album)
{
    setPlaylistContext(0);
    setColumnHidden(TrackListModel::ColAlbum, true);
    const QJsonArray items = album["tracks"].toObject()["items"].toArray();
    m_model->setTracks(items); // album: use track_number
}

void Tracks::loadPlaylist(const QJsonObject &playlist)
{
    setColumnHidden(TrackListModel::ColAlbum, false);
    const qint64 id     = static_cast<qint64>(playlist["id"].toDouble());
    const qint64 ownId  = static_cast<qint64>(playlist["owner"].toObject()["id"].toDouble());
    const qint64 myId   = AppSettings::instance().userId();
    const bool isOwned  = (myId > 0 && ownId == myId);
    setPlaylistContext(id, isOwned);
    const QJsonArray items = playlist["tracks"].toObject()["items"].toArray();
    m_model->setTracks(items, /*usePosition=*/true);
}

void Tracks::loadSearchTracks(const QJsonArray &tracks)
{
    setPlaylistContext(0);
    setColumnHidden(TrackListModel::ColAlbum, false);
    m_model->setTracks(tracks, false, /*useSequential=*/true);
}

void Tracks::setPlaylistContext(qint64 playlistId, bool isOwned)
{
    m_playlistId = playlistId;
    m_playlistIsOwned = isOwned;
}

void Tracks::setUserPlaylists(const QVector<QPair<qint64, QString>> &playlists)
{
    m_userPlaylists = playlists;
}

void Tracks::setPlayingTrackId(qint64 id)
{
    m_model->setPlayingId(id);
}

void Tracks::setFavTrackIds(const QSet<qint64> &ids)
{
    m_model->setFavIds(ids);
}

void Tracks::addFavTrackId(qint64 id)
{
    m_model->addFavId(id);
}

void Tracks::removeFavTrackId(qint64 id)
{
    m_model->removeFavId(id);
}

void Tracks::playAll(bool shuffle)
{
    const QJsonArray tracks = m_model->currentTracksJson();
    if (tracks.isEmpty()) return;
    m_queue->setContext(tracks, 0);
    // Shuffle once without touching the global shuffle flag — so a subsequent
    // double-click on a track plays in normal order (unless global shuffle is on).
    if (shuffle && !m_queue->shuffleEnabled())
        m_queue->shuffleNow();
    const qint64 firstId = static_cast<qint64>(m_queue->current()["id"].toDouble());
    if (firstId > 0)
        emit playTrackRequested(firstId);
}


void Tracks::onDoubleClicked(const QModelIndex &index)
{
    const qint64 id = m_model->data(index, TrackListModel::TrackIdRole).toLongLong();
    if (id > 0) {
        // Compute filtered row (disc headers excluded from currentTracksJson)
        int filteredRow = 0;
        for (int r = 0; r < index.row(); ++r)
            if (!m_model->trackAt(r).isDiscHeader) ++filteredRow;
        m_queue->setContext(m_model->currentTracksJson(), filteredRow);
        emit playTrackRequested(id);
    }
}

void Tracks::onContextMenu(const QPoint &pos)
{
    const QModelIndex index = indexAt(pos);
    if (!index.isValid()) return;

    const qint64      id        = m_model->data(index, TrackListModel::TrackIdRole).toLongLong();
    if (id <= 0) return;  // disc header row
    const QJsonObject trackJson = m_model->data(index, TrackListModel::TrackJsonRole).toJsonObject();

    QMenu menu(this);

    auto *playNow  = menu.addAction(QIcon(":/res/icons/media-playback-start.svg"), tr("Play now"));
    auto *playNext = menu.addAction(QIcon(":/res/icons/media-skip-forward.svg"),   tr("Play next"));
    auto *addQueue = menu.addAction(QIcon(":/res/icons/media-playlist-append.svg"), tr("Add to queue"));
    menu.addSeparator();

    const bool isFav = m_model->isFav(id);
    if (isFav) {
        auto *remFav = menu.addAction(QIcon(":/res/icons/non-starred-symbolic.svg"), tr("Remove from favorites"));
        connect(remFav, &QAction::triggered, this, [this, id] {
            m_backend->removeFavTrack(id);
            m_model->removeFavId(id);
        });
    } else {
        auto *addFav = menu.addAction(QIcon(":/res/icons/starred-symbolic.svg"), tr("Add to favorites"));
        connect(addFav, &QAction::triggered, this, [this, id] {
            m_backend->addFavTrack(id);
            m_model->addFavId(id);
        });
    }

    // Compute filtered row for multi-disc albums (disc headers excluded from currentTracksJson)
    int filteredRow = 0;
    for (int r = 0; r < index.row(); ++r)
        if (!m_model->trackAt(r).isDiscHeader) ++filteredRow;
    connect(playNow, &QAction::triggered, this, [this, id, filteredRow] {
        m_queue->setContext(m_model->currentTracksJson(), filteredRow);
        emit playTrackRequested(id);
    });
    connect(playNext, &QAction::triggered, this, [this, trackJson] {
        m_queue->playNext(trackJson);
    });
    connect(addQueue, &QAction::triggered, this, [this, trackJson] {
        m_queue->addToQueue(trackJson);
    });

    // Open album
    const QString albumId = m_model->trackAt(index.row()).albumId;
    if (!albumId.isEmpty()) {
        menu.addSeparator();
        auto *openAlbum = menu.addAction(
            QIcon(":/res/icons/view-media-album-cover.svg"),
            tr("Open album: %1").arg(QString(m_model->trackAt(index.row()).album).replace(QLatin1Char('&'), QStringLiteral("&&"))));
        connect(openAlbum, &QAction::triggered, this, [this, albumId] {
            m_backend->getAlbum(albumId);
        });
    }

    // Open artist
    const qint64 artistId = static_cast<qint64>(
        trackJson["performer"].toObject()["id"].toDouble());
    if (artistId > 0) {
        const QString artistName = trackJson["performer"].toObject()["name"].toString();
        auto *openArtist = menu.addAction(
            QIcon(":/res/icons/view-media-artist.svg"),
            tr("Open artist: %1").arg(QString(artistName).replace(QLatin1Char('&'), QStringLiteral("&&"))));
        connect(openArtist, &QAction::triggered, this, [this, artistId] {
            m_backend->getArtist(artistId);
        });
    }

    // Playlist management
    if (!m_userPlaylists.isEmpty()) {
        menu.addSeparator();
        auto *addToPlMenu = menu.addMenu(
            QIcon(":/res/icons/media-playlist-append.svg"), tr("Add to playlist"));
        for (const auto &pl : m_userPlaylists) {
            const qint64  plId   = pl.first;
            const QString plName = pl.second;
            auto *act = addToPlMenu->addAction(QString(plName).replace(QLatin1Char('&'), QStringLiteral("&&")));
            connect(act, &QAction::triggered, this, [this, id, plId] {
                emit addToPlaylistRequested(id, plId);
            });
        }
    }

    if (m_playlistId > 0 && m_playlistIsOwned) {
        const qint64 playlistTrackId =
            m_model->data(index, TrackListModel::PlaylistTrackIdRole).toLongLong();
        if (playlistTrackId > 0) {
            if (m_userPlaylists.isEmpty()) menu.addSeparator();
            auto *remFromPl = menu.addAction(tr("Remove from this playlist"));
            const qint64 curPlaylistId = m_playlistId;
            const int    curRow        = index.row();
            connect(remFromPl, &QAction::triggered, this, [this, curPlaylistId, playlistTrackId, curRow] {
                emit removeFromPlaylistRequested(curPlaylistId, playlistTrackId);
                m_model->removeTrack(curRow);  // optimistic: remove immediately from view
            });
        }
    }

    // Track info
    menu.addSeparator();
    auto *infoAction = menu.addAction(tr("Track info..."));
    connect(infoAction, &QAction::triggered, this, [this, trackJson] {
        TrackInfoDialog::show(trackJson, this);
    });

    menu.exec(viewport()->mapToGlobal(pos));
}

} // namespace List
