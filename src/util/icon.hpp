#pragma once

#include <QFile>
#include <QIcon>
#include <QString>

namespace Icon
{
    /// Load an icon by name.  Checks the embedded :/res/icons/ first, then
    /// falls back to the system theme.  The dark SVGs from spotify-qt are
    /// bundled so this always succeeds for known names.
    inline QIcon get(const QString &name)
    {
        const QString path = QStringLiteral(":/res/icons/%1.svg").arg(name);
        if (QFile::exists(path))
            return QIcon(path);
        return QIcon::fromTheme(name);
    }

    // Playback
    inline QIcon play()       { return get("media-playback-start"); }
    inline QIcon pause()      { return get("media-playback-pause"); }
    inline QIcon next()       { return get("media-skip-forward"); }
    inline QIcon previous()   { return get("media-skip-backward"); }
    inline QIcon shuffle()    { return get("media-playlist-shuffle"); }
    inline QIcon repeat()     { return get("media-playlist-repeat"); }
    inline QIcon autoplay()   { return get("media-track-show-active"); }

    // Volume
    inline QIcon volumeHigh() { return get("audio-volume-high"); }
    inline QIcon volumeMid()  { return get("audio-volume-medium"); }
    inline QIcon volumeLow()  { return get("audio-volume-low"); }
    inline QIcon volumeMute() { return get("audio-volume-low"); }

    // UI
    inline QIcon search()     { return get("edit-find"); }
    inline QIcon heart()      { return get("starred-symbolic"); }
    inline QIcon heartOff()   { return get("non-starred-symbolic"); }
    inline QIcon album()      { return get("view-media-album-cover"); }
    inline QIcon artist()     { return get("view-media-artist"); }
    inline QIcon playlist()   { return get("view-media-playlist"); }
    inline QIcon track()      { return get("view-media-track"); }
    inline QIcon queue()      { return get("media-playlist-append"); }
    inline QIcon refresh()    { return get("view-refresh"); }
    inline QIcon settings()   { return get("configure"); }
    inline QIcon sortAsc()    { return get("view-sort-ascending"); }
}
