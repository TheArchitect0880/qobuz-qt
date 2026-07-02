#include "library.hpp"

#include <QHeaderView>
#include <QJsonArray>
#include <QJsonObject>
#include <QInputDialog>
#include <QMenu>
#include <QMessageBox>

namespace List
{

static constexpr int TypeRole    = Qt::UserRole + 1;
static constexpr int IdRole      = Qt::UserRole + 2;
static constexpr int NameRole    = Qt::UserRole + 3;
static constexpr int IsOwnerRole = Qt::UserRole + 4;

enum NodeType {
    NodeFavTracks,
    NodeFavAlbums,
    NodeFavArtists,
    NodePlaylist,
    NodeBrowseGenres,
    NodeBrowsePlaylists,
};

Library::Library(QobuzBackend *backend, QWidget *parent)
    : QTreeWidget(parent)
    , m_backend(backend)
{
    setHeaderHidden(true);
    setRootIsDecorated(true);
    setContextMenuPolicy(Qt::CustomContextMenu);

    buildStaticNodes();

    connect(m_backend, &QobuzBackend::userPlaylistsLoaded,
            this, &Library::onUserPlaylistsLoaded);
    connect(m_backend, &QobuzBackend::playlistCreated,
            this, [this](const QJsonObject &) { refresh(); });
    connect(m_backend, &QobuzBackend::playlistDeleted,
            this, [this](const QJsonObject &result) {
        const qint64 deletedId = static_cast<qint64>(result["playlist_id"].toDouble());
        if (deletedId == m_openPlaylistId) {
            m_openPlaylistId = 0;
            emit openPlaylistDeleted();
        }
        refresh();
    });
    connect(this, &QTreeWidget::itemClicked,
            this, &Library::onItemClicked);
    connect(this, &QTreeWidget::itemDoubleClicked,
            this, &Library::onItemDoubleClicked);
    connect(this, &QTreeWidget::customContextMenuRequested,
            this, &Library::onContextMenuRequested);
}

void Library::buildStaticNodes()
{
    // My Library
    m_myLibNode = new QTreeWidgetItem(this, QStringList{tr("My Library")});
    m_myLibNode->setExpanded(true);

    auto *tracksItem = new QTreeWidgetItem(m_myLibNode, QStringList{tr("Favorite Tracks")});
    tracksItem->setData(0, TypeRole, NodeFavTracks);

    auto *albumsItem = new QTreeWidgetItem(m_myLibNode, QStringList{tr("Favorite Albums")});
    albumsItem->setData(0, TypeRole, NodeFavAlbums);

    auto *artistsItem = new QTreeWidgetItem(m_myLibNode, QStringList{tr("Favorite Artists")});
    artistsItem->setData(0, TypeRole, NodeFavArtists);

    // Playlists
    m_playlistsNode = new QTreeWidgetItem(this, QStringList{tr("Playlists")});
    m_playlistsNode->setExpanded(true);

    // Browse
    m_browseNode = new QTreeWidgetItem(this, QStringList{tr("Browse")});
    m_browseNode->setExpanded(true);

    auto *genresItem = new QTreeWidgetItem(m_browseNode, QStringList{tr("Genres")});
    genresItem->setData(0, TypeRole, NodeBrowseGenres);

    auto *playlistsItem = new QTreeWidgetItem(m_browseNode, QStringList{tr("Playlists")});
    playlistsItem->setData(0, TypeRole, NodeBrowsePlaylists);
}

void Library::refresh()
{
    // Remove old playlist children
    while (m_playlistsNode->childCount() > 0)
        delete m_playlistsNode->takeChild(0);

    m_backend->getUserPlaylists();
}

void Library::onUserPlaylistsLoaded(const QJsonObject &result)
{
    while (m_playlistsNode->childCount() > 0)
        delete m_playlistsNode->takeChild(0);

    QSet<qint64> allPlaylistIds;
    QVector<QPair<qint64, QString>> editablePlaylists;
    const qint64 myUserId = AppSettings::instance().userId();
    const QJsonArray items = result["items"].toArray();
    for (const auto &v : items) {
        const QJsonObject pl    = v.toObject();
        const QString     name  = pl["name"].toString();
        const qint64      id    = static_cast<qint64>(pl["id"].toDouble());
        const qint64      ownId = static_cast<qint64>(pl["owner"].toObject()["id"].toDouble());
        const bool        isOwner = (myUserId > 0 && ownId == myUserId);

        if (id > 0)
            allPlaylistIds.insert(id);

        auto *item = new QTreeWidgetItem(m_playlistsNode, QStringList{name});
        item->setData(0, TypeRole,    NodePlaylist);
        item->setData(0, IdRole,      id);
        item->setData(0, NameRole,    name);
        item->setData(0, IsOwnerRole, isOwner);

        // Only include playlists we can edit in the "Add to playlist" submenu
        if (isOwner)
            editablePlaylists.append({id, name});
    }

    emit userPlaylistIdsChanged(allPlaylistIds);
    emit userPlaylistsChanged(editablePlaylists);
}

void Library::onContextMenuRequested(const QPoint &pos)
{
    QTreeWidgetItem *item = itemAt(pos);
    if (!item) return;

    const bool isHeader = (item == m_playlistsNode);
    const bool isPlaylistItem = (!isHeader && item->parent() == m_playlistsNode &&
                                  item->data(0, TypeRole).toInt() == NodePlaylist);

    if (!isHeader && !isPlaylistItem) return;

    QMenu menu(this);

    auto *newPl = menu.addAction(tr("New Playlist…"));
    connect(newPl, &QAction::triggered, this, [this] {
        bool ok = false;
        const QString name = QInputDialog::getText(
            this, tr("New Playlist"), tr("Playlist name:"),
            QLineEdit::Normal, QString(), &ok);
        if (ok && !name.trimmed().isEmpty())
            m_backend->createPlaylist(name.trimmed());
    });

    if (isPlaylistItem) {
        const qint64  plId      = item->data(0, IdRole).toLongLong();
        const QString plName    = item->data(0, NameRole).toString();
        const bool    isOwner   = item->data(0, IsOwnerRole).toBool();

        menu.addSeparator();
        auto *downloadPl = menu.addAction(tr("Download \"%1\"").arg(plName));
        connect(downloadPl, &QAction::triggered, this, [this, plId, plName] {
            emit playlistDownloadRequested(plId, plName);
        });

        if (isOwner) {
            auto *delPl = menu.addAction(tr("Delete \"%1\"…").arg(plName));
            connect(delPl, &QAction::triggered, this, [this, plId, plName] {
                const auto answer = QMessageBox::question(
                    this,
                    tr("Delete Playlist"),
                    tr("Permanently delete \"%1\"? This cannot be undone.").arg(plName),
                    QMessageBox::Yes | QMessageBox::Cancel,
                    QMessageBox::Cancel);
                if (answer == QMessageBox::Yes)
                    m_backend->deletePlaylist(plId);
            });
        }
    }

    menu.exec(viewport()->mapToGlobal(pos));
}

void Library::onItemClicked(QTreeWidgetItem *item, int)
{
    if (!item) return;
    const int type = item->data(0, TypeRole).toInt();

    switch (type) {
    case NodeFavTracks:  emit favTracksRequested();  break;
    case NodeFavAlbums:  emit favAlbumsRequested();  break;
    case NodeFavArtists: emit favArtistsRequested(); break;
    case NodeBrowseGenres: emit browseGenresRequested(); break;
    case NodeBrowsePlaylists: emit browsePlaylistsRequested(); break;
    case NodePlaylist: {
        const qint64  id   = item->data(0, IdRole).toLongLong();
        const QString name = item->data(0, NameRole).toString();
        m_openPlaylistId = id;
        emit playlistRequested(id, name);
        break;
    }
    default: break;
    }
}

void Library::onItemDoubleClicked(QTreeWidgetItem *item, int column)
{
    onItemClicked(item, column);
}

} // namespace List
