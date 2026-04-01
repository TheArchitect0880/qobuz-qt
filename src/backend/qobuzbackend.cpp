#include "qobuzbackend.hpp"

#include <QJsonDocument>
#include <QJsonArray>
#include <QMetaObject>

QobuzBackend::QobuzBackend(QObject *parent)
    : QObject(parent)
{
    m_backend = qobuz_backend_new(&QobuzBackend::eventTrampoline, this);
    if (!m_backend) {
        qCritical("Failed to initialize Qobuz backend");
        return;
    }

    m_positionTimer = new QTimer(this);
    m_positionTimer->setInterval(50);
    connect(m_positionTimer, &QTimer::timeout, this, &QobuzBackend::onPositionTick);
    m_positionTimer->start();
}

QobuzBackend::~QobuzBackend()
{
    if (m_backend) {
        qobuz_backend_free(m_backend);
        m_backend = nullptr;
    }
}

// ---- auth ----

void QobuzBackend::login(const QString &email, const QString &password)
{
    qobuz_backend_login(m_backend, email.toUtf8().constData(), password.toUtf8().constData());
}

void QobuzBackend::setToken(const QString &token)
{
    qobuz_backend_set_token(m_backend, token.toUtf8().constData());
}

void QobuzBackend::getUser()
{
    qobuz_backend_get_user(m_backend);
}

// ---- catalog ----

void QobuzBackend::search(const QString &query, quint32 offset, quint32 limit)
{
    qobuz_backend_search(m_backend, query.toUtf8().constData(), offset, limit);
}

void QobuzBackend::mostPopularSearch(const QString &query, quint32 limit)
{
    qobuz_backend_most_popular_search(m_backend, query.toUtf8().constData(), limit);
}

void QobuzBackend::getDynamicSuggestions(const QJsonArray &listenedTrackIds, const QJsonArray &tracksToAnalyze, quint32 limit)
{
    const QByteArray listened = QJsonDocument(listenedTrackIds).toJson(QJsonDocument::Compact);
    const QByteArray analyze = QJsonDocument(tracksToAnalyze).toJson(QJsonDocument::Compact);
    qobuz_backend_get_dynamic_suggestions(
        m_backend,
        listened.constData(),
        analyze.constData(),
        limit);
}

void QobuzBackend::getAlbum(const QString &albumId)
{
    qobuz_backend_get_album(m_backend, albumId.toUtf8().constData());
}

void QobuzBackend::getArtist(qint64 artistId)
{
    qobuz_backend_get_artist(m_backend, artistId);
}

void QobuzBackend::getArtistReleases(qint64 artistId, const QString &releaseType, quint32 limit, quint32 offset)
{
    qobuz_backend_get_artist_releases(m_backend, artistId,
                                      releaseType.toUtf8().constData(), limit, offset);
}

void QobuzBackend::getAlbumsTracks(const QStringList &albumIds)
{
    const QJsonArray arr = QJsonArray::fromStringList(albumIds);
    const QByteArray json = QJsonDocument(arr).toJson(QJsonDocument::Compact);
    qobuz_backend_get_albums_tracks(m_backend, json.constData());
}

void QobuzBackend::getPlaylist(qint64 playlistId, quint32 offset, quint32 limit)
{
    qobuz_backend_get_playlist(m_backend, playlistId, offset, limit);
}

void QobuzBackend::getPlaylistAll(qint64 playlistId)
{
    qobuz_backend_get_playlist_all(m_backend, playlistId);
}

void QobuzBackend::getGenres()
{
    qobuz_backend_get_genres(m_backend);
}

void QobuzBackend::getFeaturedAlbums(const QString &genreIds, const QString &kind, quint32 limit, quint32 offset)
{
    qobuz_backend_get_featured_albums(m_backend, genreIds.toUtf8().constData(), kind.toUtf8().constData(), limit, offset);
}

void QobuzBackend::getFeaturedPlaylists(const QString &genreIds, const QString &kind, quint32 limit, quint32 offset)
{
    qobuz_backend_get_featured_playlists(m_backend, genreIds.toUtf8().constData(), kind.toUtf8().constData(), limit, offset);
}

void QobuzBackend::discoverPlaylists(const QString &genreIds, const QString &tags, quint32 limit, quint32 offset)
{
    qobuz_backend_discover_playlists(
        m_backend,
        genreIds.toUtf8().constData(),
        tags.toUtf8().constData(),
        limit,
        offset);
}

void QobuzBackend::searchPlaylists(const QString &query, quint32 limit, quint32 offset)
{
    qobuz_backend_search_playlists(m_backend, query.toUtf8().constData(), limit, offset);
}

// ---- favorites ----

void QobuzBackend::getFavTracks(quint32 offset, quint32 limit)
{
    qobuz_backend_get_fav_tracks(m_backend, offset, limit);
}

void QobuzBackend::getFavAlbums(quint32 offset, quint32 limit)
{
    qobuz_backend_get_fav_albums(m_backend, offset, limit);
}

void QobuzBackend::getFavArtists(quint32 offset, quint32 limit)
{
    qobuz_backend_get_fav_artists(m_backend, offset, limit);
}

void QobuzBackend::getUserPlaylists(quint32 offset, quint32 limit)
{
    qobuz_backend_get_user_playlists(m_backend, offset, limit);
}

// ---- playback options ----

void QobuzBackend::setReplayGain(bool enabled)
{
    qobuz_backend_set_replaygain(m_backend, enabled);
}

void QobuzBackend::setGapless(bool enabled)
{
    qobuz_backend_set_gapless(m_backend, enabled);
}

void QobuzBackend::prefetchTrack(qint64 trackId, int formatId)
{
    qobuz_backend_prefetch_track(m_backend, trackId, formatId);
}

// ---- playlist management ----

void QobuzBackend::createPlaylist(const QString &name)
{
    qobuz_backend_create_playlist(m_backend, name.toUtf8().constData());
}

void QobuzBackend::deletePlaylist(qint64 playlistId)
{
    qobuz_backend_delete_playlist(m_backend, playlistId);
}

void QobuzBackend::addTrackToPlaylist(qint64 playlistId, qint64 trackId)
{
    qobuz_backend_add_track_to_playlist(m_backend, playlistId, trackId);
}

void QobuzBackend::deleteTrackFromPlaylist(qint64 playlistId, qint64 playlistTrackId)
{
    qobuz_backend_delete_track_from_playlist(m_backend, playlistId, playlistTrackId);
}

void QobuzBackend::subscribePlaylist(qint64 playlistId)
{
    qobuz_backend_subscribe_playlist(m_backend, playlistId);
}

void QobuzBackend::unsubscribePlaylist(qint64 playlistId)
{
    qobuz_backend_unsubscribe_playlist(m_backend, playlistId);
}

// ---- fav modification ----

void QobuzBackend::addFavTrack(qint64 trackId)
{
    qobuz_backend_add_fav_track(m_backend, trackId);
}

void QobuzBackend::removeFavTrack(qint64 trackId)
{
    qobuz_backend_remove_fav_track(m_backend, trackId);
}

void QobuzBackend::addFavAlbum(const QString &albumId)
{
    qobuz_backend_add_fav_album(m_backend, albumId.toUtf8().constData());
}

void QobuzBackend::removeFavAlbum(const QString &albumId)
{
    qobuz_backend_remove_fav_album(m_backend, albumId.toUtf8().constData());
}

void QobuzBackend::addFavArtist(qint64 artistId)
{
    qobuz_backend_add_fav_artist(m_backend, artistId);
}

void QobuzBackend::removeFavArtist(qint64 artistId)
{
    qobuz_backend_remove_fav_artist(m_backend, artistId);
}

// ---- playback ----

void QobuzBackend::playTrack(qint64 trackId, int formatId)
{
    qobuz_backend_play_track(m_backend, trackId, formatId);
}

void QobuzBackend::pause()
{
    qobuz_backend_pause(m_backend);
}

void QobuzBackend::resume()
{
    qobuz_backend_resume(m_backend);
}

void QobuzBackend::stop()
{
    qobuz_backend_stop(m_backend);
}

void QobuzBackend::setVolume(int volume)
{
    qobuz_backend_set_volume(m_backend, static_cast<quint8>(qBound(0, volume, 100)));
}

void QobuzBackend::seek(quint64 positionSecs)
{
    qobuz_backend_seek(m_backend, positionSecs);
}

quint64 QobuzBackend::position() const { return qobuz_backend_get_position(m_backend); }
quint64 QobuzBackend::duration() const { return qobuz_backend_get_duration(m_backend); }
int     QobuzBackend::volume()   const { return qobuz_backend_get_volume(m_backend); }
int     QobuzBackend::state()    const { return qobuz_backend_get_state(m_backend); }

// ---- private slots ----

void QobuzBackend::onPositionTick()
{
    emit positionChanged(position(), duration());

    if (qobuz_backend_take_track_finished(m_backend))
        emit trackFinished();
        
    if (qobuz_backend_take_track_transitioned(m_backend))
        emit trackTransitioned();
}

void QobuzBackend::onEvent(int eventType, const QString &json)
{
    const QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8());
    if (!doc.isObject()) {
        emit error(tr("Malformed response from backend"));
        return;
    }
    const QJsonObject obj = doc.object();

    switch (eventType) {
    case EV_LOGIN_OK:
        emit loginSuccess(obj["token"].toString(), obj["user"].toObject());
        break;
    case EV_LOGIN_ERR:
        emit loginError(obj["error"].toString());
        break;
    case EV_SEARCH_OK:
        emit searchResult(obj);
        break;
    case EV_MOST_POPULAR_OK:
        emit mostPopularResult(obj);
        break;
    case EV_SEARCH_ERR:
        emit error(obj["error"].toString());
        break;
    case EV_ALBUM_OK:
        emit albumLoaded(obj);
        break;
    case EV_ALBUM_ERR:
        emit error(obj["error"].toString());
        break;
    case EV_ARTIST_OK:
        emit artistLoaded(obj);
        break;
    case EV_ARTIST_RELEASES_OK:
        emit artistReleasesLoaded(
            obj["release_type"].toString(),
            obj["items"].toArray(),
            obj["has_more"].toBool(),
            obj["offset"].toInt()
        );
        break;
    case EV_DEEP_SHUFFLE_OK:
        emit deepShuffleTracksLoaded(obj["tracks"].toArray());
        break;
    case EV_DYNAMIC_SUGGEST_OK:
        emit dynamicSuggestionsLoaded(obj);
        break;
    case EV_GENRES_OK:
        emit genresLoaded(obj);
        break;
    case EV_FEATURED_ALBUMS_OK:
        emit featuredAlbumsLoaded(obj);
        break;
    case EV_FEATURED_PLAYLISTS_OK:
        emit featuredPlaylistsLoaded(obj);
        break;
    case EV_DISCOVER_PLAYLISTS_OK:
        emit discoverPlaylistsLoaded(obj);
        break;
    case EV_PLAYLIST_SEARCH_OK:
        emit playlistSearchLoaded(obj);
        break;
    case EV_ARTIST_ERR:
        emit error(obj["error"].toString());
        break;
    case EV_PLAYLIST_OK:
        emit playlistLoaded(obj);
        break;
    case EV_PLAYLIST_ERR:
        emit error(obj["error"].toString());
        break;
    case EV_FAV_TRACKS_OK:
        emit favTracksLoaded(obj);
        break;
    case EV_FAV_ALBUMS_OK:
        emit favAlbumsLoaded(obj);
        break;
    case EV_FAV_ARTISTS_OK:
        emit favArtistsLoaded(obj);
        break;
    case EV_PLAYLISTS_OK:
        emit userPlaylistsLoaded(obj);
        break;
    case EV_TRACK_CHANGED:
        emit trackChanged(obj);
        break;
    case EV_STATE_CHANGED:
        emit stateChanged(obj["state"].toString());
        break;
    case EV_PLAYLIST_CREATED:
        emit playlistCreated(obj);
        break;
    case EV_PLAYLIST_DELETED:
        emit playlistDeleted(obj);
        break;
    case EV_PLAYLIST_TRACK_ADDED:
        emit playlistTrackAdded(static_cast<qint64>(obj["playlist_id"].toDouble()));
        break;
    case EV_PLAYLIST_SUBSCRIBED:
        emit playlistSubscribed(static_cast<qint64>(obj["playlist_id"].toDouble()));
        break;
    case EV_PLAYLIST_UNSUBSCRIBED:
        emit playlistUnsubscribed(static_cast<qint64>(obj["playlist_id"].toDouble()));
        break;
    case EV_USER_OK:
        emit userLoaded(obj);
        break;
    case EV_GENERIC_ERR:
    case EV_TRACK_URL_ERR:
        emit error(obj["error"].toString());
        break;
    default:
        break;
    }
}

// ---- static trampoline ----

void QobuzBackend::eventTrampoline(void *userdata, int eventType, const char *json)
{
    auto *self = static_cast<QobuzBackend *>(userdata);
    // Marshal from Rust thread → Qt main thread
    QMetaObject::invokeMethod(
        self,
        "onEvent",
        Qt::QueuedConnection,
        Q_ARG(int, eventType),
        Q_ARG(QString, QString::fromUtf8(json))
    );
}
