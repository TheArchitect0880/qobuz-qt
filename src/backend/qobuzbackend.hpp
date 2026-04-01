#pragma once

#include "qobuz_backend.h"

#include <QObject>
#include <QString>
#include <QJsonArray>
#include <QJsonObject>
#include <QTimer>

/// Qt wrapper around the Rust qobuz-backend static library.
///
/// All signals are emitted on the Qt main thread regardless of which thread
/// the Rust callback fires on (marshalled via QMetaObject::invokeMethod with
/// Qt::QueuedConnection).
class QobuzBackend : public QObject
{
    Q_OBJECT

public:
    explicit QobuzBackend(QObject *parent = nullptr);
    ~QobuzBackend() override;

    // --- auth ---
    void login(const QString &email, const QString &password);
    void setToken(const QString &token);
    void getUser();

    // --- catalog ---
    void search(const QString &query, quint32 offset = 0, quint32 limit = 20);
    void mostPopularSearch(const QString &query, quint32 limit = 8);
    void getDynamicSuggestions(const QJsonArray &listenedTrackIds, const QJsonArray &tracksToAnalyze, quint32 limit = 50);
    void getAlbum(const QString &albumId);
    void getArtist(qint64 artistId);
    void getArtistReleases(qint64 artistId, const QString &releaseType, quint32 limit = 50, quint32 offset = 0);
    void getAlbumsTracks(const QStringList &albumIds);
    void getPlaylist(qint64 playlistId, quint32 offset = 0, quint32 limit = 500);
    void getPlaylistAll(qint64 playlistId);
    void getGenres();
    void getFeaturedAlbums(const QString &genreIds, const QString &kind, quint32 limit = 50, quint32 offset = 0);
    void getFeaturedPlaylists(const QString &genreIds, const QString &kind, quint32 limit = 25, quint32 offset = 0);
    void discoverPlaylists(const QString &genreIds, const QString &tags = QString(), quint32 limit = 25, quint32 offset = 0);
    void searchPlaylists(const QString &query, quint32 limit = 8, quint32 offset = 0);

    // --- favorites ---
    void getFavTracks(quint32 offset = 0, quint32 limit = 500);
    void getFavAlbums(quint32 offset = 0, quint32 limit = 200);
    void getFavArtists(quint32 offset = 0, quint32 limit = 200);
    void getUserPlaylists(quint32 offset = 0, quint32 limit = 350);

    // --- playback options ---
    void setReplayGain(bool enabled);
    void setGapless(bool enabled);
    void prefetchTrack(qint64 trackId, int formatId = 6);

    // --- playlist management ---
    void createPlaylist(const QString &name);
    void deletePlaylist(qint64 playlistId);
    void addTrackToPlaylist(qint64 playlistId, qint64 trackId);
    void deleteTrackFromPlaylist(qint64 playlistId, qint64 playlistTrackId);
    void subscribePlaylist(qint64 playlistId);
    void unsubscribePlaylist(qint64 playlistId);

    // --- fav modification ---
    void addFavTrack(qint64 trackId);
    void removeFavTrack(qint64 trackId);
    void addFavAlbum(const QString &albumId);
    void removeFavAlbum(const QString &albumId);
    void addFavArtist(qint64 artistId);
    void removeFavArtist(qint64 artistId);

    // --- playback ---
    void playTrack(qint64 trackId, int formatId = 6);
    void pause();
    void resume();
    void stop();
    void setVolume(int volume);
    void seek(quint64 positionSecs);

    quint64 position() const;
    quint64 duration() const;
    int     volume() const;
    /// 1 = playing, 2 = paused, 0 = idle
    int     state() const;

signals:
    // auth
    void loginSuccess(const QString &token, const QJsonObject &user);
    void loginError(const QString &error);
    void userLoaded(const QJsonObject &user);

    // catalog
    void searchResult(const QJsonObject &result);
    void mostPopularResult(const QJsonObject &result);
    void albumLoaded(const QJsonObject &album);
    void artistLoaded(const QJsonObject &artist);
    void artistReleasesLoaded(const QString &releaseType, const QJsonArray &items, bool hasMore, int offset);
    void deepShuffleTracksLoaded(const QJsonArray &tracks);
    void dynamicSuggestionsLoaded(const QJsonObject &result);
    void genresLoaded(const QJsonObject &result);
    void featuredAlbumsLoaded(const QJsonObject &result);
    void featuredPlaylistsLoaded(const QJsonObject &result);
    void discoverPlaylistsLoaded(const QJsonObject &result);
    void playlistSearchLoaded(const QJsonObject &result);
    void playlistLoaded(const QJsonObject &playlist);
    void playlistCreated(const QJsonObject &playlist);
    void playlistDeleted(const QJsonObject &result);
    void playlistTrackAdded(qint64 playlistId);
    void playlistSubscribed(qint64 playlistId);
    void playlistUnsubscribed(qint64 playlistId);

    // favorites
    void favTracksLoaded(const QJsonObject &result);
    void favAlbumsLoaded(const QJsonObject &result);
    void favArtistsLoaded(const QJsonObject &result);
    void userPlaylistsLoaded(const QJsonObject &result);

    // playback
    void trackChanged(const QJsonObject &track);
    void stateChanged(const QString &state);
    void positionChanged(quint64 position, quint64 duration);
    void trackFinished();
    void trackTransitioned();

    // errors
    void error(const QString &message);

private slots:
    Q_INVOKABLE void onEvent(int eventType, const QString &json);
    void onPositionTick();

public:
    void manuallyEmitTrackChanged(const QJsonObject &track) {
        emit trackChanged(track);
    }

private:
    QobuzBackendOpaque *m_backend = nullptr;
    QTimer *m_positionTimer = nullptr;

    // Static trampoline called from Rust threads
    static void eventTrampoline(void *userdata, int eventType, const char *json);
};
