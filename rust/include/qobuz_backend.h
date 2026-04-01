#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Opaque handle
typedef struct QobuzBackendOpaque QobuzBackendOpaque;

// Event type constants (mirror lib.rs)
enum QobuzEvent {
    EV_LOGIN_OK       = 0,
    EV_LOGIN_ERR      = 1,
    EV_SEARCH_OK      = 2,
    EV_SEARCH_ERR     = 3,
    EV_ALBUM_OK       = 4,
    EV_ALBUM_ERR      = 5,
    EV_ARTIST_OK      = 6,
    EV_ARTIST_ERR     = 7,
    EV_PLAYLIST_OK    = 8,
    EV_PLAYLIST_ERR   = 9,
    EV_FAV_TRACKS_OK  = 10,
    EV_FAV_ALBUMS_OK  = 11,
    EV_FAV_ARTISTS_OK = 12,
    EV_PLAYLISTS_OK   = 13,
    EV_TRACK_CHANGED  = 14,
    EV_STATE_CHANGED  = 15,
    EV_POSITION       = 16,
    EV_TRACK_URL_OK   = 17,
    EV_TRACK_URL_ERR  = 18,
    EV_GENERIC_ERR      = 19,
    EV_PLAYLIST_CREATED      = 20,
    EV_PLAYLIST_DELETED      = 21,
    EV_PLAYLIST_TRACK_ADDED  = 22,
    EV_USER_OK               = 23,
    EV_ARTIST_RELEASES_OK    = 24,
    EV_DEEP_SHUFFLE_OK       = 25,
    EV_MOST_POPULAR_OK       = 26,
    EV_GENRES_OK             = 27,
    EV_FEATURED_ALBUMS_OK    = 28,
    EV_DYNAMIC_SUGGEST_OK    = 29,
    EV_FEATURED_PLAYLISTS_OK = 30,
    EV_DISCOVER_PLAYLISTS_OK = 31,
    EV_PLAYLIST_SEARCH_OK    = 32,
    EV_PLAYLIST_SUBSCRIBED   = 33,
    EV_PLAYLIST_UNSUBSCRIBED = 34,
};

// Callback signature
typedef void (*QobuzEventCallback)(void *userdata, int event_type, const char *json);

// Lifecycle
QobuzBackendOpaque *qobuz_backend_new(QobuzEventCallback cb, void *userdata);
void                qobuz_backend_free(QobuzBackendOpaque *backend);

// Auth
void qobuz_backend_login(QobuzBackendOpaque *backend, const char *email, const char *password);
void qobuz_backend_set_token(QobuzBackendOpaque *backend, const char *token);
void qobuz_backend_get_user(QobuzBackendOpaque *backend);

// Catalog
void qobuz_backend_search(QobuzBackendOpaque *backend, const char *query, uint32_t offset, uint32_t limit);
void qobuz_backend_most_popular_search(QobuzBackendOpaque *backend, const char *query, uint32_t limit);
void qobuz_backend_get_dynamic_suggestions(QobuzBackendOpaque *backend, const char *listened_track_ids_json, const char *tracks_to_analyze_json, uint32_t limit);
void qobuz_backend_get_album(QobuzBackendOpaque *backend, const char *album_id);
void qobuz_backend_get_artist(QobuzBackendOpaque *backend, int64_t artist_id);
void qobuz_backend_get_playlist(QobuzBackendOpaque *backend, int64_t playlist_id, uint32_t offset, uint32_t limit);
void qobuz_backend_get_playlist_all(QobuzBackendOpaque *backend, int64_t playlist_id);

// Favorites
void qobuz_backend_get_fav_tracks(QobuzBackendOpaque *backend, uint32_t offset, uint32_t limit);
void qobuz_backend_get_fav_albums(QobuzBackendOpaque *backend, uint32_t offset, uint32_t limit);
void qobuz_backend_get_fav_artists(QobuzBackendOpaque *backend, uint32_t offset, uint32_t limit);
void qobuz_backend_get_user_playlists(QobuzBackendOpaque *backend, uint32_t offset, uint32_t limit);

// Playback
void    qobuz_backend_play_track(QobuzBackendOpaque *backend, int64_t track_id, int32_t format_id);
void    qobuz_backend_pause(QobuzBackendOpaque *backend);
void    qobuz_backend_resume(QobuzBackendOpaque *backend);
void    qobuz_backend_stop(QobuzBackendOpaque *backend);
void    qobuz_backend_set_volume(QobuzBackendOpaque *backend, uint8_t volume);
void     qobuz_backend_seek(QobuzBackendOpaque *backend, uint64_t position_secs);
uint64_t qobuz_backend_get_position(const QobuzBackendOpaque *backend);
uint64_t qobuz_backend_get_duration(const QobuzBackendOpaque *backend);
uint8_t  qobuz_backend_get_volume(const QobuzBackendOpaque *backend);
int      qobuz_backend_get_state(const QobuzBackendOpaque *backend);
int      qobuz_backend_take_track_finished(QobuzBackendOpaque *backend);
int      qobuz_backend_take_track_transitioned(QobuzBackendOpaque *backend);

// ReplayGain / Gapless
void qobuz_backend_set_replaygain(QobuzBackendOpaque *backend, bool enabled);
void qobuz_backend_set_gapless(QobuzBackendOpaque *backend, bool enabled);
void qobuz_backend_prefetch_track(QobuzBackendOpaque *backend, int64_t track_id, int32_t format_id);

// Artist releases (auto-paginates to fetch all)
void qobuz_backend_get_artist_releases(QobuzBackendOpaque *backend, int64_t artist_id, const char *release_type, uint32_t limit, uint32_t offset);

// Deep shuffle: fetch tracks from multiple albums (album_ids_json is a JSON array of strings)
void qobuz_backend_get_albums_tracks(QobuzBackendOpaque *backend, const char *album_ids_json);

// Browse
void qobuz_backend_get_genres(QobuzBackendOpaque *backend);
void qobuz_backend_get_featured_albums(QobuzBackendOpaque *backend, const char *genre_ids, const char *kind, uint32_t limit, uint32_t offset);
void qobuz_backend_get_featured_playlists(QobuzBackendOpaque *backend, const char *genre_ids, const char *kind, uint32_t limit, uint32_t offset);
void qobuz_backend_discover_playlists(QobuzBackendOpaque *backend, const char *genre_ids, const char *tags, uint32_t limit, uint32_t offset);
void qobuz_backend_search_playlists(QobuzBackendOpaque *backend, const char *query, uint32_t limit, uint32_t offset);

// Playlist management
void qobuz_backend_create_playlist(QobuzBackendOpaque *backend, const char *name);
void qobuz_backend_delete_playlist(QobuzBackendOpaque *backend, int64_t playlist_id);
void qobuz_backend_add_track_to_playlist(QobuzBackendOpaque *backend, int64_t playlist_id, int64_t track_id);
void qobuz_backend_delete_track_from_playlist(QobuzBackendOpaque *backend, int64_t playlist_id, int64_t playlist_track_id);
void qobuz_backend_subscribe_playlist(QobuzBackendOpaque *backend, int64_t playlist_id);
void qobuz_backend_unsubscribe_playlist(QobuzBackendOpaque *backend, int64_t playlist_id);

// Favorites modification
void qobuz_backend_add_fav_track(QobuzBackendOpaque *backend, int64_t track_id);
void qobuz_backend_remove_fav_track(QobuzBackendOpaque *backend, int64_t track_id);
void qobuz_backend_add_fav_album(QobuzBackendOpaque *backend, const char *album_id);
void qobuz_backend_remove_fav_album(QobuzBackendOpaque *backend, const char *album_id);
void qobuz_backend_add_fav_artist(QobuzBackendOpaque *backend, int64_t artist_id);
void qobuz_backend_remove_fav_artist(QobuzBackendOpaque *backend, int64_t artist_id);

#ifdef __cplusplus
}
#endif
