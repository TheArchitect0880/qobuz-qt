#pragma once

#include "../backend/qobuzbackend.hpp"
#include "../playqueue.hpp"
#include "albumlistview.hpp"

#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QJsonArray>
#include <QJsonObject>
#include <QSet>
#include <QPushButton>
#include <QStackedWidget>
#include <QTreeWidget>
#include <QWidget>

class GenreBrowserView : public QWidget
{
    Q_OBJECT

public:
    enum class BrowseMode {
        Genres,
        PlaylistSearch,
    };

    explicit GenreBrowserView(QobuzBackend *backend, PlayQueue *queue, QWidget *parent = nullptr);

    void ensureGenresLoaded();
    void setBrowseMode(BrowseMode mode);
    bool tryHandleDeepShuffleTracks(const QJsonArray &tracks);

signals:
    void albumSelected(const QString &albumId);
    void artistSelected(qint64 artistId);
    void playlistSelected(qint64 playlistId);
    void playTrackRequested(qint64 trackId);

private slots:
    void onGenresLoaded(const QJsonObject &result);
    void onFeaturedAlbumsLoaded(const QJsonObject &result);
    void onFeaturedPlaylistsLoaded(const QJsonObject &result);
    void onDiscoverPlaylistsLoaded(const QJsonObject &result);
    void onPlaylistSearchLoaded(const QJsonObject &result);
    void onSelectionChanged();
    void onAlbumContextMenu(const QPoint &pos);
    void onPlaylistActivated(QTreeWidgetItem *item, int column);
    void onPlaylistContextMenu(const QPoint &pos);
    void onDeepShuffleClicked();

private:
    QobuzBackend *m_backend = nullptr;
    PlayQueue *m_queue = nullptr;
    QLabel *m_browseLabel = nullptr;
    QLabel *m_genreLabel = nullptr;
    QLabel *m_typeLabel = nullptr;
    QLabel *m_playlistSearchLabel = nullptr;
    QWidget *m_gapAfterKind = nullptr;
    QWidget *m_gapAfterGenre = nullptr;
    QComboBox *m_kindCombo = nullptr;
    QComboBox *m_genreCombo = nullptr;
    QComboBox *m_typeCombo = nullptr;
    QLineEdit *m_playlistSearchBox = nullptr;
    QPushButton *m_playlistSearchBtn = nullptr;
    QPushButton *m_deepShuffleBtn = nullptr;
    QStackedWidget *m_resultsStack = nullptr;
    AlbumListView *m_albumList = nullptr;
    QTreeWidget *m_playlistList = nullptr;
    BrowseMode m_mode = BrowseMode::Genres;
    bool m_genresLoaded = false;
    int m_lastGenreComboIndex = 0;
    QSet<qint64> m_multiGenreIds;
    bool m_waitingDeepShuffle = false;

    void refreshModeUi();
    void refreshGenreTypeChoices();
    QString currentGenreIds() const;
    QStringList currentAlbumIds() const;
    bool chooseMultiGenres();
    void updateMultiGenreLabel();
    void setPlaylistItems(const QJsonArray &items);
};
