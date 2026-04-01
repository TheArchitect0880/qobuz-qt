#pragma once

#include "../util/settings.hpp"

#include <QObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrlQuery>
#include <QUrl>
#include <QCryptographicHash>
#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <functional>

/// Handles Last.fm now-playing updates and scrobbling.
/// Connect to QobuzBackend signals and call onTrackStarted / onPositionChanged / onTrackFinished.
class LastFmScrobbler : public QObject
{
    Q_OBJECT

public:
    explicit LastFmScrobbler(QObject *parent = nullptr)
        : QObject(parent)
        , m_nam(new QNetworkAccessManager(this))
    {}

    /// Authenticate via auth.getMobileSession and store the session key.
    /// callback(success, errorMessage)
    void authenticate(const QString &username, const QString &password,
                      std::function<void(bool, const QString &)> callback)
    {
        const QString apiKey    = AppSettings::instance().lastFmApiKey();
        const QString apiSecret = AppSettings::instance().lastFmApiSecret();
        if (apiKey.isEmpty() || apiSecret.isEmpty()) {
            callback(false, tr("API key or secret is not set."));
            return;
        }

        QMap<QString,QString> params;
        params["method"]   = QStringLiteral("auth.getMobileSession");
        params["api_key"]  = apiKey;
        params["username"] = username;
        params["password"] = password;
        params["api_sig"]  = buildSig(params, apiSecret);
        params["format"]   = QStringLiteral("json");

        auto *reply = m_nam->post(apiRequest(), encodeBody(params));
        connect(reply, &QNetworkReply::finished, this, [reply, callback] {
            reply->deleteLater();
            const QJsonObject obj = QJsonDocument::fromJson(reply->readAll()).object();
            const QString key = obj["session"].toObject()["key"].toString();
            if (!key.isEmpty()) {
                AppSettings::instance().setLastFmSessionKey(key);
                callback(true, {});
            } else {
                callback(false, obj["message"].toString(tr("Authentication failed.")));
            }
        });
    }

    bool isEnabled() const
    {
        return AppSettings::instance().lastFmEnabled()
            && !AppSettings::instance().lastFmApiKey().isEmpty()
            && !AppSettings::instance().lastFmApiSecret().isEmpty()
            && !AppSettings::instance().lastFmSessionKey().isEmpty();
    }

public slots:
    void onTrackStarted(const QJsonObject &track)
    {
        m_title    = track["title"].toString();
        m_artist   = track["performer"].toObject()["name"].toString();
        if (m_artist.isEmpty())
            m_artist = track["album"].toObject()["artist"].toObject()["name"].toString();
        m_album    = track["album"].toObject()["title"].toString();
        m_duration = static_cast<qint64>(track["duration"].toDouble());
        m_startTime       = QDateTime::currentSecsSinceEpoch();
        m_playedSecs      = 0;
        m_lastPosition    = 0;
        m_accumulatedSecs = 0;
        m_scrobbled       = false;

        if (!isEnabled() || m_title.isEmpty() || m_duration < 30) return;
        updateNowPlaying();
    }

    void onPositionChanged(quint64 positionSecs, quint64 /*duration*/)
    {
        // Accumulate actual listening time to prevent false scrobbles from seeking
        if (positionSecs > m_lastPosition && (positionSecs - m_lastPosition) <= 2) {
            m_accumulatedSecs += (positionSecs - m_lastPosition);
        }
        m_lastPosition = positionSecs;
        m_playedSecs = positionSecs;

        if (!isEnabled() || m_scrobbled || m_title.isEmpty() || m_duration < 30) return;

        // Scrobble after 50% or 240 seconds played, whichever comes first, min 30 seconds.
        const quint64 threshold = static_cast<quint64>(qMin((qint64)240, m_duration / 2));
        if (m_accumulatedSecs >= 30 && m_accumulatedSecs >= threshold)
            scrobble();
    }

    void onTrackFinished()
    {
        if (!isEnabled() || m_scrobbled || m_title.isEmpty() || m_duration < 30) return;

        const quint64 threshold = static_cast<quint64>(qMin((qint64)240, m_duration / 2));
        if (m_accumulatedSecs >= 30 && m_accumulatedSecs >= threshold) {
            scrobble();
        }
    }

private:
    QNetworkAccessManager *m_nam = nullptr;

    QString m_title;
    QString m_artist;
    QString m_album;
    qint64  m_duration        = 0;
    qint64  m_startTime       = 0;
    quint64 m_playedSecs      = 0;
    quint64 m_lastPosition    = 0;
    quint64 m_accumulatedSecs = 0;
    bool    m_scrobbled       = false;

    void updateNowPlaying()
    {
        QMap<QString,QString> params;
        params["method"]   = QStringLiteral("track.updateNowPlaying");
        params["api_key"]  = AppSettings::instance().lastFmApiKey();
        params["sk"]       = AppSettings::instance().lastFmSessionKey();
        params["artist"]   = m_artist;
        params["track"]    = m_title;
        params["album"]    = m_album;
        params["duration"] = QString::number(m_duration);
        params["api_sig"]  = buildSig(params, AppSettings::instance().lastFmApiSecret());
        params["format"]   = QStringLiteral("json");

        auto *reply = m_nam->post(apiRequest(), encodeBody(params));
        connect(reply, &QNetworkReply::finished, reply, &QNetworkReply::deleteLater);
    }

    void scrobble()
    {
        m_scrobbled = true;
        QMap<QString,QString> params;
        params["method"]       = QStringLiteral("track.scrobble");
        params["api_key"]      = AppSettings::instance().lastFmApiKey();
        params["sk"]           = AppSettings::instance().lastFmSessionKey();

        params["artist[0]"]    = m_artist;
        params["track[0]"]     = m_title;
        params["album[0]"]     = m_album;
        params["timestamp[0]"] = QString::number(m_startTime);
        params["duration[0]"]  = QString::number(m_duration);

        params["api_sig"]      = buildSig(params, AppSettings::instance().lastFmApiSecret());
        params["format"]       = QStringLiteral("json");

        auto *reply = m_nam->post(apiRequest(), encodeBody(params));
        connect(reply, &QNetworkReply::finished, reply, &QNetworkReply::deleteLater);
    }

    /// Build the Last.fm API signature: sort params, concatenate key+value, append secret, md5.
    static QString buildSig(const QMap<QString,QString> &params, const QString &secret)
    {
        QString s;
        for (auto it = params.constBegin(); it != params.constEnd(); ++it) {
            if (it.key() != "format" && it.key() != "api_sig")
                s += it.key() + it.value();
        }
        s += secret;
        return QCryptographicHash::hash(s.toUtf8(), QCryptographicHash::Md5).toHex();
    }

    static QNetworkRequest apiRequest()
    {
        QNetworkRequest req(QUrl(QStringLiteral("https://ws.audioscrobbler.com/2.0/")));
        req.setHeader(QNetworkRequest::ContentTypeHeader,
                      QStringLiteral("application/x-www-form-urlencoded"));
        return req;
    }

    static QByteArray encodeBody(const QMap<QString,QString> &params)
    {
        QUrlQuery q;
        for (auto it = params.constBegin(); it != params.constEnd(); ++it)
            q.addQueryItem(it.key(), it.value());
        return q.toString(QUrl::FullyEncoded).toUtf8();
    }
};
