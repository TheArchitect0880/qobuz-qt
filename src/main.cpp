#include "mainwindow.hpp"
#include "backend/qobuzbackend.hpp"
#include "util/colors.hpp"

#include <QApplication>
#include <QStyleFactory>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("qobuz-qt"));
    app.setOrganizationName(QStringLiteral("qobuz-qt"));
    app.setApplicationVersion(QStringLiteral("0.1.0"));

    // Qobuz dark palette
    // Accent: #FFB232 (yellow-orange), Blue: #46B3EE, Backgrounds: #191919 / #141414
    app.setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
    QPalette darkPalette;
    darkPalette.setColor(QPalette::Window,          Colors::WindowBg);
    darkPalette.setColor(QPalette::WindowText,      Colors::LightText);
    darkPalette.setColor(QPalette::Base,            Colors::BaseBg);
    darkPalette.setColor(QPalette::AlternateBase,   Colors::AlternateBaseBg);
    darkPalette.setColor(QPalette::ToolTipBase,     Colors::WindowBg);
    darkPalette.setColor(QPalette::ToolTipText,     Colors::LightText);
    darkPalette.setColor(QPalette::Text,            Colors::LightText);
    darkPalette.setColor(QPalette::Button,          Colors::ButtonSurface);
    darkPalette.setColor(QPalette::ButtonText,      Colors::LightText);
    darkPalette.setColor(QPalette::BrightText,      Colors::QobuzOrange);
    darkPalette.setColor(QPalette::Link,            Colors::QobuzBlue);
    darkPalette.setColor(QPalette::Highlight,       Colors::QobuzOrange);
    darkPalette.setColor(QPalette::HighlightedText, Colors::HighlightedFg);
    darkPalette.setColor(QPalette::PlaceholderText,                Colors::PlaceholderText);
    darkPalette.setColor(QPalette::Disabled, QPalette::Text,       Colors::DisabledText);
    darkPalette.setColor(QPalette::Disabled, QPalette::ButtonText, Colors::DisabledText);
    darkPalette.setColor(QPalette::Mid,             Colors::MidSurface);
    darkPalette.setColor(QPalette::Dark,            Colors::DarkSurface);
    app.setPalette(darkPalette);

    // Stylesheet tweaks: orange accent on scrollbars, focus rings, etc.
    app.setStyleSheet(QStringLiteral(
        "QScrollBar:vertical { width: 6px; background: #141414; border: none; }"
        "QScrollBar::handle:vertical { background: #3a3a3a; border-radius: 3px; min-height: 20px; }"
        "QScrollBar::handle:vertical:hover { background: #FFB232; }"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }"
        "QScrollBar:horizontal { height: 6px; background: #141414; border: none; }"
        "QScrollBar::handle:horizontal { background: #3a3a3a; border-radius: 3px; min-width: 20px; }"
        "QScrollBar::handle:horizontal:hover { background: #FFB232; }"
        "QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal { width: 0; }"
        "QToolBar { background: #111111; border-bottom: 1px solid #2a2a2a; spacing: 4px; }"
        "QDockWidget { border: none; }"
        "QDockWidget::title { background: #1e1e1e; padding: 4px 8px; font-weight: bold; }"
        "QTreeView, QTreeWidget { border: none; outline: none; }"
        "QTreeView::item:selected, QTreeWidget::item:selected { color: #101010; }"
        "QHeaderView::section { background: #1e1e1e; border: none;"
        "  border-right: 1px solid #2a2a2a; padding: 4px 8px; }"
        "QMenu { background: #1e1e1e; border: 1px solid #3a3a3a; }"
        "QMenu::item:selected { background: #FFB232; color: #101010; }"
        "QPushButton { background: #2a2a2a; border: 1px solid #3a3a3a; border-radius: 4px; padding: 4px 12px; }"
        "QPushButton:hover { background: #333333; border-color: #FFB232; }"
        "QPushButton:pressed { background: #FFB232; color: #101010; }"
        "QComboBox { background: #2a2a2a; border: 1px solid #3a3a3a; border-radius: 4px; padding: 3px 8px; }"
        "QComboBox:hover { border-color: #FFB232; }"
        "QComboBox QAbstractItemView { background: #1e1e1e; selection-background-color: #FFB232; selection-color: #101010; }"
        "QLineEdit { background: #1e1e1e; border: 1px solid #3a3a3a; border-radius: 4px; padding: 4px 8px; }"
        "QLineEdit:focus { border-color: #FFB232; }"
        "QGroupBox { border: 1px solid #2f2f2f; border-radius: 6px; margin-top: 8px; padding-top: 4px; }"
        "QGroupBox::title { subcontrol-origin: margin; left: 8px; color: #FFB232; }"
        "QCheckBox::indicator:checked { background: #FFB232; border: 1px solid #FFB232; border-radius: 2px; }"
        "QSlider::groove:horizontal { height: 4px; background: #2a2a2a; border-radius: 2px; }"
        "QSlider::handle:horizontal { background: #FFB232; width: 12px; height: 12px;"
        "  margin: -4px 0; border-radius: 6px; }"
        "QSlider::sub-page:horizontal { background: #FFB232; border-radius: 2px; }"
        "QStatusBar { background: #111111; border-top: 1px solid #2a2a2a; }"
    ));

    auto *backend = new QobuzBackend(&app);
    MainWindow window(backend);
    window.show();

    return app.exec();
}
