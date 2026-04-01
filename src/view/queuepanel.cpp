#include "queuepanel.hpp"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QMenu>
#include <QAction>
#include <QPainter>
#include <QStyledItemDelegate>
#include <QApplication>

static constexpr int UpcomingIndexRole = Qt::UserRole + 1;
static constexpr int IsPlayNextRole    = Qt::UserRole + 2;
static constexpr int ArtistRole        = Qt::UserRole + 3;
static constexpr int DurationRole      = Qt::UserRole + 4;

// ---- Custom delegate -------------------------------------------------------

class QueueDelegate : public QStyledItemDelegate
{
public:
    explicit QueueDelegate(QObject *parent = nullptr) : QStyledItemDelegate(parent) {}

    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &) const override
    {
        return QSize(0, QFontMetrics(option.font).height() + 4);
    }

    void paint(QPainter *p, const QStyleOptionViewItem &option,
               const QModelIndex &index) const override
    {
        p->save();

        QStyle *style = option.widget ? option.widget->style() : QApplication::style();
        style->drawPrimitive(QStyle::PE_PanelItemViewItem, &option, p, option.widget);

        const bool    isPlayNext = index.data(IsPlayNextRole).toBool();
        const QString title      = index.data(Qt::DisplayRole).toString();
        const QString artist     = index.data(ArtistRole).toString();
        const int     dur        = index.data(DurationRole).toInt();

        const QRect r = option.rect.adjusted(10, 0, -10, 0);

        // Duration right-aligned
        QString durStr;
        if (dur > 0) {
            const int m = dur / 60, s = dur % 60;
            durStr = QStringLiteral("%1:%2").arg(m).arg(s, 2, 10, QLatin1Char('0'));
        }

        const QPalette &pal = option.palette;
        const bool selected = option.state & QStyle::State_Selected;
        QColor titleColor   = selected ? pal.highlightedText().color() : pal.text().color();
        QColor dimColor     = titleColor;
        dimColor.setAlpha(150);

        if (isPlayNext && !selected)
            titleColor = titleColor.lighter(130);

        QFont titleFont = option.font;
        titleFont.setWeight(QFont::Medium);

        QFont subFont = option.font;
        subFont.setPointSizeF(option.font.pointSizeF() * 0.85);

        // Draw duration on the far right
        int durW = 0;
        if (!durStr.isEmpty()) {
            durW = QFontMetrics(subFont).horizontalAdvance(durStr) + 6;
            p->setFont(subFont);
            p->setPen(dimColor);
            p->drawText(QRect(r.right() - durW, r.top(), durW, r.height()),
                Qt::AlignRight | Qt::AlignVCenter, durStr);
        }

        // Available width for title + separator + artist
        const int available = r.width() - durW;

        const QFontMetrics titleFm(titleFont);
        const QFontMetrics subFm(subFont);

        const QString sep = artist.isEmpty() ? QString() : QStringLiteral("  ·  ");
        const int sepW = sep.isEmpty() ? 0 : subFm.horizontalAdvance(sep);

        // Title gets up to 60% of available, artist gets the rest
        const int maxTitle  = qMax(0, available * 6 / 10 - sepW);
        const int maxArtist = qMax(0, available - sepW
            - qMin(titleFm.horizontalAdvance(title), maxTitle));

        const QString elidedTitle = titleFm.elidedText(title, Qt::ElideRight, maxTitle);
        const int drawnTitleW     = titleFm.horizontalAdvance(elidedTitle);

        int x = r.left();

        // Title
        p->setFont(titleFont);
        p->setPen(titleColor);
        p->drawText(x, r.top(), drawnTitleW, r.height(),
            Qt::AlignLeft | Qt::AlignVCenter, elidedTitle);
        x += drawnTitleW;

        // Separator + artist
        if (!artist.isEmpty()) {
            p->setFont(subFont);
            p->setPen(dimColor);
            p->drawText(x, r.top(), sepW, r.height(),
                Qt::AlignLeft | Qt::AlignVCenter, sep);
            x += sepW;

            const QString elidedArtist = subFm.elidedText(artist, Qt::ElideRight, maxArtist);
            p->drawText(x, r.top(), maxArtist, r.height(),
                Qt::AlignLeft | Qt::AlignVCenter, elidedArtist);
        }

        p->restore();
    }
};

// ---- QueuePanel ------------------------------------------------------------

QueuePanel::QueuePanel(PlayQueue *queue, QWidget *parent)
    : QDockWidget(tr("Queue"), parent)
    , m_queue(queue)
{
    setObjectName(QStringLiteral("queuePanel"));
    setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetClosable);

    auto *container = new QWidget(this);
    auto *layout    = new QVBoxLayout(container);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(4);

    auto *headerRow = new QHBoxLayout;
    m_countLabel = new QLabel(tr("Up next: 0 tracks"), container);
    m_clearBtn   = new QPushButton(tr("Clear"), container);
    m_clearBtn->setMaximumWidth(64);
    headerRow->addWidget(m_countLabel, 1);
    headerRow->addWidget(m_clearBtn);
    layout->addLayout(headerRow);

    m_list = new QListWidget(container);
    m_list->setAlternatingRowColors(true);
    m_list->setContextMenuPolicy(Qt::CustomContextMenu);
    m_list->setDragDropMode(QAbstractItemView::InternalMove);
    m_list->setDefaultDropAction(Qt::MoveAction);
    m_list->setItemDelegate(new QueueDelegate(m_list));
    layout->addWidget(m_list, 1);

    setWidget(container);
    setMinimumWidth(200);

    connect(m_queue, &PlayQueue::queueChanged, this, &QueuePanel::refresh);
    connect(m_clearBtn, &QPushButton::clicked, this, [this] { m_queue->clearUpcoming(); });
    connect(m_list, &QListWidget::itemDoubleClicked, this, &QueuePanel::onItemDoubleClicked);
    connect(m_list, &QListWidget::customContextMenuRequested, this, &QueuePanel::onContextMenu);
    connect(m_list->model(), &QAbstractItemModel::rowsMoved, this, &QueuePanel::onRowsMoved);

    refresh();
}

void QueuePanel::refresh()
{
    if (m_refreshing) return;
    m_refreshing = true;

    m_list->clear();

    const QVector<QJsonObject> upcoming = m_queue->upcomingTracks();
    const int playNextCount = m_queue->playNextCount();

    m_countLabel->setText(tr("Up next: %1 track(s)").arg(upcoming.size()));
    m_clearBtn->setEnabled(!upcoming.isEmpty());

    for (int i = 0; i < upcoming.size(); ++i) {
        const QJsonObject &t = upcoming.at(i);
        const QString base    = t["title"].toString();
        const QString ver     = t["version"].toString().trimmed();
        const QString title   = ver.isEmpty() ? base : base + QStringLiteral(" (") + ver + QLatin1Char(')');
        const QString artist = t["performer"].toObject()["name"].toString().isEmpty()
            ? t["album"].toObject()["artist"].toObject()["name"].toString()
            : t["performer"].toObject()["name"].toString();
        const int duration   = t["duration"].toInt();

        auto *item = new QListWidgetItem(title, m_list);
        item->setData(UpcomingIndexRole, i);
        item->setData(IsPlayNextRole, i < playNextCount);
        item->setData(ArtistRole, artist);
        item->setData(DurationRole, duration);
    }

    m_refreshing = false;
}

void QueuePanel::onItemDoubleClicked(QListWidgetItem *item)
{
    const int idx = item->data(UpcomingIndexRole).toInt();
    const QJsonObject track = m_queue->skipToUpcoming(idx);
    if (track.isEmpty()) return;
    const qint64 id = static_cast<qint64>(track["id"].toDouble());
    emit skipToTrackRequested(id);
}

void QueuePanel::onRowsMoved()
{
    if (m_refreshing) return;

    const QVector<QJsonObject> currentUpcoming = m_queue->upcomingTracks();
    QVector<QJsonObject> newOrder;
    newOrder.reserve(m_list->count());
    for (int i = 0; i < m_list->count(); ++i) {
        const int prevIndex = m_list->item(i)->data(UpcomingIndexRole).toInt();
        if (prevIndex >= 0 && prevIndex < currentUpcoming.size())
            newOrder.append(currentUpcoming.at(prevIndex));
    }

    m_refreshing = true;
    m_queue->setUpcomingOrder(newOrder);
    m_refreshing = false;
}

void QueuePanel::onContextMenu(const QPoint &pos)
{
    auto *item = m_list->itemAt(pos);
    if (!item) return;

    const int idx = item->data(UpcomingIndexRole).toInt();

    QMenu menu(this);
    auto *removeAct = menu.addAction(tr("Remove from queue"));
    auto *toTopAct  = menu.addAction(tr("Move to top (play next)"));

    connect(removeAct, &QAction::triggered, this, [this, idx] { m_queue->removeUpcoming(idx); });
    connect(toTopAct,  &QAction::triggered, this, [this, idx] { m_queue->moveUpcomingToTop(idx); });

    menu.exec(m_list->viewport()->mapToGlobal(pos));
}
