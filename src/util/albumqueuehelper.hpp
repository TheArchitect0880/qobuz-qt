#pragma once

#include "../backend/qobuzbackend.hpp"
#include "../playqueue.hpp"

#include <QObject>
#include <QJsonArray>
#include <QJsonObject>
#include <QString>

/// Fetches an album's tracks and queues them (play-next or add-to-queue).
/// Listens to QobuzBackend::albumLoaded and matches by album ID.
class AlbumQueueHelper : public QObject
{
public:
    enum Action { PlayNext, AddToQueue };

    explicit AlbumQueueHelper(QobuzBackend *backend, PlayQueue *queue, QObject *parent = nullptr)
        : QObject(parent), m_backend(backend), m_queue(queue)
    {
        connect(m_backend, &QobuzBackend::albumLoaded, this, [this](const QJsonObject &album) {
            if (m_pendingId.isEmpty()) return;
            QString id = album["id"].toString();
            if (id.isEmpty() && album["id"].isDouble())
                id = QString::number(static_cast<qint64>(album["id"].toDouble()));
            if (id != m_pendingId) return;

            const QJsonArray tracks = album["tracks"].toObject()["items"].toArray();
            if (m_pendingAction == PlayNext) {
                for (int i = tracks.size() - 1; i >= 0; --i) {
                    const QJsonObject t = tracks[i].toObject();
                    if (t["streamable"].toBool(true))
                        m_queue->playNext(t);
                }
            } else {
                for (const auto &v : tracks) {
                    const QJsonObject t = v.toObject();
                    if (t["streamable"].toBool(true))
                        m_queue->addToQueue(t);
                }
            }
            m_pendingId.clear();
        });
    }

    void request(const QString &albumId, Action action)
    {
        m_pendingId = albumId;
        m_pendingAction = action;
        m_backend->getAlbum(albumId);
    }

private:
    QobuzBackend *m_backend;
    PlayQueue *m_queue;
    QString m_pendingId;
    Action m_pendingAction = AddToQueue;
};
