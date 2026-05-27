#pragma once

#include <QDockWidget>
#include <QHash>
#include <QJsonObject>

class QLabel;
class QPushButton;
class QTreeWidget;
class QTreeWidgetItem;

class TransfersPanel : public QDockWidget
{
    Q_OBJECT

public:
    explicit TransfersPanel(QWidget *parent = nullptr);

    void onTransferStarted(const QJsonObject &info);
    void onTransferProgress(const QJsonObject &info);
    void onTransferFinished(const QJsonObject &info);
    void onTransferFailed(const QJsonObject &info);
    void onTransferCancelled(const QJsonObject &info);

signals:
    void cancelTransferRequested(quint64 transferId);
    void cancelAllTransfersRequested();

private slots:
    void onContextMenuRequested(const QPoint &pos);
    void clearFinished();

private:
    enum Column {
        ColTitle = 0,
        ColStatus,
        ColProgress,
        ColCurrent,
    };

    QLabel *m_summary = nullptr;
    QPushButton *m_cancelAllBtn = nullptr;
    QPushButton *m_clearFinishedBtn = nullptr;
    QTreeWidget *m_tree = nullptr;
    QHash<quint64, QTreeWidgetItem *> m_items;

    void upsertTransfer(const QJsonObject &info, const QString &statusOverride = QString());
    void updateSummary();
    bool isTerminalStatus(const QString &status) const;
};
