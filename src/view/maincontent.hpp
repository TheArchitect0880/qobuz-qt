#pragma once

#include "../list/tracks.hpp"
#include "../backend/qobuzbackend.hpp"
#include "../playqueue.hpp"
#include "albumlistview.hpp"
#include "artistlistview.hpp"
#include "artistview.hpp"
#include "genrebrowser.hpp"
#include "trackcontextheader.hpp"

#include <QWidget>
#include <QLabel>
#include <QStackedWidget>
#include <QJsonObject>
#include <QJsonArray>
#include <QSet>

class MainContent : public QWidget
{
    Q_OBJECT

public:
    explicit MainContent(QobuzBackend *backend, PlayQueue *queue, QWidget *parent = nullptr);

    List::Tracks *tracksList() const { return m_tracks; }

    void showWelcome();
    void showAlbum(const QJsonObject &album);
    void showPlaylist(const QJsonObject &playlist, bool isFollowed, bool isOwned);
    void showFavTracks(const QJsonObject &result);
    void showSearchTracks(const QJsonArray &tracks);
    void showFavAlbums(const QJsonObject &result);
    void showFavArtists(const QJsonObject &result);
    void showArtist(const QJsonObject &artist);
    void updateArtistReleases(const QString &releaseType, const QJsonArray &items, bool hasMore, int offset);
    void setFavAlbumIds(const QSet<QString> &ids);
    void setFavArtistIds(const QSet<qint64> &ids);
    void onDeepShuffleTracks(const QJsonArray &tracks);
    void showGenreBrowser();
    void showPlaylistBrowser();
    void setCurrentPlaylistFollowed(bool followed);

    ArtistView *artistView() const { return m_artistView; }

signals:
    void albumRequested(const QString &albumId);
    void artistRequested(qint64 artistId);
    void albumFavoriteToggled(const QString &albumId, bool favorite);
    void playlistRequested(qint64 playlistId);
    void playlistFollowToggled(qint64 playlistId, bool follow);
    void playTrackRequested(qint64 trackId);

private:
    enum StackPage {
        PageWelcome      = 0,
        PageTracks       = 1,
        PageAlbumList    = 2,
        PageArtistList   = 3,
        PageArtistDetail = 4,
        PageGenreBrowser = 5,
    };

    QobuzBackend        *m_backend    = nullptr;
    QStackedWidget      *m_stack      = nullptr;
    QLabel              *m_welcome    = nullptr;
    List::Tracks        *m_tracks     = nullptr;
    TrackContextHeader  *m_header     = nullptr;
    AlbumListView       *m_albumList  = nullptr;
    ArtistListView      *m_artistList = nullptr;
    ArtistView          *m_artistView = nullptr;
    GenreBrowserView    *m_genreBrowser = nullptr;
    QSet<QString>        m_favAlbumIds;
};
