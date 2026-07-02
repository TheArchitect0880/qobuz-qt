use std::path::{Path, PathBuf};
use std::sync::{atomic::{AtomicBool, Ordering}, Arc};

use anyhow::{Context, Result};
use id3::{frame::{Comment, Picture, PictureType}, TagLike, Version};
use metaflac::block::PictureType as FlacPictureType;
use serde::{Deserialize, Serialize};
use tokio::fs;
use tokio::io::AsyncWriteExt;

use crate::api::{AlbumDto, Format, GenreDto, TrackDto, TrackFileUrlDto};

const DEFAULT_FOLDER_FORMAT: &str = "{albumartist} - {title} ({year}) [{container}] [{bit_depth}B-{sampling_rate}kHz]";
const DEFAULT_TRACK_FORMAT: &str = "{tracknumber:02}. {artist} - {title}{explicit}";
pub const CANCELLED_ERROR: &str = "download cancelled";

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct DownloadSettings {
    pub folder: String,
    pub source_subdirectories: bool,
    pub disc_subdirectories: bool,
    pub add_singles_to_folder: bool,
    pub renumber_playlist_tracks: bool,
    pub set_playlist_to_album: bool,
    pub restrict_characters: bool,
    pub truncate_to: usize,
    pub folder_format: String,
    pub track_format: String,
    pub save_artwork: bool,
    pub embed_artwork: bool,
}

impl Default for DownloadSettings {
    fn default() -> Self {
        let folder = dirs::home_dir()
            .map(|p| p.join("StreamripDownloads").to_string_lossy().into_owned())
            .unwrap_or_else(|| "StreamripDownloads".to_string());
        Self {
            folder,
            source_subdirectories: false,
            disc_subdirectories: true,
            add_singles_to_folder: false,
            renumber_playlist_tracks: true,
            set_playlist_to_album: true,
            restrict_characters: false,
            truncate_to: 120,
            folder_format: DEFAULT_FOLDER_FORMAT.to_string(),
            track_format: DEFAULT_TRACK_FORMAT.to_string(),
            save_artwork: true,
            embed_artwork: true,
        }
    }
}

impl DownloadSettings {
    pub fn normalized(mut self) -> Self {
        let defaults = Self::default();
        if self.folder.trim().is_empty() {
            self.folder = defaults.folder;
        }
        if self.folder_format.trim().is_empty() {
            self.folder_format = defaults.folder_format;
        }
        if self.track_format.trim().is_empty() {
            self.track_format = defaults.track_format;
        }
        self
    }

    pub fn base_folder(&self, source: &str) -> PathBuf {
        let mut base = PathBuf::from(self.folder.trim());
        if self.source_subdirectories {
            base.push(title_case(source));
        }
        base
    }
}

#[derive(Debug, Clone)]
pub struct AudioProfile {
    pub container: String,
    pub bit_depth: i32,
    pub sampling_rate: String,
}

#[derive(Debug, Clone)]
pub struct TagMetadata {
    pub title: String,
    pub artist: String,
    pub album: String,
    pub album_artist: String,
    pub track_number: i32,
    pub track_total: i32,
    pub disc_number: i32,
    pub disc_total: i32,
    pub year: String,
    pub genre: Option<String>,
    pub composer: Option<String>,
    pub comment: Option<String>,
}

pub fn build_audio_profile(url: &TrackFileUrlDto, album: Option<&AlbumDto>, format: Format) -> AudioProfile {
    let bit_depth = url
        .bit_depth
        .or_else(|| album.and_then(|a| a.maximum_bit_depth))
        .unwrap_or(match format {
            Format::Mp3 | Format::Cd => 16,
            Format::HiRes96 | Format::HiRes192 => 24,
        });

    let sampling_rate = rate_string(
        url.sampling_rate
            .or_else(|| album.and_then(|a| a.maximum_sampling_rate))
            .unwrap_or(match format {
                Format::Mp3 | Format::Cd => 44.1,
                Format::HiRes96 => 96.0,
                Format::HiRes192 => 192.0,
            }),
    );

    AudioProfile {
        container: container_from_url_dto(url, format),
        bit_depth,
        sampling_rate,
    }
}

pub fn build_tag_metadata(
    settings: &DownloadSettings,
    track: &TrackDto,
    title: &str,
    album_disc_total: i32,
    track_total: i32,
    playlist_name: Option<&str>,
    playlist_position: Option<usize>,
) -> TagMetadata {
    let artist = track
        .performer
        .as_ref()
        .and_then(|a| a.name.as_deref())
        .unwrap_or("Unknown")
        .to_string();
    let track_album = track.album.as_ref();
    let mut album = track_album
        .and_then(|a| a.title.as_deref())
        .unwrap_or("Unknown")
        .to_string();
    let mut album_artist = track_album
        .and_then(|a| a.artist.as_ref())
        .and_then(|a| a.name.as_deref())
        .unwrap_or(artist.as_str())
        .to_string();

    if let Some(playlist_name) = playlist_name.filter(|_| settings.set_playlist_to_album) {
        album = playlist_name.to_string();
        album_artist = "Various Artists".to_string();
    }

    let track_number = playlist_position
        .filter(|_| settings.renumber_playlist_tracks)
        .map(|p| p as i32)
        .unwrap_or_else(|| track.track_number.unwrap_or(0));

    TagMetadata {
        title: title.to_string(),
        artist,
        album,
        album_artist,
        track_number,
        track_total,
        disc_number: track.media_number.unwrap_or(1).max(1),
        disc_total: album_disc_total.max(1),
        year: track_album
            .and_then(|a| a.release_date_original.as_deref())
            .map(year_from_date)
            .unwrap_or_else(|| "Unknown".to_string()),
        genre: track_album.and_then(|a| genre_name(a.genre.as_ref())),
        composer: track
            .composer
            .as_ref()
            .and_then(|c| c.name.as_deref())
            .map(ToOwned::to_owned),
        comment: Some("Downloaded with qobuz-qt".to_string()),
    }
}

pub fn album_folder_path(
    settings: &DownloadSettings,
    source: &str,
    album_id: &str,
    album_title: &str,
    album_artist: &str,
    year: &str,
    audio: &AudioProfile,
) -> PathBuf {
    let values = [
        ("albumartist", album_artist.to_string()),
        ("title", album_title.to_string()),
        ("year", year.to_string()),
        ("bit_depth", audio.bit_depth.to_string()),
        ("sampling_rate", audio.sampling_rate.clone()),
        ("id", album_id.to_string()),
        ("container", audio.container.clone()),
        ("codec", audio.container.clone()),
        ("quality", audio.container.clone()),
        ("bitrate", "Unknown".to_string()),
        ("albumcomposer", "Unknown".to_string()),
    ];
    let folder_name = clean_name(
        &format_template(&settings.folder_format, &values),
        settings.restrict_characters,
        settings.truncate_to,
    );
    settings.base_folder(source).join(folder_name)
}

pub fn playlist_folder_path(settings: &DownloadSettings, source: &str, playlist_name: &str) -> PathBuf {
    settings.base_folder(source).join(clean_name(
        playlist_name,
        settings.restrict_characters,
        settings.truncate_to,
    ))
}

pub fn track_output_path(
    settings: &DownloadSettings,
    source: &str,
    track: &TrackDto,
    title: &str,
    ext: &str,
    album_folder: Option<&Path>,
    album_disc_total: i32,
    playlist_position: Option<usize>,
    audio: Option<&AudioProfile>,
) -> PathBuf {
    let mut base = settings.base_folder(source);

    if album_folder.is_none() && settings.add_singles_to_folder {
        if let (Some(album), Some(audio)) = (track.album.as_ref(), audio) {
            let album_title = album.title.as_deref().unwrap_or("Unknown");
            let album_id = album.id.as_deref().unwrap_or("Unknown");
            let album_artist = album
                .artist
                .as_ref()
                .and_then(|a| a.name.as_deref())
                .or_else(|| track.performer.as_ref().and_then(|a| a.name.as_deref()))
                .unwrap_or("Unknown");
            let year = album
                .release_date_original
                .as_deref()
                .map(year_from_date)
                .unwrap_or_else(|| "Unknown".to_string());
            base = album_folder_path(settings, source, album_id, album_title, album_artist, &year, audio);
        }
    } else if let Some(folder) = album_folder {
        base = folder.to_path_buf();
        if settings.disc_subdirectories && album_disc_total > 1 {
            let disc_number = track.media_number.unwrap_or(1).max(1);
            base.push(format!("Disc {}", disc_number));
        }
    }

    let track_number = playlist_position
        .filter(|_| settings.renumber_playlist_tracks)
        .map(|p| p as i32)
        .unwrap_or_else(|| track.track_number.unwrap_or(0));
    let artist = track
        .performer
        .as_ref()
        .and_then(|a| a.name.as_deref())
        .unwrap_or("Unknown");
    let album_artist = track
        .album
        .as_ref()
        .and_then(|a| a.artist.as_ref())
        .and_then(|a| a.name.as_deref())
        .unwrap_or(artist);
    let values = [
        ("id", track.id.to_string()),
        ("tracknumber", track_number.to_string()),
        ("artist", artist.to_string()),
        ("albumartist", album_artist.to_string()),
        (
            "composer",
            track
                .composer
                .as_ref()
                .and_then(|c| c.name.as_deref())
                .unwrap_or("Unknown")
                .to_string(),
        ),
        ("title", title.to_string()),
        ("albumcomposer", "Unknown".to_string()),
        ("explicit", explicit_suffix(track).to_string()),
    ];
    let file_name = clean_name(
        &format_template(&settings.track_format, &values),
        settings.restrict_characters,
        settings.truncate_to,
    );
    base.join(format!("{}.{}", file_name, ext))
}

pub fn cover_art_url(album: Option<&AlbumDto>) -> Option<String> {
    album
        .and_then(|a| a.image.as_ref())
        .and_then(|img| img.large.clone().or_else(|| img.small.clone()))
        .filter(|s| !s.trim().is_empty())
}

pub fn track_title(track: &TrackDto) -> String {
    let title = track.title.as_deref().unwrap_or("Unknown").trim();
    let version = track.version.as_deref().unwrap_or("").trim();
    if version.is_empty() {
        title.to_string()
    } else {
        format!("{} ({})", title, version)
    }
}

pub fn file_extension(url: &TrackFileUrlDto, format: Format) -> String {
    let candidates = [url.url.as_deref(), url.url_template.as_deref()];
    for candidate in candidates.into_iter().flatten() {
        let trimmed = candidate.split('?').next().unwrap_or(candidate);
        if trimmed.ends_with(".flac") {
            return "flac".to_string();
        }
        if trimmed.ends_with(".mp3") {
            return "mp3".to_string();
        }
        if trimmed.ends_with(".m4a") {
            return "m4a".to_string();
        }
    }

    let mime = url.mime_type.as_deref().unwrap_or("").to_ascii_lowercase();
    if mime.contains("flac") {
        return "flac".to_string();
    }
    if mime.contains("mpeg") || mime.contains("mp3") {
        return "mp3".to_string();
    }

    match url.format_id.unwrap_or(format.id()) {
        5 => "mp3".to_string(),
        _ => "flac".to_string(),
    }
}

pub async fn download_to_file<F>(url: &str, output: &Path, cancel: Option<&Arc<AtomicBool>>, mut on_progress: F) -> Result<()>
where
    F: FnMut(u64, Option<u64>),
{
    let client = reqwest::Client::new();
    let mut response = client
        .get(url)
        .send()
        .await
        .with_context(|| format!("request failed for {url}"))?
        .error_for_status()
        .with_context(|| format!("bad response for {url}"))?;

    let total = response.content_length();
    if let Some(parent) = output.parent() {
        fs::create_dir_all(parent).await?;
    }

    let file_name = output
        .file_name()
        .and_then(|name| name.to_str())
        .unwrap_or("download");
    let tmp_path = output.with_file_name(format!("{}.part", file_name));
    if fs::try_exists(&tmp_path).await.unwrap_or(false) {
        let _ = fs::remove_file(&tmp_path).await;
    }

    let mut file = fs::File::create(&tmp_path).await?;
    let mut downloaded = 0u64;
    while let Some(chunk) = response.chunk().await? {
        if cancel.is_some_and(|flag| flag.load(Ordering::Relaxed)) {
            drop(file);
            let _ = fs::remove_file(&tmp_path).await;
            anyhow::bail!(CANCELLED_ERROR);
        }
        file.write_all(&chunk).await?;
        downloaded += chunk.len() as u64;
        on_progress(downloaded, total);
    }
    file.flush().await?;
    drop(file);

    if fs::try_exists(output).await.unwrap_or(false) {
        let _ = fs::remove_file(output).await;
    }
    fs::rename(&tmp_path, output).await?;
    Ok(())
}

pub async fn download_artwork(url: &str, folder: &Path) -> Result<Option<PathBuf>> {
    let output = folder.join("cover.jpg");
    if fs::try_exists(&output).await.unwrap_or(false) {
        return Ok(Some(output));
    }
    download_to_file(url, &output, None, |_, _| {}).await?;
    Ok(Some(output))
}

pub fn tag_audio_file(path: &Path, ext: &str, meta: &TagMetadata, cover_path: Option<&Path>) -> Result<()> {
    match ext {
        "flac" => tag_flac_file(path, meta, cover_path),
        "mp3" => tag_mp3_file(path, meta, cover_path),
        _ => Ok(()),
    }
}

pub fn year_from_date(date: &str) -> String {
    let prefix: String = date.chars().take(4).collect();
    if prefix.len() == 4 && prefix.chars().all(|ch| ch.is_ascii_digit()) {
        prefix
    } else {
        "Unknown".to_string()
    }
}

pub fn max_disc_number(tracks: &[TrackDto]) -> i32 {
    tracks
        .iter()
        .map(|track| track.media_number.unwrap_or(1).max(1))
        .max()
        .unwrap_or(1)
}

fn tag_mp3_file(path: &Path, meta: &TagMetadata, cover_path: Option<&Path>) -> Result<()> {
    let mut tag = id3::Tag::read_from_path(path).unwrap_or_else(|_| id3::Tag::new());
    tag.set_title(&meta.title);
    tag.set_artist(&meta.artist);
    tag.set_album(&meta.album);
    tag.set_album_artist(&meta.album_artist);
    if meta.track_number > 0 {
        tag.set_track(meta.track_number as u32);
    }
    if meta.track_total > 0 {
        tag.set_total_tracks(meta.track_total as u32);
    }
    if meta.disc_number > 0 {
        tag.set_disc(meta.disc_number as u32);
    }
    if meta.disc_total > 0 {
        tag.set_total_discs(meta.disc_total as u32);
    }
    if let Ok(year) = meta.year.parse::<i32>() {
        tag.set_year(year);
    }
    if let Some(genre) = &meta.genre {
        tag.set_genre(genre);
    }
    if let Some(composer) = &meta.composer {
        tag.set_text("TCOM", composer.clone());
    }
    if let Some(comment) = &meta.comment {
        tag.add_frame(Comment {
            lang: "eng".to_string(),
            description: String::new(),
            text: comment.clone(),
        });
    }
    tag.remove_all_pictures();
    if let Some(cover_path) = cover_path {
        let data = std::fs::read(cover_path)?;
        tag.add_frame(Picture {
            mime_type: "image/jpeg".to_string(),
            picture_type: PictureType::CoverFront,
            description: String::new(),
            data,
        });
    }
    tag.write_to_path(path, Version::Id3v24)?;
    Ok(())
}

fn tag_flac_file(path: &Path, meta: &TagMetadata, cover_path: Option<&Path>) -> Result<()> {
    let mut tag = metaflac::Tag::read_from_path(path).unwrap_or_else(|_| metaflac::Tag::new());
    tag.set_vorbis("TITLE", vec![meta.title.clone()]);
    tag.set_vorbis("ARTIST", vec![meta.artist.clone()]);
    tag.set_vorbis("ALBUM", vec![meta.album.clone()]);
    tag.set_vorbis("ALBUMARTIST", vec![meta.album_artist.clone()]);
    if meta.track_number > 0 {
        tag.set_vorbis("TRACKNUMBER", vec![meta.track_number.to_string()]);
    }
    if meta.track_total > 0 {
        tag.set_vorbis("TRACKTOTAL", vec![meta.track_total.to_string()]);
    }
    if meta.disc_number > 0 {
        tag.set_vorbis("DISCNUMBER", vec![meta.disc_number.to_string()]);
    }
    if meta.disc_total > 0 {
        tag.set_vorbis("DISCTOTAL", vec![meta.disc_total.to_string()]);
    }
    if meta.year != "Unknown" {
        tag.set_vorbis("DATE", vec![meta.year.clone()]);
    }
    if let Some(genre) = &meta.genre {
        tag.set_vorbis("GENRE", vec![genre.clone()]);
    }
    if let Some(composer) = &meta.composer {
        tag.set_vorbis("COMPOSER", vec![composer.clone()]);
    }
    if let Some(comment) = &meta.comment {
        tag.set_vorbis("COMMENT", vec![comment.clone()]);
    }
    tag.remove_picture_type(FlacPictureType::CoverFront);
    if let Some(cover_path) = cover_path {
        let data = std::fs::read(cover_path)?;
        tag.add_picture("image/jpeg", FlacPictureType::CoverFront, data);
    }
    tag.write_to_path(path)?;
    Ok(())
}

fn genre_name(genre: Option<&GenreDto>) -> Option<String> {
    genre.and_then(|g| g.name.clone()).filter(|s| !s.trim().is_empty())
}

fn explicit_suffix(track: &TrackDto) -> &'static str {
    if track.parental_warning.unwrap_or(false) {
        " (Explicit)"
    } else {
        ""
    }
}

fn title_case(source: &str) -> String {
    let mut chars = source.chars();
    match chars.next() {
        Some(first) => first.to_ascii_uppercase().to_string() + chars.as_str(),
        None => String::new(),
    }
}

fn container_from_url_dto(url: &TrackFileUrlDto, format: Format) -> String {
    match file_extension(url, format).as_str() {
        "mp3" => "MP3".to_string(),
        "m4a" => "M4A".to_string(),
        _ => "FLAC".to_string(),
    }
}

fn rate_string(rate: f64) -> String {
    if rate.fract() == 0.0 {
        format!("{}", rate as i32)
    } else {
        let mut s = format!("{rate:.4}");
        while s.contains('.') && s.ends_with('0') {
            s.pop();
        }
        if s.ends_with('.') {
            s.pop();
        }
        s
    }
}

fn clean_name(input: &str, restrict_characters: bool, truncate_to: usize) -> String {
    let mut out = String::with_capacity(input.len());
    for ch in input.trim().chars() {
        let invalid = matches!(ch, '<' | '>' | ':' | '"' | '/' | '\\' | '|' | '?' | '*')
            || ch.is_control();
        let safe = if invalid { '_' } else { ch };
        if restrict_characters && !((' '..='~').contains(&safe)) {
            continue;
        }
        out.push(safe);
    }

    let mut cleaned = out.trim().to_string();
    if truncate_to > 0 {
        cleaned = cleaned.chars().take(truncate_to).collect();
        cleaned = cleaned.trim().to_string();
    }
    if cleaned.is_empty() {
        "Unknown".to_string()
    } else {
        cleaned
    }
}

fn format_template(template: &str, values: &[(&str, String)]) -> String {
    let mut out = String::with_capacity(template.len() + 16);
    let chars: Vec<char> = template.chars().collect();
    let mut idx = 0usize;

    while idx < chars.len() {
        if chars[idx] != '{' {
            out.push(chars[idx]);
            idx += 1;
            continue;
        }

        let Some(end) = chars[idx + 1..].iter().position(|&ch| ch == '}') else {
            out.push(chars[idx]);
            idx += 1;
            continue;
        };
        let end = idx + 1 + end;
        let token: String = chars[idx + 1..end].iter().collect();
        let mut parts = token.splitn(2, ':');
        let key = parts.next().unwrap_or("");
        let width = parts.next().and_then(|v| v.trim_start_matches('0').parse::<usize>().ok());
        let mut value = values
            .iter()
            .find_map(|(name, value)| (*name == key).then(|| value.clone()))
            .unwrap_or_default();
        if let Some(width) = width {
            if let Ok(number) = value.parse::<i32>() {
                value = format!("{number:0width$}");
            }
        }
        out.push_str(&value);
        idx = end + 1;
    }

    out
}
