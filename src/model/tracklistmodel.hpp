#pragma once

#include <QAbstractTableModel>
#include <QJsonArray>
#include <QJsonObject>
#include <QVector>
#include <QSet>
#include <QFont>

struct TrackItem {
    qint64  id              = 0;
    qint64  playlistTrackId = 0;
    int     number          = 0;
    int     discNumber      = 1;
    bool    isDiscHeader    = false;
    QString title;
    QString artist;
    QString album;
    QString albumId;
    qint64  duration        = 0;   // seconds
    bool    hiRes           = false;
    bool    streamable      = false;
    QJsonObject raw;
};

class TrackListModel : public QAbstractTableModel
{
    Q_OBJECT

public:
    enum Column {
        ColNumber   = 0,
        ColTitle    = 1,
        ColArtist   = 2,
        ColAlbum    = 3,
        ColDuration = 4,
        ColCount
    };

    enum Role {
        TrackIdRole         = Qt::UserRole + 1,
        TrackJsonRole       = Qt::UserRole + 2,
        HiResRole           = Qt::UserRole + 3,
        PlaylistTrackIdRole = Qt::UserRole + 4,
    };

    explicit TrackListModel(QObject *parent = nullptr);

    // usePosition: use tracks[i]["position"] for the # column (playlists)
    // useSequential: use 1..n sequential numbering (favourites)
    void setTracks(const QJsonArray &tracks,
                   bool usePosition   = false,
                   bool useSequential = false);
    void appendTracks(const QJsonArray &tracks,
                      bool usePosition   = false,
                      bool useSequential = false);
    void clear();
    void setPlayingId(qint64 id);
    qint64 playingId() const { return m_playingId; }

    void setFavIds(const QSet<qint64> &ids);
    void addFavId(qint64 id);
    void removeFavId(qint64 id);
    bool isFav(qint64 id) const { return m_favIds.contains(id); }

    bool hasMultipleDiscs() const { return m_hasMultipleDiscs; }
    QVector<int> discHeaderRows() const;

    Qt::ItemFlags flags(const QModelIndex &index) const override;

    /// Optimistically remove a row (e.g. after deleting from playlist).
    void removeTrack(int row);

    const TrackItem &trackAt(int row) const { return m_tracks.at(row); }

    // Returns the current (possibly sorted) raw JSON objects in display order, skipping disc headers.
    QJsonArray currentTracksJson() const
    {
        QJsonArray out;
        for (const auto &t : m_tracks)
            if (!t.isDiscHeader) out.append(t.raw);
        return out;
    }

    int rowCount(const QModelIndex &parent = {}) const override;
    int columnCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;
    void sort(int column, Qt::SortOrder order = Qt::AscendingOrder) override;

    static QString formatDuration(qint64 secs);

signals:
    // Emitted after a sort is applied (including the initial sort after setTracks).
    // Lets external observers (e.g. PlayQueue) re-sync their order.
    void sortApplied();

private:
    QVector<TrackItem> m_tracks;
    QSet<qint64>       m_favIds;
    qint64 m_playingId        = 0;
    bool   m_hasMultipleDiscs = false;
    int    m_sortColumn = -1;
    Qt::SortOrder m_sortOrder = Qt::AscendingOrder;

    // Sort m_tracks in-place without emitting any signals.
    void sortData(int column, Qt::SortOrder order);

    // Parse a single JSON track object into a TrackItem.
    static TrackItem parseTrackItem(const QJsonObject &t, bool usePosition, bool useSequential, int &seq);

    // Emit dataChanged(DecorationRole) for all rows matching id.
    void notifyFavChanged(qint64 id);
};
