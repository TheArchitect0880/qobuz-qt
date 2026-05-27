#include "mainwindow.hpp"
#include "dialog/login.hpp"
#include "dialog/settings.hpp"
#include "util/settings.hpp"
#include "util/icon.hpp"

#ifdef USE_DBUS
#include "backend/mpris.hpp"
#endif

#include <QApplication>
#include <QMenuBar>
#include <QMenu>
#include <QAction>
#include <QShortcut>
#include <QStatusBar>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QTimer>
#include <QJsonArray>
#include <QCloseEvent>

namespace
{
constexpr int kWindowStateVersion = 1;
}

MainWindow::MainWindow(QobuzBackend *backend, QWidget *parent)
    : QMainWindow(parent)
    , m_backend(backend)
{
    setWindowTitle(QStringLiteral("Qobuz"));
    setMinimumSize(800, 500);
    resize(defaultSize());

    // ---- Queue (owned here, shared with toolbar and track list) ----
    m_queue = new PlayQueue(this);

    // Scrobbler must connect to trackTransitioned BEFORE MainToolBar does.
    // MainToolBar::onTrackTransitioned calls manuallyEmitTrackChanged, which fires
    // trackChanged → scrobbler::onTrackStarted, resetting scrobbler state.
    // If scrobbler::onTrackFinished connects after the toolbar, it fires after the
    // reset and sees accumulatedSecs=0, silently dropping the gapless scrobble.
    setupScrobbler();

    // ---- Toolbar ----
    m_toolBar = new MainToolBar(m_backend, m_queue, this);
    addToolBar(Qt::TopToolBarArea, m_toolBar);

    // ---- Central content ----
    m_content = new MainContent(m_backend, m_queue, this);
    setCentralWidget(m_content);

    setupDocks();
    m_defaultWindowState = saveState(kWindowStateVersion);
    restoreWindowLayout();
    setupMenuBar();
    statusBar()->showMessage(tr("Ready"));
    setupGapless();
    setupMpris();
    connectBackendSignals();
    connectLibrarySignals();
    connectContentSignals();
    connectToolbarSignals();

    // Ctrl+F opens search panel and focuses the search box
    auto *searchShortcut = new QShortcut(QKeySequence::Find, this);
    connect(searchShortcut, &QShortcut::activated, this, [this] {
        if (!m_sidePanel->isVisible())
            m_sidePanel->setVisible(true);
        m_sidePanel->focusSearchBox();
    });

    // Space toggles play/pause (skip if typing in a text field)
    auto *playPauseShortcut = new QShortcut(Qt::Key_Space, this);
    connect(playPauseShortcut, &QShortcut::activated, this, [this] {
        if (qobject_cast<QLineEdit *>(QApplication::focusWidget()))
            return;
        const int state = m_backend->state();
        if (state == 1)
            m_backend->pause();
        else
            m_backend->resume();
    });

    // Apply playback options from saved settings
    m_backend->setReplayGain(AppSettings::instance().replayGainEnabled());
    m_backend->setGapless(AppSettings::instance().gaplessEnabled());

    tryRestoreSession();
}

void MainWindow::setupDocks()
{
    // ---- Library dock (left) ----
    m_library = new List::Library(m_backend, this);
    m_libraryDock = new QDockWidget(tr("Library"), this);
    m_libraryDock->setObjectName(QStringLiteral("libraryDock"));
    m_libraryDock->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable);
    m_libraryDock->setWidget(m_library);
    m_libraryDock->setMinimumWidth(150);
    addDockWidget(Qt::LeftDockWidgetArea, m_libraryDock);

    // ---- Now-playing context dock (left, below library) ----
    m_contextView = new Context::View(m_backend, this);
    addDockWidget(Qt::LeftDockWidgetArea, m_contextView);

    // ---- Queue panel (right) ----
    m_queuePanel = new QueuePanel(m_backend, m_queue, this);
    m_queuePanel->hide();
    addDockWidget(Qt::RightDockWidgetArea, m_queuePanel);

    m_transfersPanel = new TransfersPanel(this);
    m_transfersPanel->hide();
    addDockWidget(Qt::RightDockWidgetArea, m_transfersPanel);

    // ---- Search side panel (right) ----
    m_sidePanel = new SidePanel::View(m_backend, m_queue, this);
    m_sidePanel->hide();
    addDockWidget(Qt::RightDockWidgetArea, m_sidePanel);
}

int MainWindow::currentLibraryDockWidth() const
{
    if (!m_libraryDock || m_libraryDock->isFloating())
        return -1;
    return m_libraryDock->width();
}

void MainWindow::restoreLibraryDockWidth(int width)
{
    if (width <= 0)
        return;

    QTimer::singleShot(0, this, [this, width] {
        if (!m_libraryDock || m_libraryDock->isFloating())
            return;
        resizeDocks(QList<QDockWidget *>{m_libraryDock},
                    QList<int>{width},
                    Qt::Horizontal);
    });
}

void MainWindow::restoreWindowLayout()
{
    AppSettings &settings = AppSettings::instance();

    const QByteArray geometry = settings.windowGeometry();
    if (!geometry.isEmpty())
        restoreGeometry(geometry);

    const QByteArray state = settings.windowState();
    if (!state.isEmpty())
        restoreState(state, kWindowStateVersion);

    restoreLibraryDockWidth(settings.libraryDockWidth());
}

void MainWindow::saveWindowLayout() const
{
    AppSettings &settings = AppSettings::instance();
    settings.setWindowGeometry(saveGeometry());
    settings.setWindowState(saveState(kWindowStateVersion));

    const int libraryWidth = currentLibraryDockWidth();
    if (libraryWidth > 0)
        settings.setLibraryDockWidth(libraryWidth);
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    saveWindowLayout();
    QMainWindow::closeEvent(event);
}

void MainWindow::setupScrobbler()
{
    m_scrobbler = new LastFmScrobbler(this);
    connect(m_backend, &QobuzBackend::trackChanged,
            m_scrobbler, &LastFmScrobbler::onTrackStarted);
    connect(m_backend, &QobuzBackend::positionChanged,
            m_scrobbler, &LastFmScrobbler::onPositionChanged);
    connect(m_backend, &QobuzBackend::trackFinished,
            m_scrobbler, &LastFmScrobbler::onTrackFinished);

    // Scrobble the finished track during a gapless transition
    connect(m_backend, &QobuzBackend::trackTransitioned,
            m_scrobbler, &LastFmScrobbler::onTrackFinished);
}

void MainWindow::setupGapless()
{
    // Gapless prefetch is handled in MainToolBar::onPositionChanged, which also
    // owns the prefetched-track-ID check in onTrackTransitioned. Nothing to do here.
}

void MainWindow::setupMpris()
{
#ifdef USE_DBUS
    m_mpris = new Mpris(this);
    connect(m_mpris->player(), &MprisPlayerAdaptor::playRequested, m_backend, [this] {
        if (m_backend->state() == 2) m_backend->resume();
    });
    connect(m_mpris->player(), &MprisPlayerAdaptor::pauseRequested, m_backend, &QobuzBackend::pause);
    connect(m_mpris->player(), &MprisPlayerAdaptor::playPauseRequested, m_backend, [this] {
        if (m_backend->state() == 1)
            m_backend->pause();
        else
            m_backend->resume();
    });
    connect(m_mpris->player(), &MprisPlayerAdaptor::stopRequested, m_backend, &QobuzBackend::stop);
    connect(m_mpris->player(), &MprisPlayerAdaptor::nextRequested, this, [this] {
        if (!m_queue->canGoNext()) return;
        const qint64 id = static_cast<qint64>(m_queue->advance()["id"].toDouble());
        if (id > 0) m_backend->playTrack(id, AppSettings::instance().preferredFormat());
    });
    connect(m_mpris->player(), &MprisPlayerAdaptor::previousRequested, this, [this] {
        if (!m_queue->canGoPrev()) return;
        const qint64 id = static_cast<qint64>(m_queue->stepBack()["id"].toDouble());
        if (id > 0) m_backend->playTrack(id, AppSettings::instance().preferredFormat());
    });
    connect(m_mpris->player(), &MprisPlayerAdaptor::seekRequested, m_backend, [this](qlonglong offsetMicroseconds) {
        qint64 newPos = m_backend->position() + (offsetMicroseconds / 1000000LL);
        if (newPos < 0) newPos = 0;
        m_backend->seek(newPos);
    });
    connect(m_mpris->player(), &MprisPlayerAdaptor::seekToRequested, m_backend, [this](qlonglong positionMicroseconds) {
        m_backend->seek(positionMicroseconds / 1000000LL);
    });
    connect(m_mpris->player(), &MprisPlayerAdaptor::volumeChangeRequested, m_backend, [this](double vol) {
        m_backend->setVolume(vol * 100);
    });

    connect(m_backend, &QobuzBackend::stateChanged, this, [this](const QString &state) {
        if (state == "playing") m_mpris->player()->setPlaybackStatus("Playing");
        else if (state == "paused") m_mpris->player()->setPlaybackStatus("Paused");
        else m_mpris->player()->setPlaybackStatus("Stopped");
    });
    connect(m_backend, &QobuzBackend::positionChanged, this, [this](quint64 pos) {
        m_mpris->player()->updatePosition(pos);
    });
    auto updateMprisNav = [this] {
        m_mpris->player()->setCanGoNext(m_queue->canGoNext());
        m_mpris->player()->setCanGoPrevious(m_queue->canGoPrev());
    };
    connect(m_queue, &PlayQueue::queueChanged, this, updateMprisNav);
    updateMprisNav();
#endif
}

void MainWindow::connectBackendSignals()
{
    connect(m_backend, &QobuzBackend::loginSuccess,   this, &MainWindow::onLoginSuccess);
    connect(m_backend, &QobuzBackend::loginError,     this, &MainWindow::onLoginError);
    connect(m_backend, &QobuzBackend::userLoaded, this, [this](const QJsonObject &user) {
        const qint64 id = static_cast<qint64>(user["id"].toDouble());
        if (id > 0) {
            AppSettings::instance().setUserId(id);
            m_library->refresh();  // re-load playlists with correct ownership now
        }
    });
    connect(m_backend, &QobuzBackend::favTracksLoaded,  this, &MainWindow::onFavTracksLoaded);
    connect(m_backend, &QobuzBackend::favAlbumsLoaded,  this, &MainWindow::onFavAlbumsLoaded);
    connect(m_backend, &QobuzBackend::favArtistsLoaded, this, &MainWindow::onFavArtistsLoaded);
    connect(m_backend, &QobuzBackend::albumLoaded,      this, &MainWindow::onAlbumLoaded);
    connect(m_backend, &QobuzBackend::artistLoaded,     this, &MainWindow::onArtistLoaded);
    connect(m_backend, &QobuzBackend::artistReleasesLoaded,
            m_content, &MainContent::updateArtistReleases);
    connect(m_backend, &QobuzBackend::deepShuffleTracksLoaded,
            m_content, &MainContent::onDeepShuffleTracks);
    connect(m_backend, &QobuzBackend::playlistLoaded,   this, &MainWindow::onPlaylistLoaded);
    connect(m_backend, &QobuzBackend::playlistCreated,   this, &MainWindow::onPlaylistCreated);
    connect(m_backend, &QobuzBackend::playlistDeleted,   this, [this](const QJsonObject &) {
        // status bar message is also shown by library's openPlaylistDeleted handler
    });
    connect(m_backend, &QobuzBackend::playlistTrackAdded, this, [this](qint64 playlistId) {
        // Refresh the currently shown playlist if a track was added to it
        if (m_content->tracksList()->playlistId() == playlistId)
            m_backend->getPlaylist(playlistId);
        statusBar()->showMessage(tr("Track added to playlist"), 3000);
    });
    connect(m_backend, &QobuzBackend::playlistSubscribed, this, [this](qint64 playlistId) {
        m_userPlaylistIds.insert(playlistId);
        m_library->refresh();
        if (m_content->tracksList()->playlistId() == playlistId)
            m_content->setCurrentPlaylistFollowed(true);
        statusBar()->showMessage(tr("Playlist followed"), 3000);
    });
    connect(m_backend, &QobuzBackend::playlistUnsubscribed, this, [this](qint64 playlistId) {
        m_userPlaylistIds.remove(playlistId);
        m_library->refresh();
        if (m_content->tracksList()->playlistId() == playlistId)
            m_content->setCurrentPlaylistFollowed(false);
        statusBar()->showMessage(tr("Playlist unfollowed"), 3000);
    });
    connect(m_backend, &QobuzBackend::trackChanged,   this, &MainWindow::onTrackChanged);
    connect(m_backend, &QobuzBackend::downloadStarted, this, &MainWindow::onDownloadStarted);
    connect(m_backend, &QobuzBackend::downloadProgress, this, &MainWindow::onDownloadProgress);
    connect(m_backend, &QobuzBackend::downloadFinished, this, &MainWindow::onDownloadFinished);
    connect(m_backend, &QobuzBackend::downloadFailed, this, &MainWindow::onDownloadFailed);
    connect(m_backend, &QobuzBackend::downloadCancelled, m_transfersPanel, &TransfersPanel::onTransferCancelled);
    connect(m_backend, &QobuzBackend::downloadCancelled, this, [this](const QJsonObject &info) {
        statusBar()->showMessage(tr("Download cancelled: %1").arg(info["label"].toString()), 5000);
    });
    connect(m_backend, &QobuzBackend::error, this, [this](const QString &msg) {
        statusBar()->showMessage(tr("Error: %1").arg(msg), 6000);
    });
}

void MainWindow::connectLibrarySignals()
{
    connect(m_library, &List::Library::userPlaylistIdsChanged,
            this, [this](const QSet<qint64> &playlistIds) {
        m_userPlaylistIds = playlistIds;
        const qint64 currentPlaylistId = m_content->tracksList()->playlistId();
        if (currentPlaylistId > 0)
            m_content->setCurrentPlaylistFollowed(m_userPlaylistIds.contains(currentPlaylistId));
    });
    connect(m_library, &List::Library::userPlaylistsChanged,
            this, &MainWindow::onUserPlaylistsChanged);
    connect(m_library, &List::Library::openPlaylistDeleted,
            this, [this] {
        const int libraryWidth = currentLibraryDockWidth();
        m_content->showWelcome();
        restoreLibraryDockWidth(libraryWidth);
        statusBar()->showMessage(tr("Playlist deleted"), 3000);
    });

    // ---- Library → backend ----
    connect(m_library, &List::Library::favTracksRequested, this, [this] {
        m_showFavTracksOnLoad = true;
        m_backend->getFavTracks();
        statusBar()->showMessage(tr("Loading favorite tracks…"));
    });
    connect(m_library, &List::Library::favAlbumsRequested, this, [this] {
        m_showFavAlbumsOnLoad = true;
        m_backend->getFavAlbums();
        statusBar()->showMessage(tr("Loading favorite albums…"));
    });
    connect(m_library, &List::Library::favArtistsRequested, this, [this] {
        m_showFavArtistsOnLoad = true;
        m_backend->getFavArtists();
        statusBar()->showMessage(tr("Loading favorite artists…"));
    });
    connect(m_library, &List::Library::playlistRequested,
            this, [this](qint64 id, const QString &name) {
        m_backend->getPlaylist(id);
        statusBar()->showMessage(tr("Loading playlist: %1…").arg(name));
    });
    connect(m_library, &List::Library::playlistDownloadRequested,
            this, [this](qint64 id, const QString &name) {
        m_backend->downloadPlaylist(id, AppSettings::instance().downloadFormat());
        statusBar()->showMessage(tr("Downloading playlist: %1…").arg(name));
    });
    connect(m_library, &List::Library::browseGenresRequested, this, [this] {
        const int libraryWidth = currentLibraryDockWidth();
        m_content->showGenreBrowser();
        restoreLibraryDockWidth(libraryWidth);
        statusBar()->showMessage(tr("Browse Genres"));
    });
    connect(m_library, &List::Library::browsePlaylistsRequested, this, [this] {
        const int libraryWidth = currentLibraryDockWidth();
        m_content->showPlaylistBrowser();
        restoreLibraryDockWidth(libraryWidth);
        statusBar()->showMessage(tr("Browse Playlists"));
    });
}

void MainWindow::connectContentSignals()
{
    // ---- Track list → playback / playlist management ----
    connect(m_content->tracksList(), &List::Tracks::playTrackRequested,
            this, &MainWindow::onPlayTrackRequested);
    connect(m_content->tracksList(), &List::Tracks::downloadTracksRequested,
            this, [this](const QVector<qint64> &trackIds) {
        const int formatId = AppSettings::instance().downloadFormat();
        for (qint64 trackId : trackIds)
            m_backend->downloadTrack(trackId, formatId);
        statusBar()->showMessage(
            trackIds.size() == 1
                ? tr("Downloading track…")
                : tr("Downloading %1 tracks…").arg(trackIds.size()));
    });
    connect(m_content->tracksList(), &List::Tracks::addToPlaylistRequested,
            this, [this](qint64 trackId, qint64 playlistId) {
        m_backend->addTrackToPlaylist(playlistId, trackId);
        statusBar()->showMessage(tr("Adding track to playlist…"), 3000);
    });
    connect(m_content->tracksList(), &List::Tracks::removeFromPlaylistRequested,
            this, [this](qint64 playlistId, qint64 playlistTrackId) {
        m_backend->deleteTrackFromPlaylist(playlistId, playlistTrackId);
        statusBar()->showMessage(tr("Removing track from playlist…"), 3000);
    });

    // ---- Search panel ----
    connect(m_sidePanel, &SidePanel::View::albumSelected,
            this, &MainWindow::onSearchAlbumSelected);
    connect(m_sidePanel, &SidePanel::View::artistSelected,
            this, &MainWindow::onSearchArtistSelected);
    connect(m_sidePanel, &SidePanel::View::trackPlayRequested,
            this, &MainWindow::onPlayTrackRequested);
    connect(m_sidePanel, &SidePanel::View::addToPlaylistRequested,
            this, [this](qint64 trackId, qint64 playlistId) {
        m_backend->addTrackToPlaylist(playlistId, trackId);
        statusBar()->showMessage(tr("Adding track to playlist..."), 3000);
    });

    // ---- Album / artist navigation from content views ----
    connect(m_content, &MainContent::albumRequested,
            this, &MainWindow::onSearchAlbumSelected);
    connect(m_content, &MainContent::artistRequested,
            this, &MainWindow::onSearchArtistSelected);
    connect(m_content, &MainContent::albumFavoriteToggled,
            this, [this](const QString &albumId, bool favorite) {
        if (favorite) {
            m_backend->addFavAlbum(albumId);
            m_favAlbumIds.insert(albumId);
            statusBar()->showMessage(tr("Added album to favorites"), 3000);
        } else {
            m_backend->removeFavAlbum(albumId);
            m_favAlbumIds.remove(albumId);
            statusBar()->showMessage(tr("Removed album from favorites"), 3000);
        }
        m_content->setFavAlbumIds(m_favAlbumIds);
    });
    connect(m_content, &MainContent::playlistRequested,
            this, [this](qint64 playlistId) {
        m_backend->getPlaylist(playlistId);
        statusBar()->showMessage(tr("Loading playlist…"));
    });
    connect(m_content, &MainContent::downloadAlbumRequested,
            this, [this](const QString &albumId) {
        m_backend->downloadAlbum(albumId, AppSettings::instance().downloadFormat());
        statusBar()->showMessage(tr("Downloading album…"));
    });
    connect(m_content, &MainContent::downloadPlaylistRequested,
            this, [this](qint64 playlistId) {
        m_backend->downloadPlaylist(playlistId, AppSettings::instance().downloadFormat());
        statusBar()->showMessage(tr("Downloading playlist…"));
    });
    connect(m_content, &MainContent::playlistFollowToggled,
            this, [this](qint64 playlistId, bool follow) {
        if (follow)
            m_backend->subscribePlaylist(playlistId);
        else
            m_backend->unsubscribePlaylist(playlistId);
    });
    connect(m_content, &MainContent::playTrackRequested,
            this, &MainWindow::onPlayTrackRequested);

    // ---- Now-playing context dock ----
    connect(m_contextView, &Context::View::albumRequested,
            this, &MainWindow::onSearchAlbumSelected);
    connect(m_contextView, &Context::View::artistRequested,
            this, &MainWindow::onSearchArtistSelected);
    connect(m_contextView, &Context::View::favTrackRequested,
            this, [this](qint64 trackId) {
        m_backend->addFavTrack(trackId);
        m_favTrackIds.insert(trackId);
        m_toolBar->addFavTrackId(trackId);
        m_queuePanel->addFavTrackId(trackId);
        m_contextView->addFavTrackId(trackId);
        m_content->tracksList()->addFavTrackId(trackId);
    });
    connect(m_contextView, &Context::View::unfavTrackRequested,
            this, [this](qint64 trackId) {
        m_backend->removeFavTrack(trackId);
        m_favTrackIds.remove(trackId);
        m_toolBar->removeFavTrackId(trackId);
        m_queuePanel->removeFavTrackId(trackId);
        m_contextView->removeFavTrackId(trackId);
        m_content->tracksList()->removeFavTrackId(trackId);
    });

    // ---- Queue panel ----
    connect(m_queuePanel, &QueuePanel::skipToTrackRequested,
            this, &MainWindow::onPlayTrackRequested);
    connect(m_queuePanel, &QueuePanel::addToPlaylistRequested,
            this, [this](qint64 trackId, qint64 playlistId) {
        m_backend->addTrackToPlaylist(playlistId, trackId);
        statusBar()->showMessage(tr("Adding track to playlist…"), 3000);
    });
    connect(m_queuePanel, &QueuePanel::favTrackRequested,
            this, [this](qint64 trackId) {
        m_backend->addFavTrack(trackId);
        m_favTrackIds.insert(trackId);
        m_toolBar->addFavTrackId(trackId);
        m_queuePanel->addFavTrackId(trackId);
        m_contextView->addFavTrackId(trackId);
        m_content->tracksList()->addFavTrackId(trackId);
    });
    connect(m_queuePanel, &QueuePanel::unfavTrackRequested,
            this, [this](qint64 trackId) {
        m_backend->removeFavTrack(trackId);
        m_favTrackIds.remove(trackId);
        m_toolBar->removeFavTrackId(trackId);
        m_queuePanel->removeFavTrackId(trackId);
        m_contextView->removeFavTrackId(trackId);
        m_content->tracksList()->removeFavTrackId(trackId);
    });
}

void MainWindow::connectToolbarSignals()
{
    connect(m_toolBar, &MainToolBar::searchToggled, this, &MainWindow::onSearchToggled);
    connect(m_toolBar, &MainToolBar::queueToggled,
            this, [this](bool v) { m_queuePanel->setVisible(v); });
    connect(m_toolBar, &MainToolBar::transfersToggled,
            this, [this](bool v) { m_transfersPanel->setVisible(v); });
    connect(m_queuePanel, &QDockWidget::visibilityChanged,
            m_toolBar, &MainToolBar::setQueueToggleChecked);
    connect(m_sidePanel, &QDockWidget::visibilityChanged,
            m_toolBar, &MainToolBar::setSearchToggleChecked);
    connect(m_transfersPanel, &QDockWidget::visibilityChanged,
            m_toolBar, &MainToolBar::setTransfersToggleChecked);
    connect(m_transfersPanel, &TransfersPanel::cancelTransferRequested,
            this, [this](quint64 transferId) { m_backend->cancelDownload(transferId); });
    connect(m_transfersPanel, &TransfersPanel::cancelAllTransfersRequested,
            this, [this] { m_backend->cancelAllDownloads(); });
    m_toolBar->setQueueToggleChecked(m_queuePanel->isVisible());
    m_toolBar->setSearchToggleChecked(m_sidePanel->isVisible());
    m_toolBar->setTransfersToggleChecked(m_transfersPanel->isVisible());

    connect(m_toolBar, &MainToolBar::albumRequested,  this, &MainWindow::onSearchAlbumSelected);
    connect(m_toolBar, &MainToolBar::artistRequested, this, &MainWindow::onSearchArtistSelected);
    connect(m_toolBar, &MainToolBar::addToPlaylistRequested,
            this, [this](qint64 trackId, qint64 playlistId) {
        m_backend->addTrackToPlaylist(playlistId, trackId);
        statusBar()->showMessage(tr("Adding track to playlist…"), 3000);
    });
    connect(m_toolBar, &MainToolBar::favTrackRequested,
            this, [this](qint64 trackId) {
        m_backend->addFavTrack(trackId);
        m_favTrackIds.insert(trackId);
        m_toolBar->addFavTrackId(trackId);
        m_queuePanel->addFavTrackId(trackId);
        m_contextView->addFavTrackId(trackId);
        m_content->tracksList()->addFavTrackId(trackId);
    });
    connect(m_toolBar, &MainToolBar::unfavTrackRequested,
            this, [this](qint64 trackId) {
        m_backend->removeFavTrack(trackId);
        m_favTrackIds.remove(trackId);
        m_toolBar->removeFavTrackId(trackId);
        m_queuePanel->removeFavTrackId(trackId);
        m_contextView->removeFavTrackId(trackId);
        m_content->tracksList()->removeFavTrackId(trackId);
    });
}

void MainWindow::setupMenuBar()
{
    auto *fileMenu = menuBar()->addMenu(tr("&File"));
    fileMenu->addAction(Icon::get("im-user-away"), tr("&Sign in…"),
                        this, &MainWindow::showLoginDialog);
    fileMenu->addSeparator();
    fileMenu->addAction(Icon::settings(), tr("&Settings…"),
                        this, &MainWindow::showSettingsDialog);
    fileMenu->addSeparator();
    auto *quitAction = fileMenu->addAction(Icon::get("application-exit"), tr("&Quit"),
                                           qApp, &QApplication::quit);
    quitAction->setShortcut(QKeySequence::Quit);

    auto *viewMenu = menuBar()->addMenu(tr("&View"));
    viewMenu->addAction(m_libraryDock->toggleViewAction());
    viewMenu->addAction(m_contextView->toggleViewAction());
    viewMenu->addAction(m_queuePanel->toggleViewAction());
    viewMenu->addAction(m_transfersPanel->toggleViewAction());
    viewMenu->addAction(m_sidePanel->toggleViewAction());
    viewMenu->addSeparator();
    viewMenu->addAction(Icon::get("view-refresh"), tr("Reset layout"),
                        this, &MainWindow::resetLayout);

    auto *helpMenu = menuBar()->addMenu(tr("&Help"));
    helpMenu->addAction(Icon::get("help-about"), tr("&About"), this, [this] {
        QMessageBox::about(this, tr("About Qobuz"),
            tr("<h3>qobuz-qt</h3>"
               "<p>A lightweight Qt client for the Qobuz streaming service.</p>"
               "<p>Audio engine: <b>Symphonia</b> (Rust) via CPAL/ALSA.<br>"
               "Icons: <b>spotify-qt</b> (dark variant).</p>"));
    });
}

void MainWindow::tryRestoreSession()
{
    const QString token = AppSettings::instance().authToken();
    if (!token.isEmpty()) {
        m_backend->setToken(token);
        if (AppSettings::instance().userId() == 0)
            m_backend->getUser();  // userLoaded will call m_library->refresh()
        else
            m_library->refresh();
        // Preload favorites so buttons/menus reflect state immediately.
        m_backend->getFavTracks();
        m_backend->getFavAlbums();
        m_backend->getFavArtists();
        const QString name = AppSettings::instance().displayName();
        statusBar()->showMessage(tr("Signed in as %1").arg(
            name.isEmpty() ? AppSettings::instance().userEmail() : name));
    } else {
        QTimer::singleShot(200, this, &MainWindow::showLoginDialog);
    }
}

// ---- slots ----

void MainWindow::resetLayout()
{
    const auto answer = QMessageBox::question(
        this,
        tr("Reset Layout"),
        tr("Reset window layout to defaults?\nThis will restore dock positions, visibility, and sizes."),
        QMessageBox::Yes | QMessageBox::Cancel,
        QMessageBox::Cancel);
    if (answer != QMessageBox::Yes)
        return;

    AppSettings::instance().clearWindowLayout();

    resize(defaultSize());
    if (!m_defaultWindowState.isEmpty())
        restoreState(m_defaultWindowState, kWindowStateVersion);

    saveWindowLayout();
    statusBar()->showMessage(tr("Layout reset"), 3000);
}

void MainWindow::showLoginDialog()
{
    auto *dlg = new LoginDialog(this);
    dlg->setAttribute(Qt::WA_DeleteOnClose);

    connect(dlg, &LoginDialog::loginRequested,
            this, [this, dlg](const QString &email, const QString &password) {
        dlg->setBusy(true);
        m_backend->login(email, password);
    });
    connect(m_backend, &QobuzBackend::loginSuccess, dlg, [dlg](const QString &, const QJsonObject &) {
        dlg->accept();
    });
    connect(m_backend, &QobuzBackend::loginError, dlg, [dlg](const QString &err) {
        dlg->setError(err);
    });

    dlg->exec();
}

void MainWindow::showSettingsDialog()
{
    SettingsDialog dlg(this);
    dlg.exec();
}

void MainWindow::onLoginSuccess(const QString &token, const QJsonObject &user)
{
    AppSettings::instance().setAuthToken(token);
    const QString displayName = user["display_name"].toString();
    const QString email       = user["email"].toString();
    AppSettings::instance().setDisplayName(displayName);
    AppSettings::instance().setUserEmail(email);
    const qint64 userId = static_cast<qint64>(user["id"].toDouble());
    if (userId > 0)
        AppSettings::instance().setUserId(userId);
    statusBar()->showMessage(tr("Signed in as %1").arg(
        displayName.isEmpty() ? email : displayName));
    m_library->refresh();
    m_backend->getFavAlbums();
    m_backend->getFavArtists();
}

void MainWindow::onLoginError(const QString &error)
{
    statusBar()->showMessage(tr("Login failed: %1").arg(error), 6000);
}

void MainWindow::onTrackChanged(const QJsonObject &track)
{
    // Update playing row highlight in the track list
    const qint64 id = static_cast<qint64>(track["id"].toDouble());
    m_content->tracksList()->setPlayingTrackId(id);

    // Update status bar with track name
    const QString title  = track["title"].toString();
    const QString artist = track["performer"].toObject()["name"].toString().isEmpty()
        ? track["album"].toObject()["artist"].toObject()["name"].toString()
        : track["performer"].toObject()["name"].toString();
    statusBar()->showMessage(
        artist.isEmpty() ? title : QStringLiteral("▶  %1 — %2").arg(artist, title));

#ifdef USE_DBUS
    QVariantMap metadata;
    metadata["mpris:trackid"] = QVariant::fromValue(QDBusObjectPath(QString("/org/qobuz/track/%1").arg(id)));
    metadata["mpris:length"] = QVariant::fromValue(qlonglong(track["duration"].toDouble() * 1000000LL));
    metadata["xesam:title"]  = title;

    QJsonObject album = track["album"].toObject();
    metadata["xesam:album"]  = album["title"].toString();

    if (!artist.isEmpty()) {
        metadata["xesam:artist"] = QStringList{artist};
    }

    if (album.contains("image") && album["image"].toObject().contains("large")) {
        metadata["mpris:artUrl"] = album["image"].toObject()["large"].toString();
    }

    m_mpris->player()->setMetadata(metadata);
#endif
}

void MainWindow::onFavTracksLoaded(const QJsonObject &result)
{
    // Always cache fav IDs (needed by context menus across the app)
    m_favTrackIds.clear();
    const QJsonArray items = result["items"].toArray();
    for (const QJsonValue &v : items) {
        const qint64 id = static_cast<qint64>(v.toObject()["id"].toDouble());
        if (id > 0) m_favTrackIds.insert(id);
    }
    m_content->tracksList()->setFavTrackIds(m_favTrackIds);
    m_toolBar->setFavTrackIds(m_favTrackIds);
    m_queuePanel->setFavTrackIds(m_favTrackIds);
    m_contextView->setFavTrackIds(m_favTrackIds);

    // Only navigate to the fav tracks page if the user explicitly requested it
    if (m_showFavTracksOnLoad) {
        m_showFavTracksOnLoad = false;
        const int libraryWidth = currentLibraryDockWidth();
        m_content->showFavTracks(result);
        restoreLibraryDockWidth(libraryWidth);
        statusBar()->showMessage(
            tr("%1 favorite tracks").arg(result["total"].toInt()), 4000);
    }
}

void MainWindow::onFavAlbumsLoaded(const QJsonObject &result)
{
    // Always cache fav album IDs (needed by the album page fav button)
    m_favAlbumIds.clear();
    const QJsonArray items = result["items"].toArray();
    for (const QJsonValue &v : items) {
        const QJsonObject album = v.toObject();
        QString id = album["id"].toString();
        if (id.isEmpty() && album["id"].isDouble())
            id = QString::number(static_cast<qint64>(album["id"].toDouble()));
        if (!id.isEmpty())
            m_favAlbumIds.insert(id);
    }
    m_content->setFavAlbumIds(m_favAlbumIds);

    // Only navigate to the fav albums page if the user explicitly requested it
    if (m_showFavAlbumsOnLoad) {
        m_showFavAlbumsOnLoad = false;
        const int libraryWidth = currentLibraryDockWidth();
        m_content->showFavAlbums(result);
        restoreLibraryDockWidth(libraryWidth);
        statusBar()->showMessage(
            tr("%1 favorite albums").arg(result["total"].toInt()), 4000);
    }
}

void MainWindow::onFavArtistsLoaded(const QJsonObject &result)
{
    // Always cache fav artist IDs (needed by the artist page fav button)
    m_favArtistIds.clear();
    const QJsonArray items = result["items"].toArray();
    for (const QJsonValue &v : items) {
        const qint64 id = static_cast<qint64>(v.toObject()["id"].toDouble());
        if (id > 0) m_favArtistIds.insert(id);
    }
    m_content->setFavArtistIds(m_favArtistIds);

    // Only navigate to the fav artists page if the user explicitly requested it
    if (m_showFavArtistsOnLoad) {
        m_showFavArtistsOnLoad = false;
        const int libraryWidth = currentLibraryDockWidth();
        m_content->showFavArtists(result);
        restoreLibraryDockWidth(libraryWidth);
        statusBar()->showMessage(
            tr("%1 favorite artists").arg(result["total"].toInt()), 4000);
    }
}

void MainWindow::onAlbumLoaded(const QJsonObject &album)
{
    const int libraryWidth = currentLibraryDockWidth();

    m_content->showAlbum(album);
    restoreLibraryDockWidth(libraryWidth);

    statusBar()->showMessage(
        tr("Album: %1").arg(album["title"].toString()), 4000);
}

void MainWindow::onArtistLoaded(const QJsonObject &artist)
{
    const int libraryWidth = currentLibraryDockWidth();

    m_content->showArtist(artist);
    restoreLibraryDockWidth(libraryWidth);

    // Fire release requests only after the artist page is shown — avoids the
    // race where a fast-responding release request arrives before setArtist()
    // clears the sections, causing setArtist() to wipe out the data.
    const qint64 artistId = static_cast<qint64>(artist["id"].toDouble());
    for (const char *type : {"album", "epSingle", "live", "compilation"})
        m_backend->getArtistReleases(artistId, QString::fromLatin1(type));
    statusBar()->showMessage(
        tr("Artist: %1").arg(artist["name"].toObject()["display"].toString()), 4000);
}

void MainWindow::onPlaylistLoaded(const QJsonObject &playlist)
{
    const bool fullLoad = playlist["full_load"].toBool(false);
    const int trackOffset = playlist["tracks"].toObject()["offset"].toInt(0);
    if (!fullLoad && trackOffset > 0) {
        m_content->tracksList()->appendPlaylistPage(playlist);
        return;
    }

    const qint64 id = static_cast<qint64>(playlist["id"].toDouble());
    const qint64 ownerId = static_cast<qint64>(playlist["owner"].toObject()["id"].toDouble());
    const qint64 myId = AppSettings::instance().userId();
    const bool isOwned = (myId > 0 && ownerId == myId);

    bool isFollowed = isOwned || m_userPlaylistIds.contains(id);
    if (!isFollowed) {
        if (playlist.contains("is_subscribed"))
            isFollowed = playlist["is_subscribed"].toBool();
        else if (playlist.contains("subscribed_at"))
            isFollowed = !playlist["subscribed_at"].isNull();
    }

    const int libraryWidth = currentLibraryDockWidth();

    m_content->showPlaylist(playlist, isFollowed, isOwned);
    restoreLibraryDockWidth(libraryWidth);

    statusBar()->showMessage(
        tr("Playlist: %1").arg(playlist["name"].toString()), 4000);
}

void MainWindow::onPlayTrackRequested(qint64 trackId)
{
    m_backend->playTrack(trackId, AppSettings::instance().preferredFormat());
}

void MainWindow::onSearchAlbumSelected(const QString &albumId)
{
    m_backend->getAlbum(albumId);
    statusBar()->showMessage(tr("Loading album…"));
}

void MainWindow::onSearchArtistSelected(qint64 artistId)
{
    m_backend->getArtist(artistId);
    statusBar()->showMessage(tr("Loading artist…"));
}

void MainWindow::onSearchToggled(bool visible)
{
    m_sidePanel->setVisible(visible);
}

void MainWindow::onPlaylistCreated(const QJsonObject &playlist)
{
    const QString name = playlist["name"].toString();
    statusBar()->showMessage(tr("Playlist '%1' created").arg(name), 4000);
    // Open the new playlist immediately
    const qint64 id = static_cast<qint64>(playlist["id"].toDouble());
    if (id > 0)
        m_backend->getPlaylist(id);
}

void MainWindow::onUserPlaylistsChanged(const QVector<QPair<qint64, QString>> &playlists)
{
    m_userPlaylists = playlists;
    m_content->tracksList()->setUserPlaylists(playlists);
    m_sidePanel->searchTab()->setUserPlaylists(playlists);
    m_toolBar->setUserPlaylists(playlists);
    m_queuePanel->setUserPlaylists(playlists);
}

void MainWindow::onDownloadStarted(const QJsonObject &info)
{
    m_transfersPanel->onTransferStarted(info);
    if (!m_transfersPanel->isVisible())
        m_transfersPanel->show();
    const QString kind = info["kind"].toString();
    const QString label = info["label"].toString();
    const int totalTracks = info["total_tracks"].toInt();
    if (kind == QLatin1String("track")) {
        statusBar()->showMessage(tr("Downloading track: %1").arg(label));
        return;
    }
    statusBar()->showMessage(tr("Downloading %1 (%2 tracks)…").arg(label).arg(totalTracks));
}

void MainWindow::onDownloadProgress(const QJsonObject &info)
{
    m_transfersPanel->onTransferProgress(info);
    const QString kind = info["kind"].toString();
    const QString title = info["track_title"].toString();
    const int current = info["current"].toInt();
    const int totalTracks = info["total_tracks"].toInt();
    const qint64 totalBytes = static_cast<qint64>(info["total_bytes"].toDouble(-1));
    const qint64 downloadedBytes = static_cast<qint64>(info["downloaded_bytes"].toDouble());
    QString suffix;
    if (totalBytes > 0) {
        const int percent = static_cast<int>((100.0 * downloadedBytes) / totalBytes);
        suffix = tr(" (%1%)").arg(percent);
    }
    if (kind == QLatin1String("track")) {
        statusBar()->showMessage(tr("Downloading %1%2").arg(title, suffix));
        return;
    }
    statusBar()->showMessage(tr("Downloading %1/%2: %3%4").arg(current).arg(totalTracks).arg(title, suffix));
}

void MainWindow::onDownloadFinished(const QJsonObject &info)
{
    m_transfersPanel->onTransferFinished(info);
    const QString kind = info["kind"].toString();
    const QString path = info["path"].toString();
    const int failedTracks = info["failed_tracks"].toInt();
    if (kind == QLatin1String("track")) {
        statusBar()->showMessage(tr("Track downloaded to %1").arg(path), 6000);
        return;
    }
    if (failedTracks > 0) {
        statusBar()->showMessage(tr("Download finished with %1 failed tracks: %2").arg(failedTracks).arg(path), 8000);
        return;
    }
    statusBar()->showMessage(tr("Download finished: %1").arg(path), 6000);
}

void MainWindow::onDownloadFailed(const QJsonObject &info)
{
    m_transfersPanel->onTransferFailed(info);
    statusBar()->showMessage(tr("Download failed: %1").arg(info["error"].toString()), 8000);
}
