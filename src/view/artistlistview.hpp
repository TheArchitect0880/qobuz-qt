#pragma once

#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QHeaderView>
#include <QJsonObject>
#include <QJsonArray>

/// A simple list of artists.
/// Double-clicking an item emits artistSelected(artistId).
class ArtistListView : public QTreeWidget
{
    Q_OBJECT

public:
    explicit ArtistListView(QWidget *parent = nullptr) : QTreeWidget(parent)
    {
        setColumnCount(2);
        setHeaderLabels({tr("Artist"), tr("Albums")});
        setRootIsDecorated(false);
        setAlternatingRowColors(true);
        setSelectionBehavior(QAbstractItemView::SelectRows);

        header()->setStretchLastSection(false);
        header()->setSectionResizeMode(0, QHeaderView::Stretch);
        header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);

        connect(this, &QTreeWidget::itemDoubleClicked,
                this, [this](QTreeWidgetItem *item, int) {
            const qint64 id = item->data(0, Qt::UserRole).toLongLong();
            if (id > 0) emit artistSelected(id);
        });
    }

    void setArtists(const QJsonArray &artists)
    {
        clear();
        for (const auto &v : artists) {
            const QJsonObject a = v.toObject();
            const qint64 id     = static_cast<qint64>(a["id"].toDouble());
            const QString name  = a["name"].toString();
            const int albums    = a["albums_count"].toInt();

            auto *item = new QTreeWidgetItem(this);
            item->setText(0, name);
            item->setText(1, albums > 0 ? QString::number(albums) : QString());
            item->setData(0, Qt::UserRole, id);
        }
    }

signals:
    void artistSelected(qint64 artistId);
};
