#pragma once

#include <QString>
#include <QByteArray>
#include <QDir>
#include <QSettings>

class AppSettings
{
public:
    static AppSettings &instance()
    {
        static AppSettings inst;
        return inst;
    }

    QString authToken() const        { return m_settings.value("auth/token").toString(); }
    void setAuthToken(const QString &t) { m_settings.setValue("auth/token", t); }

    QString authRefreshToken() const { return m_settings.value("auth/refresh_token").toString(); }
    void setAuthRefreshToken(const QString &t) { m_settings.setValue("auth/refresh_token", t); }

    qint64 authExpiresAt() const     { return m_settings.value("auth/expires_at", 0LL).toLongLong(); }
    void setAuthExpiresAt(qint64 t)  { m_settings.setValue("auth/expires_at", t); }

    QString userEmail() const        { return m_settings.value("auth/email").toString(); }
    void setUserEmail(const QString &e) { m_settings.setValue("auth/email", e); }

    QString displayName() const      { return m_settings.value("user/display_name").toString(); }
    void setDisplayName(const QString &n) { m_settings.setValue("user/display_name", n); }

    qint64 userId() const            { return m_settings.value("user/id", 0LL).toLongLong(); }
    void setUserId(qint64 id)        { m_settings.setValue("user/id", id); }

    // 5 = MP3, 6 = CD, 7 = HiRes96, 27 = HiRes192
    int preferredFormat() const      { return m_settings.value("playback/format", 6).toInt(); }
    void setPreferredFormat(int f)   { m_settings.setValue("playback/format", f); }

    int volume() const               { return m_settings.value("playback/volume", 80).toInt(); }
    void setVolume(int v)            { m_settings.setValue("playback/volume", v); }

    bool rememberLogin() const       { return m_settings.value("auth/remember", true).toBool(); }
    void setRememberLogin(bool r)    { m_settings.setValue("auth/remember", r); }

    // --- Playback extras ---
    bool replayGainEnabled() const     { return m_settings.value("playback/replaygain", false).toBool(); }
    void setReplayGainEnabled(bool v)  { m_settings.setValue("playback/replaygain", v); }

    bool gaplessEnabled() const        { return m_settings.value("playback/gapless", false).toBool(); }
    void setGaplessEnabled(bool v)     { m_settings.setValue("playback/gapless", v); }

    bool autoplayEnabled() const       { return m_settings.value("playback/autoplay", false).toBool(); }
    void setAutoplayEnabled(bool v)    { m_settings.setValue("playback/autoplay", v); }

    QString downloadFolder() const
    {
        return m_settings.value("downloads/folder", QDir::homePath() + QStringLiteral("/StreamripDownloads")).toString();
    }
    void setDownloadFolder(const QString &path) { m_settings.setValue("downloads/folder", path); }

    int downloadFormat() const         { return m_settings.value("downloads/format", 7).toInt(); }
    void setDownloadFormat(int f)      { m_settings.setValue("downloads/format", f); }

    bool downloadSourceSubdirectories() const { return m_settings.value("downloads/source_subdirectories", false).toBool(); }
    void setDownloadSourceSubdirectories(bool v) { m_settings.setValue("downloads/source_subdirectories", v); }

    bool downloadDiscSubdirectories() const { return m_settings.value("downloads/disc_subdirectories", true).toBool(); }
    void setDownloadDiscSubdirectories(bool v) { m_settings.setValue("downloads/disc_subdirectories", v); }

    bool downloadAddSinglesToFolder() const { return m_settings.value("downloads/add_singles_to_folder", false).toBool(); }
    void setDownloadAddSinglesToFolder(bool v) { m_settings.setValue("downloads/add_singles_to_folder", v); }

    bool downloadRenumberPlaylistTracks() const { return m_settings.value("downloads/renumber_playlist_tracks", true).toBool(); }
    void setDownloadRenumberPlaylistTracks(bool v) { m_settings.setValue("downloads/renumber_playlist_tracks", v); }

    bool downloadSetPlaylistToAlbum() const { return m_settings.value("downloads/set_playlist_to_album", true).toBool(); }
    void setDownloadSetPlaylistToAlbum(bool v) { m_settings.setValue("downloads/set_playlist_to_album", v); }

    bool downloadRestrictCharacters() const { return m_settings.value("downloads/restrict_characters", false).toBool(); }
    void setDownloadRestrictCharacters(bool v) { m_settings.setValue("downloads/restrict_characters", v); }

    int downloadTruncateTo() const     { return m_settings.value("downloads/truncate_to", 120).toInt(); }
    void setDownloadTruncateTo(int v)  { m_settings.setValue("downloads/truncate_to", v); }

    QString downloadFolderFormat() const
    {
        return m_settings.value(
            "downloads/folder_format",
            QStringLiteral("{albumartist} - {title} ({year}) [{container}] [{bit_depth}B-{sampling_rate}kHz]"))
            .toString();
    }
    void setDownloadFolderFormat(const QString &v) { m_settings.setValue("downloads/folder_format", v); }

    QString downloadTrackFormat() const
    {
        return m_settings.value(
            "downloads/track_format",
            QStringLiteral("{tracknumber:02}. {artist} - {title}{explicit}"))
            .toString();
    }
    void setDownloadTrackFormat(const QString &v) { m_settings.setValue("downloads/track_format", v); }

    bool downloadSaveArtwork() const { return m_settings.value("downloads/save_artwork", true).toBool(); }
    void setDownloadSaveArtwork(bool v) { m_settings.setValue("downloads/save_artwork", v); }

    bool downloadEmbedArtwork() const { return m_settings.value("downloads/embed_artwork", true).toBool(); }
    void setDownloadEmbedArtwork(bool v) { m_settings.setValue("downloads/embed_artwork", v); }

    // --- Last.fm ---
    bool    lastFmEnabled()   const  { return m_settings.value("lastfm/enabled", false).toBool(); }
    void    setLastFmEnabled(bool v) { m_settings.setValue("lastfm/enabled", v); }

    QString lastFmApiKey()    const  { return m_settings.value("lastfm/api_key").toString(); }
    void    setLastFmApiKey(const QString &v)    { m_settings.setValue("lastfm/api_key", v); }

    QString lastFmApiSecret() const  { return m_settings.value("lastfm/api_secret").toString(); }
    void    setLastFmApiSecret(const QString &v) { m_settings.setValue("lastfm/api_secret", v); }

    QString lastFmSessionKey() const { return m_settings.value("lastfm/session_key").toString(); }
    void    setLastFmSessionKey(const QString &v){ m_settings.setValue("lastfm/session_key", v); }

    // --- UI layout ---
    QByteArray windowGeometry() const { return m_settings.value("ui/window_geometry").toByteArray(); }
    void setWindowGeometry(const QByteArray &geometry) { m_settings.setValue("ui/window_geometry", geometry); }

    QByteArray windowState() const { return m_settings.value("ui/window_state").toByteArray(); }
    void setWindowState(const QByteArray &state) { m_settings.setValue("ui/window_state", state); }

    int libraryDockWidth() const { return m_settings.value("ui/library_dock_width", -1).toInt(); }
    void setLibraryDockWidth(int width) { m_settings.setValue("ui/library_dock_width", width); }
    void clearWindowLayout()
    {
        m_settings.remove("ui/window_geometry");
        m_settings.remove("ui/window_state");
        m_settings.remove("ui/library_dock_width");
    }

private:
    AppSettings() : m_settings("qobuz-qt", "qobuz-qt") {}
    QSettings m_settings;
};
