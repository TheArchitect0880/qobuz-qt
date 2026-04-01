#pragma once

#include "../util/colors.hpp"

#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QHeaderView>
#include <QFont>
#include <QJsonObject>
#include <QJsonArray>

/// A simple list of albums (used for fav albums and artist detail pages).
/// Double-clicking an item emits albumSelected(albumId).
/// Column 0 shows a small gold "H" for hi-res streamable albums.
class AlbumListView : public QTreeWidget
{
    Q_OBJECT

public:
    explicit AlbumListView(QWidget *parent = nullptr) : QTreeWidget(parent)
    {
        setColumnCount(5);
        setHeaderLabels({tr(""), tr("Title"), tr("Artist"), tr("Year"), tr("Tracks")});
        setRootIsDecorated(false);
        setAlternatingRowColors(true);
        setSelectionBehavior(QAbstractItemView::SelectRows);
        setSortingEnabled(true);

        header()->setStretchLastSection(false);
        header()->setSectionResizeMode(0, QHeaderView::ResizeToContents); // H column
        header()->setSectionResizeMode(1, QHeaderView::Stretch);
        header()->setSectionResizeMode(2, QHeaderView::Stretch);
        header()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
        header()->setSectionResizeMode(4, QHeaderView::ResizeToContents);

        connect(this, &QTreeWidget::itemDoubleClicked,
                this, [this](QTreeWidgetItem *item, int) {
            const QString id = item->data(1, Qt::UserRole).toString();
            if (!id.isEmpty()) emit albumSelected(id);
        });
    }

    void setAlbums(const QJsonArray &albums)
    {
        clear();
        addAlbums(albums);
    }

    /// Configure for artist page: hide Artist column, set fixed column widths
    /// that match the Popular Tracks list for perfect vertical alignment.
    void setArtistPageMode()
    {
        setColumnHidden(2, true); // Artist — redundant on artist page
        header()->setSectionResizeMode(0, QHeaderView::Fixed);
        header()->setSectionResizeMode(1, QHeaderView::Stretch);
        header()->setSectionResizeMode(3, QHeaderView::Fixed);
        header()->setSectionResizeMode(4, QHeaderView::Fixed);
        header()->resizeSection(0, 40);
        header()->resizeSection(3, 120);
        header()->resizeSection(4, 70);
    }

    void addAlbums(const QJsonArray &albums)
    {
        QFont hiResFont;
        hiResFont.setBold(true);
        hiResFont.setPointSizeF(hiResFont.pointSizeF() * 0.85);

        for (const auto &v : albums) {
            const QJsonObject a  = v.toObject();
            const QString id      = a["id"].toString();
            const QString base    = a["title"].toString();
            const QString ver     = a["version"].toString().trimmed();
            const QString title   = ver.isEmpty() ? base : base + QStringLiteral(" (") + ver + QLatin1Char(')');

            const QJsonValue artistNameVal = a["artist"].toObject()["name"];
            const QString artist = artistNameVal.isObject()
                ? artistNameVal.toObject()["display"].toString()
                : artistNameVal.toString();

            const QString date = a["release_date_original"].toString();
            const QString year = date.isEmpty()
                ? a["dates"].toObject()["original"].toString().left(4)
                : date.left(4);
            const qint64 artistId = static_cast<qint64>(a["artist"].toObject()["id"].toDouble());

            const int tracks  = a["tracks_count"].toInt();
            const bool hiRes  = a["hires_streamable"].toBool()
                             || a["rights"].toObject()["hires_streamable"].toBool();

            auto *item = new QTreeWidgetItem(this);
            if (hiRes) {
                item->setText(0, QStringLiteral("H"));
                item->setForeground(0, Colors::QobuzOrange);
                item->setFont(0, hiResFont);
                item->setTextAlignment(0, Qt::AlignCenter);
            }
            item->setText(1, title);
            item->setText(2, artist);
            item->setText(3, year);
            item->setText(4, tracks > 0 ? QString::number(tracks) : QString());
            item->setData(1, Qt::UserRole, id);
            item->setData(2, Qt::UserRole, artistId);
        }
    }

signals:
    void albumSelected(const QString &albumId);
};
