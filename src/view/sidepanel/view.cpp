#include "view.hpp"
#include "../../util/colors.hpp"
#include "../../util/trackinfo.hpp"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QHeaderView>
#include <QFont>
#include <QJsonArray>
#include <QMenu>

static constexpr int IdRole   = Qt::UserRole + 1;
static constexpr int TypeRole = Qt::UserRole + 2;
static constexpr int JsonRole = Qt::UserRole + 3;

namespace SidePanel
{

// ---- SearchTab ----

SearchTab::SearchTab(QobuzBackend *backend, PlayQueue *queue, QWidget *parent)
    : QWidget(parent)
    , m_backend(backend)
    , m_queue(queue)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);

    // Search bar
    auto *barLayout = new QHBoxLayout;
    m_searchBox     = new QLineEdit(this);
    m_searchBox->setPlaceholderText(tr("Search Qobuz..."));
    m_searchBox->setClearButtonEnabled(true);
    auto *searchBtn = new QPushButton(tr("Go"), this);
    barLayout->addWidget(m_searchBox);
    barLayout->addWidget(searchBtn);
    layout->addLayout(barLayout);

    // Result tabs
    m_resultTabs = new QTabWidget(this);

    m_topResults = new QTreeWidget(this);
    m_topResults->setHeaderLabels({tr(""), tr("Top Result"), tr("Info")});
    m_topResults->setRootIsDecorated(false);
    m_topResults->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_topResults->header()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_topResults->header()->setSectionResizeMode(2, QHeaderView::Stretch);
    m_topResults->header()->setStretchLastSection(false);
    m_topResults->setContextMenuPolicy(Qt::CustomContextMenu);

    m_trackResults  = new QTreeWidget(this);
    m_trackResults->setHeaderLabels({tr("Title"), tr("Artist"), tr("Album")});
    m_trackResults->setRootIsDecorated(false);
    m_trackResults->setContextMenuPolicy(Qt::CustomContextMenu);

    m_albumResults  = new QTreeWidget(this);
    m_albumResults->setHeaderLabels({tr(""), tr("Album"), tr("Artist")});
    m_albumResults->setRootIsDecorated(false);
    m_albumResults->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_albumResults->header()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_albumResults->header()->setSectionResizeMode(2, QHeaderView::Stretch);
    m_albumResults->header()->setStretchLastSection(false);
    m_albumResults->setContextMenuPolicy(Qt::CustomContextMenu);

    m_artistResults = new QTreeWidget(this);
    m_artistResults->setHeaderLabels({tr("Artist")});
    m_artistResults->setRootIsDecorated(false);
    m_artistResults->setContextMenuPolicy(Qt::CustomContextMenu);

    m_resultTabs->addTab(m_topResults, tr("Top Results"));
    m_resultTabs->addTab(m_trackResults,  tr("Tracks"));
    m_resultTabs->addTab(m_albumResults,  tr("Albums"));
    m_resultTabs->addTab(m_artistResults, tr("Artists"));
    layout->addWidget(m_resultTabs);

    connect(searchBtn,   &QPushButton::clicked,  this, &SearchTab::onSearchSubmit);
    connect(m_searchBox, &QLineEdit::returnPressed, this, &SearchTab::onSearchSubmit);

    connect(m_backend, &QobuzBackend::searchResult, this, &SearchTab::onSearchResult);
    connect(m_backend, &QobuzBackend::mostPopularResult, this, &SearchTab::onMostPopularResult);

    connect(m_topResults,   &QTreeWidget::itemDoubleClicked, this, &SearchTab::onItemDoubleClicked);
    connect(m_trackResults,  &QTreeWidget::itemDoubleClicked, this, &SearchTab::onItemDoubleClicked);
    connect(m_albumResults,  &QTreeWidget::itemDoubleClicked, this, &SearchTab::onItemDoubleClicked);
    connect(m_artistResults, &QTreeWidget::itemDoubleClicked, this, &SearchTab::onItemDoubleClicked);

    // Context menus
    connect(m_topResults, &QTreeWidget::customContextMenuRequested,
            this, &SearchTab::onTopResultContextMenu);
    connect(m_trackResults, &QTreeWidget::customContextMenuRequested,
            this, &SearchTab::onTrackContextMenu);
    connect(m_albumResults, &QTreeWidget::customContextMenuRequested,
            this, &SearchTab::onAlbumContextMenu);
    connect(m_artistResults, &QTreeWidget::customContextMenuRequested,
            this, &SearchTab::onArtistContextMenu);

    m_albumQueueHelper = new AlbumQueueHelper(m_backend, m_queue, this);
}

void SearchTab::focusSearchBox()
{
    m_searchBox->setFocus();
    m_searchBox->selectAll();
}

void SearchTab::setUserPlaylists(const QVector<QPair<qint64, QString>> &playlists)
{
    m_userPlaylists = playlists;
}

void SearchTab::onSearchSubmit()
{
    const QString q = m_searchBox->text().trimmed();
    if (!q.isEmpty()) {
        m_backend->mostPopularSearch(q, 8);
        m_backend->search(q, 0, 20);
        m_resultTabs->setCurrentIndex(0);
    }
}

void SearchTab::onMostPopularResult(const QJsonObject &result)
{
    m_topResults->clear();

    QFont badgeFont;
    badgeFont.setBold(true);

    const QJsonArray items = result["most_popular"].toObject()["items"].toArray();
    for (const auto &value : items) {
        const QJsonObject itemObj = value.toObject();
        const QString type = itemObj["type"].toString();
        const QJsonObject content = itemObj["content"].toObject();

        auto *item = new QTreeWidgetItem(m_topResults);
        item->setData(0, JsonRole, content);

        if (type == QStringLiteral("tracks")) {
            const QString title = content["title"].toString();
            const QString artist = content["performer"].toObject()["name"].toString();
            const QString album = content["album"].toObject()["title"].toString();
            item->setText(0, QStringLiteral("T"));
            item->setForeground(0, Colors::BadgeGreen);
            item->setFont(0, badgeFont);
            item->setTextAlignment(0, Qt::AlignCenter);
            item->setText(1, title);
            item->setText(2, artist.isEmpty() ? album : QStringLiteral("%1 - %2").arg(artist, album));
            item->setData(0, TypeRole, QStringLiteral("track"));
            item->setData(0, IdRole, static_cast<qint64>(content["id"].toDouble()));
        } else if (type == QStringLiteral("albums")) {
            const QString title = content["title"].toString();
            const QString artist = content["artist"].toObject()["name"].toString();
            const bool hiRes = content["hires_streamable"].toBool()
                || content["rights"].toObject()["hires_streamable"].toBool();
            item->setText(0, hiRes ? QStringLiteral("H") : QStringLiteral("A"));
            item->setForeground(0, hiRes
                ? Colors::QobuzOrange
                : Colors::BadgeGray);
            item->setFont(0, badgeFont);
            item->setTextAlignment(0, Qt::AlignCenter);
            item->setText(1, title);
            item->setText(2, artist);
            item->setData(0, TypeRole, QStringLiteral("album"));
            item->setData(1, IdRole, content["id"].toString());
        } else if (type == QStringLiteral("artists")) {
            item->setText(0, QStringLiteral("A"));
            item->setForeground(0, Colors::BadgeBlue);
            item->setFont(0, badgeFont);
            item->setTextAlignment(0, Qt::AlignCenter);
            item->setText(1, content["name"].toString());
            item->setText(2, tr("Artist"));
            item->setData(0, TypeRole, QStringLiteral("artist"));
            item->setData(0, IdRole, static_cast<qint64>(content["id"].toDouble()));
        }
    }
}

void SearchTab::onSearchResult(const QJsonObject &result)
{
    // Populate tracks
    m_trackResults->clear();
    const QJsonArray tracks = result["tracks"].toObject()["items"].toArray();
    for (const auto &v : tracks) {
        const QJsonObject t = v.toObject();
        const QString performer = t["performer"].toObject()["name"].toString();
        const QString album     = t["album"].toObject()["title"].toString();
        auto *item = new QTreeWidgetItem(m_trackResults,
            QStringList{t["title"].toString(), performer, album});
        item->setData(0, IdRole,   static_cast<qint64>(t["id"].toDouble()));
        item->setData(0, TypeRole, QStringLiteral("track"));
        item->setData(0, JsonRole, t);
    }

    // Populate albums
    m_albumResults->clear();
    {
        QFont hiResFont;
        hiResFont.setBold(true);
        hiResFont.setPointSizeF(hiResFont.pointSizeF() * 0.85);

        const QJsonArray albums = result["albums"].toObject()["items"].toArray();
        for (const auto &v : albums) {
            const QJsonObject a  = v.toObject();
            const QString artist = a["artist"].toObject()["name"].toString();
            const bool hiRes     = a["hires_streamable"].toBool();

            auto *item = new QTreeWidgetItem(m_albumResults,
                QStringList{QString(), a["title"].toString(), artist});
            if (hiRes) {
                item->setText(0, QStringLiteral("H"));
                item->setForeground(0, Colors::QobuzOrange);
                item->setFont(0, hiResFont);
                item->setTextAlignment(0, Qt::AlignCenter);
            }
            item->setData(0, TypeRole, QStringLiteral("album"));
            item->setData(1, IdRole,   a["id"].toString());
            item->setData(0, JsonRole, a);
        }
    }

    // Populate artists
    m_artistResults->clear();
    const QJsonArray artists = result["artists"].toObject()["items"].toArray();
    for (const auto &v : artists) {
        const QJsonObject ar = v.toObject();
        auto *item = new QTreeWidgetItem(m_artistResults,
            QStringList{ar["name"].toString()});
        item->setData(0, IdRole,   static_cast<qint64>(ar["id"].toDouble()));
        item->setData(0, TypeRole, QStringLiteral("artist"));
    }
}

void SearchTab::onItemDoubleClicked(QTreeWidgetItem *item, int)
{
    if (!item) return;
    const QString type = item->data(0, TypeRole).toString();

    if (type == QStringLiteral("track")) {
        emit trackPlayRequested(item->data(0, IdRole).toLongLong());
    } else if (type == QStringLiteral("album")) {
        emit albumSelected(item->data(1, IdRole).toString());
    } else if (type == QStringLiteral("artist")) {
        emit artistSelected(item->data(0, IdRole).toLongLong());
    }
}

void SearchTab::onTopResultContextMenu(const QPoint &pos)
{
    auto *item = m_topResults->itemAt(pos);
    if (!item) return;
    const QString type = item->data(0, TypeRole).toString();
    if (type == QStringLiteral("track"))
        showTrackContextMenu(m_topResults, pos);
    else if (type == QStringLiteral("album"))
        showAlbumContextMenu(m_topResults, pos);
    else if (type == QStringLiteral("artist"))
        showArtistContextMenu(m_topResults, pos);
}

void SearchTab::onTrackContextMenu(const QPoint &pos)
{
    showTrackContextMenu(m_trackResults, pos);
}

void SearchTab::onAlbumContextMenu(const QPoint &pos)
{
    showAlbumContextMenu(m_albumResults, pos);
}

void SearchTab::showTrackContextMenu(QTreeWidget *tree, const QPoint &pos)
{
    auto *item = tree->itemAt(pos);
    if (!item) return;

    const qint64 trackId = item->data(0, IdRole).toLongLong();
    const QJsonObject trackJson = item->data(0, JsonRole).toJsonObject();
    if (trackId <= 0) return;

    QMenu menu(this);

    auto *playNow  = menu.addAction(QIcon(":/res/icons/media-playback-start.svg"), tr("Play now"));
    auto *playNext = menu.addAction(QIcon(":/res/icons/media-skip-forward.svg"),   tr("Play next"));
    auto *addQueue = menu.addAction(QIcon(":/res/icons/media-playlist-append.svg"), tr("Add to queue"));
    menu.addSeparator();

    auto *favAction = menu.addAction(QIcon(":/res/icons/starred-symbolic.svg"), tr("Add to favorites"));
    connect(favAction, &QAction::triggered, this, [this, trackId] {
        m_backend->addFavTrack(trackId);
    });

    // Open album / artist
    const QString albumId = trackJson["album"].toObject()["id"].toString();
    const qint64 artistId = static_cast<qint64>(
        trackJson["performer"].toObject()["id"].toDouble());
    const QString artistName = trackJson["performer"].toObject()["name"].toString();
    const QString albumTitle = trackJson["album"].toObject()["title"].toString();

    if (!albumId.isEmpty() || artistId > 0)
        menu.addSeparator();
    if (!albumId.isEmpty()) {
        auto *openAlbum = menu.addAction(
            QIcon(":/res/icons/view-media-album-cover.svg"),
            tr("Open album: %1").arg(QString(albumTitle).replace(QLatin1Char('&'), QStringLiteral("&&"))));
        connect(openAlbum, &QAction::triggered, this, [this, albumId] {
            emit albumSelected(albumId);
        });
        auto *albumNext = menu.addAction(
            QIcon(":/res/icons/media-skip-forward.svg"), tr("Play album next"));
        connect(albumNext, &QAction::triggered, this, [this, albumId] {
            m_albumQueueHelper->request(albumId, AlbumQueueHelper::PlayNext);
        });
        auto *albumQueue = menu.addAction(
            QIcon(":/res/icons/media-playlist-append.svg"), tr("Add album to queue"));
        connect(albumQueue, &QAction::triggered, this, [this, albumId] {
            m_albumQueueHelper->request(albumId, AlbumQueueHelper::AddToQueue);
        });
    }
    if (artistId > 0) {
        auto *openArtist = menu.addAction(
            QIcon(":/res/icons/view-media-artist.svg"),
            tr("Open artist: %1").arg(QString(artistName).replace(QLatin1Char('&'), QStringLiteral("&&"))));
        connect(openArtist, &QAction::triggered, this, [this, artistId] {
            emit artistSelected(artistId);
        });
    }

    // Add to playlist submenu
    if (!m_userPlaylists.isEmpty()) {
        menu.addSeparator();
        auto *plMenu = menu.addMenu(QIcon(":/res/icons/media-playlist-append.svg"), tr("Add to playlist"));
        for (const auto &pl : m_userPlaylists) {
            auto *act = plMenu->addAction(QString(pl.second).replace(QLatin1Char('&'), QStringLiteral("&&")));
            connect(act, &QAction::triggered, this, [this, trackId, plId = pl.first] {
                emit addToPlaylistRequested(trackId, plId);
            });
        }
    }

    // Track info
    menu.addSeparator();
    auto *info = menu.addAction(tr("Track info..."));

    connect(playNow, &QAction::triggered, this, [this, trackId] {
        emit trackPlayRequested(trackId);
    });
    connect(playNext, &QAction::triggered, this, [this, trackJson] {
        m_queue->playNext(trackJson);
    });
    connect(addQueue, &QAction::triggered, this, [this, trackJson] {
        m_queue->addToQueue(trackJson);
    });
    connect(info, &QAction::triggered, this, [this, trackJson] {
        showTrackInfo(trackJson);
    });

    menu.exec(tree->viewport()->mapToGlobal(pos));
}

void SearchTab::showAlbumContextMenu(QTreeWidget *tree, const QPoint &pos)
{
    auto *item = tree->itemAt(pos);
    if (!item) return;

    const QString albumId = item->data(1, IdRole).toString();
    const QJsonObject albumJson = item->data(0, JsonRole).toJsonObject();
    if (albumId.isEmpty()) return;

    QMenu menu(this);

    auto *openAlbum   = menu.addAction(QIcon(":/res/icons/view-media-album-cover.svg"), tr("Open album"));
    menu.addSeparator();
    auto *playNextAct = menu.addAction(QIcon(":/res/icons/media-skip-forward.svg"),   tr("Play album next"));
    auto *addQueueAct = menu.addAction(QIcon(":/res/icons/media-playlist-append.svg"), tr("Add album to queue"));
    menu.addSeparator();
    auto *addFav      = menu.addAction(QIcon(":/res/icons/starred-symbolic.svg"), tr("Add to favorites"));

    const qint64 artistId = static_cast<qint64>(
        albumJson["artist"].toObject()["id"].toDouble());
    const QString artistName = albumJson["artist"].toObject()["name"].toString();
    if (artistId > 0) {
        menu.addSeparator();
        auto *openArtist = menu.addAction(
            QIcon(":/res/icons/view-media-artist.svg"),
            tr("Open artist: %1").arg(QString(artistName).replace(QLatin1Char('&'), QStringLiteral("&&"))));
        connect(openArtist, &QAction::triggered, this, [this, artistId] {
            emit artistSelected(artistId);
        });
    }

    connect(openAlbum, &QAction::triggered, this, [this, albumId] {
        emit albumSelected(albumId);
    });
    connect(playNextAct, &QAction::triggered, this, [this, albumId] {
        m_albumQueueHelper->request(albumId, AlbumQueueHelper::PlayNext);
    });
    connect(addQueueAct, &QAction::triggered, this, [this, albumId] {
        m_albumQueueHelper->request(albumId, AlbumQueueHelper::AddToQueue);
    });
    connect(addFav, &QAction::triggered, this, [this, albumId] {
        m_backend->addFavAlbum(albumId);
    });

    menu.exec(tree->viewport()->mapToGlobal(pos));
}

void SearchTab::onArtistContextMenu(const QPoint &pos)
{
    showArtistContextMenu(m_artistResults, pos);
}

void SearchTab::showArtistContextMenu(QTreeWidget *tree, const QPoint &pos)
{
    auto *item = tree->itemAt(pos);
    if (!item) return;

    const qint64 artistId = item->data(0, IdRole).toLongLong();
    if (artistId <= 0) return;

    const QString artistName = item->text(1).isEmpty() ? item->text(0) : item->text(1);

    QMenu menu(this);

    auto *openArtist = menu.addAction(
        QIcon(":/res/icons/view-media-artist.svg"),
        tr("Open artist: %1").arg(QString(artistName).replace(QLatin1Char('&'), QStringLiteral("&&"))));
    connect(openArtist, &QAction::triggered, this, [this, artistId] {
        emit artistSelected(artistId);
    });

    menu.addSeparator();
    auto *addFav = menu.addAction(QIcon(":/res/icons/starred-symbolic.svg"), tr("Add to favorites"));
    connect(addFav, &QAction::triggered, this, [this, artistId] {
        m_backend->addFavArtist(artistId);
    });

    menu.exec(tree->viewport()->mapToGlobal(pos));
}

void SearchTab::showTrackInfo(const QJsonObject &track)
{
    TrackInfoDialog::show(track, this);
}

// ---- View ----

View::View(QobuzBackend *backend, PlayQueue *queue, QWidget *parent)
    : QDockWidget(tr("Search"), parent)
{
    setObjectName(QStringLiteral("searchPanel"));
    setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable | QDockWidget::DockWidgetClosable);
    setMinimumWidth(220);

    m_search = new SearchTab(backend, queue, this);
    setWidget(m_search);

    connect(m_search, &SearchTab::albumSelected,           this, &View::albumSelected);
    connect(m_search, &SearchTab::artistSelected,          this, &View::artistSelected);
    connect(m_search, &SearchTab::trackPlayRequested,      this, &View::trackPlayRequested);
    connect(m_search, &SearchTab::addToPlaylistRequested,  this, &View::addToPlaylistRequested);
}

} // namespace SidePanel
