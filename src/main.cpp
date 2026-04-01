#include "mainwindow.hpp"
#include "backend/qobuzbackend.hpp"

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
    darkPalette.setColor(QPalette::Window,          QColor(0x19, 0x19, 0x19));
    darkPalette.setColor(QPalette::WindowText,      QColor(0xe8, 0xe8, 0xe8));
    darkPalette.setColor(QPalette::Base,            QColor(0x14, 0x14, 0x14));
    darkPalette.setColor(QPalette::AlternateBase,   QColor(0x1e, 0x1e, 0x1e));
    darkPalette.setColor(QPalette::ToolTipBase,     QColor(0x19, 0x19, 0x19));
    darkPalette.setColor(QPalette::ToolTipText,     QColor(0xe8, 0xe8, 0xe8));
    darkPalette.setColor(QPalette::Text,            QColor(0xe8, 0xe8, 0xe8));
    darkPalette.setColor(QPalette::Button,          QColor(0x2a, 0x2a, 0x2a));
    darkPalette.setColor(QPalette::ButtonText,      QColor(0xe8, 0xe8, 0xe8));
    darkPalette.setColor(QPalette::BrightText,      QColor(0xFF, 0xB2, 0x32));
    darkPalette.setColor(QPalette::Link,            QColor(0x46, 0xB3, 0xEE)); // Qobuz blue
    darkPalette.setColor(QPalette::Highlight,       QColor(0xFF, 0xB2, 0x32)); // Qobuz orange
    darkPalette.setColor(QPalette::HighlightedText, QColor(0x10, 0x10, 0x10)); // dark on orange
    darkPalette.setColor(QPalette::PlaceholderText,                QColor(0x66, 0x66, 0x66));
    darkPalette.setColor(QPalette::Disabled, QPalette::Text,       QColor(0x55, 0x55, 0x55));
    darkPalette.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(0x55, 0x55, 0x55));
    darkPalette.setColor(QPalette::Mid,             QColor(0x2f, 0x2f, 0x2f));
    darkPalette.setColor(QPalette::Dark,            QColor(0x0e, 0x0e, 0x0e));
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

    auto *backend = new QobuzBackend;
    MainWindow window(backend);
    window.show();

    return app.exec();
}
