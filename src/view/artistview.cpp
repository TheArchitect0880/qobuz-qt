#include "artistview.hpp"
#include "albumlistview.hpp"
#include "../model/tracklistmodel.hpp"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QScrollArea>
#include <QHeaderView>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPixmap>
#include <QUrl>
#include <QFont>
#include <QRegularExpression>

// Shared button style (mirrors TrackContextHeader)
static const QString kBtnBase = QStringLiteral(
    "QPushButton { padding: 5px 16px; border-radius: 4px; font-weight: bold; }"
);

// Section-toggle style: flat QPushButton, truly left-aligned
static const QString kToggleStyle = QStringLiteral(
    "QPushButton { text-align: left; font-weight: bold; font-size: 13px;"
    "  padding: 6px 8px; border: none; border-bottom: 1px solid #333;"
    "  background: transparent; }"
    "QPushButton:hover { background: #1e1e1e; }"
);

// ---------------------------------------------------------------------------
// ArtistSection
// ---------------------------------------------------------------------------

ArtistSection::ArtistSection(const QString &title, const QString &releaseType, QWidget *parent)
    : QWidget(parent)
    , m_baseTitle(title)
    , m_releaseType(releaseType)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    m_toggle = new QPushButton(this);
    m_toggle->setCheckable(true);
    m_toggle->setChecked(true);
    m_toggle->setFlat(true);
    m_toggle->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_toggle->setStyleSheet(kToggleStyle);
    layout->addWidget(m_toggle);

    m_list = new AlbumListView(this);
    layout->addWidget(m_list);

    connect(m_toggle, &QPushButton::toggled, this, [this](bool checked) {
        m_list->setVisible(checked);
        updateToggleText();
    });
    connect(m_list, &AlbumListView::albumSelected, this, &ArtistSection::albumSelected);

    updateToggleText();
}

void ArtistSection::setAlbums(const QJsonArray &albums)
{
    m_list->setAlbums(albums);
    updateToggleText();
}

bool ArtistSection::isEmpty() const
{
    return m_list->topLevelItemCount() == 0;
}

QStringList ArtistSection::albumIds() const
{
    QStringList ids;
    for (int i = 0; i < m_list->topLevelItemCount(); ++i) {
        const QString id = m_list->topLevelItem(i)->data(1, Qt::UserRole).toString();
        if (!id.isEmpty())
            ids.append(id);
    }
    return ids;
}

void ArtistSection::setArtistPageMode()
{
    m_list->setArtistPageMode();
}

void ArtistSection::updateToggleText()
{
    const int count = m_list->topLevelItemCount();
    const QString arrow = m_toggle->isChecked() ? QStringLiteral("▼ ") : QStringLiteral("▶ ");
    const QString text  = count > 0
        ? QStringLiteral("%1%2  (%3)").arg(arrow, m_baseTitle).arg(count)
        : arrow + m_baseTitle;
    m_toggle->setText(text);
}

// ---------------------------------------------------------------------------
// ArtistView
// ---------------------------------------------------------------------------

ArtistView::ArtistView(QobuzBackend *backend, PlayQueue *queue, QWidget *parent)
    : QWidget(parent)
    , m_backend(backend)
    , m_queue(queue)
{
    auto *outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(0, 0, 0, 0);
    outerLayout->setSpacing(0);

    // --- Artist header (same structure as TrackContextHeader) ---
    auto *header = new QWidget(this);
    header->setFixedHeight(148);
    auto *hlay = new QHBoxLayout(header);
    hlay->setContentsMargins(12, 8, 12, 8);
    hlay->setSpacing(14);

    m_artLabel = new QLabel(header);
    m_artLabel->setFixedSize(120, 120);
    m_artLabel->setScaledContents(true);
    m_artLabel->setAlignment(Qt::AlignCenter);
    m_artLabel->setStyleSheet(QStringLiteral("background: #1a1a1a; border-radius: 4px;"));
    hlay->addWidget(m_artLabel, 0, Qt::AlignVCenter);

    auto *info = new QWidget(header);
    auto *vlay = new QVBoxLayout(info);
    vlay->setContentsMargins(0, 0, 0, 0);
    vlay->setSpacing(4);

    m_nameLabel = new QLabel(info);
    QFont f = m_nameLabel->font();
    f.setPointSize(f.pointSize() + 5);
    f.setBold(true);
    m_nameLabel->setFont(f);
    vlay->addWidget(m_nameLabel);

    m_bioEdit = new QTextEdit(info);
    m_bioEdit->setReadOnly(true);
    m_bioEdit->setFrameShape(QFrame::NoFrame);
    m_bioEdit->setMaximumHeight(56);
    m_bioEdit->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_bioEdit->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    vlay->addWidget(m_bioEdit);

    auto *btnRow = new QHBoxLayout;
    btnRow->setSpacing(8);
    btnRow->setContentsMargins(0, 4, 0, 0);

    static const QString kOutlineBtn = kBtnBase +
        QStringLiteral("QPushButton { background: #2a2a2a; color: #FFB232; border: 1px solid #FFB232; }"
                       "QPushButton:pressed { background: #333; }");

    m_playBtn = new QPushButton(tr("▶  Play"), info);
    m_playBtn->setStyleSheet(kBtnBase +
        QStringLiteral("QPushButton { background: #FFB232; color: #000; }"
                       "QPushButton:pressed { background: #e09e28; }"));

    m_shuffleTopBtn = new QPushButton(tr("⇄  Shuffle"), info);
    m_shuffleTopBtn->setStyleSheet(kOutlineBtn);

    m_shuffleBtn = new QPushButton(tr("⇄  Shuffle All"), info);
    m_shuffleBtn->setStyleSheet(kOutlineBtn);

    m_favBtn = new QPushButton(tr("♡  Favourite"), info);
    m_favBtn->setStyleSheet(kBtnBase +
        QStringLiteral("QPushButton { background: #2a2a2a; color: #ccc; border: 1px solid #555; }"
                       "QPushButton:pressed { background: #333; }"));

    btnRow->addWidget(m_playBtn);
    btnRow->addWidget(m_shuffleTopBtn);
    btnRow->addWidget(m_shuffleBtn);
    btnRow->addWidget(m_favBtn);
    btnRow->addStretch();
    vlay->addLayout(btnRow);
    vlay->addStretch(1);

    hlay->addWidget(info, 1);
    outerLayout->addWidget(header);

    // --- Network manager for portrait ---
    m_nam = new QNetworkAccessManager(this);
    QObject::connect(m_nam, &QNetworkAccessManager::finished,
                     this, [this](QNetworkReply *reply) {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) return;
        QPixmap pix;
        if (pix.loadFromData(reply->readAll()))
            m_artLabel->setPixmap(pix);
    });

    // --- Scrollable sections area ---
    auto *scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);

    auto *content = new QWidget(scroll);
    auto *sectLayout = new QVBoxLayout(content);
    sectLayout->setContentsMargins(0, 0, 0, 0);
    sectLayout->setSpacing(0);

    // Popular Tracks section — same toggle style as release sections
    m_topTracksSection = new QWidget(content);
    auto *ttLayout = new QVBoxLayout(m_topTracksSection);
    ttLayout->setContentsMargins(0, 0, 0, 0);
    ttLayout->setSpacing(0);

    m_topTracksToggle = new QPushButton(m_topTracksSection);
    m_topTracksToggle->setCheckable(true);
    m_topTracksToggle->setChecked(true);
    m_topTracksToggle->setFlat(true);
    m_topTracksToggle->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_topTracksToggle->setStyleSheet(kToggleStyle);
    ttLayout->addWidget(m_topTracksToggle);

    m_topTracks = new List::Tracks(backend, queue, m_topTracksSection);
    m_topTracks->setMaximumHeight(320);
    // Artist page column layout: hide Artist & Album, match album-section widths
    m_topTracks->setColumnHidden(TrackListModel::ColArtist, true);
    m_topTracks->setColumnHidden(TrackListModel::ColAlbum, true);
    m_topTracks->header()->setSectionResizeMode(TrackListModel::ColNumber,   QHeaderView::Fixed);
    m_topTracks->header()->setSectionResizeMode(TrackListModel::ColTitle,    QHeaderView::Stretch);
    m_topTracks->header()->setSectionResizeMode(TrackListModel::ColDuration, QHeaderView::Fixed);
    m_topTracks->header()->resizeSection(TrackListModel::ColNumber,   40);
    m_topTracks->header()->resizeSection(TrackListModel::ColDuration, 70);
    ttLayout->addWidget(m_topTracks);

    connect(m_topTracksToggle, &QPushButton::toggled, m_topTracks, &QWidget::setVisible);
    connect(m_topTracks, &List::Tracks::playTrackRequested, this, &ArtistView::playTrackRequested);

    sectLayout->addWidget(m_topTracksSection);

    // Release sections
    m_secAlbums       = new ArtistSection(tr("Albums"),        QStringLiteral("album"),       content);
    m_secEps          = new ArtistSection(tr("Singles & EPs"), QStringLiteral("epSingle"),    content);
    m_secLive         = new ArtistSection(tr("Live"),          QStringLiteral("live"),        content);
    m_secCompilations = new ArtistSection(tr("Compilations"),  QStringLiteral("compilation"), content);
    m_secOther        = new ArtistSection(tr("Other"),         QStringLiteral("other"),       content);

    // Uniform column layout: hide Artist column, match fixed widths across all sections
    for (ArtistSection *sec : {m_secAlbums, m_secEps, m_secLive, m_secCompilations, m_secOther})
        sec->setArtistPageMode();

    sectLayout->addWidget(m_secAlbums);
    sectLayout->addWidget(m_secEps);
    sectLayout->addWidget(m_secLive);
    sectLayout->addWidget(m_secCompilations);
    sectLayout->addWidget(m_secOther);
    sectLayout->addStretch();

    scroll->setWidget(content);
    outerLayout->addWidget(scroll, 1);

    // Play / shuffle top tracks
    connect(m_playBtn,       &QPushButton::clicked, m_topTracks, [this] { m_topTracks->playAll(false); });
    connect(m_shuffleTopBtn, &QPushButton::clicked, m_topTracks, [this] { m_topTracks->playAll(true); });

    // Deep shuffle: fetch all album tracks, combine, shuffle, play
    connect(m_shuffleBtn, &QPushButton::clicked, this, [this] {
        const QStringList ids = allAlbumIds();
        if (ids.isEmpty()) return;
        m_shuffleBtn->setEnabled(false);
        m_shuffleBtn->setText(tr("Loading…"));
        m_backend->getAlbumsTracks(ids);
    });

    // Favourite button
    connect(m_favBtn, &QPushButton::clicked, this, [this] {
        if (m_artistId <= 0) return;
        m_isFaved = !m_isFaved;
        if (m_isFaved) {
            m_backend->addFavArtist(m_artistId);
            m_favArtistIds.insert(m_artistId);
        } else {
            m_backend->removeFavArtist(m_artistId);
            m_favArtistIds.remove(m_artistId);
        }
        setFaved(m_isFaved);
    });

    // Album section connections
    connect(m_secAlbums,       &ArtistSection::albumSelected, this, &ArtistView::albumSelected);
    connect(m_secEps,          &ArtistSection::albumSelected, this, &ArtistView::albumSelected);
    connect(m_secLive,         &ArtistSection::albumSelected, this, &ArtistView::albumSelected);
    connect(m_secCompilations, &ArtistSection::albumSelected, this, &ArtistView::albumSelected);
    connect(m_secOther,        &ArtistSection::albumSelected, this, &ArtistView::albumSelected);
}

void ArtistView::setArtist(const QJsonObject &artist)
{
    m_artistId = static_cast<qint64>(artist["id"].toDouble());
    setFaved(m_favArtistIds.contains(m_artistId));

    m_nameLabel->setText(artist["name"].toObject()["display"].toString());

    // Biography: strip HTML tags
    const QString bioHtml = artist["biography"].toObject()["content"].toString();
    if (!bioHtml.isEmpty()) {
        QString plain = bioHtml;
        plain.remove(QRegularExpression(QStringLiteral("<[^>]*>")));
        plain.replace(QStringLiteral("&amp;"),  QStringLiteral("&"));
        plain.replace(QStringLiteral("&lt;"),   QStringLiteral("<"));
        plain.replace(QStringLiteral("&gt;"),   QStringLiteral(">"));
        plain.replace(QStringLiteral("&quot;"), QStringLiteral("\""));
        plain.replace(QStringLiteral("&#39;"),  QStringLiteral("'"));
        plain.replace(QStringLiteral("&nbsp;"), QStringLiteral(" "));
        plain = plain.trimmed();
        m_bioEdit->setPlainText(plain);
        m_bioEdit->setVisible(!plain.isEmpty());
    } else {
        m_bioEdit->setVisible(false);
    }

    // Artist portrait: images.portrait.hash + format → CDN URL
    const QJsonObject portrait = artist["images"].toObject()["portrait"].toObject();
    const QString hash   = portrait["hash"].toString();
    const QString format = portrait["format"].toString();
    QString artUrl;
    if (!hash.isEmpty()) {
        artUrl = QStringLiteral("https://static.qobuz.com/images/artists/covers/large/%1.%2")
                     .arg(hash, format.isEmpty() ? QStringLiteral("jpg") : format);
    } else {
        const QJsonObject img = artist["image"].toObject();
        artUrl = img["large"].toString();
        if (artUrl.isEmpty()) artUrl = img["small"].toString();
    }
    if (!artUrl.isEmpty() && artUrl != m_currentArtUrl) {
        m_currentArtUrl = artUrl;
        m_nam->get(QNetworkRequest(QUrl(artUrl)));
    } else if (artUrl.isEmpty()) {
        m_artLabel->setPixmap(QPixmap());
        m_currentArtUrl.clear();
    }

    // Popular tracks (flat array)
    const QJsonArray topTracks = artist["top_tracks"].toArray();
    m_topTracks->loadTracks(topTracks);

    const int ttCount = topTracks.size();
    disconnect(m_topTracksToggle, &QPushButton::toggled, nullptr, nullptr);
    connect(m_topTracksToggle, &QPushButton::toggled, m_topTracks, &QWidget::setVisible);
    connect(m_topTracksToggle, &QPushButton::toggled, this, [this, ttCount](bool open) {
        const QString a = open ? QStringLiteral("▼ ") : QStringLiteral("▶ ");
        m_topTracksToggle->setText(ttCount > 0
            ? QStringLiteral("%1Popular Tracks  (%2)").arg(a).arg(ttCount)
            : a + tr("Popular Tracks"));
    });
    m_topTracksToggle->setChecked(true);
    m_topTracks->setVisible(true);
    m_topTracksToggle->setText(ttCount > 0
        ? QStringLiteral("▼ Popular Tracks  (%1)").arg(ttCount)
        : QStringLiteral("▼ Popular Tracks"));
    m_topTracksSection->setVisible(!topTracks.isEmpty());

    // Reset shuffle button state
    m_shuffleBtn->setEnabled(true);
    m_shuffleBtn->setText(tr("⇄  Shuffle All"));

    // Clear release sections
    for (ArtistSection *sec : {m_secAlbums, m_secEps, m_secLive, m_secCompilations, m_secOther}) {
        sec->setAlbums({});
        sec->setVisible(false);
    }
}

void ArtistView::setReleases(const QString &releaseType, const QJsonArray &items,
                              bool /*hasMore*/, int /*offset*/)
{
    ArtistSection *sec = nullptr;
    if      (releaseType == QStringLiteral("album"))       sec = m_secAlbums;
    else if (releaseType == QStringLiteral("epSingle"))    sec = m_secEps;
    else if (releaseType == QStringLiteral("live"))        sec = m_secLive;
    else if (releaseType == QStringLiteral("compilation")) sec = m_secCompilations;
    else                                                   sec = m_secOther;

    // Rust auto-paginates, so we always get the full list at once
    sec->setAlbums(items);
    sec->setVisible(!sec->isEmpty());
}

void ArtistView::setFavArtistIds(const QSet<qint64> &ids)
{
    m_favArtistIds = ids;
    if (m_artistId > 0)
        setFaved(ids.contains(m_artistId));
}

void ArtistView::onDeepShuffleTracks(const QJsonArray &tracks)
{
    m_shuffleBtn->setEnabled(true);
    m_shuffleBtn->setText(tr("⇄  Shuffle All"));

    if (tracks.isEmpty()) return;

    m_queue->setContext(tracks, 0);
    m_queue->shuffleNow();

    const QJsonObject first = m_queue->current();
    const qint64 id = static_cast<qint64>(first["id"].toDouble());
    if (id > 0)
        emit playTrackRequested(id);
}

QStringList ArtistView::allAlbumIds() const
{
    QStringList ids;
    for (const ArtistSection *sec : {m_secAlbums, m_secEps, m_secLive, m_secCompilations, m_secOther})
        ids.append(sec->albumIds());
    return ids;
}

void ArtistView::setFaved(bool faved)
{
    m_isFaved = faved;
    if (faved) {
        m_favBtn->setText(tr("♥  Favourited"));
        m_favBtn->setStyleSheet(kBtnBase +
            QStringLiteral("QPushButton { background: #2a2a2a; color: #FFB232; border: 1px solid #FFB232; }"
                           "QPushButton:pressed { background: #333; }"));
    } else {
        m_favBtn->setText(tr("♡  Favourite"));
        m_favBtn->setStyleSheet(kBtnBase +
            QStringLiteral("QPushButton { background: #2a2a2a; color: #ccc; border: 1px solid #555; }"
                           "QPushButton:pressed { background: #333; }"));
    }
}
