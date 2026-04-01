use anyhow::{bail, Result};
use base64::{engine::general_purpose::URL_SAFE_NO_PAD, Engine};
use cbc::cipher::{block_padding::NoPadding, BlockDecryptMut, KeyIvInit};
use hkdf::Hkdf;
use reqwest::Client;
use serde_json::Value;
use sha2::Sha256;
use std::time::{SystemTime, UNIX_EPOCH};

use super::models::*;

const BASE_URL: &str = "https://www.qobuz.com/api.json/0.2/";
const USER_AGENT: &str =
    "Dalvik/2.1.0 (Linux; U; Android 9; Nexus 6P Build/PQ3A.190801.002) QobuzMobileAndroid/9.7.0.3-b26022717";
const APP_VERSION: &str = "9.7.0.3";
pub const DEFAULT_APP_ID: &str = "312369995";
pub const DEFAULT_APP_SECRET: &str = "e79f8b9be485692b0e5f9dd895826368";

pub struct QobuzClient {
    http: Client,
    pub auth_token: Option<String>,
    /// Playback session ID from POST /session/start — sent as X-Session-Id for /file/url.
    session_id: Option<String>,
    session_expires_at: Option<u64>,
    /// `infos` field from the session/start response — used to derive the KEK for track key unwrapping.
    session_infos: Option<String>,
    app_id: String,
    app_secret: String,
}

// ── qbz-1 key derivation helpers ─────────────────────────────────────────────

type Aes128CbcDec = cbc::Decryptor<aes::Aes128>;

/// Decode a base64url string (with or without padding).
fn b64url_decode(s: &str) -> Result<Vec<u8>> {
    Ok(URL_SAFE_NO_PAD.decode(s.trim_end_matches('='))?)
}

/// Full qbz-1 key derivation:
///   Phase 1: HKDF-SHA256(ikm=hex(app_secret), salt=b64url(infos[0]), info=b64url(infos[1])) → 16-byte KEK
///   Phase 2: AES-128-CBC/NoPadding(key=KEK, iv=b64url(key_field[2])).decrypt(b64url(key_field[1]))[..16]
fn derive_track_key(
    session_infos: &str,
    app_secret_hex: &str,
    key_field: &str,
) -> Result<[u8; 16]> {
    // Phase 1: HKDF
    let infos_parts: Vec<&str> = session_infos.splitn(2, '.').collect();
    if infos_parts.len() != 2 {
        bail!("session_infos must be '<salt_b64>.<info_b64>', got: {session_infos}");
    }
    let salt = b64url_decode(infos_parts[0])?;
    let info = b64url_decode(infos_parts[1])?;
    let ikm = hex::decode(app_secret_hex)?;

    let hk = Hkdf::<Sha256>::new(Some(&salt), &ikm);
    let mut kek = [0u8; 16];
    hk.expand(&info, &mut kek)
        .map_err(|e| anyhow::anyhow!("HKDF expand failed: {e:?}"))?;

    // Phase 2: AES-128-CBC/NoPadding
    let key_parts: Vec<&str> = key_field.splitn(3, '.').collect();
    if key_parts.len() != 3 || key_parts[0] != "qbz-1" {
        bail!("unexpected key field format: {key_field}");
    }
    let ct = b64url_decode(key_parts[1])?;
    let iv_bytes = b64url_decode(key_parts[2])?;
    if ct.len() < 16 || iv_bytes.len() < 16 {
        bail!(
            "key field ciphertext/iv too short ({} / {} bytes)",
            ct.len(),
            iv_bytes.len()
        );
    }

    let iv: [u8; 16] = iv_bytes[..16].try_into()?;
    let mut buf = ct;
    let decrypted = Aes128CbcDec::new(&kek.into(), &iv.into())
        .decrypt_padded_mut::<NoPadding>(&mut buf)
        .map_err(|e| anyhow::anyhow!("AES-CBC decrypt failed: {e:?}"))?;

    let mut track_key = [0u8; 16];
    track_key.copy_from_slice(&decrypted[..16]);
    Ok(track_key)
}

// ─────────────────────────────────────────────────────────────────────────────

impl QobuzClient {
    pub fn new() -> Result<Self> {
        Self::new_with_config(None, None)
    }

    pub fn new_with_config(app_id: Option<&str>, app_secret: Option<&str>) -> Result<Self> {
        let app_id = app_id.unwrap_or(DEFAULT_APP_ID).to_string();
        let app_secret = app_secret.unwrap_or(DEFAULT_APP_SECRET).to_string();

        let http = Client::builder()
            .user_agent(USER_AGENT)
            .default_headers({
                let mut h = reqwest::header::HeaderMap::new();
                h.insert("X-App-Id", app_id.parse()?);
                h.insert("X-App-Version", APP_VERSION.parse()?);
                h.insert("X-Device-Platform", "android".parse()?);
                h.insert("X-Device-Model", "Nexus 6P".parse()?);
                h.insert("X-Device-Os-Version", "9".parse()?);
                h
            })
            .build()?;

        Ok(Self {
            http,
            auth_token: None,
            session_id: None,
            session_expires_at: None,
            session_infos: None,
            app_id,
            app_secret,
        })
    }

    pub fn set_auth_token(&mut self, token: String) {
        self.auth_token = Some(token);
    }

    fn ts() -> u64 {
        SystemTime::now()
            .duration_since(UNIX_EPOCH)
            .unwrap()
            .as_secs()
    }

    /// Compute the request signature required by the Qobuz API.
    /// NOTE: MD5 is mandated by the Qobuz API protocol — not our choice.
    fn request_sig(&self, method: &str, params: &mut Vec<(&str, String)>, ts: u64) -> String {
        params.sort_by_key(|(k, _)| *k);
        let mut s = method.replace('/', "");
        for (k, v) in params.iter() {
            s.push_str(k);
            s.push_str(v);
        }
        s.push_str(&ts.to_string());
        s.push_str(&self.app_secret);
        format!("{:x}", md5::compute(s.as_bytes()))
    }

    fn url(&self, method: &str) -> String {
        format!("{}{}", BASE_URL, method)
    }

    async fn check_response(resp: reqwest::Response) -> Result<Value> {
        let status = resp.status();
        let body: Value = resp.json().await?;
        if !status.is_success() {
            let msg = body
                .get("message")
                .and_then(|m| m.as_str())
                .unwrap_or("unknown API error");
            bail!("HTTP {}: {}", status, msg);
        }
        Ok(body)
    }

    fn post_request(&self, method: &str) -> reqwest::RequestBuilder {
        let mut builder = self.http.post(self.url(method));
        builder = builder.query(&[("app_id", self.app_id.as_str())]);
        if let Some(token) = &self.auth_token {
            builder = builder.header("Authorization", format!("Bearer {}", token));
        }
        builder
    }

    fn get_request(&self, method: &str) -> reqwest::RequestBuilder {
        let mut builder = self.http.get(self.url(method));
        builder = builder.query(&[("app_id", self.app_id.as_str())]);
        if let Some(token) = &self.auth_token {
            builder = builder.header("Authorization", format!("Bearer {}", token));
        }
        if let Some(sid) = &self.session_id {
            builder = builder.header("X-Session-Id", sid.as_str());
        }
        builder
    }

    // --- Auth ---

    /// NOTE: Qobuz API requires credentials as GET query params — not our choice.
    pub async fn login(&mut self, email: &str, password: &str) -> Result<OAuthLoginResponse> {
        let ts = Self::ts();
        let mut sign_params: Vec<(&str, String)> = vec![
            ("password", password.to_string()),
            ("username", email.to_string()),
        ];
        let sig = self.request_sig("oauth2login", &mut sign_params, ts);

        let resp = self
            .http
            .get(self.url("oauth2/login"))
            .query(&[
                ("app_id", self.app_id.as_str()),
                ("username", email),
                ("password", password),
                ("request_ts", ts.to_string().as_str()),
                ("request_sig", sig.as_str()),
            ])
            .send()
            .await?;

        let status = resp.status();
        let body: Value = resp.json().await?;
        if !status.is_success() {
            let msg = body
                .get("message")
                .and_then(|m| m.as_str())
                .unwrap_or("login failed");
            bail!("login failed ({}): {}", status, msg);
        }

        self.extract_and_store_token(serde_json::from_value(body)?)
    }

    fn extract_and_store_token(&mut self, login: OAuthLoginResponse) -> Result<OAuthLoginResponse> {
        // auth_token = OAuth2 bearer (preferred) or legacy session token
        if let Some(token) = login
            .oauth2
            .as_ref()
            .and_then(|o| o.access_token.clone())
            .or_else(|| login.user_auth_token.clone())
        {
            self.auth_token = Some(token);
        }
        // Reset any cached playback session — it belongs to a different auth context.
        self.session_id = None;
        self.session_expires_at = None;
        self.session_infos = None;
        Ok(login)
    }

    /// Start a playback session via POST /session/start.
    /// The returned session_id is required as X-Session-Id when calling /file/url.
    /// Sessions expire; we cache and reuse until 60s before expiry.
    async fn ensure_session(&mut self) -> Result<()> {
        let now = Self::ts();
        if let (Some(_), Some(exp)) = (&self.session_id, self.session_expires_at) {
            if now + 60 < exp {
                return Ok(()); // still valid
            }
        }

        let ts = Self::ts();
        let mut sign_params: Vec<(&str, String)> = vec![("profile", "qbz-1".to_string())];
        let sig = self.request_sig("sessionstart", &mut sign_params, ts);

        let resp = self
            .http
            .post(self.url("session/start"))
            .query(&[
                ("app_id", self.app_id.as_str()),
                ("request_ts", ts.to_string().as_str()),
                ("request_sig", sig.as_str()),
            ])
            .header(
                "Authorization",
                format!("Bearer {}", self.auth_token.as_deref().unwrap_or("")),
            )
            .form(&[("profile", "qbz-1")])
            .send()
            .await?;

        let body = Self::check_response(resp).await?;
        let session_id = body["session_id"]
            .as_str()
            .ok_or_else(|| anyhow::anyhow!("session/start: no session_id in response"))?
            .to_string();
        let expires_at = body["expires_at"].as_u64().unwrap_or(now + 3600);
        let infos = body["infos"].as_str().map(|s| s.to_string());
        eprintln!(
            "[session] started session_id={}... expires_at={} infos={:?}",
            &session_id[..session_id.len().min(8)],
            expires_at,
            infos
        );
        self.session_id = Some(session_id);
        self.session_expires_at = Some(expires_at);
        self.session_infos = infos;
        Ok(())
    }

    // --- User ---

    pub async fn get_user(&self) -> Result<UserDto> {
        let resp = self.get_request("user/get").send().await?;
        let body = Self::check_response(resp).await?;
        let user: UserDto = serde_json::from_value(body["user"].clone())
            .or_else(|_| serde_json::from_value(body.clone()))?;
        Ok(user)
    }

    // --- Track ---

    pub async fn get_track(&self, track_id: i64) -> Result<TrackDto> {
        let resp = self
            .get_request("track/get")
            .query(&[("track_id", track_id.to_string())])
            .send()
            .await?;
        let body = Self::check_response(resp).await?;
        Ok(serde_json::from_value(body)?)
    }

    pub async fn get_track_url(
        &mut self,
        track_id: i64,
        format: Format,
    ) -> Result<TrackFileUrlDto> {
        self.ensure_session().await?;

        let ts = Self::ts();
        let intent = "stream";
        let mut sign_params: Vec<(&str, String)> = vec![
            ("format_id", format.id().to_string()),
            ("intent", intent.to_string()),
            ("track_id", track_id.to_string()),
        ];
        let sig = self.request_sig("fileurl", &mut sign_params, ts);

        let resp = self
            .get_request("file/url")
            .query(&[
                ("track_id", track_id.to_string()),
                ("format_id", format.id().to_string()),
                ("intent", intent.to_string()),
                ("request_ts", ts.to_string()),
                ("request_sig", sig),
            ])
            .send()
            .await?;

        let body = Self::check_response(resp).await?;
        eprintln!(
            "[file/url] response: {}",
            serde_json::to_string(&body).unwrap_or_default()
        );
        let mut url_dto: TrackFileUrlDto = serde_json::from_value(body)?;

        // Unwrap the per-track key: decrypt the CBC-wrapped key using HKDF-derived KEK.
        if let (Some(key_field), Some(infos)) = (url_dto.key.clone(), self.session_infos.as_deref())
        {
            match derive_track_key(infos, &self.app_secret, &key_field) {
                Ok(track_key) => {
                    url_dto.key = Some(hex::encode(track_key));
                    eprintln!("[key] track key unwrapped OK");
                }
                Err(e) => {
                    eprintln!("[key] track key unwrap failed: {e}");
                    url_dto.key = None; // disable decryption rather than play garbage
                }
            }
        }

        Ok(url_dto)
    }

    // --- Album ---

    pub async fn get_album(&self, album_id: &str) -> Result<AlbumDto> {
        let resp = self
            .get_request("album/get")
            .query(&[("album_id", album_id), ("limit", "50"), ("offset", "0")])
            .send()
            .await?;
        let body = Self::check_response(resp).await?;
        Ok(serde_json::from_value(body)?)
    }

    // --- Artist ---

    pub async fn get_artist_page(&self, artist_id: i64) -> Result<Value> {
        let resp = self
            .get_request("artist/page")
            .query(&[("artist_id", artist_id.to_string())])
            .send()
            .await?;
        Self::check_response(resp).await
    }

    pub async fn get_artist_releases_list(
        &self,
        artist_id: i64,
        release_type: &str,
        limit: u32,
        offset: u32,
    ) -> Result<Value> {
        let resp = self
            .get_request("artist/getReleasesList")
            .query(&[
                ("artist_id", artist_id.to_string()),
                ("release_type", release_type.to_string()),
                ("sort", "release_date".to_string()),
                ("order", "desc".to_string()),
                ("limit", limit.to_string()),
                ("offset", offset.to_string()),
            ])
            .send()
            .await?;
        Self::check_response(resp).await
    }

    // --- Browse ---

    pub async fn get_genres(&self) -> Result<Value> {
        let resp = self.get_request("genre/list").send().await?;
        Self::check_response(resp).await
    }

    pub async fn get_featured_albums(
        &self,
        genre_ids: &str,
        kind: &str,
        limit: u32,
        offset: u32,
    ) -> Result<Value> {
        let resp = self
            .get_request("album/getFeatured")
            .query(&[
                ("type", kind.to_string()),
                ("genre_id", genre_ids.to_string()),
                ("limit", limit.to_string()),
                ("offset", offset.to_string()),
            ])
            .send()
            .await?;
        Self::check_response(resp).await
    }

    pub async fn get_featured_playlists(
        &self,
        genre_ids: &str,
        kind: &str,
        limit: u32,
        offset: u32,
    ) -> Result<Value> {
        let resp = self
            .get_request("playlist/getFeatured")
            .query(&[
                ("type", kind.to_string()),
                ("genre_ids", genre_ids.to_string()),
                ("limit", limit.to_string()),
                ("offset", offset.to_string()),
            ])
            .send()
            .await?;
        Self::check_response(resp).await
    }

    pub async fn discover_playlists(
        &self,
        genre_ids: &str,
        tags: &str,
        limit: u32,
        offset: u32,
    ) -> Result<Value> {
        let mut query = vec![
            ("genre_ids", genre_ids.to_string()),
            ("limit", limit.to_string()),
            ("offset", offset.to_string()),
        ];
        if !tags.is_empty() {
            query.push(("tags", tags.to_string()));
        }

        let resp = self
            .get_request("discover/playlists")
            .query(&query)
            .send()
            .await?;
        Self::check_response(resp).await
    }

    pub async fn search_playlists(&self, query: &str, limit: u32, offset: u32) -> Result<Value> {
        let resp = self
            .get_request("playlist/search")
            .query(&[
                ("query", query.to_string()),
                ("limit", limit.to_string()),
                ("offset", offset.to_string()),
            ])
            .send()
            .await?;
        Self::check_response(resp).await
    }

    // --- Search ---

    pub async fn search(&self, query: &str, offset: u32, limit: u32) -> Result<SearchCatalogDto> {
        let (tracks_res, albums_res, artists_res) = tokio::join!(
            self.search_tracks(query, offset, limit),
            self.search_albums(query, offset, limit),
            self.search_artists(query, offset, limit),
        );

        // Convert successful Results into Some(value) and Errors into None
        Ok(SearchCatalogDto {
            query: Some(query.to_string()),
            tracks: tracks_res.ok(),
            albums: albums_res.ok(),
            artists: artists_res.ok(),
            playlists: None,
        })
    }

    pub async fn get_most_popular(
        &self,
        query: &str,
        offset: u32,
        limit: u32,
    ) -> Result<Value> {
        let resp = self
            .get_request("most-popular/get")
            .query(&[
                ("query", query.to_string()),
                ("offset", offset.to_string()),
                ("limit", limit.to_string()),
            ])
            .send()
            .await?;
        Self::check_response(resp).await
    }

    pub async fn get_dynamic_suggestions(
        &self,
        limit: u32,
        listened_tracks_ids: Value,
        tracks_to_analyze: Value,
    ) -> Result<Value> {
        let resp = self
            .post_request("dynamic/suggest")
            .json(&serde_json::json!({
                "limit": limit,
                "listened_tracks_ids": listened_tracks_ids,
                "track_to_analysed": tracks_to_analyze,
            }))
            .send()
            .await?;
        Self::check_response(resp).await
    }

    async fn search_tracks(
        &self,
        query: &str,
        offset: u32,
        limit: u32,
    ) -> Result<SearchResultItems<TrackDto>> {
        let resp = self
            .get_request("track/search")
            .query(&[
                ("query", query),
                ("offset", &offset.to_string()),
                ("limit", &limit.to_string()),
            ])
            .send()
            .await?;
        let body = Self::check_response(resp).await?;
        Ok(serde_json::from_value(body["tracks"].clone())?)
    }

    async fn search_albums(
        &self,
        query: &str,
        offset: u32,
        limit: u32,
    ) -> Result<SearchResultItems<AlbumDto>> {
        let resp = self
            .get_request("album/search")
            .query(&[
                ("query", query),
                ("offset", &offset.to_string()),
                ("limit", &limit.to_string()),
            ])
            .send()
            .await?;
        let body = Self::check_response(resp).await?;
        Ok(serde_json::from_value(body["albums"].clone())?)
    }

    async fn search_artists(
        &self,
        query: &str,
        offset: u32,
        limit: u32,
    ) -> Result<SearchResultItems<ArtistDto>> {
        let resp = self
            .get_request("artist/search")
            .query(&[
                ("query", query),
                ("offset", &offset.to_string()),
                ("limit", &limit.to_string()),
            ])
            .send()
            .await?;
        let body = Self::check_response(resp).await?;
        Ok(serde_json::from_value(body["artists"].clone())?)
    }

    // --- Favorites / Library ---

    pub async fn get_user_playlists(&self, offset: u32, limit: u32) -> Result<UserPlaylistsDto> {
        let resp = self
            .get_request("playlist/getUserPlaylists")
            .query(&[
                ("filter", "owner,subscriber"),
                ("offset", &offset.to_string()),
                ("limit", &limit.to_string()),
            ])
            .send()
            .await?;
        let body = Self::check_response(resp).await?;
        Ok(serde_json::from_value(body)?)
    }

    pub async fn get_playlist(
        &self,
        playlist_id: i64,
        offset: u32,
        limit: u32,
    ) -> Result<PlaylistDto> {
        let resp = self
            .get_request("playlist/get")
            .query(&[
                ("playlist_id", &playlist_id.to_string()),
                ("extra", &"tracks".to_string()),
                ("offset", &offset.to_string()),
                ("limit", &limit.to_string()),
            ])
            .send()
            .await?;
        let body = Self::check_response(resp).await?;
        Ok(serde_json::from_value(body)?)
    }

    pub async fn get_playlist_all(&self, playlist_id: i64) -> Result<PlaylistDto> {
        const PAGE_LIMIT: u32 = 500;

        let mut playlist = self.get_playlist(playlist_id, 0, PAGE_LIMIT).await?;

        let mut all_items = playlist
            .tracks
            .as_ref()
            .and_then(|t| t.items.clone())
            .unwrap_or_default();

        let mut total = playlist
            .tracks
            .as_ref()
            .and_then(|t| t.total)
            .unwrap_or(all_items.len() as i32);
        if total < all_items.len() as i32 {
            total = all_items.len() as i32;
        }

        let mut offset = all_items.len() as u32;
        while (offset as i32) < total {
            let page = self.get_playlist(playlist_id, offset, PAGE_LIMIT).await?;
            let mut page_items = page
                .tracks
                .as_ref()
                .and_then(|t| t.items.clone())
                .unwrap_or_default();
            if page_items.is_empty() {
                break;
            }
            all_items.append(&mut page_items);
            offset = all_items.len() as u32;
        }

        if let Some(tracks) = playlist.tracks.as_mut() {
            tracks.items = Some(all_items);
            tracks.total = Some(total);
            tracks.offset = Some(0);
            tracks.limit = Some(PAGE_LIMIT as i32);
        }

        Ok(playlist)
    }

    /// Fetch all favorite IDs (tracks, albums, artists) in one call.
    async fn get_fav_ids(&self) -> Result<FavIdsDto> {
        let resp = self
            .get_request("favorite/getUserFavoriteIds")
            .send()
            .await?;
        let body = Self::check_response(resp).await?;
        Ok(serde_json::from_value(body)?)
    }

    /// Batch-fetch tracks by ID via POST /track/getList.
    async fn get_tracks_by_ids(&self, ids: &[i64]) -> Result<Vec<TrackDto>> {
        if ids.is_empty() {
            return Ok(vec![]);
        }
        let resp = self
            .post_request("track/getList")
            .json(&serde_json::json!({ "tracks_id": ids }))
            .send()
            .await?;
        let body = Self::check_response(resp).await?;
        let items: Vec<TrackDto> =
            serde_json::from_value(body["tracks"]["items"].clone()).unwrap_or_default();
        Ok(items)
    }

    pub async fn get_fav_tracks(
        &self,
        offset: u32,
        limit: u32,
    ) -> Result<SearchResultItems<TrackDto>> {
        let ids = self.get_fav_ids().await?;
        let all_ids = ids.tracks.unwrap_or_default();
        let total = all_ids.len() as i32;
        let page: Vec<i64> = all_ids
            .into_iter()
            .skip(offset as usize)
            .take(limit as usize)
            .collect();
        let items = self.get_tracks_by_ids(&page).await?;
        Ok(SearchResultItems {
            items: Some(items),
            total: Some(total),
            offset: Some(offset as i32),
            limit: Some(limit as i32),
        })
    }

    pub async fn get_fav_albums(
        &self,
        offset: u32,
        limit: u32,
    ) -> Result<SearchResultItems<AlbumDto>> {
        let ids = self.get_fav_ids().await?;
        let all_ids = ids.albums.unwrap_or_default();
        let total = all_ids.len() as i32;
        let page: Vec<&str> = all_ids
            .iter()
            .skip(offset as usize)
            .take(limit as usize)
            .map(|s| s.as_str())
            .collect();
        let mut items = Vec::with_capacity(page.len());
        for album_id in page {
            match self.get_album(album_id).await {
                Ok(a) => items.push(a),
                Err(e) => eprintln!("[fav] failed to fetch album {}: {}", album_id, e),
            }
        }
        Ok(SearchResultItems {
            items: Some(items),
            total: Some(total),
            offset: Some(offset as i32),
            limit: Some(limit as i32),
        })
    }

    pub async fn get_fav_artists(
        &self,
        offset: u32,
        limit: u32,
    ) -> Result<SearchResultItems<FavArtistDto>> {
        let ids = self.get_fav_ids().await?;
        let all_ids = ids.artists.unwrap_or_default();
        let total = all_ids.len() as i32;
        let page: Vec<i64> = all_ids
            .into_iter()
            .skip(offset as usize)
            .take(limit as usize)
            .collect();
        let mut items = Vec::with_capacity(page.len());
        for artist_id in page {
            match self.get_artist_page(artist_id).await {
                Ok(v) => {
                    let id = v.get("id").and_then(|i| i.as_i64());
                    let name = v
                        .get("name")
                        .and_then(|n| n.get("display"))
                        .and_then(|d| d.as_str())
                        .map(|s| s.to_string());

                    let mut image_dto = None;
                    if let Some(imgs) = v.get("images") {
                        if let Some(portrait) = imgs.get("portrait") {
                            if let (Some(hash), Some(format)) = (
                                portrait.get("hash").and_then(|h| h.as_str()),
                                portrait.get("format").and_then(|f| f.as_str()),
                            ) {
                                image_dto = Some(ImageDto {
                                    small: Some(format!("https://static.qobuz.com/images/artists/covers/small/{}.{}", hash, format)),
                                    thumbnail: Some(format!("https://static.qobuz.com/images/artists/covers/small/{}.{}", hash, format)),
                                    large: Some(format!("https://static.qobuz.com/images/artists/covers/large/{}.{}", hash, format)),
                                    back: None,
                                });
                            }
                        }
                    }

                    if id.is_some() && name.is_some() {
                        items.push(FavArtistDto {
                            id,
                            name,
                            albums_count: v
                                .get("albums_count")
                                .and_then(|c| c.as_i64())
                                .map(|c| c as i32),
                            image: image_dto,
                        });
                    }
                }
                Err(e) => eprintln!("[fav] failed to fetch artist {}: {}", artist_id, e),
            }
        }
        Ok(SearchResultItems {
            items: Some(items),
            total: Some(total),
            offset: Some(offset as i32),
            limit: Some(limit as i32),
        })
    }

    // --- Playlist management ---

    pub async fn create_playlist(&self, name: &str) -> Result<PlaylistDto> {
        let resp = self
            .post_request("playlist/create")
            .form(&[
                ("name", name),
                ("is_public", "false"),
                ("is_collaborative", "false"),
            ])
            .send()
            .await?;
        let body = Self::check_response(resp).await?;
        Ok(serde_json::from_value(body)?)
    }

    pub async fn add_track_to_playlist(&self, playlist_id: i64, track_id: i64) -> Result<()> {
        let resp = self
            .post_request("playlist/addTracks")
            .form(&[
                ("playlist_id", playlist_id.to_string()),
                ("track_ids", track_id.to_string()),
                ("no_duplicate", "true".to_string()),
            ])
            .send()
            .await?;
        Self::check_response(resp).await?;
        Ok(())
    }

    pub async fn delete_playlist(&self, playlist_id: i64) -> Result<()> {
        let resp = self
            .get_request("playlist/delete")
            .query(&[("playlist_id", &playlist_id.to_string())])
            .send()
            .await?;
        Self::check_response(resp).await?;
        Ok(())
    }

    pub async fn delete_track_from_playlist(
        &self,
        playlist_id: i64,
        playlist_track_id: i64,
    ) -> Result<()> {
        let resp = self
            .post_request("playlist/deleteTracks")
            .form(&[
                ("playlist_id", playlist_id.to_string()),
                ("playlist_track_ids", playlist_track_id.to_string()),
            ])
            .send()
            .await?;
        Self::check_response(resp).await?;
        Ok(())
    }

    pub async fn subscribe_playlist(&self, playlist_id: i64) -> Result<()> {
        let resp = self
            .get_request("playlist/subscribe")
            .query(&[("playlist_id", playlist_id.to_string())])
            .send()
            .await?;
        Self::check_response(resp).await?;
        Ok(())
    }

    pub async fn unsubscribe_playlist(&self, playlist_id: i64) -> Result<()> {
        let resp = self
            .get_request("playlist/unsubscribe")
            .query(&[("playlist_id", playlist_id.to_string())])
            .send()
            .await?;
        Self::check_response(resp).await?;
        Ok(())
    }

    pub async fn add_fav_track(&self, track_id: i64) -> Result<()> {
        let resp = self
            .get_request("favorite/create")
            .query(&[("type", "tracks"), ("track_ids", &track_id.to_string())])
            .send()
            .await?;
        Self::check_response(resp).await?;
        Ok(())
    }

    pub async fn remove_fav_track(&self, track_id: i64) -> Result<()> {
        let resp = self
            .get_request("favorite/delete")
            .query(&[("type", "tracks"), ("track_ids", &track_id.to_string())])
            .send()
            .await?;
        Self::check_response(resp).await?;
        Ok(())
    }

    pub async fn add_fav_album(&self, album_id: &str) -> Result<()> {
        let resp = self
            .get_request("favorite/create")
            .query(&[("type", "albums"), ("album_ids", album_id)])
            .send()
            .await?;
        Self::check_response(resp).await?;
        Ok(())
    }

    pub async fn remove_fav_album(&self, album_id: &str) -> Result<()> {
        let resp = self
            .get_request("favorite/delete")
            .query(&[("type", "albums"), ("album_ids", album_id)])
            .send()
            .await?;
        Self::check_response(resp).await?;
        Ok(())
    }

    pub async fn add_fav_artist(&self, artist_id: i64) -> Result<()> {
        let resp = self
            .get_request("favorite/create")
            .query(&[("type", "artists"), ("artist_ids", &artist_id.to_string())])
            .send()
            .await?;
        Self::check_response(resp).await?;
        Ok(())
    }

    pub async fn remove_fav_artist(&self, artist_id: i64) -> Result<()> {
        let resp = self
            .get_request("favorite/delete")
            .query(&[("type", "artists"), ("artist_ids", &artist_id.to_string())])
            .send()
            .await?;
        Self::check_response(resp).await?;
        Ok(())
    }
}
