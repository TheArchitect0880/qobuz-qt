#include "transferspanel.hpp"

#include "../util/icon.hpp"

#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QPushButton>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace
{
constexpr int TransferIdRole = Qt::UserRole + 1;
constexpr int StatusRole = Qt::UserRole + 2;
}

TransfersPanel::TransfersPanel(QWidget *parent)
    : QDockWidget(tr("Transfers"), parent)
{
    setObjectName(QStringLiteral("transfersPanel"));
    setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable | QDockWidget::DockWidgetClosable);

    auto *container = new QWidget(this);
    auto *layout = new QVBoxLayout(container);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(4);

    auto *headerRow = new QHBoxLayout;
    m_summary = new QLabel(tr("No active transfers"), container);
    m_cancelAllBtn = new QPushButton(tr("Cancel All"), container);
    m_clearFinishedBtn = new QPushButton(tr("Clear Finished"), container);
    headerRow->addWidget(m_summary, 1);
    headerRow->addWidget(m_cancelAllBtn);
    headerRow->addWidget(m_clearFinishedBtn);
    layout->addLayout(headerRow);

    m_tree = new QTreeWidget(container);
    m_tree->setColumnCount(4);
    m_tree->setHeaderLabels({tr("Transfer"), tr("Status"), tr("Progress"), tr("Current")});
    m_tree->setRootIsDecorated(false);
    m_tree->setAlternatingRowColors(true);
    m_tree->setContextMenuPolicy(Qt::CustomContextMenu);
    m_tree->header()->setSectionResizeMode(ColTitle, QHeaderView::Stretch);
    m_tree->header()->setSectionResizeMode(ColStatus, QHeaderView::ResizeToContents);
    m_tree->header()->setSectionResizeMode(ColProgress, QHeaderView::ResizeToContents);
    m_tree->header()->setSectionResizeMode(ColCurrent, QHeaderView::Stretch);
    layout->addWidget(m_tree, 1);

    setWidget(container);
    setMinimumWidth(320);

    connect(m_tree, &QTreeWidget::customContextMenuRequested, this, &TransfersPanel::onContextMenuRequested);
    connect(m_cancelAllBtn, &QPushButton::clicked, this, &TransfersPanel::cancelAllTransfersRequested);
    connect(m_clearFinishedBtn, &QPushButton::clicked, this, &TransfersPanel::clearFinished);

    updateSummary();
}

void TransfersPanel::onTransferStarted(const QJsonObject &info)
{
    upsertTransfer(info);
}

void TransfersPanel::onTransferProgress(const QJsonObject &info)
{
    upsertTransfer(info);
}

void TransfersPanel::onTransferFinished(const QJsonObject &info)
{
    upsertTransfer(info);
}

void TransfersPanel::onTransferFailed(const QJsonObject &info)
{
    upsertTransfer(info);
}

void TransfersPanel::onTransferCancelled(const QJsonObject &info)
{
    upsertTransfer(info);
}

void TransfersPanel::onContextMenuRequested(const QPoint &pos)
{
    auto *item = m_tree->itemAt(pos);
    if (!item)
        return;

    const quint64 transferId = item->data(0, TransferIdRole).toULongLong();
    const QString status = item->data(0, StatusRole).toString();

    QMenu menu(this);
    if (!isTerminalStatus(status)) {
        auto *cancel = menu.addAction(Icon::get(QStringLiteral("dialog-cancel")), tr("Cancel transfer"));
        connect(cancel, &QAction::triggered, this, [this, transferId] {
            emit cancelTransferRequested(transferId);
        });
    } else {
        auto *remove = menu.addAction(Icon::get(QStringLiteral("list-remove")), tr("Remove from list"));
        connect(remove, &QAction::triggered, this, [this, transferId] {
            if (auto *item = m_items.take(transferId)) {
                delete item;
                updateSummary();
            }
        });
    }

    menu.exec(m_tree->viewport()->mapToGlobal(pos));
}

void TransfersPanel::clearFinished()
{
    for (auto it = m_items.begin(); it != m_items.end();) {
        QTreeWidgetItem *item = it.value();
        if (isTerminalStatus(item->data(0, StatusRole).toString())) {
            delete item;
            it = m_items.erase(it);
        } else {
            ++it;
        }
    }
    updateSummary();
}

void TransfersPanel::upsertTransfer(const QJsonObject &info, const QString &statusOverride)
{
    const quint64 transferId = static_cast<quint64>(info["transfer_id"].toDouble());
    if (transferId == 0)
        return;

    QTreeWidgetItem *item = m_items.value(transferId, nullptr);
    if (!item) {
        item = new QTreeWidgetItem(m_tree);
        item->setData(0, TransferIdRole, QVariant::fromValue(transferId));
        m_items.insert(transferId, item);
    }

    const QString label = info["label"].toString(info["id"].toString());
    const QString status = statusOverride.isEmpty() ? info["status"].toString() : statusOverride;
    const int current = info["current"].toInt();
    const int total = info["total_tracks"].toInt();
    const int failed = info["failed_tracks"].toInt();
    const QString currentTrack = info["track_title"].toString();
    const qint64 downloaded = static_cast<qint64>(info["downloaded_bytes"].toDouble());
    const qint64 totalBytes = static_cast<qint64>(info["total_bytes"].toDouble(-1));

    item->setText(ColTitle, label);
    item->setText(ColStatus, status.isEmpty() ? tr("running") : status);
    item->setData(0, StatusRole, status);

    QString progressText;
    if (total > 0)
        progressText = tr("%1/%2").arg(current).arg(total);
    if (totalBytes > 0) {
        const int percent = static_cast<int>((100.0 * downloaded) / totalBytes);
        progressText += progressText.isEmpty() ? QString() : QStringLiteral("  ");
        progressText += tr("%1%").arg(percent);
    }
    if (failed > 0) {
        progressText += progressText.isEmpty() ? QString() : QStringLiteral("  ");
        progressText += tr("%1 failed").arg(failed);
    }
    item->setText(ColProgress, progressText);

    QString currentText = currentTrack;
    if (status == QLatin1String("failed"))
        currentText = info["error"].toString();
    else if (status == QLatin1String("completed"))
        currentText = info["path"].toString();
    item->setText(ColCurrent, currentText);

    updateSummary();
}

void TransfersPanel::updateSummary()
{
    int active = 0;
    int finished = 0;
    for (auto *item : std::as_const(m_items)) {
        if (isTerminalStatus(item->data(0, StatusRole).toString()))
            ++finished;
        else
            ++active;
    }

    if (m_items.isEmpty())
        m_summary->setText(tr("No active transfers"));
    else
        m_summary->setText(tr("%1 active  ·  %2 finished").arg(active).arg(finished));

    m_cancelAllBtn->setEnabled(active > 0);
    m_clearFinishedBtn->setEnabled(finished > 0);
}

bool TransfersPanel::isTerminalStatus(const QString &status) const
{
    return status == QLatin1String("completed")
        || status == QLatin1String("failed")
        || status == QLatin1String("cancelled");
}
