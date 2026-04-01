#pragma once

#include <QString>
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

    // --- Last.fm ---
    bool    lastFmEnabled()   const  { return m_settings.value("lastfm/enabled", false).toBool(); }
    void    setLastFmEnabled(bool v) { m_settings.setValue("lastfm/enabled", v); }

    QString lastFmApiKey()    const  { return m_settings.value("lastfm/api_key").toString(); }
    void    setLastFmApiKey(const QString &v)    { m_settings.setValue("lastfm/api_key", v); }

    QString lastFmApiSecret() const  { return m_settings.value("lastfm/api_secret").toString(); }
    void    setLastFmApiSecret(const QString &v) { m_settings.setValue("lastfm/api_secret", v); }

    QString lastFmSessionKey() const { return m_settings.value("lastfm/session_key").toString(); }
    void    setLastFmSessionKey(const QString &v){ m_settings.setValue("lastfm/session_key", v); }

private:
    AppSettings() : m_settings("qobuz-qt", "qobuz-qt") {}
    QSettings m_settings;
};
