#pragma once

#include "../../backend/qobuzbackend.hpp"
#include "../../util/colors.hpp"

#include <QDockWidget>
#include <QWidget>
#include <QLabel>
#include <QPixmap>
#include <QPainter>
#include <QPaintEvent>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QJsonObject>

namespace Context
{
    /// Square art widget: always as wide as its parent allows, height follows width.
    class ArtWidget : public QWidget
    {
    public:
        explicit ArtWidget(QWidget *parent = nullptr) : QWidget(parent)
        {
            setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        }

        void setPixmap(const QPixmap &px) { m_pix = px; update(); }
        bool hasHeightForWidth() const override { return true; }
        int  heightForWidth(int w) const override { return w; }

    protected:
        void paintEvent(QPaintEvent *) override
        {
            QPainter p(this);
            if (m_pix.isNull()) {
                p.fillRect(rect(), Colors::ContextBg);
                return;
            }
            const QPixmap scaled = m_pix.scaled(size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
            p.fillRect(rect(), Colors::ContextBg);
            p.drawPixmap((width()  - scaled.width())  / 2,
                         (height() - scaled.height()) / 2,
                         scaled);
        }

    private:
        QPixmap m_pix;
    };

    class View : public QDockWidget
    {
        Q_OBJECT

    public:
        explicit View(QobuzBackend *backend, QWidget *parent = nullptr);

    private slots:
        void onTrackChanged(const QJsonObject &track);
        void onArtReady(QNetworkReply *reply);

    private:
        QobuzBackend          *m_backend       = nullptr;
        ArtWidget             *m_albumArt      = nullptr;
        QLabel                *m_title         = nullptr;
        QLabel                *m_artist        = nullptr;
        QNetworkAccessManager *m_nam           = nullptr;
        QString                m_currentArtUrl;
    };
} // namespace Context
