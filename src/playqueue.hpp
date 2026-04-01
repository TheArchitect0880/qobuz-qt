#pragma once

#include <QObject>
#include <QVector>
#include <QJsonObject>
#include <QJsonArray>
#include <algorithm>
#include <random>

/// Local playback queue.  Holds the ordered list of tracks for the current
/// context (album / playlist / search result / favourites) plus a separate
/// "play-next" prepend queue that mirrors the spotify-qt pattern.
class PlayQueue : public QObject
{
    Q_OBJECT

public:
    explicit PlayQueue(QObject *parent = nullptr) : QObject(parent) {}

    // ---- Loading a new context ----

    /// Replace the queue with all tracks from an album/playlist JSON context.
    /// @param startIndex  Index of the track to start playing (-1 = first).
    void setContext(const QJsonArray &tracks, int startIndex = 0)
    {
        m_queue.clear();
        m_playNext.clear();

        // Only queue streamable tracks; find the filtered index for startIndex
        int filteredStart = 0;
        int filteredIdx   = 0;
        bool found        = false;
        for (int orig = 0; orig < tracks.size(); ++orig) {
            const QJsonObject t = tracks[orig].toObject();
            if (!t["streamable"].toBool(true))
                continue;
            if (!found && orig >= startIndex) {
                filteredStart = filteredIdx;
                found = true;
            }
            m_queue.append(t);
            ++filteredIdx;
        }
        m_index = qBound(0, filteredStart, qMax(0, m_queue.size() - 1));

        if (m_shuffle)
            shuffleQueue(m_index);

        emit queueChanged();
    }

    // ---- Re-order after a sort (keeps m_playNext, updates m_index) ----

    void reorderContext(const QJsonArray &tracks, qint64 currentId)
    {
        m_queue.clear();
        for (const auto &v : tracks) {
            const QJsonObject t = v.toObject();
            if (t["streamable"].toBool(true))
                m_queue.append(t);
        }

        m_index = 0;
        for (int i = 0; i < m_queue.size(); ++i) {
            if (static_cast<qint64>(m_queue[i]["id"].toDouble()) == currentId) {
                m_index = i;
                break;
            }
        }
        emit queueChanged();
    }

    // ---- Clear / remove upcoming ----

    /// Remove all "up next" entries (playNext + remaining main queue after current).
    void clearUpcoming()
    {
        m_playNext.clear();
        if (m_index < m_queue.size())
            m_queue.resize(m_index + 1); // keep up to and including current
        emit queueChanged();
    }

    /// Remove one upcoming track by its index in upcomingTracks().
    void removeUpcoming(int upcomingIndex)
    {
        if (upcomingIndex < m_playNext.size()) {
            m_playNext.removeAt(upcomingIndex);
        } else {
            const int queueIdx = m_index + 1 + (upcomingIndex - m_playNext.size());
            if (queueIdx < m_queue.size())
                m_queue.removeAt(queueIdx);
        }
        emit queueChanged();
    }

    // ---- Shuffle ----

    bool shuffleEnabled() const { return m_shuffle; }

    void setShuffle(bool enabled)
    {
        if (m_shuffle == enabled) return;
        m_shuffle = enabled;
        if (enabled && !m_queue.isEmpty())
            shuffleQueue(m_index);
        emit queueChanged();
    }

    /// Shuffle the current queue once without changing the global shuffle flag.
    void shuffleNow()
    {
        if (m_queue.isEmpty()) return;
        shuffleQueue(m_index);
        emit queueChanged();
    }

    // ---- Play-next prepend queue (like "Add to queue" ----

    void addToQueue(const QJsonObject &track)
    {
        m_playNext.append(track);
        emit queueChanged();
    }

    void playNext(const QJsonObject &track)
    {
        m_playNext.prepend(track);
        emit queueChanged();
    }

    // ---- Navigation ----

    bool hasCurrent() const
    {
        return (!m_playNext.isEmpty()) || (!m_queue.isEmpty());
    }

    QJsonObject current() const
    {
        if (!m_playNext.isEmpty()) return m_playNext.first();
        if (m_index < m_queue.size()) return m_queue.at(m_index);
        return {};
    }

    qint64 currentId() const
    {
        return static_cast<qint64>(current()["id"].toDouble());
    }

    /// Advance and return the track to play next.  Returns {} at end of queue.
    QJsonObject advance()
    {
        if (!m_playNext.isEmpty()) {
            // Return the playNext item directly — do NOT call current() after
            // removal, as that would fall back to the already-playing m_index track.
            const QJsonObject next = m_playNext.takeFirst();
            emit queueChanged();
            return next;
        }
        ++m_index;
        emit queueChanged();
        return current();
    }

    /// Step backwards in the main queue (play-next is not affected).
    QJsonObject stepBack()
    {
        if (m_index > 0) --m_index;
        emit queueChanged();
        return current();
    }

    bool canGoNext() const
    {
        return !m_playNext.isEmpty() || (m_index + 1 < m_queue.size());
    }

    bool canGoPrev() const { return m_index > 0; }

    // ---- Index lookup ----

    /// Set the current position by track id (after user double-clicks a row).
    void setCurrentById(qint64 id)
    {
        m_playNext.clear();
        for (int i = 0; i < m_queue.size(); ++i) {
            if (static_cast<qint64>(m_queue[i]["id"].toDouble()) == id) {
                m_index = i;
                emit queueChanged();
                return;
            }
        }
    }

    // ---- Accessors for queue panel ----

    QVector<QJsonObject> upcomingTracks(int maxCount = 200) const
    {
        QVector<QJsonObject> result;
        result.append(m_playNext);
        for (int i = m_index + 1; i < m_queue.size() && result.size() < maxCount; ++i)
            result.append(m_queue.at(i));
        return result;
    }

    int playNextCount()  const { return m_playNext.size(); }
    int totalSize()      const { return m_playNext.size() + m_queue.size(); }
    int currentIndex()   const { return m_index; }

    /// Skip to upcoming[upcomingIndex]: removes everything before it, pops and returns it.
    QJsonObject skipToUpcoming(int upcomingIndex)
    {
        // Remove items 0..upcomingIndex-1 from the front of upcoming
        for (int i = 0; i < upcomingIndex; ++i) {
            if (!m_playNext.isEmpty())
                m_playNext.removeFirst();
            else if (m_index + 1 < m_queue.size())
                ++m_index;
        }
        // Pop and return the target (now at upcoming[0])
        if (!m_playNext.isEmpty()) {
            const QJsonObject t = m_playNext.takeFirst();
            emit queueChanged();
            return t;
        }
        if (m_index + 1 < m_queue.size()) {
            ++m_index;
            emit queueChanged();
            return m_queue.at(m_index);
        }
        emit queueChanged();
        return {};
    }

    /// Replace the upcoming list with a new order (used after drag-reorder in UI).
    void setUpcomingOrder(const QVector<QJsonObject> &newOrder)
    {
        m_playNext = newOrder;
        m_queue.resize(m_index + 1); // drop old main-queue tail
        emit queueChanged();
    }

    /// Append tracks to the main queue tail (autoplay/discovery).
    void appendToContext(const QJsonArray &tracks)
    {
        for (const auto &v : tracks) {
            const QJsonObject t = v.toObject();
            if (t["streamable"].toBool(true))
                m_queue.append(t);
        }
        emit queueChanged();
    }

    /// Move an upcoming item (by its index in upcomingTracks()) to the front of playNext.
    void moveUpcomingToTop(int upcomingIndex)
    {
        if (upcomingIndex < 0) return;
        QJsonObject track;
        if (upcomingIndex < m_playNext.size()) {
            if (upcomingIndex == 0) return; // already at top
            track = m_playNext.takeAt(upcomingIndex);
        } else {
            const int queueIdx = m_index + 1 + (upcomingIndex - m_playNext.size());
            if (queueIdx >= m_queue.size()) return;
            track = m_queue.takeAt(queueIdx);
        }
        m_playNext.prepend(track);
        emit queueChanged();
    }

signals:
    void queueChanged();

private:
    QVector<QJsonObject> m_queue;    // main context (album / playlist)
    QVector<QJsonObject> m_playNext; // prepended "play next" tracks
    int  m_index   = 0;
    bool m_shuffle = false;

    void shuffleQueue(int keepAtFront)
    {
        if (m_queue.isEmpty()) return;
        // Keep the current track at index 0 of the remaining queue
        if (keepAtFront >= 0 && keepAtFront < m_queue.size()) {
            QJsonObject current = m_queue.takeAt(keepAtFront);
            std::mt19937 rng(std::random_device{}());
            std::shuffle(m_queue.begin(), m_queue.end(), rng);
            m_queue.prepend(current);
        } else {
            std::mt19937 rng(std::random_device{}());
            std::shuffle(m_queue.begin(), m_queue.end(), rng);
        }
        m_index = 0;
    }
};
