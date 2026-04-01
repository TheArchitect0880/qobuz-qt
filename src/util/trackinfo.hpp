#pragma once

#include <QDialog>
#include <QFormLayout>
#include <QDialogButtonBox>
#include <QLabel>
#include <QJsonObject>
#include <QWidget>

namespace TrackInfoDialog
{

inline void show(const QJsonObject &track, QWidget *parent)
{
    auto *dlg = new QDialog(parent);
    dlg->setWindowTitle(QObject::tr("Track Info"));
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->setMinimumWidth(360);

    auto *form = new QFormLayout(dlg);

    auto addRow = [&](const QString &label, const QString &value) {
        if (value.isEmpty()) return;
        auto *val = new QLabel(value, dlg);
        val->setTextInteractionFlags(Qt::TextSelectableByMouse);
        val->setWordWrap(true);
        form->addRow(QStringLiteral("<b>%1</b>").arg(label), val);
    };

    const QString title   = track["title"].toString();
    const QString version = track["version"].toString().trimmed();
    addRow(QObject::tr("Title"),
           version.isEmpty() ? title : title + QStringLiteral(" (%1)").arg(version));

    addRow(QObject::tr("Performer"), track["performer"].toObject()["name"].toString());

    const QJsonObject composer = track["composer"].toObject();
    if (!composer.isEmpty())
        addRow(QObject::tr("Composer"), composer["name"].toString());

    const QJsonObject album = track["album"].toObject();
    addRow(QObject::tr("Album"), album["title"].toString());
    addRow(QObject::tr("Album artist"), album["artist"].toObject()["name"].toString());

    const int trackNum = track["track_number"].toInt();
    const int discNum  = track["media_number"].toInt();
    if (trackNum > 0) {
        const QString pos = discNum > 1
            ? QStringLiteral("%1-%2").arg(discNum).arg(trackNum)
            : QString::number(trackNum);
        addRow(QObject::tr("Track #"), pos);
    }

    const qint64 dur = static_cast<qint64>(track["duration"].toDouble());
    if (dur > 0) {
        const int m = static_cast<int>(dur / 60);
        const int s = static_cast<int>(dur % 60);
        addRow(QObject::tr("Duration"),
               QStringLiteral("%1:%2").arg(m).arg(s, 2, 10, QLatin1Char('0')));
    }

    const int bitDepth     = track["maximum_bit_depth"].toInt();
    const double sampleRate = track["maximum_sampling_rate"].toDouble();
    if (bitDepth > 0 && sampleRate > 0) {
        addRow(QObject::tr("Quality"),
               QStringLiteral("%1-bit / %2 kHz").arg(bitDepth).arg(sampleRate, 0, 'f', 1));
    } else if (bitDepth > 0) {
        addRow(QObject::tr("Bit depth"), QStringLiteral("%1-bit").arg(bitDepth));
    }

    const bool hiRes = track["hires_streamable"].toBool() || track["hires"].toBool();
    addRow(QObject::tr("Hi-Res"), hiRes ? QObject::tr("Yes") : QObject::tr("No"));

    const bool streamable = track["streamable"].toBool(true);
    if (!streamable)
        addRow(QObject::tr("Streamable"), QObject::tr("No"));

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, dlg);
    form->addRow(buttons);
    QObject::connect(buttons, &QDialogButtonBox::rejected, dlg, &QDialog::close);

    dlg->show();
}

} // namespace TrackInfoDialog
