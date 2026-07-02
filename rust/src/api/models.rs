use serde::{Deserialize, Serialize};

// --- Auth ---

#[derive(Debug, Deserialize, Clone, Serialize)]
pub struct OAuthDto {
    pub token_type: Option<String>,
    pub access_token: Option<String>,
    pub refresh_token: Option<String>,
    pub expires_in: Option<i64>,
}

#[derive(Debug, Deserialize, Serialize)]
pub struct OAuthLoginResponse {
    pub status: Option<String>,
    pub user: Option<UserDto>,
    pub oauth2: Option<OAuthDto>,
    pub user_auth_token: Option<String>,
}

// --- User ---

#[derive(Debug, Deserialize, Clone, Serialize)]
pub struct UserDto {
    pub id: Option<i64>,
    pub login: Option<String>,
    pub firstname: Option<String>,
    pub lastname: Option<String>,
    pub email: Option<String>,
    pub display_name: Option<String>,
    pub country_code: Option<String>,
    pub subscription: Option<SubscriptionDto>,
}

#[derive(Debug, Deserialize, Clone, Serialize)]
pub struct SubscriptionDto {
    pub description: Option<String>,
    pub end_date: Option<String>,
    pub is_recurring: Option<bool>,
    pub offer: Option<String>,
}

// --- Track ---

#[derive(Debug, Deserialize, Clone, Serialize)]
pub struct TrackDto {
    pub id: i64,
    pub title: Option<String>,
    pub version: Option<String>,
    pub duration: Option<i64>,
    pub track_number: Option<i32>,
    pub playlist_track_id: Option<i64>,
    pub album: Option<AlbumDto>,
    pub performer: Option<ArtistDto>,
    pub composer: Option<ArtistDto>,
    pub work: Option<String>,
    pub media_number: Option<i32>,
    pub parental_warning: Option<bool>,
    pub streamable: Option<bool>,
    pub purchasable: Option<bool>,
    pub hires: Option<bool>,
    pub hires_streamable: Option<bool>,
    pub audio_info: Option<AudioInfoDto>,
    pub maximum_bit_depth: Option<i32>,
    pub maximum_sampling_rate: Option<f64>,
    pub maximum_channel_count: Option<i32>,
}

#[derive(Debug, Deserialize, Clone, Serialize)]
pub struct AudioInfoDto {
    pub replaygain_track_gain: Option<f64>,
    pub replaygain_track_peak: Option<f64>,
}

#[derive(Debug, Deserialize, Clone, Serialize)]
pub struct TrackFileUrlDto {
    pub track_id: Option<i64>,
    pub duration: Option<f64>,
    pub url: Option<String>,
    pub url_template: Option<String>,
    pub n_segments: Option<u32>,
    pub format_id: Option<i32>,
    pub mime_type: Option<String>,
    pub sampling_rate: Option<f64>,
    pub bit_depth: Option<i32>,
    /// qbz-1 encryption key: "qbz-1.<base64url_aes_key>.<base64url_iv>"
    pub key: Option<String>,
}

// --- Album ---

#[derive(Debug, Deserialize, Clone, Serialize)]
pub struct AlbumDto {
    pub id: Option<String>,
    pub title: Option<String>,
    pub artist: Option<ArtistDto>,
    pub tracks_count: Option<i32>,
    pub duration: Option<i64>,
    pub genre: Option<GenreDto>,
    pub image: Option<ImageDto>,
    pub label: Option<LabelDto>,
    pub release_date_original: Option<String>,
    pub maximum_bit_depth: Option<i32>,
    pub maximum_sampling_rate: Option<f64>,
    pub hires_streamable: Option<bool>,
    pub streamable: Option<bool>,
    pub release_type: Option<String>,
    pub tracks: Option<TracksWrapper>,
}

#[derive(Debug, Deserialize, Clone, Serialize)]
pub struct TracksWrapper {
    pub items: Option<Vec<TrackDto>>,
    pub total: Option<i32>,
    pub offset: Option<i32>,
    pub limit: Option<i32>,
}

// --- Artist ---

#[derive(Debug, Deserialize, Clone, Serialize)]
pub struct ArtistDto {
    pub id: Option<i64>,
    pub name: Option<String>,
    pub albums_count: Option<i32>,
    pub image: Option<ImageDto>,
    pub biography: Option<BiographyDto>,
    pub albums: Option<SearchResultItems<AlbumDto>>,
    #[serde(rename = "epSingles")]
    pub ep_singles: Option<SearchResultItems<AlbumDto>>,
    #[serde(rename = "liveAlbums")]
    pub live_albums: Option<SearchResultItems<AlbumDto>>,
}

#[derive(Debug, Deserialize, Clone, Serialize)]
pub struct BiographyDto {
    pub content: Option<String>,
    pub summary: Option<String>,
}

// --- Genre ---

#[derive(Debug, Deserialize, Clone, Serialize)]
pub struct GenreDto {
    pub id: Option<i64>,
    pub name: Option<String>,
    pub slug: Option<String>,
}

// --- Image ---

#[derive(Debug, Deserialize, Clone, Serialize)]
pub struct ImageDto {
    pub small: Option<String>,
    pub thumbnail: Option<String>,
    pub large: Option<String>,
    pub back: Option<String>,
}

// --- Label ---

#[derive(Debug, Deserialize, Clone, Serialize)]
pub struct LabelDto {
    pub id: Option<i64>,
    pub name: Option<String>,
}

// --- Search ---

#[derive(Debug, Deserialize, Serialize)]
pub struct SearchCatalogDto {
    pub query: Option<String>,
    pub albums: Option<SearchResultItems<AlbumDto>>,
    pub tracks: Option<SearchResultItems<TrackDto>>,
    pub artists: Option<SearchResultItems<ArtistDto>>,
    pub playlists: Option<SearchResultItems<PlaylistDto>>,
}

#[derive(Debug, Deserialize, Clone, Serialize)]
pub struct SearchResultItems<T> {
    pub items: Option<Vec<T>>,
    pub total: Option<i32>,
    pub offset: Option<i32>,
    pub limit: Option<i32>,
}

// --- Playlist ---

#[derive(Debug, Deserialize, Clone, Serialize)]
pub struct PlaylistDto {
    pub id: Option<i64>,
    pub name: Option<String>,
    pub tracks_count: Option<i32>,
    pub duration: Option<i64>,
    pub description: Option<String>,
    pub owner: Option<PlaylistOwnerDto>,
    /// 4-cover mosaic at 300 px (preferred)
    pub images300: Option<Vec<String>>,
    /// 4-cover mosaic at 150 px (fallback)
    pub images150: Option<Vec<String>>,
    /// 4-cover mosaic at 50 px (last resort)
    pub images: Option<Vec<String>>,
    pub tracks: Option<TracksWrapper>,
}

#[derive(Debug, Deserialize, Clone, Serialize)]
pub struct PlaylistOwnerDto {
    pub id: Option<i64>,
    pub name: Option<String>,
}

// --- User library ---

#[derive(Debug, Deserialize, Serialize)]
pub struct UserPlaylistsDto {
    pub playlists: Option<SearchResultItems<PlaylistDto>>,
}

#[derive(Debug, Deserialize, Serialize)]
pub struct FavArtistDto {
    pub id: Option<i64>,
    pub name: Option<String>,
    pub albums_count: Option<i32>,
    pub image: Option<ImageDto>,
}

// --- Favorite IDs ---

#[derive(Debug, Deserialize, Serialize)]
pub struct FavIdsDto {
    pub tracks: Option<Vec<i64>>,
    pub albums: Option<Vec<String>>,
    pub artists: Option<Vec<i64>>,
}

// --- Format ---

#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize)]
pub enum Format {
    Mp3 = 5,
    Cd = 6,
    HiRes96 = 7,
    HiRes192 = 27,
}

impl Format {
    pub fn id(self) -> i32 {
        self as i32
    }

    pub fn from_id(id: i32) -> Self {
        match id {
            5 => Format::Mp3,
            6 => Format::Cd,
            7 => Format::HiRes96,
            27 => Format::HiRes192,
            _ => Format::Cd,
        }
    }

}
