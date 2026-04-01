#pragma once

#include "albumlistview.hpp"
#include "../list/tracks.hpp"
#include "../backend/qobuzbackend.hpp"
#include "../playqueue.hpp"

#include <QWidget>
#include <QLabel>
#include <QTextEdit>
#include <QPushButton>
#include <QNetworkAccessManager>
#include <QJsonObject>
#include <QJsonArray>
#include <QSet>

class AlbumListView;

/// One collapsible section (Albums / EPs / Live / etc.) inside ArtistView.
class ArtistSection : public QWidget
{
    Q_OBJECT
public:
    explicit ArtistSection(const QString &title, const QString &releaseType, QWidget *parent = nullptr);

    void setAlbums(const QJsonArray &albums);
    bool isEmpty() const;
    QStringList albumIds() const;
    void setArtistPageMode();

signals:
    void albumSelected(const QString &albumId);

private:
    QString        m_baseTitle;
    QString        m_releaseType;
    QPushButton   *m_toggle       = nullptr;
    AlbumListView *m_list         = nullptr;

    void updateToggleText();
};

/// Artist detail page.
class ArtistView : public QWidget
{
    Q_OBJECT

public:
    explicit ArtistView(QobuzBackend *backend, PlayQueue *queue, QWidget *parent = nullptr);

    void setArtist(const QJsonObject &artist);
    void setReleases(const QString &releaseType, const QJsonArray &items,
                     bool hasMore = false, int offset = 0);
    void setFavArtistIds(const QSet<qint64> &ids);
    void onDeepShuffleTracks(const QJsonArray &tracks);

signals:
    void albumSelected(const QString &albumId);
    void playTrackRequested(qint64 trackId);

private:
    QobuzBackend *m_backend    = nullptr;
    PlayQueue    *m_queue      = nullptr;
    qint64        m_artistId   = 0;

    // Header widgets
    QLabel                *m_artLabel   = nullptr;
    QLabel                *m_nameLabel  = nullptr;
    QTextEdit             *m_bioEdit    = nullptr;
    QPushButton           *m_playBtn       = nullptr;
    QPushButton           *m_shuffleTopBtn = nullptr;
    QPushButton           *m_shuffleBtn    = nullptr;
    QPushButton           *m_favBtn        = nullptr;
    QNetworkAccessManager *m_nam        = nullptr;
    QString                m_currentArtUrl;
    bool                   m_isFaved    = false;
    QSet<qint64>           m_favArtistIds;

    // Popular tracks section
    QWidget      *m_topTracksSection = nullptr;
    QPushButton  *m_topTracksToggle  = nullptr;
    List::Tracks *m_topTracks        = nullptr;

    // Release sections
    ArtistSection *m_secAlbums       = nullptr;
    ArtistSection *m_secEps          = nullptr;
    ArtistSection *m_secLive         = nullptr;
    ArtistSection *m_secCompilations = nullptr;
    ArtistSection *m_secOther        = nullptr;

    QStringList allAlbumIds() const;
    void setFaved(bool faved);
};
