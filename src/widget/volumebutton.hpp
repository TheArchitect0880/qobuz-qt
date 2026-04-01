#pragma once

#include "clickableslider.hpp"
#include "../util/icon.hpp"

#include <QToolButton>
#include <QFrame>
#include <QVBoxLayout>
#include <QLabel>

/// A toolbar button that shows a volume slider popup when clicked.
class VolumeButton : public QToolButton
{
    Q_OBJECT

public:
    explicit VolumeButton(QWidget *parent = nullptr) : QToolButton(parent)
    {
        setIcon(Icon::volumeHigh());

        // Qt::Popup closes automatically when the user clicks outside.
        m_popup = new QFrame(this, Qt::Popup);
        m_popup->setFrameShape(QFrame::StyledPanel);
        m_popup->setFrameShadow(QFrame::Raised);

        auto *layout = new QVBoxLayout(m_popup);
        layout->setContentsMargins(10, 10, 10, 10);
        layout->setSpacing(6);

        m_label = new QLabel(QStringLiteral("80%"), m_popup);
        m_label->setAlignment(Qt::AlignCenter);
        layout->addWidget(m_label);

        m_slider = new ClickableSlider(Qt::Vertical, m_popup);
        m_slider->setRange(0, 100);
        m_slider->setValue(80);
        m_slider->setFixedHeight(120);
        layout->addWidget(m_slider, 0, Qt::AlignHCenter);

        // Size the popup at its maximum (label = "100%") and lock it
        m_label->setText(QStringLiteral("100%"));
        m_popup->adjustSize();
        m_popup->setFixedSize(m_popup->sizeHint());
        m_label->setText(QStringLiteral("80%"));

        connect(this, &QToolButton::clicked, this, &VolumeButton::togglePopup);
        connect(m_slider, &QSlider::valueChanged, this, [this](int v) {
            m_label->setText(QString::number(v) + QStringLiteral("%"));
            updateIcon(v);
            emit volumeChanged(v);
        });
    }

    int value() const { return m_slider->value(); }

    void setValue(int v)
    {
        m_slider->blockSignals(true);
        m_slider->setValue(v);
        m_slider->blockSignals(false);
        m_label->setText(QString::number(v) + QStringLiteral("%"));
        updateIcon(v);
    }

signals:
    void volumeChanged(int volume);

private slots:
    void togglePopup()
    {
        if (m_popup->isVisible()) {
            m_popup->hide();
            return;
        }
        // Centre popup horizontally over button, place below it
        const QPoint global = mapToGlobal(
            QPoint(width() / 2 - m_popup->width() / 2,
                   height() + 4));
        m_popup->move(global);
        m_popup->show();
        m_popup->raise();
    }

private:
    QFrame          *m_popup  = nullptr;
    ClickableSlider *m_slider = nullptr;
    QLabel          *m_label  = nullptr;

    void updateIcon(int v)
    {
        if (v == 0)      setIcon(Icon::volumeMute());
        else if (v < 50) setIcon(Icon::volumeMid());
        else             setIcon(Icon::volumeHigh());
    }
};
