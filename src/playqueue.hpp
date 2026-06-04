#pragma once

#include <QObject>
#include <QVector>
#include <QJsonObject>
#include <QJsonArray>

/// Local playback queue.  Holds the ordered list of tracks for the current
/// context (album / playlist / search result / favourites) plus a separate
/// "play-next" prepend queue that mirrors the spotify-qt pattern.
class PlayQueue : public QObject
{
    Q_OBJECT

public:
    explicit PlayQueue(QObject *parent = nullptr);

    // ---- Loading a new context ----

    /// Replace the queue with all tracks from an album/playlist JSON context.
    /// @param startIndex  Original index of the track to start at. If it points
    ///                    to a non-streamable track, the first streamable track
    ///                    at or after that index is used.
    void setContext(const QJsonArray &tracks, int startIndex = 0);

    // ---- Re-order after a sort (keeps m_playNext, updates m_index) ----

    void reorderContext(const QJsonArray &tracks, qint64 currentId);

    // ---- Clear / remove upcoming ----

    /// Remove all "up next" entries (playNext + remaining main queue after current).
    void clearUpcoming();

    /// Remove one upcoming track by its index in upcomingTracks().
    void removeUpcoming(int upcomingIndex);

    // ---- Shuffle ----

    bool shuffleEnabled() const { return m_shuffle; }

    void setShuffle(bool enabled);

    /// Shuffle the current queue once without changing the global shuffle flag.
    void shuffleNow();

    // ---- Play-next prepend queue (like "Add to queue" ----

    void addToQueue(const QJsonObject &track);

    void playNext(const QJsonObject &track);

    // ---- Navigation ----

    bool hasCurrent() const;

    QJsonObject current() const;

    qint64 currentId() const;

    /// Advance and return the track to play next.  Returns {} at end of queue.
    QJsonObject advance();

    /// Step backwards in the main queue (play-next is not affected).
    QJsonObject stepBack();

    bool canGoNext() const;

    bool canGoPrev() const { return m_index > 0; }

    // ---- Index lookup ----

    /// Set the current position by track id (after user double-clicks a row).
    void setCurrentById(qint64 id);

    // ---- Accessors for queue panel ----

    QVector<QJsonObject> upcomingTracks() const;

    /// Peek at the very next track that would play, without advancing. Returns {} if none.
    QJsonObject peekNext() const;

    int playNextCount()  const { return m_playNext.size(); }
    int totalSize()      const { return m_playNext.size() + m_queue.size(); }
    int currentIndex()   const { return m_index; }

    /// Skip to upcoming[upcomingIndex]: removes everything before it, pops and returns it.
    QJsonObject skipToUpcoming(int upcomingIndex);

    /// Replace the upcoming list with a new order (used after drag-reorder in UI).
    void setUpcomingOrder(const QVector<QJsonObject> &newOrder);

    /// Append tracks to the main queue tail (autoplay/discovery).
    void appendToContext(const QJsonArray &tracks);

    /// Move an upcoming item (by its index in upcomingTracks()) to the front of playNext.
    void moveUpcomingToTop(int upcomingIndex);

signals:
    void queueChanged();

private:
    QVector<QJsonObject> m_queue;    // main context (album / playlist)
    QVector<QJsonObject> m_playNext; // prepended "play next" tracks
    int  m_index   = 0;
    bool m_shuffle = false;

    void shuffleQueue(int keepAtFront);
};
