//! qobuz-backend: C-ABI library consumed by the Qt frontend.
#![allow(clippy::missing_safety_doc)]

mod api;
mod download;
mod player;

use std::{
    collections::HashMap,
    ffi::{CStr, CString},
    os::raw::{c_char, c_int, c_void},
    sync::{atomic::{AtomicBool, AtomicU64, Ordering}, Arc},
};

use api::{Format, QobuzClient};
use download::{
    album_folder_path, build_audio_profile, build_tag_metadata, cover_art_url, download_artwork,
    download_to_file, file_extension, max_disc_number, playlist_folder_path, tag_audio_file,
    track_output_path, track_title, year_from_date, DownloadSettings, CANCELLED_ERROR,
};
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
pub const EV_AUTH_REFRESH_OK: c_int = 40;
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
pub const EV_DOWNLOAD_STARTED: c_int = 35;
pub const EV_DOWNLOAD_PROGRESS: c_int = 36;
pub const EV_DOWNLOAD_FINISHED: c_int = 37;
pub const EV_DOWNLOAD_FAILED: c_int = 38;
pub const EV_DOWNLOAD_CANCELLED: c_int = 39;

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
    next_transfer_id: AtomicU64,
    download_cancels: Arc<Mutex<HashMap<u64, Arc<AtomicBool>>>>,
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

fn auth_json(token: &str, refresh_token: &str, expires_at: u64) -> String {
    serde_json::json!({
        "token": token,
        "refresh_token": refresh_token,
        "expires_at": expires_at,
    })
    .to_string()
}

fn parse_download_settings(json: &str) -> DownloadSettings {
    serde_json::from_str::<DownloadSettings>(json)
        .unwrap_or_default()
        .normalized()
}

fn emit_download_started(
    cb: EventCallback,
    ud: SendPtr,
    kind: &str,
    id: &str,
    transfer_id: u64,
    label: &str,
    total_tracks: usize,
) {
    call_cb(
        cb,
        ud,
        EV_DOWNLOAD_STARTED,
        &serde_json::json!({
            "kind": kind,
            "id": id,
            "transfer_id": transfer_id,
            "label": label,
            "status": "running",
            "total_tracks": total_tracks,
        })
        .to_string(),
    );
}

fn emit_download_progress(
    cb: EventCallback,
    ud: SendPtr,
    kind: &str,
    id: &str,
    transfer_id: u64,
    current: usize,
    total_tracks: usize,
    track_id: i64,
    title: &str,
    downloaded: u64,
    total_bytes: Option<u64>,
    failed_tracks: usize,
) {
    call_cb(
        cb,
        ud,
        EV_DOWNLOAD_PROGRESS,
        &serde_json::json!({
            "kind": kind,
            "id": id,
            "transfer_id": transfer_id,
            "status": "running",
            "current": current,
            "total_tracks": total_tracks,
            "track_id": track_id,
            "track_title": title,
            "downloaded_bytes": downloaded,
            "total_bytes": total_bytes,
            "failed_tracks": failed_tracks,
        })
        .to_string(),
    );
}

fn emit_download_finished(
    cb: EventCallback,
    ud: SendPtr,
    kind: &str,
    id: &str,
    transfer_id: u64,
    label: &str,
    path: &str,
    total_tracks: usize,
    failed_tracks: usize,
) {
    call_cb(
        cb,
        ud,
        EV_DOWNLOAD_FINISHED,
        &serde_json::json!({
            "kind": kind,
            "id": id,
            "transfer_id": transfer_id,
            "label": label,
            "status": "completed",
            "path": path,
            "current": total_tracks,
            "total_tracks": total_tracks,
            "failed_tracks": failed_tracks,
        })
        .to_string(),
    );
}

fn emit_download_failed(cb: EventCallback, ud: SendPtr, kind: &str, id: &str, transfer_id: u64, error: &str) {
    call_cb(
        cb,
        ud,
        EV_DOWNLOAD_FAILED,
        &serde_json::json!({
            "kind": kind,
            "id": id,
            "transfer_id": transfer_id,
            "status": "failed",
            "error": error,
        })
        .to_string(),
    );
}

fn emit_download_cancelled(cb: EventCallback, ud: SendPtr, kind: &str, id: &str, transfer_id: u64, label: &str) {
    call_cb(
        cb,
        ud,
        EV_DOWNLOAD_CANCELLED,
        &serde_json::json!({
            "kind": kind,
            "id": id,
            "transfer_id": transfer_id,
            "status": "cancelled",
            "label": label,
        })
        .to_string(),
    );
}

fn spawn<F>(inner: &BackendInner, f: F)
where
    F: std::future::Future<Output = ()> + Send + 'static,
{
    inner.rt.spawn(f);
}

fn is_cancelled_error(err: &anyhow::Error) -> bool {
    err.to_string().contains(CANCELLED_ERROR)
}

async fn prepare_cover_art(
    url: Option<String>,
    target_folder: &std::path::Path,
    temp_name: &str,
    keep_saved_copy: bool,
) -> Option<std::path::PathBuf> {
    let url = url?;
    if keep_saved_copy {
        return download_artwork(&url, target_folder).await.ok().flatten();
    }

    let temp_path = std::env::temp_dir().join(temp_name);
    if download_to_file(&url, &temp_path, None, |_, _| {}).await.is_ok() {
        Some(temp_path)
    } else {
        None
    }
}

async fn cleanup_temporary_cover_art(path: Option<std::path::PathBuf>, keep_saved_copy: bool) {
    if keep_saved_copy {
        return;
    }
    if let Some(path) = path {
        let _ = tokio::fs::remove_file(path).await;
    }
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
        next_transfer_id: AtomicU64::new(1),
        download_cancels: Arc::new(Mutex::new(HashMap::new())),
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
                let refresh_token = resp
                    .oauth2
                    .as_ref()
                    .and_then(|o| o.refresh_token.as_deref())
                    .unwrap_or("")
                    .to_string();
                let expires_at = resp
                    .oauth2
                    .as_ref()
                    .and_then(|o| o.expires_in)
                    .map(|expires_in| {
                        std::time::SystemTime::now()
                            .duration_since(std::time::UNIX_EPOCH)
                            .unwrap()
                            .as_secs()
                            + expires_in.max(0) as u64
                    })
                    .unwrap_or(0);
                let user_val = resp
                    .user
                    .as_ref()
                    .map(|u| serde_json::to_value(u).unwrap_or_default())
                    .unwrap_or_default();
                (
                    EV_LOGIN_OK,
                    serde_json::json!({
                        "token": token,
                        "refresh_token": refresh_token,
                        "expires_at": expires_at,
                        "user": user_val,
                    })
                    .to_string(),
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

#[no_mangle]
pub unsafe extern "C" fn qobuz_backend_restore_session(
    ptr: *mut Backend,
    token: *const c_char,
    refresh_token: *const c_char,
    expires_at: u64,
) {
    let inner = &(*ptr).0;
    let token = CStr::from_ptr(token).to_string_lossy().into_owned();
    let refresh_token = CStr::from_ptr(refresh_token).to_string_lossy().into_owned();
    inner.client.blocking_lock().set_auth_state(
        (!token.is_empty()).then_some(token),
        (!refresh_token.is_empty()).then_some(refresh_token),
        (expires_at > 0).then_some(expires_at),
    );
}

#[no_mangle]
pub unsafe extern "C" fn qobuz_backend_refresh_auth(ptr: *mut Backend) {
    let inner = &(*ptr).0;
    let client = inner.client.clone();
    let cb = inner.cb;
    let ud = inner.ud;

    spawn(inner, async move {
        let result = {
            let mut client = client.lock().await;
            client.refresh_oauth_token().await.map(|_| client.auth_state())
        };
        let (ev, json) = match result {
            Ok((token, refresh_token, expires_at)) => (
                EV_AUTH_REFRESH_OK,
                auth_json(
                    token.as_deref().unwrap_or(""),
                    refresh_token.as_deref().unwrap_or(""),
                    expires_at.unwrap_or(0),
                ),
            ),
            Err(e) => (EV_GENERIC_ERR, err_json(&e.to_string())),
        };
        call_cb(cb, ud, ev, &json);
    });
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
                    "offset": offset,
                    "limit": limit,
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
                    "offset": offset,
                    "limit": limit,
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
                    "offset": offset,
                    "limit": limit,
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
                    "offset": offset,
                    "limit": limit,
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

#[no_mangle]
pub unsafe extern "C" fn qobuz_backend_get_playlist_all(ptr: *mut Backend, playlist_id: i64) {
    let inner = &(*ptr).0;
    let client = inner.client.clone();
    let cb = inner.cb;
    let ud = inner.ud;
    spawn(inner, async move {
        let result = client.lock().await.get_playlist_all(playlist_id).await;
        let (ev, json) = match result {
            Ok(r) => {
                let mut v = serde_json::to_value(&r).unwrap_or_default();
                if let serde_json::Value::Object(ref mut obj) = v {
                    obj.insert("full_load".to_string(), serde_json::Value::Bool(true));
                }
                (EV_PLAYLIST_OK, serde_json::to_string(&v).unwrap_or_default())
            }
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

// ---------- Downloads ----------

#[no_mangle]
pub unsafe extern "C" fn qobuz_backend_download_track(
    ptr: *mut Backend,
    track_id: i64,
    format_id: i32,
    settings_json: *const c_char,
) {
    let inner = &(*ptr).0;
    let client = inner.client.clone();
    let cb = inner.cb;
    let ud = inner.ud;
    let format = Format::from_id(format_id);
    let settings = parse_download_settings(&CStr::from_ptr(settings_json).to_string_lossy());
    let transfer_id = inner.next_transfer_id.fetch_add(1, Ordering::Relaxed);
    let cancel = Arc::new(AtomicBool::new(false));
    inner.download_cancels.blocking_lock().insert(transfer_id, cancel.clone());
    let download_cancels = inner.download_cancels.clone();

    spawn(inner, async move {
        let result = async {
            let track = client.lock().await.get_track(track_id).await?;
            let download = client.lock().await.get_track_download_url(track_id, format).await?;
            let ext = file_extension(&download, format);
            let audio = build_audio_profile(&download, track.album.as_ref(), format);
            let title = track_title(&track);
            let out_path = track_output_path(
                &settings,
                "qobuz",
                &track,
                &title,
                &ext,
                None,
                0,
                None,
                Some(&audio),
            );
            emit_download_started(cb, ud, "track", &track_id.to_string(), transfer_id, &title, 1);

            let art_folder = out_path.parent().unwrap_or_else(|| std::path::Path::new(&settings.folder));
            let cover_path = if settings.save_artwork || settings.embed_artwork {
                prepare_cover_art(
                    cover_art_url(track.album.as_ref()),
                    art_folder,
                    &format!("qobuz-qt-track-cover-{track_id}.jpg"),
                    settings.save_artwork,
                )
                .await
            } else {
                None
            };

            let stream_url = download.url.ok_or_else(|| anyhow::anyhow!("download URL missing"))?;
            download_to_file(&stream_url, &out_path, Some(&cancel), |downloaded, total| {
                emit_download_progress(
                    cb,
                    ud,
                    "track",
                    &track_id.to_string(),
                    transfer_id,
                    1,
                    1,
                    track_id,
                    &title,
                    downloaded,
                    total,
                    0,
                );
            })
            .await?;

            let tag_meta = build_tag_metadata(&settings, &track, &title, 1, 1, None, None);
            tag_audio_file(
                &out_path,
                &ext,
                &tag_meta,
                if settings.embed_artwork { cover_path.as_deref() } else { None },
            )?;
            cleanup_temporary_cover_art(cover_path.clone(), settings.save_artwork).await;

            Ok::<_, anyhow::Error>(out_path)
        }
        .await;

        download_cancels.lock().await.remove(&transfer_id);

        match result {
            Ok(path) => emit_download_finished(
                cb,
                ud,
                "track",
                &track_id.to_string(),
                transfer_id,
                "Track",
                &path.to_string_lossy(),
                1,
                0,
            ),
            Err(e) if is_cancelled_error(&e) => emit_download_cancelled(cb, ud, "track", &track_id.to_string(), transfer_id, "Track"),
            Err(e) => emit_download_failed(cb, ud, "track", &track_id.to_string(), transfer_id, &e.to_string()),
        }
    });
}

#[no_mangle]
pub unsafe extern "C" fn qobuz_backend_download_album(
    ptr: *mut Backend,
    album_id: *const c_char,
    format_id: i32,
    settings_json: *const c_char,
) {
    let inner = &(*ptr).0;
    let client = inner.client.clone();
    let cb = inner.cb;
    let ud = inner.ud;
    let format = Format::from_id(format_id);
    let album_id = CStr::from_ptr(album_id).to_string_lossy().into_owned();
    let settings = parse_download_settings(&CStr::from_ptr(settings_json).to_string_lossy());
    let transfer_id = inner.next_transfer_id.fetch_add(1, Ordering::Relaxed);
    let cancel = Arc::new(AtomicBool::new(false));
    inner.download_cancels.blocking_lock().insert(transfer_id, cancel.clone());
    let download_cancels = inner.download_cancels.clone();

    spawn(inner, async move {
        let result = async {
            let album = client.lock().await.get_album(&album_id).await?;
            let tracks = album
                .tracks
                .as_ref()
                .and_then(|tracks| tracks.items.clone())
                .ok_or_else(|| anyhow::anyhow!("album has no tracks"))?;
            if tracks.is_empty() {
                return Err(anyhow::anyhow!("album has no tracks"));
            }

            let album_title = album.title.as_deref().unwrap_or("Unknown");
            let album_artist = album
                .artist
                .as_ref()
                .and_then(|artist| artist.name.as_deref())
                .unwrap_or("Unknown");
            let year = album
                .release_date_original
                .as_deref()
                .map(year_from_date)
                .unwrap_or_else(|| "Unknown".to_string());
            let preview = client
                .lock()
                .await
                .get_track_download_url(tracks[0].id, format)
                .await
                .unwrap_or(api::models::TrackFileUrlDto {
                    track_id: None,
                    duration: None,
                    url: None,
                    url_template: None,
                    n_segments: None,
                    format_id: Some(format.id()),
                    mime_type: None,
                    sampling_rate: None,
                    bit_depth: None,
                    key: None,
                });
            let audio = build_audio_profile(&preview, Some(&album), format);
            let folder = album_folder_path(&settings, "qobuz", &album_id, album_title, album_artist, &year, &audio);
            emit_download_started(cb, ud, "album", &album_id, transfer_id, album_title, tracks.len());

            if settings.save_artwork {
                let _ = prepare_cover_art(
                    cover_art_url(Some(&album)),
                    &folder,
                    &format!("qobuz-qt-album-cover-{}.jpg", album_id),
                    true,
                )
                .await;
            }

            let total_tracks = tracks.len();
            let disc_total = max_disc_number(&tracks);
            let shared_cover = if settings.embed_artwork {
                prepare_cover_art(
                    cover_art_url(Some(&album)),
                    &folder,
                    &format!("qobuz-qt-album-cover-{}.jpg", album_id),
                    settings.save_artwork,
                )
                .await
            } else {
                None
            };
            let mut failed_tracks = 0usize;
            for (index, track) in tracks.iter().enumerate() {
                if cancel.load(Ordering::Relaxed) {
                    anyhow::bail!(CANCELLED_ERROR);
                }
                let title = track_title(track);
                let track_result = async {
                    let download = client.lock().await.get_track_download_url(track.id, format).await?;
                    let ext = file_extension(&download, format);
                    let out_path = track_output_path(
                        &settings,
                        "qobuz",
                        track,
                        &title,
                        &ext,
                        Some(&folder),
                        disc_total,
                        None,
                        None,
                    );
                    let stream_url = download.url.ok_or_else(|| anyhow::anyhow!("download URL missing"))?;
                    download_to_file(&stream_url, &out_path, Some(&cancel), |downloaded, total| {
                        emit_download_progress(
                            cb,
                            ud,
                            "album",
                            &album_id,
                            transfer_id,
                            index + 1,
                            total_tracks,
                            track.id,
                            &title,
                            downloaded,
                            total,
                            failed_tracks,
                        );
                    })
                    .await?;
                    let tag_meta = build_tag_metadata(
                        &settings,
                        track,
                        &title,
                        disc_total,
                        total_tracks as i32,
                        None,
                        None,
                    );
                    tag_audio_file(
                        &out_path,
                        &ext,
                        &tag_meta,
                        if settings.embed_artwork { shared_cover.as_deref() } else { None },
                    )?;
                    Ok::<(), anyhow::Error>(())
                }
                .await;

                if let Err(err) = track_result {
                    if is_cancelled_error(&err) {
                        cleanup_temporary_cover_art(shared_cover.clone(), settings.save_artwork).await;
                        return Err(err);
                    }
                    failed_tracks += 1;
                    emit_download_progress(
                        cb,
                        ud,
                        "album",
                        &album_id,
                        transfer_id,
                        index + 1,
                        total_tracks,
                        track.id,
                        &title,
                        0,
                        None,
                        failed_tracks,
                    );
                }
            }

            cleanup_temporary_cover_art(shared_cover.clone(), settings.save_artwork).await;

            Ok::<_, anyhow::Error>((folder, failed_tracks, total_tracks, album_title.to_string()))
        }
        .await;

        download_cancels.lock().await.remove(&transfer_id);

        match result {
            Ok((path, failed_tracks, total_tracks, label)) => emit_download_finished(
                cb,
                ud,
                "album",
                &album_id,
                transfer_id,
                &label,
                &path.to_string_lossy(),
                total_tracks,
                failed_tracks,
            ),
            Err(e) if is_cancelled_error(&e) => emit_download_cancelled(cb, ud, "album", &album_id, transfer_id, &album_id),
            Err(e) => emit_download_failed(cb, ud, "album", &album_id, transfer_id, &e.to_string()),
        }
    });
}

#[no_mangle]
pub unsafe extern "C" fn qobuz_backend_download_playlist(
    ptr: *mut Backend,
    playlist_id: i64,
    format_id: i32,
    settings_json: *const c_char,
) {
    let inner = &(*ptr).0;
    let client = inner.client.clone();
    let cb = inner.cb;
    let ud = inner.ud;
    let format = Format::from_id(format_id);
    let settings = parse_download_settings(&CStr::from_ptr(settings_json).to_string_lossy());
    let transfer_id = inner.next_transfer_id.fetch_add(1, Ordering::Relaxed);
    let cancel = Arc::new(AtomicBool::new(false));
    inner.download_cancels.blocking_lock().insert(transfer_id, cancel.clone());
    let download_cancels = inner.download_cancels.clone();

    spawn(inner, async move {
        let result = async {
            let playlist = client.lock().await.get_playlist_all(playlist_id).await?;
            let tracks = playlist
                .tracks
                .as_ref()
                .and_then(|tracks| tracks.items.clone())
                .ok_or_else(|| anyhow::anyhow!("playlist has no tracks"))?;
            if tracks.is_empty() {
                return Err(anyhow::anyhow!("playlist has no tracks"));
            }
            let name = playlist.name.as_deref().unwrap_or("Unknown");
            let folder = playlist_folder_path(&settings, "qobuz", name);
            emit_download_started(cb, ud, "playlist", &playlist_id.to_string(), transfer_id, name, tracks.len());

            let total_tracks = tracks.len();
            let mut failed_tracks = 0usize;
            for (index, track) in tracks.iter().enumerate() {
                if cancel.load(Ordering::Relaxed) {
                    anyhow::bail!(CANCELLED_ERROR);
                }
                let title = track_title(track);
                let playlist_position = if settings.renumber_playlist_tracks {
                    Some(index + 1)
                } else {
                    None
                };
                let track_result = async {
                    let download = client.lock().await.get_track_download_url(track.id, format).await?;
                    let ext = file_extension(&download, format);
                    let out_path = track_output_path(
                        &settings,
                        "qobuz",
                        track,
                        &title,
                        &ext,
                        Some(&folder),
                        1,
                        playlist_position,
                        None,
                    );
                    let cover_path = if settings.embed_artwork {
                        prepare_cover_art(
                            cover_art_url(track.album.as_ref()),
                            &folder,
                            &format!("qobuz-qt-playlist-cover-{}-{}.jpg", playlist_id, track.id),
                            false,
                        )
                        .await
                    } else {
                        None
                    };
                    let stream_url = download.url.ok_or_else(|| anyhow::anyhow!("download URL missing"))?;
                    download_to_file(&stream_url, &out_path, Some(&cancel), |downloaded, total| {
                        emit_download_progress(
                            cb,
                            ud,
                            "playlist",
                            &playlist_id.to_string(),
                            transfer_id,
                            index + 1,
                            total_tracks,
                            track.id,
                            &title,
                            downloaded,
                            total,
                            failed_tracks,
                        );
                    })
                    .await?;
                    let tag_meta = build_tag_metadata(
                        &settings,
                        track,
                        &title,
                        1,
                        total_tracks as i32,
                        Some(name),
                        playlist_position,
                    );
                    tag_audio_file(
                        &out_path,
                        &ext,
                        &tag_meta,
                        if settings.embed_artwork { cover_path.as_deref() } else { None },
                    )?;
                    cleanup_temporary_cover_art(cover_path, false).await;
                    Ok::<(), anyhow::Error>(())
                }
                .await;

                if let Err(err) = track_result {
                    if is_cancelled_error(&err) {
                        return Err(err);
                    }
                    failed_tracks += 1;
                    emit_download_progress(
                        cb,
                        ud,
                        "playlist",
                        &playlist_id.to_string(),
                        transfer_id,
                        index + 1,
                        total_tracks,
                        track.id,
                        &title,
                        0,
                        None,
                        failed_tracks,
                    );
                }
            }

            Ok::<_, anyhow::Error>((folder, failed_tracks, total_tracks, name.to_string()))
        }
        .await;

        download_cancels.lock().await.remove(&transfer_id);

        match result {
            Ok((path, failed_tracks, total_tracks, label)) => emit_download_finished(
                cb,
                ud,
                "playlist",
                &playlist_id.to_string(),
                transfer_id,
                &label,
                &path.to_string_lossy(),
                total_tracks,
                failed_tracks,
            ),
            Err(e) if is_cancelled_error(&e) => emit_download_cancelled(cb, ud, "playlist", &playlist_id.to_string(), transfer_id, &playlist_id.to_string()),
            Err(e) => emit_download_failed(cb, ud, "playlist", &playlist_id.to_string(), transfer_id, &e.to_string()),
        }
    });
}

#[no_mangle]
pub unsafe extern "C" fn qobuz_backend_cancel_download(ptr: *mut Backend, transfer_id: u64) {
    let inner = &(*ptr).0;
    if let Some(cancel) = inner.download_cancels.blocking_lock().get(&transfer_id).cloned() {
        cancel.store(true, Ordering::Relaxed);
    }
}

#[no_mangle]
pub unsafe extern "C" fn qobuz_backend_cancel_all_downloads(ptr: *mut Backend) {
    let inner = &(*ptr).0;
    for cancel in inner.download_cancels.blocking_lock().values() {
        cancel.store(true, Ordering::Relaxed);
    }
}
