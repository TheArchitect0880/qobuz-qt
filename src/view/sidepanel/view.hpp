#pragma once

#include "../../backend/qobuzbackend.hpp"
#include "../../playqueue.hpp"

#include <QWidget>
#include <QDockWidget>
#include <QTabWidget>
#include <QLineEdit>
#include <QTreeWidget>
#include <QJsonObject>
#include <QVector>
#include <QPair>

namespace SidePanel
{
    class SearchTab : public QWidget
    {
        Q_OBJECT
    public:
        explicit SearchTab(QobuzBackend *backend, PlayQueue *queue, QWidget *parent = nullptr);

        void setUserPlaylists(const QVector<QPair<qint64, QString>> &playlists);

    signals:
        void albumSelected(const QString &albumId);
        void artistSelected(qint64 artistId);
        void trackPlayRequested(qint64 trackId);
        void addToPlaylistRequested(qint64 trackId, qint64 playlistId);

    private slots:
        void onSearchResult(const QJsonObject &result);
        void onMostPopularResult(const QJsonObject &result);
        void onSearchSubmit();
        void onItemDoubleClicked(QTreeWidgetItem *item, int column);

    private:
        QobuzBackend *m_backend    = nullptr;
        PlayQueue    *m_queue      = nullptr;
        QLineEdit    *m_searchBox  = nullptr;
        QTabWidget   *m_resultTabs = nullptr;
        QTreeWidget  *m_topResults = nullptr;
        QTreeWidget  *m_trackResults  = nullptr;
        QTreeWidget  *m_albumResults  = nullptr;
        QTreeWidget  *m_artistResults = nullptr;
        QVector<QPair<qint64, QString>> m_userPlaylists;

        void onTrackContextMenu(const QPoint &pos);
        void onAlbumContextMenu(const QPoint &pos);
        void showTrackInfo(const QJsonObject &track);
    };

    class View : public QDockWidget
    {
        Q_OBJECT
    public:
        explicit View(QobuzBackend *backend, PlayQueue *queue, QWidget *parent = nullptr);

        SearchTab *searchTab() const { return m_search; }

    signals:
        void albumSelected(const QString &albumId);
        void artistSelected(qint64 artistId);
        void trackPlayRequested(qint64 trackId);
        void addToPlaylistRequested(qint64 trackId, qint64 playlistId);

    private:
        SearchTab *m_search = nullptr;
    };
}
