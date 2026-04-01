#include "view.hpp"

#include <QWidget>
#include <QVBoxLayout>
#include <QNetworkRequest>

namespace Context
{

View::View(QobuzBackend *backend, QWidget *parent)
    : QDockWidget(tr("Now Playing"), parent)
    , m_backend(backend)
{
    setObjectName(QStringLiteral("contextDock"));
    setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetClosable);

    m_nam = new QNetworkAccessManager(this);
    connect(m_nam, &QNetworkAccessManager::finished, this, &View::onArtReady);

    auto *container = new QWidget(this);
    auto *layout = new QVBoxLayout(container);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(6);

    m_albumArt = new ArtWidget(container);
    layout->addWidget(m_albumArt);

    m_title = new QLabel(tr("Not playing"), container);
    m_title->setAlignment(Qt::AlignCenter);
    m_title->setWordWrap(true);
    QFont titleFont = m_title->font();
    titleFont.setPointSizeF(titleFont.pointSizeF() * 1.05);
    titleFont.setBold(true);
    m_title->setFont(titleFont);
    layout->addWidget(m_title);

    m_artist = new QLabel(QString(), container);
    m_artist->setAlignment(Qt::AlignCenter);
    m_artist->setWordWrap(true);
    layout->addWidget(m_artist);

    layout->addStretch();
    setWidget(container);

    connect(m_backend, &QobuzBackend::trackChanged, this, &View::onTrackChanged);
}

void View::onTrackChanged(const QJsonObject &track)
{
    const QString title  = track["title"].toString();
    const QString artist = track["performer"].toObject()["name"].toString().isEmpty()
        ? track["album"].toObject()["artist"].toObject()["name"].toString()
        : track["performer"].toObject()["name"].toString();

    m_title->setText(title.isEmpty() ? tr("Not playing") : title);
    m_artist->setText(artist);

    const QJsonObject img = track["album"].toObject()["image"].toObject();
    QString artUrl = img["large"].toString();
    if (artUrl.isEmpty())
        artUrl = img["small"].toString();

    if (!artUrl.isEmpty() && artUrl != m_currentArtUrl) {
        m_currentArtUrl = artUrl;
        m_nam->get(QNetworkRequest(QUrl(artUrl)));
    }
}

void View::onArtReady(QNetworkReply *reply)
{
    reply->deleteLater();
    if (reply->error() != QNetworkReply::NoError)
        return;
    QPixmap pix;
    if (pix.loadFromData(reply->readAll()))
        m_albumArt->setPixmap(pix);
}

} // namespace Context
