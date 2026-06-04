#include "playqueue.hpp"

#include <algorithm>
#include <random>

PlayQueue::PlayQueue(QObject *parent) : QObject(parent) {}

void PlayQueue::setContext(const QJsonArray &tracks, int startIndex)
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

void PlayQueue::reorderContext(const QJsonArray &tracks, qint64 currentId)
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

void PlayQueue::clearUpcoming()
{
    m_playNext.clear();
    if (m_index < m_queue.size())
        m_queue.resize(m_index + 1); // keep up to and including current
    emit queueChanged();
}

void PlayQueue::removeUpcoming(int upcomingIndex)
{
    if (upcomingIndex < 0) return;
    if (upcomingIndex < m_playNext.size()) {
        m_playNext.removeAt(upcomingIndex);
    } else {
        const int queueIdx = m_index + 1 + (upcomingIndex - m_playNext.size());
        if (queueIdx >= m_queue.size()) return;
        m_queue.removeAt(queueIdx);
    }
    emit queueChanged();
}

void PlayQueue::setShuffle(bool enabled)
{
    if (m_shuffle == enabled) return;
    m_shuffle = enabled;
    if (enabled && !m_queue.isEmpty())
        shuffleQueue(m_index);
    emit queueChanged();
}

void PlayQueue::shuffleNow()
{
    if (m_queue.isEmpty()) return;
    shuffleQueue(m_index);
    emit queueChanged();
}

void PlayQueue::addToQueue(const QJsonObject &track)
{
    m_playNext.append(track);
    emit queueChanged();
}

void PlayQueue::playNext(const QJsonObject &track)
{
    m_playNext.prepend(track);
    emit queueChanged();
}

bool PlayQueue::hasCurrent() const
{
    return (!m_playNext.isEmpty()) || (!m_queue.isEmpty());
}

QJsonObject PlayQueue::current() const
{
    if (!m_playNext.isEmpty()) return m_playNext.first();
    if (m_index < m_queue.size()) return m_queue.at(m_index);
    return {};
}

qint64 PlayQueue::currentId() const
{
    return static_cast<qint64>(current()["id"].toDouble());
}

QJsonObject PlayQueue::advance()
{
    if (!m_playNext.isEmpty()) {
        // Splice the play-next item into the main queue right after the current
        // index so current()/currentId() reflect the track actually playing.
        const QJsonObject next = m_playNext.takeFirst();
        const int insertAt = qMin(m_index + 1, m_queue.size());
        m_queue.insert(insertAt, next);
        m_index = insertAt;
        emit queueChanged();
        return next;
    }
    if (m_index + 1 >= m_queue.size())
        return {};
    ++m_index;
    emit queueChanged();
    return current();
}

QJsonObject PlayQueue::stepBack()
{
    if (m_index > 0) {
        --m_index;
        emit queueChanged();
    }
    return m_index < m_queue.size() ? m_queue.at(m_index) : QJsonObject{};
}

bool PlayQueue::canGoNext() const
{
    return !m_playNext.isEmpty() || (m_index + 1 < m_queue.size());
}

void PlayQueue::setCurrentById(qint64 id)
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

QVector<QJsonObject> PlayQueue::upcomingTracks() const
{
    QVector<QJsonObject> result;
    const int tailCount = qMax(0, m_queue.size() - m_index - 1);
    result.reserve(m_playNext.size() + tailCount);
    result.append(m_playNext);
    for (int i = m_index + 1; i < m_queue.size(); ++i)
        result.append(m_queue.at(i));
    return result;
}

QJsonObject PlayQueue::peekNext() const
{
    if (!m_playNext.isEmpty())
        return m_playNext.first();
    if (m_index + 1 < m_queue.size())
        return m_queue.at(m_index + 1);
    return {};
}

QJsonObject PlayQueue::skipToUpcoming(int upcomingIndex)
{
    if (upcomingIndex < 0) return {};
    const int tailCount = qMax(0, m_queue.size() - m_index - 1);
    if (upcomingIndex >= m_playNext.size() + tailCount) return {};
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

void PlayQueue::setUpcomingOrder(const QVector<QJsonObject> &newOrder)
{
    m_playNext = newOrder;
    m_queue.resize(m_index + 1); // drop old main-queue tail
    emit queueChanged();
}

void PlayQueue::appendToContext(const QJsonArray &tracks)
{
    for (const auto &v : tracks) {
        const QJsonObject t = v.toObject();
        if (t["streamable"].toBool(true))
            m_queue.append(t);
    }
    emit queueChanged();
}

void PlayQueue::moveUpcomingToTop(int upcomingIndex)
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

void PlayQueue::shuffleQueue(int keepAtFront)
{
    if (m_queue.isEmpty()) return;
    static thread_local std::mt19937 rng{std::random_device{}()};
    // Keep the current track at index 0 of the remaining queue
    if (keepAtFront >= 0 && keepAtFront < m_queue.size()) {
        QJsonObject current = m_queue.takeAt(keepAtFront);
        std::shuffle(m_queue.begin(), m_queue.end(), rng);
        m_queue.prepend(current);
    } else {
        std::shuffle(m_queue.begin(), m_queue.end(), rng);
    }
    m_index = 0;
    emit queueChanged();
}
