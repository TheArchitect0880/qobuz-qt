#pragma once

#include "backend/qobuzbackend.hpp"
#include "playqueue.hpp"
#include "view/maintoolbar.hpp"
#include "view/maincontent.hpp"
#include "view/context/view.hpp"
#include "view/queuepanel.hpp"
#include "view/sidepanel/view.hpp"
#include "list/library.hpp"
#include "scrobbler/lastfm.hpp"

#include <QMainWindow>
#include <QDockWidget>
#include <QJsonObject>
#include <QJsonArray>
#include <QVector>
#include <QSet>
#include <QPair>
#include <QString>

class Mpris;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QobuzBackend *backend, QWidget *parent = nullptr);
    static QSize defaultSize() { return {1100, 700}; }

private slots:
    void onLoginSuccess(const QString &token, const QJsonObject &user);
    void onLoginError(const QString &error);

    void onFavTracksLoaded(const QJsonObject &result);
    void onFavAlbumsLoaded(const QJsonObject &result);
    void onFavArtistsLoaded(const QJsonObject &result);
    void onAlbumLoaded(const QJsonObject &album);
    void onArtistLoaded(const QJsonObject &artist);
    void onPlaylistLoaded(const QJsonObject &playlist);

    void onTrackChanged(const QJsonObject &track);
    void onPlayTrackRequested(qint64 trackId);
    void onSearchAlbumSelected(const QString &albumId);
    void onSearchArtistSelected(qint64 artistId);
    void onSearchToggled(bool visible);
    void onPlaylistCreated(const QJsonObject &playlist);
    void onUserPlaylistsChanged(const QVector<QPair<qint64, QString>> &playlists);

    void showLoginDialog();
    void showSettingsDialog();

private:
    QobuzBackend    *m_backend     = nullptr;
    PlayQueue       *m_queue       = nullptr;
    QVector<QPair<qint64, QString>> m_userPlaylists;
    QSet<qint64> m_userPlaylistIds;
    QSet<QString> m_favAlbumIds;
    QSet<qint64> m_favArtistIds;
    bool m_showFavAlbumsOnLoad = false;
    bool m_showFavArtistsOnLoad = false;
    MainToolBar     *m_toolBar     = nullptr;
    MainContent     *m_content     = nullptr;
    List::Library   *m_library     = nullptr;
    Context::View   *m_contextView = nullptr;
    QueuePanel      *m_queuePanel  = nullptr;
    SidePanel::View *m_sidePanel   = nullptr;
    QDockWidget     *m_libraryDock = nullptr;
    LastFmScrobbler *m_scrobbler   = nullptr;
    Mpris           *m_mpris       = nullptr;
    bool m_nextTrackPrefetched = false;

    void setupMenuBar();
    void tryRestoreSession();
};
