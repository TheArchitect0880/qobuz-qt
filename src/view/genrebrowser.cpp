#include "genrebrowser.hpp"

#include <QAction>
#include <QDialog>
#include <QDialogButtonBox>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTreeWidgetItem>
#include <QVBoxLayout>

#include <algorithm>

GenreBrowserView::GenreBrowserView(QobuzBackend *backend, PlayQueue *queue, QWidget *parent)
    : QWidget(parent)
    , m_backend(backend)
    , m_queue(queue)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto *topBar = new QWidget(this);
    auto *topLayout = new QHBoxLayout(topBar);
    topLayout->setContentsMargins(8, 6, 8, 6);
    topLayout->setSpacing(6);

    QFont topFont = topBar->font();
    if (topFont.pointSize() > 0)
        topFont.setPointSize(topFont.pointSize() + 1);
    static constexpr int controlHeight = 30;

    m_browseLabel = new QLabel(tr("Show:"), this);
    m_browseLabel->setFont(topFont);
    topLayout->addWidget(m_browseLabel);
    m_kindCombo = new QComboBox(this);
    m_kindCombo->addItem(tr("Albums"), QStringLiteral("albums"));
    m_kindCombo->addItem(tr("Playlists"), QStringLiteral("playlists"));
    m_kindCombo->setFont(topFont);
    m_kindCombo->setMinimumWidth(110);
    m_kindCombo->setFixedHeight(controlHeight);
    topLayout->addWidget(m_kindCombo);

    m_gapAfterKind = new QWidget(this);
    m_gapAfterKind->setFixedWidth(6);
    topLayout->addWidget(m_gapAfterKind);

    m_genreLabel = new QLabel(tr("Genre:"), this);
    m_genreLabel->setFont(topFont);
    topLayout->addWidget(m_genreLabel);
    m_genreCombo = new QComboBox(this);
    m_genreCombo->setFont(topFont);
    m_genreCombo->setMinimumWidth(180);
    m_genreCombo->setFixedHeight(controlHeight);
    topLayout->addWidget(m_genreCombo);

    m_gapAfterGenre = new QWidget(this);
    m_gapAfterGenre->setFixedWidth(10);
    topLayout->addWidget(m_gapAfterGenre);

    m_typeLabel = new QLabel(tr("Type:"), this);
    m_typeLabel->setFont(topFont);
    topLayout->addWidget(m_typeLabel);
    m_typeCombo = new QComboBox(this);
    m_typeCombo->setFont(topFont);
    m_typeCombo->setMinimumWidth(180);
    m_typeCombo->setFixedHeight(controlHeight);
    topLayout->addWidget(m_typeCombo);

    m_playlistSearchLabel = new QLabel(tr("Search:"), this);
    m_playlistSearchLabel->setFont(topFont);
    m_playlistSearchLabel->setVisible(false);
    topLayout->addWidget(m_playlistSearchLabel);

    m_playlistSearchBox = new QLineEdit(this);
    m_playlistSearchBox->setFont(topFont);
    m_playlistSearchBox->setPlaceholderText(tr("Search playlists..."));
    m_playlistSearchBox->setClearButtonEnabled(true);
    m_playlistSearchBox->setVisible(false);
    m_playlistSearchBox->setMinimumWidth(220);
    m_playlistSearchBox->setMaximumWidth(320);
    m_playlistSearchBox->setFixedHeight(controlHeight);
    topLayout->addWidget(m_playlistSearchBox);

    m_playlistSearchBtn = new QPushButton(tr("Go"), this);
    m_playlistSearchBtn->setFont(topFont);
    m_playlistSearchBtn->setVisible(false);
    m_playlistSearchBtn->setFixedHeight(controlHeight);
    topLayout->addWidget(m_playlistSearchBtn);

    m_deepShuffleBtn = new QPushButton(tr("⇄  Deep Shuffle"), this);
    m_deepShuffleBtn->setFont(topFont);
    m_deepShuffleBtn->setVisible(false);
    m_deepShuffleBtn->setFixedHeight(controlHeight);
    topLayout->addWidget(m_deepShuffleBtn);

    topLayout->addStretch();
    layout->addWidget(topBar);

    m_resultsStack = new QStackedWidget(this);

    m_albumList = new AlbumListView(this);
    m_albumList->setContextMenuPolicy(Qt::CustomContextMenu);

    m_playlistList = new QTreeWidget(this);
    m_playlistList->setColumnCount(4);
    m_playlistList->setHeaderLabels({tr(""), tr("Playlist"), tr("Owner"), tr("Tracks")});
    m_playlistList->setRootIsDecorated(false);
    m_playlistList->setAlternatingRowColors(true);
    m_playlistList->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_playlistList->setSortingEnabled(true);
    m_playlistList->setContextMenuPolicy(Qt::CustomContextMenu);
    m_playlistList->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_playlistList->header()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_playlistList->header()->setSectionResizeMode(2, QHeaderView::Stretch);
    m_playlistList->header()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    m_playlistList->header()->setStretchLastSection(false);

    m_resultsStack->addWidget(m_albumList);
    m_resultsStack->addWidget(m_playlistList);
    layout->addWidget(m_resultsStack, 1);

    connect(m_backend, &QobuzBackend::genresLoaded,
            this, &GenreBrowserView::onGenresLoaded);
    connect(m_backend, &QobuzBackend::featuredAlbumsLoaded,
            this, &GenreBrowserView::onFeaturedAlbumsLoaded);
    connect(m_backend, &QobuzBackend::featuredPlaylistsLoaded,
            this, &GenreBrowserView::onFeaturedPlaylistsLoaded);
    connect(m_backend, &QobuzBackend::discoverPlaylistsLoaded,
            this, &GenreBrowserView::onDiscoverPlaylistsLoaded);
    connect(m_backend, &QobuzBackend::playlistSearchLoaded,
            this, &GenreBrowserView::onPlaylistSearchLoaded);

    connect(m_genreCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int index) {
        const QString data = m_genreCombo->itemData(index).toString();
        if (data == QStringLiteral("__multi__")) {
            if (!chooseMultiGenres()) {
                const QSignalBlocker blocker(m_genreCombo);
                m_genreCombo->setCurrentIndex(m_lastGenreComboIndex);
                return;
            }
            m_lastGenreComboIndex = index;
            updateMultiGenreLabel();
        } else {
            m_lastGenreComboIndex = index;
            if (data == QStringLiteral("__all__")) {
                m_multiGenreIds.clear();
                updateMultiGenreLabel();
            }
        }
        onSelectionChanged();
    });
    connect(m_kindCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int) {
        refreshGenreTypeChoices();
        onSelectionChanged();
    });
    connect(m_typeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &GenreBrowserView::onSelectionChanged);
    connect(m_playlistSearchBox, &QLineEdit::returnPressed,
            this, &GenreBrowserView::onSelectionChanged);
    connect(m_playlistSearchBtn, &QPushButton::clicked,
            this, &GenreBrowserView::onSelectionChanged);
    connect(m_deepShuffleBtn, &QPushButton::clicked,
            this, &GenreBrowserView::onDeepShuffleClicked);
    connect(m_albumList, &AlbumListView::albumSelected,
            this, &GenreBrowserView::albumSelected);
    connect(m_albumList, &QTreeWidget::customContextMenuRequested,
            this, &GenreBrowserView::onAlbumContextMenu);
    connect(m_playlistList, &QTreeWidget::itemDoubleClicked,
            this, &GenreBrowserView::onPlaylistActivated);
    connect(m_playlistList, &QTreeWidget::customContextMenuRequested,
            this, &GenreBrowserView::onPlaylistContextMenu);

    m_kindCombo->setCurrentIndex(0);
    refreshModeUi();
}

void GenreBrowserView::ensureGenresLoaded()
{
    if (!m_genresLoaded)
        m_backend->getGenres();
}

void GenreBrowserView::setBrowseMode(BrowseMode mode)
{
    if (m_mode == mode)
        return;

    m_mode = mode;
    refreshModeUi();
    onSelectionChanged();
}

void GenreBrowserView::refreshModeUi()
{
    const bool genreMode = (m_mode == BrowseMode::Genres);

    m_browseLabel->setVisible(genreMode);
    m_kindCombo->setVisible(genreMode);
    m_gapAfterKind->setVisible(genreMode);
    m_genreLabel->setVisible(genreMode);
    m_genreCombo->setVisible(genreMode);
    m_gapAfterGenre->setVisible(genreMode);
    m_typeLabel->setVisible(genreMode);
    m_typeCombo->setVisible(genreMode);

    if (genreMode) {
        m_playlistSearchBox->setVisible(false);
        m_playlistSearchLabel->setVisible(false);
        m_playlistSearchBtn->setVisible(false);
        m_deepShuffleBtn->setVisible(m_kindCombo->currentData().toString() == QStringLiteral("albums"));
        refreshGenreTypeChoices();
        return;
    }

    m_typeCombo->blockSignals(true);
    m_typeCombo->clear();
    m_typeCombo->addItem(tr("Search"), QStringLiteral("search"));
    m_typeCombo->blockSignals(false);
    m_playlistSearchLabel->setVisible(true);
    m_playlistSearchBox->setVisible(true);
    m_playlistSearchBtn->setVisible(true);
    m_deepShuffleBtn->setVisible(false);
    m_resultsStack->setCurrentIndex(1);
}

void GenreBrowserView::refreshGenreTypeChoices()
{
    m_typeCombo->blockSignals(true);
    m_typeCombo->clear();

    const QString kind = m_kindCombo->currentData().toString();
    if (kind == QStringLiteral("playlists")) {
        m_typeCombo->addItem(tr("Featured: Last Created"), QStringLiteral("last-created"));
        m_typeCombo->addItem(tr("Discover: New"), QStringLiteral("discover-new"));
        m_typeCombo->addItem(tr("Discover: Hi-Res"), QStringLiteral("discover-hires"));
        m_typeCombo->addItem(tr("Discover: Focus"), QStringLiteral("discover-focus"));
        m_typeCombo->addItem(tr("Discover: Qobuz Digs"), QStringLiteral("discover-qobuzdigs"));
        m_resultsStack->setCurrentIndex(1);
        m_deepShuffleBtn->setVisible(false);
    } else {
        m_typeCombo->addItem(tr("New Releases"), QStringLiteral("new-releases"));
        m_typeCombo->addItem(tr("Best Sellers"), QStringLiteral("best-sellers"));
        m_typeCombo->addItem(tr("Most Streamed"), QStringLiteral("most-streamed"));
        m_typeCombo->addItem(tr("Editor Picks"), QStringLiteral("editor-picks"));
        m_typeCombo->addItem(tr("Press Awards"), QStringLiteral("press-awards"));
        m_resultsStack->setCurrentIndex(0);
        m_deepShuffleBtn->setVisible(m_mode == BrowseMode::Genres);
    }

    m_typeCombo->blockSignals(false);
}

QString GenreBrowserView::currentGenreIds() const
{
    const QString data = m_genreCombo->currentData().toString();
    if (data == QStringLiteral("__all__"))
        return QString();

    if (data == QStringLiteral("__multi__")) {
        if (m_multiGenreIds.isEmpty())
            return QString();

        QList<qint64> ids = m_multiGenreIds.values();
        std::sort(ids.begin(), ids.end());
        QStringList out;
        out.reserve(ids.size());
        for (qint64 id : ids)
            out.push_back(QString::number(id));
        return out.join(QLatin1Char(','));
    }

    return data;
}

bool GenreBrowserView::chooseMultiGenres()
{
    QDialog dlg(this);
    dlg.setWindowTitle(tr("Select genres"));
    dlg.resize(320, 420);

    auto *layout = new QVBoxLayout(&dlg);
    auto *list = new QListWidget(&dlg);
    list->setAlternatingRowColors(true);
    layout->addWidget(list, 1);

    for (int i = 0; i < m_genreCombo->count(); ++i) {
        const QString data = m_genreCombo->itemData(i).toString();
        if (data == QStringLiteral("__all__") || data == QStringLiteral("__multi__"))
            continue;

        auto *item = new QListWidgetItem(m_genreCombo->itemText(i), list);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        const qint64 id = data.toLongLong();
        item->setData(Qt::UserRole, id);
        item->setCheckState(m_multiGenreIds.contains(id) ? Qt::Checked : Qt::Unchecked);
    }

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    if (dlg.exec() != QDialog::Accepted)
        return false;

    m_multiGenreIds.clear();
    for (int i = 0; i < list->count(); ++i) {
        QListWidgetItem *item = list->item(i);
        if (item->checkState() == Qt::Checked)
            m_multiGenreIds.insert(item->data(Qt::UserRole).toLongLong());
    }
    return true;
}

void GenreBrowserView::updateMultiGenreLabel()
{
    const int multiIndex = m_genreCombo->count() - 1;
    if (multiIndex < 0)
        return;

    const QString text = m_multiGenreIds.isEmpty()
        ? tr("Multiple...")
        : tr("Multiple (%1)").arg(m_multiGenreIds.size());
    m_genreCombo->setItemText(multiIndex, text);
}

void GenreBrowserView::onGenresLoaded(const QJsonObject &result)
{
    m_genresLoaded = true;
    m_genreCombo->blockSignals(true);
    m_genreCombo->clear();
    m_genreCombo->addItem(tr("All genres"), QStringLiteral("__all__"));

    const QJsonArray items = result["items"].toArray();
    for (const auto &value : items) {
        const QJsonObject genre = value.toObject();
        m_genreCombo->addItem(
            genre["name"].toString(),
            static_cast<qint64>(genre["id"].toDouble()));
    }

    m_genreCombo->addItem(tr("Multiple..."), QStringLiteral("__multi__"));
    updateMultiGenreLabel();
    m_lastGenreComboIndex = 0;
    m_genreCombo->setCurrentIndex(0);

    m_genreCombo->blockSignals(false);
    onSelectionChanged();
}

void GenreBrowserView::onFeaturedAlbumsLoaded(const QJsonObject &result)
{
    m_resultsStack->setCurrentIndex(0);
    m_albumList->setAlbums(result["items"].toArray());
}

void GenreBrowserView::onFeaturedPlaylistsLoaded(const QJsonObject &result)
{
    m_resultsStack->setCurrentIndex(1);
    setPlaylistItems(result["items"].toArray());
}

void GenreBrowserView::onDiscoverPlaylistsLoaded(const QJsonObject &result)
{
    m_resultsStack->setCurrentIndex(1);
    setPlaylistItems(result["items"].toArray());
}

void GenreBrowserView::onPlaylistSearchLoaded(const QJsonObject &result)
{
    m_resultsStack->setCurrentIndex(1);
    setPlaylistItems(result["items"].toArray());
}

void GenreBrowserView::onSelectionChanged()
{
    if (m_mode == BrowseMode::PlaylistSearch) {
        m_resultsStack->setCurrentIndex(1);
        m_playlistSearchLabel->setVisible(true);
        m_playlistSearchBox->setVisible(true);
        m_playlistSearchBtn->setVisible(true);
        m_deepShuffleBtn->setVisible(false);
        const QString query = m_playlistSearchBox->text().trimmed();
        if (query.size() < 2) {
            m_playlistList->clear();
        } else {
            m_backend->searchPlaylists(query, 8, 0);
        }
        return;
    }

    if (m_genreCombo->count() == 0)
        return;

    const QString genreIds = currentGenreIds();
    const QString type = m_typeCombo->currentData().toString();
    const QString kind = m_kindCombo->currentData().toString();
    m_playlistSearchLabel->setVisible(false);
    m_playlistSearchBox->setVisible(false);
    m_playlistSearchBtn->setVisible(false);

    if (kind == QStringLiteral("playlists")) {
        m_resultsStack->setCurrentIndex(1);
        if (type == QStringLiteral("discover-new"))
            m_backend->discoverPlaylists(genreIds, QStringLiteral("new"), 25, 0);
        else if (type == QStringLiteral("discover-hires"))
            m_backend->discoverPlaylists(genreIds, QStringLiteral("hi-res"), 25, 0);
        else if (type == QStringLiteral("discover-focus"))
            m_backend->discoverPlaylists(genreIds, QStringLiteral("focus"), 25, 0);
        else if (type == QStringLiteral("discover-qobuzdigs"))
            m_backend->discoverPlaylists(genreIds, QStringLiteral("qobuzdigs"), 25, 0);
        else
            m_backend->getFeaturedPlaylists(genreIds, type, 25, 0);
    } else {
        m_resultsStack->setCurrentIndex(0);
        m_deepShuffleBtn->setVisible(m_mode == BrowseMode::Genres);
        m_backend->getFeaturedAlbums(genreIds, type, 50, 0);
    }
}

QStringList GenreBrowserView::currentAlbumIds() const
{
    QStringList ids;
    for (int i = 0; i < m_albumList->topLevelItemCount(); ++i) {
        const QString id = m_albumList->topLevelItem(i)->data(1, Qt::UserRole).toString();
        if (!id.isEmpty())
            ids.push_back(id);
    }
    return ids;
}

void GenreBrowserView::onDeepShuffleClicked()
{
    const QStringList albumIds = currentAlbumIds();
    if (albumIds.isEmpty())
        return;

    m_waitingDeepShuffle = true;
    m_deepShuffleBtn->setEnabled(false);
    m_deepShuffleBtn->setText(tr("Loading…"));
    m_backend->getAlbumsTracks(albumIds);
}

bool GenreBrowserView::tryHandleDeepShuffleTracks(const QJsonArray &tracks)
{
    if (!m_waitingDeepShuffle)
        return false;

    m_waitingDeepShuffle = false;
    m_deepShuffleBtn->setEnabled(true);
    m_deepShuffleBtn->setText(tr("⇄  Deep Shuffle"));

    if (tracks.isEmpty())
        return true;

    m_queue->setContext(tracks, 0);
    m_queue->shuffleNow();
    const QJsonObject first = m_queue->current();
    const qint64 id = static_cast<qint64>(first["id"].toDouble());
    if (id > 0)
        emit playTrackRequested(id);
    return true;
}

void GenreBrowserView::onAlbumContextMenu(const QPoint &pos)
{
    QTreeWidgetItem *item = m_albumList->itemAt(pos);
    if (!item)
        return;

    const QString albumId = item->data(1, Qt::UserRole).toString();
    const qint64 artistId = item->data(2, Qt::UserRole).toLongLong();

    QMenu menu(this);

    auto *openAlbum = menu.addAction(tr("Open Album"));
    connect(openAlbum, &QAction::triggered, this, [this, albumId] {
        emit albumSelected(albumId);
    });

    if (artistId > 0) {
        auto *openArtist = menu.addAction(tr("Open Artist"));
        connect(openArtist, &QAction::triggered, this, [this, artistId] {
            emit artistSelected(artistId);
        });
    }

    menu.exec(m_albumList->viewport()->mapToGlobal(pos));
}

void GenreBrowserView::onPlaylistActivated(QTreeWidgetItem *item, int)
{
    if (!item)
        return;

    const qint64 playlistId = item->data(0, Qt::UserRole).toLongLong();
    if (playlistId > 0)
        emit playlistSelected(playlistId);
}

void GenreBrowserView::onPlaylistContextMenu(const QPoint &pos)
{
    QTreeWidgetItem *item = m_playlistList->itemAt(pos);
    if (!item)
        return;

    const qint64 playlistId = item->data(0, Qt::UserRole).toLongLong();
    if (playlistId <= 0)
        return;

    QMenu menu(this);
    auto *openPlaylist = menu.addAction(tr("Open Playlist"));
    connect(openPlaylist, &QAction::triggered, this, [this, playlistId] {
        emit playlistSelected(playlistId);
    });
    menu.exec(m_playlistList->viewport()->mapToGlobal(pos));
}

void GenreBrowserView::setPlaylistItems(const QJsonArray &items)
{
    m_playlistList->clear();

    QFont tagFont;
    tagFont.setBold(true);
    tagFont.setPointSizeF(tagFont.pointSizeF() * 0.85);

    for (const auto &value : items) {
        const QJsonObject playlist = value.toObject();
        const qint64 playlistId = static_cast<qint64>(playlist["id"].toDouble());
        const QString name = playlist["name"].toString();
        const QString owner = playlist["owner"].toObject()["name"].toString();
        const int tracksCount = playlist["tracks_count"].toInt();

        auto *item = new QTreeWidgetItem(m_playlistList,
            QStringList{QStringLiteral("P"), name, owner, tracksCount > 0 ? QString::number(tracksCount) : QString()});
        item->setData(0, Qt::UserRole, playlistId);
        item->setForeground(0, QColor(QStringLiteral("#2B7CD3")));
        item->setFont(0, tagFont);
        item->setTextAlignment(0, Qt::AlignCenter);
    }
}
