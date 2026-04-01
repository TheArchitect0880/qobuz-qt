//! qobuz-backend: C-ABI library consumed by the Qt frontend.
#![allow(clippy::missing_safety_doc)]

mod api;
mod player;

use std::{
    ffi::{CStr, CString},
    os::raw::{c_char, c_int, c_void},
    sync::Arc,
};

use api::{Format, QobuzClient};
use player::{Player, PlayerState};
use tokio::runtime::Runtime;
use tokio::sync::Mutex;

// ---------- Send-safe raw pointer wrapper ----------

#[derive(Clone, Copy)]
struct SendPtr(*mut c_void);
unsafe impl Send for SendPtr {}
unsafe impl Sync for SendPtr {}

// ---------- Event type constants ----------

pub const EV_LOGIN_OK: c_int = 0;
pub const EV_LOGIN_ERR: c_int = 1;
pub const EV_SEARCH_OK: c_int = 2;
pub const EV_SEARCH_ERR: c_int = 3;
pub const EV_ALBUM_OK: c_int = 4;
pub const EV_ALBUM_ERR: c_int = 5;
pub const EV_ARTIST_OK: c_int = 6;
pub const EV_ARTIST_ERR: c_int = 7;
pub const EV_PLAYLIST_OK: c_int = 8;
pub const EV_PLAYLIST_ERR: c_int = 9;
pub const EV_FAV_TRACKS_OK: c_int = 10;
pub const EV_FAV_ALBUMS_OK: c_int = 11;
pub const EV_FAV_ARTISTS_OK: c_int = 12;
pub const EV_PLAYLISTS_OK: c_int = 13;
pub const EV_TRACK_CHANGED: c_int = 14;
pub const EV_STATE_CHANGED: c_int = 15;
pub const EV_POSITION: c_int = 16;
pub const EV_TRACK_URL_OK: c_int = 17;
pub const EV_TRACK_URL_ERR: c_int = 18;
pub const EV_GENERIC_ERR: c_int = 19;
pub const EV_ARTIST_RELEASES_OK: c_int = 24;
pub const EV_DEEP_SHUFFLE_OK: c_int = 25;
pub const EV_MOST_POPULAR_OK: c_int = 26;
pub const EV_GENRES_OK: c_int = 27;
pub const EV_FEATURED_ALBUMS_OK: c_int = 28;
pub const EV_DYNAMIC_SUGGEST_OK: c_int = 29;
pub const EV_FEATURED_PLAYLISTS_OK: c_int = 30;
pub const EV_DISCOVER_PLAYLISTS_OK: c_int = 31;
pub const EV_PLAYLIST_SEARCH_OK: c_int = 32;
pub const EV_PLAYLIST_SUBSCRIBED: c_int = 33;
pub const EV_PLAYLIST_UNSUBSCRIBED: c_int = 34;

// ---------- Callback ----------

pub type EventCallback = unsafe extern "C" fn(*mut c_void, c_int, *const c_char);

// ---------- Backend ----------

struct PrefetchedTrack {
    track_id: i64,
    track: api::models::TrackDto,
    url: String,
    n_segments: u32,
    encryption_key: Option<String>,
    prefetch_data: Option<player::decoder::PrefetchData>,
}

struct BackendInner {
    client: Arc<Mutex<QobuzClient>>,
    player: Player,
    rt: Runtime,
    cb: EventCallback,
    ud: SendPtr,
    replaygain_enabled: std::sync::Arc<std::sync::atomic::AtomicBool>,
    prefetch: std::sync::Arc<tokio::sync::Mutex<Option<PrefetchedTrack>>>,
}

pub struct Backend(BackendInner);

// ---------- Helpers ----------

fn call_cb(cb: EventCallback, ud: SendPtr, ev: c_int, json: &str) {
    let safe = json.replace('\0', "");
    let cstr = CString::new(safe).unwrap_or_else(|_| CString::new("{}").unwrap());
    unsafe { cb(ud.0, ev, cstr.as_ptr()) };
}

fn err_json(msg: &str) -> String {
    serde_json::json!({ "error": msg }).to_string()
}

fn spawn<F>(inner: &BackendInner, f: F)
where
    F: std::future::Future<Output = ()> + Send + 'static,
{
    inner.rt.spawn(f);
}

// ---------- Construction / destruction ----------

#[no_mangle]
pub unsafe extern "C" fn qobuz_backend_new(
    event_cb: EventCallback,
    userdata: *mut c_void,
) -> *mut Backend {
    let rt = match Runtime::new() {
        Ok(r) => r,
        Err(_) => return std::ptr::null_mut(),
    };
    let client = match QobuzClient::new() {
        Ok(c) => Arc::new(Mutex::new(c)),
        Err(_) => return std::ptr::null_mut(),
    };
    let player = Player::new();

    Box::into_raw(Box::new(Backend(BackendInner {
        client,
        player,
        rt,
        cb: event_cb,
        ud: SendPtr(userdata),
        replaygain_enabled: std::sync::Arc::new(std::sync::atomic::AtomicBool::new(false)),
        prefetch: std::sync::Arc::new(tokio::sync::Mutex::new(None)),
    })))
}

#[no_mangle]
pub unsafe extern "C" fn qobuz_backend_free(ptr: *mut Backend) {
    if !ptr.is_null() {
        drop(Box::from_raw(ptr));
    }
}

// ---------- Auth ----------

#[no_mangle]
pub unsafe extern "C" fn qobuz_backend_login(
    ptr: *mut Backend,
    email: *const c_char,
    password: *const c_char,
) {
    let inner = &(*ptr).0;
    let email = CStr::from_ptr(email).to_string_lossy().into_owned();
    let password = CStr::from_ptr(password).to_string_lossy().into_owned();
    let client = inner.client.clone();
    let cb = inner.cb;
    let ud = inner.ud;

    spawn(inner, async move {
        let result = client.lock().await.login(&email, &password).await;
        let (ev, json) = match result {
            Ok(resp) => {
                let token = resp
                    .oauth2
                    .as_ref()
                    .and_then(|o| o.access_token.as_deref())
                    .or(resp.user_auth_token.as_deref())
                    .unwrap_or("")
                    .to_string();
                let user_val = resp
                    .user
                    .as_ref()
                    .map(|u| serde_json::to_value(u).unwrap_or_default())
                    .unwrap_or_default();
                (
                    EV_LOGIN_OK,
                    serde_json::json!({"token": token, "user": user_val}).to_string(),
                )
            }
            Err(e) => (EV_LOGIN_ERR, err_json(&e.to_string())),
        };
        call_cb(cb, ud, ev, &json);
    });
}

#[no_mangle]
pub unsafe extern "C" fn qobuz_backend_set_token(ptr: *mut Backend, token: *const c_char) {
    let inner = &(*ptr).0;
    let token = CStr::from_ptr(token).to_string_lossy().into_owned();
    inner.client.blocking_lock().set_auth_token(token);
}

// ---------- Search ----------

#[no_mangle]
pub unsafe extern "C" fn qobuz_backend_search(
    ptr: *mut Backend,
    query: *const c_char,
    offset: u32,
    limit: u32,
) {
    let inner = &(*ptr).0;
    let query = CStr::from_ptr(query).to_string_lossy().into_owned();
    let client = inner.client.clone();
    let cb = inner.cb;
    let ud = inner.ud;

    spawn(inner, async move {
        let result = client.lock().await.search(&query, offset, limit).await;
        let (ev, json) = match result {
            Ok(r) => (EV_SEARCH_OK, serde_json::to_string(&r).unwrap_or_default()),
            Err(e) => (EV_SEARCH_ERR, err_json(&e.to_string())),
        };
        call_cb(cb, ud, ev, &json);
    });
}

#[no_mangle]
pub unsafe extern "C" fn qobuz_backend_most_popular_search(
    ptr: *mut Backend,
    query: *const c_char,
    limit: u32,
) {
    let inner = &(*ptr).0;
    let query = CStr::from_ptr(query).to_string_lossy().into_owned();
    let client = inner.client.clone();
    let cb = inner.cb;
    let ud = inner.ud;

    spawn(inner, async move {
        let result = client
            .lock()
            .await
            .get_most_popular(&query, 0, limit)
            .await;
        match result {
            Ok(r) => call_cb(
                cb,
                ud,
                EV_MOST_POPULAR_OK,
                &serde_json::to_string(&r).unwrap_or_default(),
            ),
            Err(e) => call_cb(cb, ud, EV_SEARCH_ERR, &err_json(&e.to_string())),
        }
    });
}

#[no_mangle]
pub unsafe extern "C" fn qobuz_backend_get_dynamic_suggestions(
    ptr: *mut Backend,
    listened_track_ids_json: *const c_char,
    tracks_to_analyze_json: *const c_char,
    limit: u32,
) {
    let inner = &(*ptr).0;
    let client = inner.client.clone();
    let cb = inner.cb;
    let ud = inner.ud;

    let listened_str = CStr::from_ptr(listened_track_ids_json)
        .to_string_lossy()
        .into_owned();
    let analyze_str = CStr::from_ptr(tracks_to_analyze_json)
        .to_string_lossy()
        .into_owned();

    let listened: serde_json::Value = match serde_json::from_str(&listened_str) {
        Ok(v) => v,
        Err(e) => {
            call_cb(cb, ud, EV_GENERIC_ERR, &err_json(&e.to_string()));
            return;
        }
    };

    let to_analyze: serde_json::Value = match serde_json::from_str(&analyze_str) {
        Ok(v) => v,
        Err(e) => {
            call_cb(cb, ud, EV_GENERIC_ERR, &err_json(&e.to_string()));
            return;
        }
    };

    spawn(inner, async move {
        let result = client
            .lock()
            .await
            .get_dynamic_suggestions(limit, listened, to_analyze)
            .await;
        match result {
            Ok(r) => call_cb(
                cb,
                ud,
                EV_DYNAMIC_SUGGEST_OK,
                &serde_json::to_string(&r).unwrap_or_default(),
            ),
            Err(e) => call_cb(cb, ud, EV_GENERIC_ERR, &err_json(&e.to_string())),
        }
    });
}

// ---------- Album ----------

#[no_mangle]
pub unsafe extern "C" fn qobuz_backend_get_album(ptr: *mut Backend, album_id: *const c_char) {
    let inner = &(*ptr).0;
    let album_id = CStr::from_ptr(album_id).to_string_lossy().into_owned();
    let client = inner.client.clone();
    let cb = inner.cb;
    let ud = inner.ud;

    spawn(inner, async move {
        let result = client.lock().await.get_album(&album_id).await;
        let (ev, json) = match result {
            Ok(r) => (EV_ALBUM_OK, serde_json::to_string(&r).unwrap_or_default()),
            Err(e) => (EV_ALBUM_ERR, err_json(&e.to_string())),
        };
        call_cb(cb, ud, ev, &json);
    });
}

// ---------- Artist ----------

#[no_mangle]
pub unsafe extern "C" fn qobuz_backend_get_artist(ptr: *mut Backend, artist_id: i64) {
    let inner = &(*ptr).0;
    let client = inner.client.clone();
    let cb = inner.cb;
    let ud = inner.ud;

    spawn(inner, async move {
        let result = client.lock().await.get_artist_page(artist_id).await;
        let (ev, json) = match result {
            Ok(r) => (EV_ARTIST_OK, serde_json::to_string(&r).unwrap_or_default()),
            Err(e) => (EV_ARTIST_ERR, err_json(&e.to_string())),
        };
        call_cb(cb, ud, ev, &json);
    });
}

// ---------- Artist releases ----------

#[no_mangle]
pub unsafe extern "C" fn qobuz_backend_get_artist_releases(
    ptr: *mut Backend,
    artist_id: i64,
    release_type: *const c_char,
    limit: u32,
    _offset: u32,
) {
    let inner = &(*ptr).0;
    let client = inner.client.clone();
    let cb = inner.cb;
    let ud = inner.ud;
    let rtype = CStr::from_ptr(release_type).to_string_lossy().into_owned();

    spawn(inner, async move {
        let mut all_items: Vec<serde_json::Value> = Vec::new();
        let mut offset: u32 = 0;
        loop {
            let result = client
                .lock()
                .await
                .get_artist_releases_list(artist_id, &rtype, limit, offset)
                .await;
            match result {
                Ok(r) => {
                    let obj = r.as_object().cloned().unwrap_or_default();
                    if let Some(items) = obj.get("items").and_then(|v| v.as_array()) {
                        all_items.extend(items.iter().cloned());
                    }
                    let has_more = obj
                        .get("has_more")
                        .and_then(|v| v.as_bool())
                        .unwrap_or(false);
                    if !has_more {
                        break;
                    }
                    offset += limit;
                }
                Err(e) => {
                    call_cb(cb, ud, EV_GENERIC_ERR, &err_json(&e.to_string()));
                    return;
                }
            }
        }
        let result = serde_json::json!({
            "release_type": rtype,
            "items": all_items,
            "has_more": false,
            "offset": 0
        });
        call_cb(
            cb,
            ud,
            EV_ARTIST_RELEASES_OK,
            &serde_json::to_string(&result).unwrap_or_default(),
        );
    });
}

// ---------- Deep shuffle (fetch tracks from multiple albums) ----------

#[no_mangle]
pub unsafe extern "C" fn qobuz_backend_get_albums_tracks(
    ptr: *mut Backend,
    album_ids_json: *const c_char,
) {
    let inner = &(*ptr).0;
    let client = inner.client.clone();
    let cb = inner.cb;
    let ud = inner.ud;
    let ids_str = CStr::from_ptr(album_ids_json)
        .to_string_lossy()
        .into_owned();

    let album_ids: Vec<String> = match serde_json::from_str(&ids_str) {
        Ok(v) => v,
        Err(e) => {
            call_cb(cb, ud, EV_GENERIC_ERR, &err_json(&e.to_string()));
            return;
        }
    };

    spawn(inner, async move {
        let mut all_tracks: Vec<serde_json::Value> = Vec::new();
        for id in &album_ids {
            let result = client.lock().await.get_album(id).await;
            if let Ok(album) = result {
                if let Some(tracks) = album.tracks.as_ref().and_then(|t| t.items.as_ref()) {
                    for t in tracks {
                        if let Ok(mut tv) = serde_json::to_value(t) {
                            if let Some(obj) = tv.as_object_mut() {
                                if obj.get("album").is_none() || obj["album"].is_null() {
                                    obj.insert(
                                        "album".to_string(),
                                        serde_json::json!({
                                            "id": album.id,
                                            "title": album.title,
                                            "artist": album.artist,
                                            "image": album.image,
                                        }),
                                    );
                                }
                            }
                            all_tracks.push(tv);
                        }
                    }
                }
            }
        }
        let result = serde_json::json!({ "tracks": all_tracks });
        call_cb(
            cb,
            ud,
            EV_DEEP_SHUFFLE_OK,
            &serde_json::to_string(&result).unwrap_or_default(),
        );
    });
}

// ---------- Playlist ----------

// ---------- Browse (genres / featured) ----------

#[no_mangle]
pub unsafe extern "C" fn qobuz_backend_get_genres(ptr: *mut Backend) {
    let inner = &(*ptr).0;
    let client = inner.client.clone();
    let cb = inner.cb;
    let ud = inner.ud;

    spawn(inner, async move {
        let result = client.lock().await.get_genres().await;
        match result {
            Ok(r) => {
                let items = r["genres"]["items"].clone();
                let total = r["genres"]["total"].as_i64().unwrap_or(0);
                let out = serde_json::json!({"items": items, "total": total});
                call_cb(
                    cb,
                    ud,
                    EV_GENRES_OK,
                    &serde_json::to_string(&out).unwrap_or_default(),
                );
            }
            Err(e) => call_cb(cb, ud, EV_GENERIC_ERR, &err_json(&e.to_string())),
        }
    });
}

#[no_mangle]
pub unsafe extern "C" fn qobuz_backend_get_featured_albums(
    ptr: *mut Backend,
    genre_ids: *const c_char,
    kind: *const c_char,
    limit: u32,
    offset: u32,
) {
    let inner = &(*ptr).0;
    let client = inner.client.clone();
    let cb = inner.cb;
    let ud = inner.ud;
    let genre_ids_str = CStr::from_ptr(genre_ids).to_string_lossy().into_owned();
    let kind_str = CStr::from_ptr(kind).to_string_lossy().into_owned();

    spawn(inner, async move {
        let result = client
            .lock()
            .await
            .get_featured_albums(&genre_ids_str, &kind_str, limit, offset)
            .await;
        match result {
            Ok(r) => {
                let items = r["albums"]["items"].clone();
                let total = r["albums"]["total"].as_i64().unwrap_or(0);
                let out = serde_json::json!({
                    "items": items,
                    "total": total,
                    "type": kind_str,
                    "genre_ids": genre_ids_str,
                });
                call_cb(
                    cb,
                    ud,
                    EV_FEATURED_ALBUMS_OK,
                    &serde_json::to_string(&out).unwrap_or_default(),
                );
            }
            Err(e) => call_cb(cb, ud, EV_GENERIC_ERR, &err_json(&e.to_string())),
        }
    });
}

#[no_mangle]
pub unsafe extern "C" fn qobuz_backend_get_featured_playlists(
    ptr: *mut Backend,
    genre_ids: *const c_char,
    kind: *const c_char,
    limit: u32,
    offset: u32,
) {
    let inner = &(*ptr).0;
    let client = inner.client.clone();
    let cb = inner.cb;
    let ud = inner.ud;
    let kind_str = CStr::from_ptr(kind).to_string_lossy().into_owned();
    let genre_ids_str = CStr::from_ptr(genre_ids).to_string_lossy().into_owned();

    spawn(inner, async move {
        let result = client
            .lock()
            .await
            .get_featured_playlists(&genre_ids_str, &kind_str, limit, offset)
            .await;
        match result {
            Ok(r) => {
                let items = r["playlists"]["items"].clone();
                let total = r["playlists"]["total"].as_i64().unwrap_or(0);
                let out = serde_json::json!({
                    "items": items,
                    "total": total,
                    "type": kind_str,
                    "genre_ids": genre_ids_str,
                });
                call_cb(
                    cb,
                    ud,
                    EV_FEATURED_PLAYLISTS_OK,
                    &serde_json::to_string(&out).unwrap_or_default(),
                );
            }
            Err(e) => call_cb(cb, ud, EV_GENERIC_ERR, &err_json(&e.to_string())),
        }
    });
}

#[no_mangle]
pub unsafe extern "C" fn qobuz_backend_discover_playlists(
    ptr: *mut Backend,
    genre_ids: *const c_char,
    tags: *const c_char,
    limit: u32,
    offset: u32,
) {
    let inner = &(*ptr).0;
    let client = inner.client.clone();
    let cb = inner.cb;
    let ud = inner.ud;
    let genre_ids_str = CStr::from_ptr(genre_ids).to_string_lossy().into_owned();
    let tags_str = CStr::from_ptr(tags).to_string_lossy().into_owned();

    spawn(inner, async move {
        let result = client
            .lock()
            .await
            .discover_playlists(&genre_ids_str, &tags_str, limit, offset)
            .await;
        match result {
            Ok(r) => {
                let items = r["items"].clone();
                let total = r["total"].as_i64().unwrap_or(0);
                let out = serde_json::json!({
                    "items": items,
                    "total": total,
                    "genre_ids": genre_ids_str,
                    "tags": tags_str,
                });
                call_cb(
                    cb,
                    ud,
                    EV_DISCOVER_PLAYLISTS_OK,
                    &serde_json::to_string(&out).unwrap_or_default(),
                );
            }
            Err(e) => call_cb(cb, ud, EV_GENERIC_ERR, &err_json(&e.to_string())),
        }
    });
}

#[no_mangle]
pub unsafe extern "C" fn qobuz_backend_search_playlists(
    ptr: *mut Backend,
    query: *const c_char,
    limit: u32,
    offset: u32,
) {
    let inner = &(*ptr).0;
    let client = inner.client.clone();
    let cb = inner.cb;
    let ud = inner.ud;
    let query_str = CStr::from_ptr(query).to_string_lossy().into_owned();

    spawn(inner, async move {
        let result = client
            .lock()
            .await
            .search_playlists(&query_str, limit, offset)
            .await;
        match result {
            Ok(r) => {
                let items = r["playlists"]["items"].clone();
                let total = r["playlists"]["total"].as_i64().unwrap_or(0);
                let out = serde_json::json!({
                    "items": items,
                    "total": total,
                    "query": query_str,
                });
                call_cb(
                    cb,
                    ud,
                    EV_PLAYLIST_SEARCH_OK,
                    &serde_json::to_string(&out).unwrap_or_default(),
                );
            }
            Err(e) => call_cb(cb, ud, EV_GENERIC_ERR, &err_json(&e.to_string())),
        }
    });
}

// ---------- Playlist ----------

#[no_mangle]
pub unsafe extern "C" fn qobuz_backend_get_playlist(
    ptr: *mut Backend,
    playlist_id: i64,
    offset: u32,
    limit: u32,
) {
    let inner = &(*ptr).0;
    let client = inner.client.clone();
    let cb = inner.cb;
    let ud = inner.ud;

    spawn(inner, async move {
        let result = client
            .lock()
            .await
            .get_playlist(playlist_id, offset, limit)
            .await;
        let (ev, json) = match result {
            Ok(r) => (
                EV_PLAYLIST_OK,
                serde_json::to_string(&r).unwrap_or_default(),
            ),
            Err(e) => (EV_PLAYLIST_ERR, err_json(&e.to_string())),
        };
        call_cb(cb, ud, ev, &json);
    });
}

// ---------- Favorites ----------

#[no_mangle]
pub unsafe extern "C" fn qobuz_backend_get_fav_tracks(ptr: *mut Backend, offset: u32, limit: u32) {
    let inner = &(*ptr).0;
    let client = inner.client.clone();
    let cb = inner.cb;
    let ud = inner.ud;
    spawn(inner, async move {
        let result = client.lock().await.get_fav_tracks(offset, limit).await;
        let (ev, json) = match result {
            Ok(r) => (
                EV_FAV_TRACKS_OK,
                serde_json::to_string(&r).unwrap_or_default(),
            ),
            Err(e) => (EV_GENERIC_ERR, err_json(&e.to_string())),
        };
        call_cb(cb, ud, ev, &json);
    });
}

#[no_mangle]
pub unsafe extern "C" fn qobuz_backend_get_fav_albums(ptr: *mut Backend, offset: u32, limit: u32) {
    let inner = &(*ptr).0;
    let client = inner.client.clone();
    let cb = inner.cb;
    let ud = inner.ud;
    spawn(inner, async move {
        let result = client.lock().await.get_fav_albums(offset, limit).await;
        let (ev, json) = match result {
            Ok(r) => (
                EV_FAV_ALBUMS_OK,
                serde_json::to_string(&r).unwrap_or_default(),
            ),
            Err(e) => (EV_GENERIC_ERR, err_json(&e.to_string())),
        };
        call_cb(cb, ud, ev, &json);
    });
}

#[no_mangle]
pub unsafe extern "C" fn qobuz_backend_get_fav_artists(ptr: *mut Backend, offset: u32, limit: u32) {
    let inner = &(*ptr).0;
    let client = inner.client.clone();
    let cb = inner.cb;
    let ud = inner.ud;
    spawn(inner, async move {
        let result = client.lock().await.get_fav_artists(offset, limit).await;
        let (ev, json) = match result {
            Ok(r) => (
                EV_FAV_ARTISTS_OK,
                serde_json::to_string(&r).unwrap_or_default(),
            ),
            Err(e) => (EV_GENERIC_ERR, err_json(&e.to_string())),
        };
        call_cb(cb, ud, ev, &json);
    });
}

#[no_mangle]
pub unsafe extern "C" fn qobuz_backend_get_user_playlists(
    ptr: *mut Backend,
    offset: u32,
    limit: u32,
) {
    let inner = &(*ptr).0;
    let client = inner.client.clone();
    let cb = inner.cb;
    let ud = inner.ud;
    spawn(inner, async move {
        let result = client.lock().await.get_user_playlists(offset, limit).await;
        let (ev, json) = match result {
            Ok(r) => {
                let items = r
                    .playlists
                    .as_ref()
                    .and_then(|p| p.items.as_ref())
                    .cloned()
                    .unwrap_or_default();
                let total = r.playlists.as_ref().and_then(|p| p.total).unwrap_or(0);
                (
                    EV_PLAYLISTS_OK,
                    serde_json::json!({"items": items, "total": total}).to_string(),
                )
            }
            Err(e) => (EV_GENERIC_ERR, err_json(&e.to_string())),
        };
        call_cb(cb, ud, ev, &json);
    });
}

// ---------- Playback ----------

#[no_mangle]
pub unsafe extern "C" fn qobuz_backend_play_track(
    ptr: *mut Backend,
    track_id: i64,
    format_id: i32,
) {
    let inner = &(*ptr).0;
    let client = inner.client.clone();
    let cb = inner.cb;
    let ud = inner.ud;
    let format = Format::from_id(format_id);
    let cmd_tx = inner.player.cmd_tx.clone();
    let status = inner.player.status.clone();
    let prefetch = inner.prefetch.clone();
    let rg_enabled = inner.replaygain_enabled.clone();

    spawn(inner, async move {
        let cached = {
            let mut lock = prefetch.lock().await;
            if lock
                .as_ref()
                .map(|p| p.track_id == track_id)
                .unwrap_or(false)
            {
                lock.take()
            } else {
                None
            }
        };

        // Extract prefetch_data to embed directly into TrackInfo
        let (track, url, n_segments, encryption_key, prefetch_data) = if let Some(pf) = cached {
            (
                pf.track,
                pf.url,
                pf.n_segments,
                pf.encryption_key,
                pf.prefetch_data,
            )
        } else {
            let track = match client.lock().await.get_track(track_id).await {
                Ok(t) => t,
                Err(e) => {
                    call_cb(cb, ud, EV_TRACK_URL_ERR, &err_json(&e.to_string()));
                    return;
                }
            };
            let url_dto = match client.lock().await.get_track_url(track_id, format).await {
                Ok(u) => u,
                Err(e) => {
                    call_cb(cb, ud, EV_TRACK_URL_ERR, &err_json(&e.to_string()));
                    return;
                }
            };
            let encryption_key = url_dto.key.clone();

            let (url, n_segments) =
                if let (Some(tmpl), Some(n)) = (url_dto.url_template, url_dto.n_segments) {
                    (tmpl, n)
                } else if let Some(u) = url_dto.url {
                    (u, 0u32)
                } else {
                    call_cb(cb, ud, EV_TRACK_URL_ERR, &err_json("no stream URL"));
                    return;
                };
            (track, url, n_segments, encryption_key, None)
        };

        if let Ok(j) = serde_json::to_string(&track) {
            call_cb(cb, ud, EV_TRACK_CHANGED, &j);
        }

        let replaygain_db = if rg_enabled.load(std::sync::atomic::Ordering::Relaxed) {
            track
                .audio_info
                .as_ref()
                .and_then(|ai| ai.replaygain_track_gain)
        } else {
            None
        };

        *status.current_track.lock().unwrap() = Some(track.clone());
        if let Some(dur) = track.duration {
            status
                .duration_secs
                .store(dur as u64, std::sync::atomic::Ordering::Relaxed);
        }

        let _ = cmd_tx.send(player::PlayerCommand::Play(player::TrackInfo {
            track,
            url,
            n_segments,
            encryption_key,
            replaygain_db,
            prefetch_data,
        }));

        call_cb(cb, ud, EV_STATE_CHANGED, r#"{"state":"playing"}"#);
    });
}

#[no_mangle]
pub unsafe extern "C" fn qobuz_backend_pause(ptr: *mut Backend) {
    let inner = &(*ptr).0;
    inner.player.pause();
    call_cb(
        inner.cb,
        inner.ud,
        EV_STATE_CHANGED,
        r#"{"state":"paused"}"#,
    );
}

#[no_mangle]
pub unsafe extern "C" fn qobuz_backend_resume(ptr: *mut Backend) {
    let inner = &(*ptr).0;
    inner.player.resume();
    call_cb(
        inner.cb,
        inner.ud,
        EV_STATE_CHANGED,
        r#"{"state":"playing"}"#,
    );
}

#[no_mangle]
pub unsafe extern "C" fn qobuz_backend_stop(ptr: *mut Backend) {
    let inner = &(*ptr).0;
    inner.player.stop();
    call_cb(inner.cb, inner.ud, EV_STATE_CHANGED, r#"{"state":"idle"}"#);
}

#[no_mangle]
pub unsafe extern "C" fn qobuz_backend_set_volume(ptr: *mut Backend, volume: u8) {
    (*ptr).0.player.set_volume(volume);
}

#[no_mangle]
pub unsafe extern "C" fn qobuz_backend_seek(ptr: *mut Backend, position_secs: u64) {
    (*ptr).0.player.seek(position_secs);
}

#[no_mangle]
pub unsafe extern "C" fn qobuz_backend_get_position(ptr: *const Backend) -> u64 {
    (*ptr).0.player.status.get_position()
}

#[no_mangle]
pub unsafe extern "C" fn qobuz_backend_get_duration(ptr: *const Backend) -> u64 {
    (*ptr).0.player.status.get_duration()
}

#[no_mangle]
pub unsafe extern "C" fn qobuz_backend_get_volume(ptr: *const Backend) -> u8 {
    (*ptr).0.player.status.get_volume()
}

#[no_mangle]
pub unsafe extern "C" fn qobuz_backend_get_state(ptr: *const Backend) -> c_int {
    match (*ptr).0.player.status.get_state() {
        PlayerState::Playing => 1,
        PlayerState::Paused => 2,
        _ => 0,
    }
}

#[no_mangle]
pub unsafe extern "C" fn qobuz_backend_take_track_finished(ptr: *mut Backend) -> c_int {
    let finished = (*ptr)
        .0
        .player
        .status
        .track_finished
        .swap(false, std::sync::atomic::Ordering::SeqCst);
    if finished {
        1
    } else {
        0
    }
}

#[no_mangle]
pub unsafe extern "C" fn qobuz_backend_take_track_transitioned(ptr: *mut Backend) -> c_int {
    let inner = &(*ptr).0;
    let transitioned = inner
        .player
        .status
        .track_transitioned
        .swap(false, std::sync::atomic::Ordering::SeqCst);

    if transitioned {
        // Emit track changed so the Qt UI and Scrobbler automatically pick up the new song
        if let Some(track) = inner.player.status.current_track.lock().unwrap().as_ref() {
            if let Ok(j) = serde_json::to_string(track) {
                call_cb(inner.cb, inner.ud, EV_TRACK_CHANGED, &j);
            }
        }
        1
    } else {
        0
    }
}

// ---------- ReplayGain / Gapless ----------

#[no_mangle]
pub unsafe extern "C" fn qobuz_backend_set_replaygain(ptr: *mut Backend, enabled: bool) {
    (*ptr)
        .0
        .replaygain_enabled
        .store(enabled, std::sync::atomic::Ordering::Relaxed);
}

#[no_mangle]
pub unsafe extern "C" fn qobuz_backend_set_gapless(ptr: *mut Backend, enabled: bool) {
    (*ptr)
        .0
        .player
        .status
        .gapless
        .store(enabled, std::sync::atomic::Ordering::Relaxed);
}

#[no_mangle]
pub unsafe extern "C" fn qobuz_backend_prefetch_track(
    ptr: *mut Backend,
    track_id: i64,
    format_id: i32,
) {
    let inner = &(*ptr).0;
    let client = inner.client.clone();
    let format = Format::from_id(format_id);
    let cmd_tx = inner.player.cmd_tx.clone();
    let rg_enabled = inner.replaygain_enabled.clone();

    spawn(inner, async move {
        let track = match client.lock().await.get_track(track_id).await {
            Ok(t) => t,
            Err(_) => return,
        };
        let url_dto = match client.lock().await.get_track_url(track_id, format).await {
            Ok(u) => u,
            Err(_) => return,
        };
        let encryption_key = url_dto.key.clone();
        let (url, n_segments) =
            if let (Some(tmpl), Some(n)) = (url_dto.url_template, url_dto.n_segments) {
                (tmpl, n)
            } else if let Some(u) = url_dto.url {
                (u, 0u32)
            } else {
                return;
            };

        // KICKSTART DOWNLOADING IMMEDIATELY
        let prefetch_data = if n_segments > 0 {
            Some(player::decoder::start_prefetch(
                url.clone(),
                n_segments,
                encryption_key.as_deref(),
                1,
            ))
        } else {
            None
        };

        let replaygain_db = if rg_enabled.load(std::sync::atomic::Ordering::Relaxed) {
            track
                .audio_info
                .as_ref()
                .and_then(|ai| ai.replaygain_track_gain)
        } else {
            None
        };

        let _ = cmd_tx.send(player::PlayerCommand::QueueNext(player::TrackInfo {
            track,
            url,
            n_segments,
            encryption_key,
            replaygain_db,
            prefetch_data,
        }));
    });
}

// ---------- Favorites modification ----------

#[no_mangle]
pub unsafe extern "C" fn qobuz_backend_add_fav_track(ptr: *mut Backend, track_id: i64) {
    let inner = &(*ptr).0;
    let client = inner.client.clone();
    let cb = inner.cb;
    let ud = inner.ud;
    spawn(inner, async move {
        if let Err(e) = client.lock().await.add_fav_track(track_id).await {
            call_cb(cb, ud, EV_GENERIC_ERR, &err_json(&e.to_string()));
        }
    });
}

#[no_mangle]
pub unsafe extern "C" fn qobuz_backend_remove_fav_track(ptr: *mut Backend, track_id: i64) {
    let inner = &(*ptr).0;
    let client = inner.client.clone();
    let cb = inner.cb;
    let ud = inner.ud;
    spawn(inner, async move {
        if let Err(e) = client.lock().await.remove_fav_track(track_id).await {
            call_cb(cb, ud, EV_GENERIC_ERR, &err_json(&e.to_string()));
        }
    });
}

#[no_mangle]
pub unsafe extern "C" fn qobuz_backend_add_fav_album(ptr: *mut Backend, album_id: *const c_char) {
    let inner = &(*ptr).0;
    let album_id = CStr::from_ptr(album_id).to_string_lossy().into_owned();
    let client = inner.client.clone();
    let cb = inner.cb;
    let ud = inner.ud;
    spawn(inner, async move {
        if let Err(e) = client.lock().await.add_fav_album(&album_id).await {
            call_cb(cb, ud, EV_GENERIC_ERR, &err_json(&e.to_string()));
        }
    });
}

#[no_mangle]
pub unsafe extern "C" fn qobuz_backend_remove_fav_album(
    ptr: *mut Backend,
    album_id: *const c_char,
) {
    let inner = &(*ptr).0;
    let album_id = CStr::from_ptr(album_id).to_string_lossy().into_owned();
    let client = inner.client.clone();
    let cb = inner.cb;
    let ud = inner.ud;
    spawn(inner, async move {
        if let Err(e) = client.lock().await.remove_fav_album(&album_id).await {
            call_cb(cb, ud, EV_GENERIC_ERR, &err_json(&e.to_string()));
        }
    });
}

#[no_mangle]
pub unsafe extern "C" fn qobuz_backend_add_fav_artist(ptr: *mut Backend, artist_id: i64) {
    let inner = &(*ptr).0;
    let client = inner.client.clone();
    let cb = inner.cb;
    let ud = inner.ud;
    spawn(inner, async move {
        if let Err(e) = client.lock().await.add_fav_artist(artist_id).await {
            call_cb(cb, ud, EV_GENERIC_ERR, &err_json(&e.to_string()));
        }
    });
}

#[no_mangle]
pub unsafe extern "C" fn qobuz_backend_remove_fav_artist(ptr: *mut Backend, artist_id: i64) {
    let inner = &(*ptr).0;
    let client = inner.client.clone();
    let cb = inner.cb;
    let ud = inner.ud;
    spawn(inner, async move {
        if let Err(e) = client.lock().await.remove_fav_artist(artist_id).await {
            call_cb(cb, ud, EV_GENERIC_ERR, &err_json(&e.to_string()));
        }
    });
}

// ---------- User ----------

pub const EV_USER_OK: c_int = 23;

#[no_mangle]
pub unsafe extern "C" fn qobuz_backend_get_user(ptr: *mut Backend) {
    let inner = &(*ptr).0;
    let client = inner.client.clone();
    let cb = inner.cb;
    let ud = inner.ud;
    spawn(inner, async move {
        let result = client.lock().await.get_user().await;
        let (ev, json) = match result {
            Ok(r) => (EV_USER_OK, serde_json::to_string(&r).unwrap_or_default()),
            Err(e) => (EV_GENERIC_ERR, err_json(&e.to_string())),
        };
        call_cb(cb, ud, ev, &json);
    });
}

// ---------- Playlist management ----------

pub const EV_PLAYLIST_CREATED: c_int = 20;
pub const EV_PLAYLIST_DELETED: c_int = 21;
pub const EV_PLAYLIST_TRACK_ADDED: c_int = 22;

#[no_mangle]
pub unsafe extern "C" fn qobuz_backend_create_playlist(ptr: *mut Backend, name: *const c_char) {
    let inner = &(*ptr).0;
    let name = CStr::from_ptr(name).to_string_lossy().into_owned();
    let client = inner.client.clone();
    let cb = inner.cb;
    let ud = inner.ud;
    spawn(inner, async move {
        match client.lock().await.create_playlist(&name).await {
            Ok(p) => call_cb(
                cb,
                ud,
                EV_PLAYLIST_CREATED,
                &serde_json::to_string(&p).unwrap_or_default(),
            ),
            Err(e) => call_cb(cb, ud, EV_GENERIC_ERR, &err_json(&e.to_string())),
        }
    });
}

#[no_mangle]
pub unsafe extern "C" fn qobuz_backend_delete_playlist(ptr: *mut Backend, playlist_id: i64) {
    let inner = &(*ptr).0;
    let client = inner.client.clone();
    let cb = inner.cb;
    let ud = inner.ud;
    spawn(inner, async move {
        match client.lock().await.delete_playlist(playlist_id).await {
            Ok(()) => call_cb(
                cb,
                ud,
                EV_PLAYLIST_DELETED,
                &serde_json::json!({"playlist_id": playlist_id}).to_string(),
            ),
            Err(e) => call_cb(cb, ud, EV_GENERIC_ERR, &err_json(&e.to_string())),
        }
    });
}

#[no_mangle]
pub unsafe extern "C" fn qobuz_backend_add_track_to_playlist(
    ptr: *mut Backend,
    playlist_id: i64,
    track_id: i64,
) {
    let inner = &(*ptr).0;
    let client = inner.client.clone();
    let cb = inner.cb;
    let ud = inner.ud;
    spawn(inner, async move {
        match client
            .lock()
            .await
            .add_track_to_playlist(playlist_id, track_id)
            .await
        {
            Ok(()) => call_cb(
                cb,
                ud,
                EV_PLAYLIST_TRACK_ADDED,
                &serde_json::json!({"playlist_id": playlist_id}).to_string(),
            ),
            Err(e) => call_cb(cb, ud, EV_GENERIC_ERR, &err_json(&e.to_string())),
        }
    });
}

#[no_mangle]
pub unsafe extern "C" fn qobuz_backend_delete_track_from_playlist(
    ptr: *mut Backend,
    playlist_id: i64,
    playlist_track_id: i64,
) {
    let inner = &(*ptr).0;
    let client = inner.client.clone();
    let cb = inner.cb;
    let ud = inner.ud;
    spawn(inner, async move {
        if let Err(e) = client
            .lock()
            .await
            .delete_track_from_playlist(playlist_id, playlist_track_id)
            .await
        {
            call_cb(cb, ud, EV_GENERIC_ERR, &err_json(&e.to_string()));
        }
    });
}

#[no_mangle]
pub unsafe extern "C" fn qobuz_backend_subscribe_playlist(ptr: *mut Backend, playlist_id: i64) {
    let inner = &(*ptr).0;
    let client = inner.client.clone();
    let cb = inner.cb;
    let ud = inner.ud;
    spawn(inner, async move {
        match client.lock().await.subscribe_playlist(playlist_id).await {
            Ok(()) => call_cb(
                cb,
                ud,
                EV_PLAYLIST_SUBSCRIBED,
                &serde_json::json!({"playlist_id": playlist_id}).to_string(),
            ),
            Err(e) => call_cb(cb, ud, EV_GENERIC_ERR, &err_json(&e.to_string())),
        }
    });
}

#[no_mangle]
pub unsafe extern "C" fn qobuz_backend_unsubscribe_playlist(ptr: *mut Backend, playlist_id: i64) {
    let inner = &(*ptr).0;
    let client = inner.client.clone();
    let cb = inner.cb;
    let ud = inner.ud;
    spawn(inner, async move {
        match client.lock().await.unsubscribe_playlist(playlist_id).await {
            Ok(()) => call_cb(
                cb,
                ud,
                EV_PLAYLIST_UNSUBSCRIBED,
                &serde_json::json!({"playlist_id": playlist_id}).to_string(),
            ),
            Err(e) => call_cb(cb, ud, EV_GENERIC_ERR, &err_json(&e.to_string())),
        }
    });
}
