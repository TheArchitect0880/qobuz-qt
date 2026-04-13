#include "maintoolbar.hpp"
#include "../util/settings.hpp"
#include "../util/trackinfo.hpp"
#include "../util/albumqueuehelper.hpp"
#include "../model/tracklistmodel.hpp"

#include <QNetworkRequest>
#include <QResizeEvent>
#include <QMouseEvent>
#include <QMenu>
#include <QDateTime>
#include <QSignalBlocker>

MainToolBar::MainToolBar(QobuzBackend *backend, PlayQueue *queue, QWidget *parent)
    : QToolBar(parent)
    , m_backend(backend)
    , m_queue(queue)
{
    setMovable(false);
    setFloatable(false);
    setContextMenuPolicy(Qt::PreventContextMenu);
    setIconSize(QSize(22, 22));

    m_nam = new QNetworkAccessManager(this);
    m_albumQueueHelper = new AlbumQueueHelper(m_backend, m_queue, this);
    connect(m_nam, &QNetworkAccessManager::finished, this, &MainToolBar::onAlbumArtReady);

    // ---- Album art ----
    m_artLabel = new QLabel(this);
    m_artLabel->setFixedSize(36, 36);
    m_artLabel->setScaledContents(true);
    m_artLabel->setStyleSheet("border: 1px solid #444; background: #1a1a1a; border-radius: 3px;");
    m_artLabel->setPixmap(QIcon(":/res/icons/view-media-album-cover.svg").pixmap(32, 32));
    m_artLabel->setCursor(Qt::PointingHandCursor);
    m_artLabel->installEventFilter(this);
    addWidget(m_artLabel);

    // ---- Track label ----
    m_trackLabel = new QLabel(tr("Not playing"), this);
    m_trackLabel->setFixedWidth(140);
    m_trackLabel->setAlignment(Qt::AlignVCenter | Qt::AlignLeft);
    m_trackLabel->setTextFormat(Qt::RichText);
    addWidget(m_trackLabel);

    m_trackLabel->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_trackLabel, &QLabel::customContextMenuRequested,
            this, [this](const QPoint &pos) {
        if (m_currentTrack.isEmpty()) return;

        const qint64 trackId = static_cast<qint64>(m_currentTrack["id"].toDouble());
        const QString albumId  = m_currentTrack["album"].toObject()["id"].toString();
        const QString albumTitle = m_currentTrack["album"].toObject()["title"].toString();
        const qint64  artistId = static_cast<qint64>(
            m_currentTrack["performer"].toObject()["id"].toDouble());
        const QString artistName = m_currentTrack["performer"].toObject()["name"].toString();

        QMenu menu(this);

        auto *playNext = menu.addAction(QIcon(":/res/icons/media-skip-forward.svg"), tr("Play next"));
        auto *addQueue = menu.addAction(QIcon(":/res/icons/media-playlist-append.svg"), tr("Add to queue"));
        menu.addSeparator();

        const bool isFav = m_favTrackIds.contains(trackId);
        if (isFav) {
            auto *remFav = menu.addAction(QIcon(":/res/icons/non-starred-symbolic.svg"), tr("Remove from favorites"));
            connect(remFav, &QAction::triggered, this, [this, trackId] {
                emit unfavTrackRequested(trackId);
            });
        } else {
            auto *addFav = menu.addAction(QIcon(":/res/icons/starred-symbolic.svg"), tr("Add to favorites"));
            connect(addFav, &QAction::triggered, this, [this, trackId] {
                emit favTrackRequested(trackId);
            });
        }

        if (!albumId.isEmpty() || artistId > 0)
            menu.addSeparator();
        if (!albumId.isEmpty()) {
            auto *openAlbum = menu.addAction(
                QIcon(":/res/icons/view-media-album-cover.svg"),
                tr("Open album: %1").arg(QString(albumTitle).replace(QLatin1Char('&'), QStringLiteral("&&"))));
            connect(openAlbum, &QAction::triggered, this, [this, albumId] {
                emit albumRequested(albumId);
            });
        }
        if (artistId > 0) {
            auto *openArtist = menu.addAction(
                QIcon(":/res/icons/view-media-artist.svg"),
                tr("Open artist: %1").arg(QString(artistName).replace(QLatin1Char('&'), QStringLiteral("&&"))));
            connect(openArtist, &QAction::triggered, this, [this, artistId] {
                emit artistRequested(artistId);
            });
        }

        // Album queue actions
        if (!albumId.isEmpty()) {
            menu.addSeparator();
            auto *albumNext = menu.addAction(
                QIcon(":/res/icons/media-skip-forward.svg"), tr("Play album next"));
            connect(albumNext, &QAction::triggered, this, [this, albumId] {
                m_albumQueueHelper->request(albumId, AlbumQueueHelper::PlayNext);
            });
            auto *albumQueue = menu.addAction(
                QIcon(":/res/icons/media-playlist-append.svg"), tr("Add album to queue"));
            connect(albumQueue, &QAction::triggered, this, [this, albumId] {
                m_albumQueueHelper->request(albumId, AlbumQueueHelper::AddToQueue);
            });
        }

        if (!m_userPlaylists.isEmpty()) {
            menu.addSeparator();
            auto *plMenu = menu.addMenu(QIcon(":/res/icons/media-playlist-append.svg"), tr("Add to playlist"));
            for (const auto &pl : m_userPlaylists) {
                auto *act = plMenu->addAction(QString(pl.second).replace(QLatin1Char('&'), QStringLiteral("&&")));
                connect(act, &QAction::triggered, this, [this, trackId, plId = pl.first] {
                    emit addToPlaylistRequested(trackId, plId);
                });
            }
        }

        // Track info
        menu.addSeparator();
        auto *infoAction = menu.addAction(tr("Track info..."));

        connect(playNext, &QAction::triggered, this, [this] {
            m_queue->playNext(m_currentTrack);
        });
        connect(addQueue, &QAction::triggered, this, [this] {
            m_queue->addToQueue(m_currentTrack);
        });
        connect(infoAction, &QAction::triggered, this, [this] {
            TrackInfoDialog::show(m_currentTrack, this);
        });

        menu.exec(m_trackLabel->mapToGlobal(pos));
    });

    addSeparator();

    // ---- Media controls ----
    m_previous = addAction(Icon::previous(), tr("Previous"));
    connect(m_previous, &QAction::triggered, this, &MainToolBar::onPrevious);

    m_playPause = addAction(Icon::play(), tr("Play"));
    connect(m_playPause, &QAction::triggered, this, &MainToolBar::onPlayPause);

    m_next = addAction(Icon::next(), tr("Next"));
    connect(m_next, &QAction::triggered, this, &MainToolBar::onNext);

    // ---- Left spacer (pushes progress toward center) ----
    m_leftSpacer = new QWidget(this);
    m_leftSpacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    addWidget(m_leftSpacer);

    // ---- Progress slider ----
    m_progress = new ClickableSlider(Qt::Horizontal, this);
    m_progress->setRange(0, 1000);
    m_progress->setValue(0);
    m_progress->setMinimumWidth(200);
    m_progress->setMaximumWidth(500);
    addWidget(m_progress);
    connect(m_progress, &QSlider::sliderPressed,  this, [this] { m_seeking = true; });
    connect(m_progress, &QSlider::sliderReleased, this, &MainToolBar::onProgressReleased);

    // ---- Position label ----
    m_position = new QLabel(QStringLiteral("0:00 / 0:00"), this);
    m_position->setAlignment(Qt::AlignVCenter | Qt::AlignLeft);
    m_position->setMinimumWidth(80);
    addWidget(m_position);

    // ---- Right spacer (mirrors left spacer) ----
    m_rightSpacer = new QWidget(this);
    m_rightSpacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    addWidget(m_rightSpacer);

    // ---- Shuffle ----
    m_shuffle = addAction(Icon::get(QStringLiteral("media-playlist-shuffle")), tr("Shuffle"));
    m_shuffle->setCheckable(true);
    connect(m_shuffle, &QAction::toggled, this, &MainToolBar::onShuffleToggled);

    m_autoplay = addAction(Icon::autoplay(), tr("Autoplay"));
    m_autoplay->setCheckable(true);
    m_autoplay->setChecked(AppSettings::instance().autoplayEnabled());
    connect(m_autoplay, &QAction::toggled, this, &MainToolBar::onAutoplayToggled);

    // ---- Volume ----
    m_volume = new VolumeButton(this);
    addWidget(m_volume);
    connect(m_volume, &VolumeButton::volumeChanged, this, &MainToolBar::onVolumeChanged);
    // Set volume after connecting so the backend receives the initial value
    m_volume->setValue(AppSettings::instance().volume());
    m_backend->setVolume(AppSettings::instance().volume());

    // ---- Queue toggle ----
    m_queueBtn = addAction(Icon::queue(), tr("Queue"));
    m_queueBtn->setCheckable(true);
    connect(m_queueBtn, &QAction::toggled, this, &MainToolBar::queueToggled);

    // ---- Search toggle ----
    m_search = addAction(Icon::search(), tr("Search"));
    m_search->setCheckable(true);
    connect(m_search, &QAction::toggled, this, &MainToolBar::searchToggled);

    // ---- Backend signals ----
    connect(m_backend, &QobuzBackend::stateChanged,    this, &MainToolBar::onBackendStateChanged);
    connect(m_backend, &QobuzBackend::trackChanged,    this, &MainToolBar::onTrackChanged);
    connect(m_backend, &QobuzBackend::positionChanged, this, &MainToolBar::onPositionChanged);
    connect(m_backend, &QobuzBackend::trackFinished,   this, &MainToolBar::onTrackFinished);
    connect(m_backend, &QobuzBackend::trackTransitioned, this, &MainToolBar::onTrackTransitioned);
    connect(m_backend, &QobuzBackend::dynamicSuggestionsLoaded,
            this, &MainToolBar::onDynamicSuggestionsLoaded);

    // ---- Queue signals ----
    connect(m_queue, &PlayQueue::queueChanged, this, &MainToolBar::onQueueChanged);
    onQueueChanged();
}

// ---- resize: keep spacers equal so progress stays centred ----

void MainToolBar::resizeEvent(QResizeEvent *event)
{
    QToolBar::resizeEvent(event);
    const int spacerWidth = event->size().width() / 6;
    m_leftSpacer->setMinimumWidth(spacerWidth);
    m_rightSpacer->setMinimumWidth(spacerWidth);
}

bool MainToolBar::eventFilter(QObject *obj, QEvent *event)
{
    if (obj == m_artLabel && event->type() == QEvent::MouseButtonRelease) {
        auto *me = static_cast<QMouseEvent *>(event);
        if (me->button() == Qt::LeftButton && !m_currentTrack.isEmpty()) {
            const QString albumId = m_currentTrack["album"].toObject()["id"].toString();
            if (!albumId.isEmpty())
                emit albumRequested(albumId);
        }
        return true;
    }
    return QToolBar::eventFilter(obj, event);
}

// ---- public ----

void MainToolBar::setPlaying(bool playing)
{
    m_playing = playing;
    m_playPause->setIcon(playing ? Icon::pause() : Icon::play());
    m_playPause->setText(playing ? tr("Pause") : tr("Play"));
}

void MainToolBar::setCurrentTrack(const QJsonObject &track)
{
    m_currentTrack = track;
    const QString title  = track["title"].toString();
    const QString artist = track["performer"].toObject()["name"].toString().isEmpty()
        ? track["album"].toObject()["artist"].toObject()["name"].toString()
        : track["performer"].toObject()["name"].toString();

    if (title.isEmpty()) {
        m_trackLabel->setText(tr("Not playing"));
        m_trackLabel->setToolTip(QString());
    } else if (artist.isEmpty()) {
        m_trackLabel->setText(title.toHtmlEscaped());
        m_trackLabel->setToolTip(title);
    } else {
        m_trackLabel->setText(QStringLiteral("<span style='font-weight:600;'>%1</span>"
            "<br><span style='font-size:small; color:#aaa;'>%2</span>")
            .arg(title.toHtmlEscaped(), artist.toHtmlEscaped()));
        m_trackLabel->setToolTip(QStringLiteral("%1\n%2").arg(title, artist));
    }

    const QString artUrl = track["album"].toObject()["image"].toObject()["small"].toString();
    if (!artUrl.isEmpty() && artUrl != m_currentArtUrl) {
        m_currentArtUrl = artUrl;
        fetchAlbumArt(artUrl);
    }
}

void MainToolBar::updateProgress(quint64 position, quint64 duration)
{
    if (m_seeking) return;
    const int sliderPos = (duration > 0)
        ? static_cast<int>(qMin(position * 1000 / duration, quint64(1000))) : 0;
    m_progress->blockSignals(true);
    m_progress->setValue(sliderPos);
    m_progress->blockSignals(false);
    m_position->setText(
        QStringLiteral("%1 / %2")
            .arg(TrackListModel::formatDuration(static_cast<qint64>(position)),
                 TrackListModel::formatDuration(static_cast<qint64>(duration))));
}

void MainToolBar::setQueueToggleChecked(bool checked)
{
    const QSignalBlocker blocker(m_queueBtn);
    m_queueBtn->setChecked(checked);
}

void MainToolBar::setSearchToggleChecked(bool checked)
{
    const QSignalBlocker blocker(m_search);
    m_search->setChecked(checked);
}

// ---- private slots ----

void MainToolBar::onPlayPause()
{
    if (m_playing) m_backend->pause();
    else           m_backend->resume();
}

void MainToolBar::onPrevious()
{
    if (!m_queue->canGoPrev()) return;
    const QJsonObject track = m_queue->stepBack();
    const qint64 id = static_cast<qint64>(track["id"].toDouble());
    if (id > 0)
        m_backend->playTrack(id, AppSettings::instance().preferredFormat());
}

void MainToolBar::onNext()
{
    if (!m_queue->canGoNext()) return;
    const QJsonObject track = m_queue->advance();
    const qint64 id = static_cast<qint64>(track["id"].toDouble());
    if (id > 0)
        m_backend->playTrack(id, AppSettings::instance().preferredFormat());
}

void MainToolBar::onProgressReleased()
{
    m_seeking = false;
    const quint64 dur = m_backend->duration();
    if (dur > 0) {
        const quint64 target = dur * static_cast<quint64>(m_progress->value()) / 1000;
        m_seekPending = true;
        m_pendingSeekTarget = target;
        m_pendingSeekStartedMs = QDateTime::currentMSecsSinceEpoch();
        updateProgress(target, dur);
        m_backend->seek(target);
    }
}

void MainToolBar::onVolumeChanged(int volume)
{
    m_backend->setVolume(volume);
    AppSettings::instance().setVolume(volume);
}

void MainToolBar::onBackendStateChanged(const QString &state)
{
    setPlaying(state == QStringLiteral("playing"));
}

void MainToolBar::onTrackChanged(const QJsonObject &track)
{
    m_prefetchedTrackId = 0;
    m_seekPending = false;
    m_seeking = false;
    setCurrentTrack(track);

    const qint64 trackId = static_cast<qint64>(track["id"].toDouble());
    if (trackId <= 0)
        return;

    const qint64 artistId = static_cast<qint64>(
        track["performer"].toObject()["id"].toDouble());
    const qint64 genreId = static_cast<qint64>(
        track["album"].toObject()["genre"].toObject()["id"].toDouble());
    const qint64 labelId = static_cast<qint64>(
        track["album"].toObject()["label"].toObject()["id"].toDouble());

    m_recentTracks.append(RecentTrackSeed{trackId, artistId, genreId, labelId});
    while (m_recentTracks.size() > 32)
        m_recentTracks.removeFirst();
}

void MainToolBar::onPositionChanged(quint64 position, quint64 duration)
{
    // Gapless prefetch: buffer the next track in the backend before the current one ends
    if (AppSettings::instance().gaplessEnabled() && duration > 0) {
        if ((position > duration / 2) || (duration > 60 && (duration - position) <= 60)) {
            if (m_prefetchedTrackId == 0 && m_queue->canGoNext()) {
                const auto upcoming = m_queue->upcomingTracks(1);
                if (!upcoming.isEmpty()) {
                    const qint64 nextId = static_cast<qint64>(upcoming.first()["id"].toDouble());
                    if (nextId > 0) {
                        m_prefetchedTrackId = nextId;
                        m_backend->prefetchTrack(nextId, AppSettings::instance().preferredFormat());
                    }
                }
            }
        }
    }

    if (m_seekPending) {
        const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
        const quint64 delta = (position > m_pendingSeekTarget)
            ? (position - m_pendingSeekTarget)
            : (m_pendingSeekTarget - position);

        if (delta > 2 && (nowMs - m_pendingSeekStartedMs) < 1500)
            return;

        m_seekPending = false;
    }

    updateProgress(position, duration);
}

void MainToolBar::onTrackFinished()
{
    if (m_queue->canGoNext()) {
        onNext();
    } else if (AppSettings::instance().autoplayEnabled()) {
        requestAutoplaySuggestions();
    } else {
        setPlaying(false);
        m_progress->setValue(0);
        m_position->setText(QStringLiteral("0:00 / 0:00"));
    }
}

void MainToolBar::onTrackTransitioned()
{
    if (m_queue->canGoNext()) {
        const QJsonObject track = m_queue->advance();
        const qint64 id = static_cast<qint64>(track["id"].toDouble());
        setCurrentTrack(track);
        m_backend->manuallyEmitTrackChanged(track);
        // If play-next was added after the gapless prefetch, the backend has a
        // different track buffered. Override it with an explicit playTrack call.
        if (m_prefetchedTrackId != 0 && id != m_prefetchedTrackId && id > 0)
            m_backend->playTrack(id, AppSettings::instance().preferredFormat());
        m_prefetchedTrackId = 0;
    } else {
        m_prefetchedTrackId = 0;
        onTrackFinished();
    }
}

void MainToolBar::onQueueChanged()
{
    m_previous->setEnabled(m_queue->canGoPrev());
    m_next->setEnabled(m_queue->canGoNext());
}

void MainToolBar::onShuffleToggled(bool checked)
{
    m_queue->setShuffle(checked);
}

void MainToolBar::onAutoplayToggled(bool checked)
{
    AppSettings::instance().setAutoplayEnabled(checked);
}

void MainToolBar::requestAutoplaySuggestions()
{
    if (m_fetchingAutoplay)
        return;

    QJsonArray listenedIds;
    QJsonArray analyze;

    const int n = m_recentTracks.size();
    for (int i = 0; i < n; ++i) {
        const RecentTrackSeed &t = m_recentTracks.at(i);

        listenedIds.append(t.trackId);

        if (i < qMax(0, n - 5))
            continue;

        analyze.append(QJsonObject{
            {"track_id", t.trackId},
            {"artist_id", t.artistId},
            {"genre_id", t.genreId},
            {"label_id", t.labelId},
        });
    }

    if (listenedIds.isEmpty() || analyze.isEmpty()) {
        setPlaying(false);
        m_progress->setValue(0);
        m_position->setText(QStringLiteral("0:00 / 0:00"));
        return;
    }

    m_fetchingAutoplay = true;
    m_backend->getDynamicSuggestions(listenedIds, analyze, 50);
}

void MainToolBar::onDynamicSuggestionsLoaded(const QJsonObject &result)
{
    m_fetchingAutoplay = false;

    QJsonArray items;
    if (result["tracks"].isObject())
        items = result["tracks"].toObject()["items"].toArray();
    if (items.isEmpty() && result["dynamic"].isObject())
        items = result["dynamic"].toObject()["items"].toArray();
    if (items.isEmpty())
        items = result["items"].toArray();

    if (items.isEmpty()) {
        setPlaying(false);
        m_progress->setValue(0);
        m_position->setText(QStringLiteral("0:00 / 0:00"));
        return;
    }

    m_queue->appendToContext(items);
    if (m_queue->canGoNext())
        onNext();
}

void MainToolBar::fetchAlbumArt(const QString &url)
{
    m_nam->get(QNetworkRequest(QUrl(url)));
}

void MainToolBar::onAlbumArtReady(QNetworkReply *reply)
{
    reply->deleteLater();
    if (reply->error() != QNetworkReply::NoError) return;
    QPixmap pix;
    if (pix.loadFromData(reply->readAll()))
        m_artLabel->setPixmap(pix);
}
