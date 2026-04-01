#pragma once

#include <QSlider>
#include <QMouseEvent>

/// A QSlider that jumps to the clicked position instead of only moving one step.
class ClickableSlider : public QSlider
{
    Q_OBJECT

public:
    explicit ClickableSlider(Qt::Orientation orientation, QWidget *parent = nullptr)
        : QSlider(orientation, parent) {}

protected:
    void mousePressEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton) {
            // Jump the handle to the clicked position first so Qt's own
            // press handler sees the click "on" the handle and starts drag.
            const int val = posToValue(event->pos());
            setSliderPosition(val);
            emit sliderMoved(val);
        }
        // Always forward so the slider enters drag mode normally.
        QSlider::mousePressEvent(event);
    }

    void mouseMoveEvent(QMouseEvent *event) override
    {
        // During a drag, keep snapping to cursor position.
        if (event->buttons() & Qt::LeftButton) {
            const int val = posToValue(event->pos());
            setSliderPosition(val);
            emit sliderMoved(val);
        }
        QSlider::mouseMoveEvent(event);
    }

private:
    int posToValue(const QPoint &pos) const
    {
        const int span  = orientation() == Qt::Horizontal ? width() : height();
        const int pixel = orientation() == Qt::Horizontal ? pos.x() : (height() - pos.y());
        if (span <= 0) return minimum();
        const int val = minimum() + (maximum() - minimum()) * pixel / span;
        return qBound(minimum(), val, maximum());
    }
};
