#include "maincontent.hpp"

#include <QVBoxLayout>
#include <QPushButton>

MainContent::MainContent(QobuzBackend *backend, PlayQueue *queue, QWidget *parent)
    : QWidget(parent)
    , m_backend(backend)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    m_stack = new QStackedWidget(this);
    layout->addWidget(m_stack);

    m_welcome = new QLabel(
        tr("<h2>Welcome to Qobuz</h2>"
           "<p>Select something from the library on the left to get started,<br>"
           "or use the search panel to find music.</p>"),
        this);
    m_welcome->setAlignment(Qt::AlignCenter);

    // Tracks page: context header + track list
    auto *tracksPage = new QWidget(this);
    auto *tracksLayout = new QVBoxLayout(tracksPage);
    tracksLayout->setContentsMargins(0, 0, 0, 0);
    tracksLayout->setSpacing(0);

    m_header = new TrackContextHeader(tracksPage);
    m_header->hide();
    m_tracks = new List::Tracks(m_backend, queue, tracksPage);
    tracksLayout->addWidget(m_header);
    tracksLayout->addWidget(m_tracks, 1);

    QObject::connect(m_header->playButton(),    &QPushButton::clicked,
                     [this] { m_tracks->playAll(false); });
    QObject::connect(m_header->shuffleButton(), &QPushButton::clicked,
                     [this] { m_tracks->playAll(true); });
    QObject::connect(m_header->subtitleButton(), &QPushButton::clicked,
                     [this] {
        const qint64 id = m_header->artistId();
        if (id > 0) emit artistRequested(id);
    });
    QObject::connect(m_header->followButton(), &QPushButton::clicked,
                     [this] {
        const qint64 id = m_header->playlistId();
        if (id <= 0 || m_header->playlistOwned())
            return;
        emit playlistFollowToggled(id, !m_header->playlistFollowed());
    });
    QObject::connect(m_header->downloadButton(), &QPushButton::clicked,
                     [this] {
        const QString albumId = m_header->albumId();
        if (!albumId.isEmpty()) {
            emit downloadAlbumRequested(albumId);
            return;
        }

        const qint64 playlistId = m_header->playlistId();
        if (playlistId > 0)
            emit downloadPlaylistRequested(playlistId);
    });
    QObject::connect(m_header->favButton(), &QPushButton::clicked,
                     [this] {
        const QString albumId = m_header->albumId();
        if (albumId.isEmpty())
            return;
        emit albumFavoriteToggled(albumId, !m_header->albumFaved());
    });

    m_albumList  = new AlbumListView(this);
    m_artistList = new ArtistListView(this);
    m_artistView = new ArtistView(backend, queue, this);
    m_genreBrowser = new GenreBrowserView(backend, queue, this);

    m_stack->addWidget(m_welcome);      // PageWelcome
    m_stack->addWidget(tracksPage);     // PageTracks
    m_stack->addWidget(m_albumList);    // PageAlbumList
    m_stack->addWidget(m_artistList);   // PageArtistList
    m_stack->addWidget(m_artistView);   // PageArtistDetail
    m_stack->addWidget(m_genreBrowser); // PageGenreBrowser

    m_stack->setCurrentIndex(PageWelcome);

    connect(m_albumList,  &AlbumListView::albumSelected,   this, &MainContent::albumRequested);
    connect(m_artistList, &ArtistListView::artistSelected, this, &MainContent::artistRequested);
    connect(m_artistView, &ArtistView::albumSelected,      this, &MainContent::albumRequested);
    connect(m_artistView, &ArtistView::playTrackRequested, this, &MainContent::playTrackRequested);
    connect(m_genreBrowser, &GenreBrowserView::albumSelected, this, &MainContent::albumRequested);
    connect(m_genreBrowser, &GenreBrowserView::artistSelected, this, &MainContent::artistRequested);
    connect(m_genreBrowser, &GenreBrowserView::playlistSelected, this, &MainContent::playlistRequested);
    connect(m_genreBrowser, &GenreBrowserView::playTrackRequested, this, &MainContent::playTrackRequested);
}

void MainContent::showWelcome()       { m_stack->setCurrentIndex(PageWelcome); }

void MainContent::showAlbum(const QJsonObject &album)
{
    QString albumId = album["id"].toString();
    if (albumId.isEmpty() && album["id"].isDouble())
        albumId = QString::number(static_cast<qint64>(album["id"].toDouble()));
    m_header->setAlbum(album, m_favAlbumIds.contains(albumId));
    m_tracks->loadAlbum(album);
    m_stack->setCurrentIndex(PageTracks);
}

void MainContent::showPlaylist(const QJsonObject &playlist, bool isFollowed, bool isOwned)
{
    m_header->setPlaylist(playlist, isFollowed, isOwned);
    m_tracks->loadPlaylist(playlist);
    m_stack->setCurrentIndex(PageTracks);
}

void MainContent::showFavTracks(const QJsonObject &result)
{
    m_header->hide();
    m_tracks->loadTracks(result["items"].toArray());
    m_stack->setCurrentIndex(PageTracks);
}

void MainContent::showSearchTracks(const QJsonArray &tracks)
{
    m_header->hide();
    m_tracks->loadSearchTracks(tracks);
    m_stack->setCurrentIndex(PageTracks);
}

void MainContent::showFavAlbums(const QJsonObject &result)
{
    m_albumList->setAlbums(result["items"].toArray());
    m_stack->setCurrentIndex(PageAlbumList);
}

void MainContent::showFavArtists(const QJsonObject &result)
{
    m_artistList->setArtists(result["items"].toArray());
    m_stack->setCurrentIndex(PageArtistList);
}

void MainContent::showArtist(const QJsonObject &artist)
{
    m_artistView->setArtist(artist);
    m_stack->setCurrentIndex(PageArtistDetail);
}

void MainContent::updateArtistReleases(const QString &releaseType, const QJsonArray &items, bool hasMore, int offset)
{
    m_artistView->setReleases(releaseType, items, hasMore, offset);
}

void MainContent::setFavArtistIds(const QSet<qint64> &ids)
{
    m_artistView->setFavArtistIds(ids);
}

void MainContent::setFavAlbumIds(const QSet<QString> &ids)
{
    m_favAlbumIds = ids;
    const QString shownAlbumId = m_header->albumId();
    if (!shownAlbumId.isEmpty())
        m_header->setAlbumFaved(m_favAlbumIds.contains(shownAlbumId));
}

void MainContent::onDeepShuffleTracks(const QJsonArray &tracks)
{
    if (m_genreBrowser->tryHandleDeepShuffleTracks(tracks))
        return;
    m_artistView->onDeepShuffleTracks(tracks);
}

void MainContent::showGenreBrowser()
{
    m_genreBrowser->ensureGenresLoaded();
    m_genreBrowser->setBrowseMode(GenreBrowserView::BrowseMode::Genres);
    m_stack->setCurrentIndex(PageGenreBrowser);
}

void MainContent::showPlaylistBrowser()
{
    m_genreBrowser->ensureGenresLoaded();
    m_genreBrowser->setBrowseMode(GenreBrowserView::BrowseMode::PlaylistSearch);
    m_stack->setCurrentIndex(PageGenreBrowser);
}

void MainContent::setCurrentPlaylistFollowed(bool followed)
{
    m_header->setPlaylistFollowed(followed);
}
