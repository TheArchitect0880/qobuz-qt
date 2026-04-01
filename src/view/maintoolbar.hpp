#pragma once

#include "../backend/qobuzbackend.hpp"
#include "../playqueue.hpp"
#include "../widget/volumebutton.hpp"
#include "../widget/clickableslider.hpp"
#include "../util/icon.hpp"

#include <QToolBar>
#include <QLabel>
#include <QAction>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QJsonObject>
#include <QPair>
#include <QVector>

class MainToolBar : public QToolBar
{
    Q_OBJECT

public:
    explicit MainToolBar(QobuzBackend *backend, PlayQueue *queue, QWidget *parent = nullptr);

    void setPlaying(bool playing);
    void setCurrentTrack(const QJsonObject &track);
    void updateProgress(quint64 position, quint64 duration);
    void setQueueToggleChecked(bool checked);
    void setSearchToggleChecked(bool checked);

    void setUserPlaylists(const QVector<QPair<qint64, QString>> &playlists) { m_userPlaylists = playlists; }

signals:
    void searchToggled(bool visible);
    void queueToggled(bool visible);
    void albumRequested(const QString &albumId);
    void artistRequested(qint64 artistId);
    void addToPlaylistRequested(qint64 trackId, qint64 playlistId);
    void favTrackRequested(qint64 trackId);

protected:
    void resizeEvent(QResizeEvent *event) override;

private slots:
    void onPlayPause();
    void onPrevious();
    void onNext();
    void onProgressReleased();
    void onVolumeChanged(int volume);

    void onBackendStateChanged(const QString &state);
    void onTrackChanged(const QJsonObject &track);
    void onPositionChanged(quint64 position, quint64 duration);
    void onTrackFinished();
    void onTrackTransitioned();
    void onQueueChanged();
    void onShuffleToggled(bool checked);
    void onAutoplayToggled(bool checked);
    void onDynamicSuggestionsLoaded(const QJsonObject &result);

    void fetchAlbumArt(const QString &url);
    void onAlbumArtReady(QNetworkReply *reply);

private:
    struct RecentTrackSeed {
        qint64 trackId = 0;
        qint64 artistId = 0;
        qint64 genreId = 0;
        qint64 labelId = 0;
    };

    QobuzBackend *m_backend = nullptr;
    PlayQueue    *m_queue   = nullptr;

    QLabel          *m_artLabel    = nullptr;
    QLabel          *m_trackLabel  = nullptr;
    QAction         *m_previous    = nullptr;
    QAction         *m_playPause   = nullptr;
    QAction         *m_next        = nullptr;
    QWidget         *m_leftSpacer  = nullptr;
    ClickableSlider *m_progress    = nullptr;
    QLabel          *m_position    = nullptr;
    QWidget         *m_rightSpacer = nullptr;
    QAction         *m_shuffle     = nullptr;
    QAction         *m_autoplay    = nullptr;
    VolumeButton    *m_volume      = nullptr;
    QAction         *m_queueBtn    = nullptr;
    QAction         *m_search      = nullptr;

    QNetworkAccessManager *m_nam = nullptr;
    QString     m_currentArtUrl;
    QJsonObject m_currentTrack;
    QVector<RecentTrackSeed> m_recentTracks;
    bool        m_playing = false;
    bool        m_seeking = false;
    bool        m_seekPending = false;
    quint64     m_pendingSeekTarget = 0;
    qint64      m_pendingSeekStartedMs = 0;
    bool        m_fetchingAutoplay = false;

    QVector<QPair<qint64, QString>> m_userPlaylists;

    void requestAutoplaySuggestions();
};
