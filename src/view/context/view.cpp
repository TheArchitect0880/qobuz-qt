#include "view.hpp"

#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QNetworkRequest>

namespace Context
{

static const QString kButtonStyle = QStringLiteral(
    "QPushButton { border: none; background: none; padding: 0; margin: 0; text-align: center; }"
    "QPushButton:enabled:hover { color: #FFB232; }"
    "QPushButton:!enabled { color: palette(text); }"
);

View::View(QobuzBackend *backend, QWidget *parent)
    : QDockWidget(tr("Now Playing"), parent)
    , m_backend(backend)
{
    setObjectName(QStringLiteral("contextDock"));
    setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable | QDockWidget::DockWidgetClosable);

    m_nam = new QNetworkAccessManager(this);
    connect(m_nam, &QNetworkAccessManager::finished, this, &View::onArtReady);

    auto *container = new QWidget(this);
    auto *layout = new QVBoxLayout(container);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(4);

    m_albumArt = new ArtWidget(container);
    layout->addWidget(m_albumArt);

    // Title (plain label, centered)
    m_title = new QLabel(tr("Not playing"), container);
    m_title->setAlignment(Qt::AlignCenter);
    m_title->setWordWrap(true);
    m_title->setMinimumWidth(0);
    m_title->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    QFont titleFont = m_title->font();
    titleFont.setPointSizeF(titleFont.pointSizeF() * 1.05);
    titleFont.setBold(true);
    m_title->setFont(titleFont);
    layout->addWidget(m_title);

    // Artist (clickable button)
    m_artistBtn = new QPushButton(container);
    m_artistBtn->setFlat(true);
    m_artistBtn->setStyleSheet(kButtonStyle);
    m_artistBtn->setCursor(Qt::PointingHandCursor);
    m_artistBtn->setMinimumWidth(0);
    m_artistBtn->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    m_artistBtn->setEnabled(false);
    layout->addWidget(m_artistBtn);
    connect(m_artistBtn, &QPushButton::clicked, this, [this] {
        const qint64 artistId = static_cast<qint64>(
            m_currentTrack["performer"].toObject()["id"].toDouble());
        if (artistId > 0)
            emit artistRequested(artistId);
    });

    // Album name (clickable button)
    m_albumBtn = new QPushButton(container);
    m_albumBtn->setFlat(true);
    m_albumBtn->setStyleSheet(kButtonStyle);
    m_albumBtn->setCursor(Qt::PointingHandCursor);
    m_albumBtn->setMinimumWidth(0);
    m_albumBtn->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    m_albumBtn->setEnabled(false);
    QFont albumFont = m_albumBtn->font();
    albumFont.setPointSizeF(albumFont.pointSizeF() * 0.9);
    m_albumBtn->setFont(albumFont);
    QPalette albumPal = m_albumBtn->palette();
    albumPal.setColor(QPalette::ButtonText, Colors::SubduedText);
    m_albumBtn->setPalette(albumPal);
    layout->addWidget(m_albumBtn);
    connect(m_albumBtn, &QPushButton::clicked, this, [this] {
        QString albumId = m_currentTrack["album"].toObject()["id"].toString();
        if (albumId.isEmpty()) {
            const auto &idVal = m_currentTrack["album"].toObject()["id"];
            if (idVal.isDouble())
                albumId = QString::number(static_cast<qint64>(idVal.toDouble()));
        }
        if (!albumId.isEmpty())
            emit albumRequested(albumId);
    });

    // Quality info
    m_quality = new QLabel(container);
    m_quality->setAlignment(Qt::AlignCenter);
    m_quality->setMinimumWidth(0);
    m_quality->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    QFont qualFont = m_quality->font();
    qualFont.setPointSizeF(qualFont.pointSizeF() * 0.8);
    m_quality->setFont(qualFont);
    QPalette qualPal = m_quality->palette();
    qualPal.setColor(QPalette::WindowText, Colors::SubduedText);
    m_quality->setPalette(qualPal);
    layout->addWidget(m_quality);

    // Favorite button
    m_favBtn = new QPushButton(container);
    m_favBtn->setFlat(true);
    m_favBtn->setFixedHeight(28);
    m_favBtn->setCursor(Qt::PointingHandCursor);
    m_favBtn->setStyleSheet(QStringLiteral(
        "QPushButton { border: 1px solid #555; border-radius: 4px; padding: 2px 12px;"
        "  background: #2a2a2a; color: #ccc; font-weight: bold; }"
        "QPushButton:hover { border-color: #FFB232; color: #FFB232; }"
        "QPushButton:pressed { background: #333; }"));
    m_favBtn->setText(tr("♡  Favourite"));
    m_favBtn->hide();
    layout->addWidget(m_favBtn, 0, Qt::AlignCenter);
    connect(m_favBtn, &QPushButton::clicked, this, [this] {
        const qint64 trackId = static_cast<qint64>(m_currentTrack["id"].toDouble());
        if (trackId <= 0) return;
        if (m_favTrackIds.contains(trackId))
            emit unfavTrackRequested(trackId);
        else
            emit favTrackRequested(trackId);
    });

    layout->addStretch();
    setWidget(container);

    connect(m_backend, &QobuzBackend::trackChanged, this, &View::onTrackChanged);
}

void View::onTrackChanged(const QJsonObject &track)
{
    m_currentTrack = track;

    const QString title  = track["title"].toString();
    const QString artist = track["performer"].toObject()["name"].toString().isEmpty()
        ? track["album"].toObject()["artist"].toObject()["name"].toString()
        : track["performer"].toObject()["name"].toString();
    const QString albumTitle = track["album"].toObject()["title"].toString();

    m_title->setText(title.isEmpty() ? tr("Not playing") : title);

    m_artistBtn->setText(artist);
    const qint64 artistId = static_cast<qint64>(
        track["performer"].toObject()["id"].toDouble());
    m_artistBtn->setEnabled(artistId > 0);
    m_artistBtn->setCursor(artistId > 0 ? Qt::PointingHandCursor : Qt::ArrowCursor);

    QString albumId = track["album"].toObject()["id"].toString();
    if (albumId.isEmpty() && track["album"].toObject()["id"].isDouble())
        albumId = QString::number(static_cast<qint64>(track["album"].toObject()["id"].toDouble()));
    m_albumBtn->setText(albumTitle);
    m_albumBtn->setEnabled(!albumId.isEmpty());
    m_albumBtn->setCursor(!albumId.isEmpty() ? Qt::PointingHandCursor : Qt::ArrowCursor);
    m_albumBtn->setVisible(!albumTitle.isEmpty());

    // Quality info
    const QJsonObject album = track["album"].toObject();
    const int bits = album["maximum_bit_depth"].toInt();
    const double rate = album["maximum_sampling_rate"].toDouble();
    if (bits > 0 && rate > 0) {
        const QString rateStr = (rate == static_cast<int>(rate))
            ? QString::number(static_cast<int>(rate))
            : QString::number(rate, 'g', 4);
        m_quality->setText(QStringLiteral("%1-bit / %2 kHz").arg(bits).arg(rateStr));
        m_quality->show();
    } else {
        m_quality->hide();
    }

    // Favorite button
    const qint64 trackId = static_cast<qint64>(track["id"].toDouble());
    m_favBtn->setVisible(trackId > 0);
    updateFavButton();

    // Album art
    const QJsonObject img = track["album"].toObject()["image"].toObject();
    QString artUrl = img["large"].toString();
    if (artUrl.isEmpty())
        artUrl = img["small"].toString();

    if (!artUrl.isEmpty() && artUrl != m_currentArtUrl) {
        m_currentArtUrl = artUrl;
        m_nam->get(QNetworkRequest(QUrl(artUrl)));
    }
}

void View::updateFavButton()
{
    const qint64 trackId = static_cast<qint64>(m_currentTrack["id"].toDouble());
    if (trackId <= 0) return;

    const bool isFav = m_favTrackIds.contains(trackId);
    if (isFav) {
        m_favBtn->setText(tr("♥  Favourited"));
        m_favBtn->setStyleSheet(QStringLiteral(
            "QPushButton { border: 1px solid #FFB232; border-radius: 4px; padding: 2px 12px;"
            "  background: #2a2a2a; color: #FFB232; font-weight: bold; }"
            "QPushButton:hover { background: #333; }"
            "QPushButton:pressed { background: #3a3a3a; }"));
    } else {
        m_favBtn->setText(tr("♡  Favourite"));
        m_favBtn->setStyleSheet(QStringLiteral(
            "QPushButton { border: 1px solid #555; border-radius: 4px; padding: 2px 12px;"
            "  background: #2a2a2a; color: #ccc; font-weight: bold; }"
            "QPushButton:hover { border-color: #FFB232; color: #FFB232; }"
            "QPushButton:pressed { background: #333; }"));
    }
}

void View::onArtReady(QNetworkReply *reply)
{
    reply->deleteLater();
    if (reply->error() != QNetworkReply::NoError)
        return;
    if (reply->url().toString() != m_currentArtUrl)
        return;
    QPixmap pix;
    if (pix.loadFromData(reply->readAll()))
        m_albumArt->setPixmap(pix);
}

} // namespace Context
