#include "tracklistmodel.hpp"
#include "../util/colors.hpp"

#include <QJsonValue>
#include <QColor>
#include <QIcon>
#include <algorithm>

TrackListModel::TrackListModel(QObject *parent)
    : QAbstractTableModel(parent)
{}

TrackItem TrackListModel::parseTrackItem(const QJsonObject &t,
                                         bool usePosition,
                                         bool useSequential,
                                         int &seq)
{
    TrackItem item;
    item.id               = static_cast<qint64>(t["id"].toDouble());
    item.playlistTrackId  = static_cast<qint64>(t["playlist_track_id"].toDouble());
    item.discNumber       = t["media_number"].toInt(1);
    item.duration         = static_cast<qint64>(t["duration"].toDouble());
    item.streamable       = t["streamable"].toBool(true);
    item.hiRes            = t["hires_streamable"].toBool();
    item.raw              = t;

    // Combine title + version ("Melody" + "Vocal Remix" → "Melody (Vocal Remix)")
    const QString base    = t["title"].toString();
    const QString version = t["version"].toString().trimmed();
    item.title = version.isEmpty() ? base
               : base + QStringLiteral(" (") + version + QLatin1Char(')');

    if (useSequential) {
        item.number = seq++;
    } else if (usePosition) {
        const int pos = t["position"].toInt();
        item.number = pos > 0 ? pos : seq;
        ++seq;
    } else {
        item.number = t["track_number"].toInt();
    }

    const QJsonObject performer = t["performer"].toObject();
    item.artist = performer["name"].toString();
    if (item.artist.isEmpty()) {
        // album.artist.name may be a plain string or {display:"..."} object
        const QJsonValue n = t["album"].toObject()["artist"].toObject()["name"];
        item.artist = n.isObject() ? n.toObject()["display"].toString() : n.toString();
    }
    if (item.artist.isEmpty()) {
        // top_tracks format: artist.name.display
        const QJsonValue n = t["artist"].toObject()["name"];
        item.artist = n.isObject() ? n.toObject()["display"].toString() : n.toString();
    }

    const QJsonObject album = t["album"].toObject();
    item.album   = album["title"].toString();
    item.albumId = album["id"].toString();

    return item;
}

void TrackListModel::setTracks(const QJsonArray &tracks,
                               bool usePosition,
                               bool useSequential)
{
    beginResetModel();
    m_tracks.clear();
    m_tracks.reserve(tracks.size());

    // Parse into a temporary list first so we can detect multi-disc
    QVector<TrackItem> parsed;
    parsed.reserve(tracks.size());

    int seq = 1;
    for (const QJsonValue &v : tracks)
        parsed.append(parseTrackItem(v.toObject(), usePosition, useSequential, seq));

    // Multi-disc only makes sense for album context (not playlists / fav / search)
    int maxDisc = 1;
    if (!usePosition && !useSequential) {
        for (const TrackItem &t : parsed)
            maxDisc = qMax(maxDisc, t.discNumber);
    }
    m_hasMultipleDiscs = (maxDisc > 1);

    if (m_hasMultipleDiscs) {
        // Sort by disc then track number
        std::stable_sort(parsed.begin(), parsed.end(), [](const TrackItem &a, const TrackItem &b) {
            return a.discNumber != b.discNumber ? a.discNumber < b.discNumber
                                               : a.number < b.number;
        });
        // Interleave disc header items
        int currentDisc = -1;
        for (const TrackItem &t : parsed) {
            if (t.discNumber != currentDisc) {
                TrackItem header;
                header.isDiscHeader = true;
                header.discNumber   = t.discNumber;
                header.title        = tr("Disc %1").arg(t.discNumber);
                m_tracks.append(header);
                currentDisc = t.discNumber;
            }
            m_tracks.append(t);
        }
    } else {
        m_tracks = parsed;
        // Re-apply sort silently inside the reset
        if (m_sortColumn >= 0)
            sortData(m_sortColumn, m_sortOrder);
    }

    endResetModel();

    if (!m_hasMultipleDiscs && m_sortColumn >= 0)
        emit sortApplied();
}

void TrackListModel::appendTracks(const QJsonArray &tracks,
                                  bool usePosition,
                                  bool useSequential)
{
    if (tracks.isEmpty())
        return;

    // Keep append path simple and stable: disc-header mode is handled by reset path.
    if (m_hasMultipleDiscs && !usePosition && !useSequential) {
        QJsonArray all = currentTracksJson();
        for (const QJsonValue &v : tracks)
            all.append(v);
        setTracks(all, usePosition, useSequential);
        return;
    }

    int seq = 1;
    if (useSequential || usePosition) {
        for (const TrackItem &t : m_tracks)
            if (!t.isDiscHeader)
                ++seq;
    }

    QVector<TrackItem> parsed;
    parsed.reserve(tracks.size());
    for (const QJsonValue &v : tracks)
        parsed.append(parseTrackItem(v.toObject(), usePosition, useSequential, seq));

    if (parsed.isEmpty())
        return;

    const int first = m_tracks.size();
    const int last  = first + parsed.size() - 1;
    beginInsertRows({}, first, last);
    m_tracks += parsed;
    endInsertRows();

    if (!m_hasMultipleDiscs && m_sortColumn >= 0) {
        emit layoutAboutToBeChanged();
        sortData(m_sortColumn, m_sortOrder);
        emit layoutChanged();
        emit sortApplied();
    }
}

void TrackListModel::clear()
{
    beginResetModel();
    m_tracks.clear();
    endResetModel();
}

void TrackListModel::removeTrack(int row)
{
    if (row < 0 || row >= m_tracks.size()) return;
    beginRemoveRows({}, row, row);
    m_tracks.removeAt(row);
    endRemoveRows();
}

void TrackListModel::notifyFavChanged(qint64 id)
{
    for (int r = 0; r < m_tracks.size(); ++r) {
        if (m_tracks[r].id == id) {
            const auto idx = index(r, ColTitle);
            emit dataChanged(idx, idx, {Qt::DecorationRole});
        }
    }
}

void TrackListModel::setFavIds(const QSet<qint64> &ids)
{
    m_favIds = ids;
    if (!m_tracks.isEmpty())
        emit dataChanged(index(0, ColTitle), index(rowCount() - 1, ColTitle),
                         {Qt::DecorationRole});
}

void TrackListModel::addFavId(qint64 id)
{
    m_favIds.insert(id);
    notifyFavChanged(id);
}

void TrackListModel::removeFavId(qint64 id)
{
    m_favIds.remove(id);
    notifyFavChanged(id);
}

void TrackListModel::setPlayingId(qint64 id)
{
    m_playingId = id;
    if (!m_tracks.isEmpty())
        emit dataChanged(index(0, 0), index(rowCount() - 1, columnCount() - 1),
                         {Qt::FontRole, Qt::DecorationRole});
}

Qt::ItemFlags TrackListModel::flags(const QModelIndex &index) const
{
    if (!index.isValid() || index.row() >= m_tracks.size())
        return Qt::NoItemFlags;
    if (m_tracks.at(index.row()).isDiscHeader)
        return Qt::ItemIsEnabled;
    return Qt::ItemIsEnabled | Qt::ItemIsSelectable;
}

QVector<int> TrackListModel::discHeaderRows() const
{
    QVector<int> rows;
    for (int i = 0; i < m_tracks.size(); ++i)
        if (m_tracks[i].isDiscHeader) rows.append(i);
    return rows;
}

int TrackListModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_tracks.size();
}

int TrackListModel::columnCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : ColCount;
}

QVariant TrackListModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_tracks.size())
        return {};

    const TrackItem &t = m_tracks.at(index.row());

    // Disc header rows: styled separator spanning all columns via setFirstColumnSpanned
    if (t.isDiscHeader) {
        if (role == Qt::DisplayRole && index.column() == ColNumber)
            return t.title;
        if (role == Qt::FontRole) {
            QFont f; f.setBold(true); return f;
        }
        if (role == Qt::ForegroundRole)
            return Colors::QobuzOrange;
        return {};
    }

    const bool isPlaying = (t.id == m_playingId && m_playingId != 0);

    if (role == Qt::DisplayRole) {
        switch (index.column()) {
        case ColNumber:   return t.number > 0 ? QString::number(t.number) : QString();
        case ColTitle:    return t.title;
        case ColArtist:   return t.artist;
        case ColAlbum:    return t.album;
        case ColDuration: return formatDuration(t.duration);
        }
    }

    if (role == Qt::FontRole && isPlaying) {
        QFont f;
        f.setBold(true);
        return f;
    }

    if (role == Qt::ForegroundRole) {
        if (!t.streamable) return Colors::DisabledText;
        if (isPlaying)     return Colors::QobuzOrange;
    }

    if (role == Qt::DecorationRole && index.column() == ColNumber && isPlaying) {
        return QIcon(QStringLiteral(":/res/icons/media-track-show-active.svg"));
    }

    if (role == Qt::DecorationRole && index.column() == ColTitle && m_favIds.contains(t.id)) {
        return QIcon(QStringLiteral(":/res/icons/starred-symbolic.svg"));
    }

    if (role == TrackIdRole)         return t.id;
    if (role == TrackJsonRole)       return t.raw;
    if (role == HiResRole)           return t.hiRes;
    if (role == PlaylistTrackIdRole) return t.playlistTrackId;

    return {};
}

QVariant TrackListModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation != Qt::Horizontal) return {};

    if (role == Qt::DisplayRole) {
        switch (section) {
        case ColNumber:   return tr("#");
        case ColTitle:    return tr("Title");
        case ColArtist:   return tr("Artist");
        case ColAlbum:    return tr("Album");
        case ColDuration: return tr("Duration");
        }
    }

    if (role == Qt::DecorationRole && section == m_sortColumn) {
        return QIcon(QStringLiteral(":/res/icons/view-sort-ascending.svg"));
    }

    return {};
}

void TrackListModel::sortData(int column, Qt::SortOrder order)
{
    auto cmp = [&](const TrackItem &a, const TrackItem &b) -> bool {
        bool less = false;
        switch (column) {
        case ColNumber:   less = a.number   < b.number;   break;
        case ColTitle:    less = a.title    < b.title;    break;
        case ColArtist:   less = a.artist   < b.artist;   break;
        case ColAlbum:    less = a.album    < b.album;    break;
        case ColDuration: less = a.duration < b.duration; break;
        default:          less = false;
        }
        return order == Qt::AscendingOrder ? less : !less;
    };
    std::stable_sort(m_tracks.begin(), m_tracks.end(), cmp);
}

void TrackListModel::sort(int column, Qt::SortOrder order)
{
    m_sortColumn = column;
    m_sortOrder  = order;

    // Multi-disc albums keep their disc-ordered layout; don't re-sort
    if (m_hasMultipleDiscs || m_tracks.isEmpty()) return;

    emit layoutAboutToBeChanged();
    sortData(column, order);
    emit layoutChanged();

    emit sortApplied();
}

QString TrackListModel::formatDuration(qint64 secs)
{
    if (secs < 0) secs = 0;
    const qint64 m = secs / 60;
    const qint64 s = secs % 60;
    return QStringLiteral("%1:%2").arg(m).arg(s, 2, 10, QLatin1Char('0'));
}
