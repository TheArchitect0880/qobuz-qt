#pragma once

#include "../util/colors.hpp"

#include <QWidget>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QLabel>
#include <QFont>
#include <QPixmap>
#include <QPushButton>
#include <QStringList>
#include <QJsonObject>
#include <QJsonArray>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>

/// Header strip shown above the track list when an album or playlist is open.
/// Displays album art, title, subtitle, metadata, and Play/Shuffle buttons.
class TrackContextHeader : public QWidget
{
public:
    explicit TrackContextHeader(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        setFixedHeight(148);

        auto *hlay = new QHBoxLayout(this);
        hlay->setContentsMargins(12, 8, 12, 8);
        hlay->setSpacing(14);

        m_art = new QLabel(this);
        m_art->setFixedSize(120, 120);
        m_art->setScaledContents(true);
        m_art->setAlignment(Qt::AlignCenter);
        m_art->setStyleSheet(QStringLiteral("background: #1a1a1a; border-radius: 4px;"));
        hlay->addWidget(m_art, 0, Qt::AlignVCenter);

        auto *info = new QWidget(this);
        auto *vlay = new QVBoxLayout(info);
        vlay->setContentsMargins(0, 0, 0, 0);
        vlay->setSpacing(4);

        m_title = new QLabel(info);
        QFont tf = m_title->font();
        tf.setPointSize(tf.pointSize() + 5);
        tf.setBold(true);
        m_title->setFont(tf);
        m_title->setWordWrap(true);
        m_title->setMinimumWidth(0);
        m_title->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        vlay->addWidget(m_title);

        m_subtitle = new QPushButton(info);
        m_subtitle->setFlat(true);
        m_subtitle->setStyleSheet(QStringLiteral(
            "QPushButton { border: none; background: none; text-align: left; padding: 0; margin: 0; }"
            "QPushButton:enabled:hover { color: #FFB232; }"
            "QPushButton:!enabled { color: palette(text); }"
        ));
        m_subtitle->setMinimumWidth(0);
        m_subtitle->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        QFont sf = m_subtitle->font();
        sf.setPointSize(sf.pointSize() + 1);
        m_subtitle->setFont(sf);
        vlay->addWidget(m_subtitle);

        m_meta = new QLabel(info);
        m_meta->setMinimumWidth(0);
        m_meta->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        QPalette mp = m_meta->palette();
        mp.setColor(QPalette::WindowText, Colors::SubduedText);
        m_meta->setPalette(mp);
        vlay->addWidget(m_meta);

        // Play / Shuffle buttons
        auto *btnRow = new QHBoxLayout;
        btnRow->setSpacing(8);
        btnRow->setContentsMargins(0, 4, 0, 0);

        static const QString btnBase = QStringLiteral(
            "QPushButton {"
            "  padding: 5px 16px;"
            "  border-radius: 4px;"
            "  font-weight: bold;"
            "}"
            "QPushButton:hover { opacity: 0.85; }"
        );

        m_playBtn = new QPushButton(tr("▶  Play"), info);
        m_playBtn->setStyleSheet(btnBase +
            QStringLiteral("QPushButton { background: #FFB232; color: #000; }"
                           "QPushButton:pressed { background: #e09e28; }"));
        btnRow->addWidget(m_playBtn);

        m_shuffleBtn = new QPushButton(tr("⇄  Shuffle"), info);
        m_shuffleBtn->setStyleSheet(btnBase +
            QStringLiteral("QPushButton { background: #2a2a2a; color: #FFB232; border: 1px solid #FFB232; }"
                           "QPushButton:pressed { background: #333; }"));
        btnRow->addWidget(m_shuffleBtn);

        m_favBtn = new QPushButton(tr("♡  Favourite"), info);
        m_favBtn->setStyleSheet(btnBase +
            QStringLiteral("QPushButton { background: #2a2a2a; color: #ccc; border: 1px solid #555; }"
                           "QPushButton:pressed { background: #333; }"));
        m_favBtn->hide();
        btnRow->addWidget(m_favBtn);

        m_followBtn = new QPushButton(tr("Follow"), info);
        m_followBtn->setStyleSheet(btnBase +
            QStringLiteral("QPushButton { background: #2a2a2a; color: #ddd; border: 1px solid #666; }"
                           "QPushButton:pressed { background: #333; }"));
        m_followBtn->hide();
        btnRow->addWidget(m_followBtn);

        m_downloadBtn = new QPushButton(tr("Download"), info);
        m_downloadBtn->setStyleSheet(btnBase +
            QStringLiteral("QPushButton { background: #2a2a2a; color: #ddd; border: 1px solid #666; }"
                           "QPushButton:pressed { background: #333; }"));
        m_downloadBtn->hide();
        btnRow->addWidget(m_downloadBtn);

        btnRow->addStretch();
        vlay->addLayout(btnRow);
        vlay->addStretch(1);

        hlay->addWidget(info, 1);

        m_nam = new QNetworkAccessManager(this);
        QObject::connect(m_nam, &QNetworkAccessManager::finished,
                [this](QNetworkReply *reply) {
            reply->deleteLater();
            if (reply->error() != QNetworkReply::NoError)
                return;
            QPixmap pix;
            if (pix.loadFromData(reply->readAll()))
                m_art->setPixmap(pix);
        });
    }

    QPushButton *playButton()    { return m_playBtn; }
    QPushButton *shuffleButton() { return m_shuffleBtn; }
    QPushButton *favButton()     { return m_favBtn; }
    QPushButton *followButton()  { return m_followBtn; }
    QPushButton *downloadButton() { return m_downloadBtn; }

    QPushButton *subtitleButton() { return m_subtitle; }
    QString albumId() const      { return m_albumId; }
    bool albumFaved() const      { return m_albumFaved; }
    qint64 artistId() const      { return m_artistId; }
    qint64 playlistId() const    { return m_playlistId; }
    bool playlistFollowed() const { return m_playlistFollowed; }
    bool playlistOwned() const    { return m_playlistOwned; }

    void setAlbum(const QJsonObject &album, bool isFaved)
    {
        const QString base = album["title"].toString();
        const QString ver  = album["version"].toString().trimmed();
        m_title->setText(ver.isEmpty() ? base : base + QStringLiteral(" (") + ver + QLatin1Char(')'));
        m_albumId = album["id"].toString();
        if (m_albumId.isEmpty() && album["id"].isDouble())
            m_albumId = QString::number(static_cast<qint64>(album["id"].toDouble()));
        m_artistId = static_cast<qint64>(album["artist"].toObject()["id"].toDouble());
        m_subtitle->setText(album["artist"].toObject()["name"].toString());
        m_subtitle->setEnabled(m_artistId > 0);
        m_subtitle->setCursor(m_artistId > 0 ? Qt::PointingHandCursor : Qt::ArrowCursor);
        m_meta->setText(buildAlbumMeta(album));

        setAlbumFaved(isFaved);
        m_favBtn->setEnabled(!m_albumId.isEmpty());
        m_favBtn->show();
        m_downloadBtn->setText(tr("Download Album"));
        m_downloadBtn->setEnabled(!m_albumId.isEmpty());
        m_downloadBtn->show();

        m_followBtn->hide();
        m_playlistId = 0;
        m_playlistFollowed = false;
        m_playlistOwned = false;
        fetchArt(album["image"].toObject());
        show();
    }

    void setPlaylist(const QJsonObject &playlist, bool isFollowed, bool isOwned)
    {
        m_title->setText(playlist["name"].toString());
        m_artistId = 0;
        m_playlistId = static_cast<qint64>(playlist["id"].toDouble());
        m_playlistFollowed = isFollowed;
        m_playlistOwned = isOwned;
        const QString desc  = playlist["description"].toString();
        const QString owner = playlist["owner"].toObject()["name"].toString();
        m_subtitle->setText(desc.isEmpty() ? owner : desc);
        m_subtitle->setEnabled(false);
        m_subtitle->setCursor(Qt::ArrowCursor);
        m_meta->setText(buildPlaylistMeta(playlist));

        m_albumId.clear();
        m_albumFaved = false;
        m_favBtn->hide();

        if (m_playlistOwned) {
            m_followBtn->setText(tr("Owned"));
            m_followBtn->setEnabled(false);
            m_followBtn->show();
        } else {
            m_followBtn->setText(m_playlistFollowed ? tr("Unfollow") : tr("Follow"));
            m_followBtn->setEnabled(m_playlistId > 0);
            m_followBtn->show();
        }

        // Try images300 → images150 → images (API returns mosaic arrays, not image_rectangle)
        const QJsonArray imgs300 = playlist["images300"].toArray();
        const QJsonArray imgs150 = playlist["images150"].toArray();
        const QJsonArray imgs    = playlist["images"].toArray();
        const QJsonArray &best   = !imgs300.isEmpty() ? imgs300
                                 : !imgs150.isEmpty() ? imgs150 : imgs;
        if (!best.isEmpty())
            fetchUrl(best.first().toString());
        else
            m_art->setPixmap(QPixmap());

        m_downloadBtn->setText(tr("Download Playlist"));
        m_downloadBtn->setEnabled(m_playlistId > 0);
        m_downloadBtn->show();

        show();
    }

    void setPlaylistFollowed(bool followed)
    {
        m_playlistFollowed = followed;
        if (!m_playlistOwned)
            m_followBtn->setText(m_playlistFollowed ? tr("Unfollow") : tr("Follow"));
    }

    void setAlbumFaved(bool faved)
    {
        m_albumFaved = faved;
        if (faved) {
            m_favBtn->setText(tr("♥  Favourited"));
            m_favBtn->setStyleSheet(QStringLiteral(
                "QPushButton { padding: 5px 16px; border-radius: 4px; font-weight: bold;"
                "  background: #2a2a2a; color: #FFB232; border: 1px solid #FFB232; }"
                "QPushButton:pressed { background: #333; }"));
        } else {
            m_favBtn->setText(tr("♡  Favourite"));
            m_favBtn->setStyleSheet(QStringLiteral(
                "QPushButton { padding: 5px 16px; border-radius: 4px; font-weight: bold;"
                "  background: #2a2a2a; color: #ccc; border: 1px solid #555; }"
                "QPushButton:pressed { background: #333; }"));
        }
    }

private:
    void fetchArt(const QJsonObject &img)
    {
        QString url = img["large"].toString();
        if (url.isEmpty()) url = img["small"].toString();
        fetchUrl(url);
    }

    void fetchUrl(const QString &url)
    {
        if (url.isEmpty()) {
            m_art->setPixmap(QPixmap());
            return;
        }
        if (url == m_currentArtUrl)
            return;
        m_currentArtUrl = url;
        m_nam->get(QNetworkRequest(QUrl(url)));
    }

    static QString formatDuration(int totalSecs)
    {
        const int h = totalSecs / 3600;
        const int m = (totalSecs % 3600) / 60;
        if (h > 0)
            return QStringLiteral("%1h %2m").arg(h).arg(m);
        return QStringLiteral("%1 min").arg(m);
    }

    static QString buildAlbumMeta(const QJsonObject &album)
    {
        QStringList parts;
        const QString year = album["release_date_original"].toString().left(4);
        if (!year.isEmpty()) parts << year;
        const int tracks = album["tracks_count"].toInt();
        if (tracks > 0) parts << QStringLiteral("%1 tracks").arg(tracks);
        const int dur = static_cast<int>(album["duration"].toDouble());
        if (dur > 0) parts << formatDuration(dur);
        const int    bits = album["maximum_bit_depth"].toInt();
        const double rate = album["maximum_sampling_rate"].toDouble();
        if (bits > 0 && rate > 0) {
            const QString rateStr = (rate == static_cast<int>(rate))
                ? QString::number(static_cast<int>(rate))
                : QString::number(rate, 'g', 4);
            parts << QStringLiteral("%1-bit / %2 kHz").arg(bits).arg(rateStr);
        }
        return parts.join(QStringLiteral("  ·  "));
    }

    static QString buildPlaylistMeta(const QJsonObject &playlist)
    {
        QStringList parts;
        const int tracks = playlist["tracks_count"].toInt();
        if (tracks > 0) parts << QStringLiteral("%1 tracks").arg(tracks);
        const int dur = static_cast<int>(playlist["duration"].toDouble());
        if (dur > 0) parts << formatDuration(dur);
        return parts.join(QStringLiteral("  ·  "));
    }

    QLabel                *m_art        = nullptr;
    QLabel                *m_title      = nullptr;
    QPushButton           *m_subtitle   = nullptr;
    QLabel                *m_meta       = nullptr;
    QPushButton           *m_playBtn    = nullptr;
    QPushButton           *m_shuffleBtn = nullptr;
    QPushButton           *m_favBtn     = nullptr;
    QPushButton           *m_followBtn  = nullptr;
    QPushButton           *m_downloadBtn = nullptr;
    QNetworkAccessManager *m_nam        = nullptr;
    QString                m_currentArtUrl;
    QString                m_albumId;
    bool                   m_albumFaved = false;
    qint64                 m_artistId   = 0;
    qint64                 m_playlistId = 0;
    bool                   m_playlistFollowed = false;
    bool                   m_playlistOwned = false;
};
