#include "gui/theme.h"

#include <QApplication>
#include <QPalette>
#include <QStyleHints>
#include <QWidget>

namespace theme {

namespace {

QPalette palette(bool dark)
{
    QColor window, base, text, disabled, mid, shade, highlight, link;
    if (dark) {
        window = QColor(37, 38, 42), base = QColor(27, 28, 31), text = QColor(225, 225, 228);
        disabled = QColor(130, 130, 135), mid = QColor(70, 70, 76), shade = QColor(20, 20, 22);
        highlight = QColor(60, 110, 200), link = QColor(110, 170, 255);
    } else {
        window = QColor(239, 239, 241), base = QColor(255, 255, 255), text = QColor(25, 25, 28);
        disabled = QColor(120, 120, 125), mid = QColor(180, 180, 186), shade = QColor(150, 150, 155);
        highlight = QColor(48, 110, 210), link = QColor(20, 90, 200);
    }
    QPalette p;
    p.setColor(QPalette::Window, window);
    p.setColor(QPalette::WindowText, text);
    p.setColor(QPalette::Base, base);
    p.setColor(QPalette::AlternateBase, window);
    p.setColor(QPalette::ToolTipBase, window);
    p.setColor(QPalette::ToolTipText, text);
    p.setColor(QPalette::PlaceholderText, disabled);
    p.setColor(QPalette::Text, text);
    p.setColor(QPalette::Button, window);
    p.setColor(QPalette::ButtonText, text);
    p.setColor(QPalette::BrightText, Qt::red);
    p.setColor(QPalette::Link, link);
    p.setColor(QPalette::Highlight, highlight);
    p.setColor(QPalette::HighlightedText, Qt::white);
    p.setColor(QPalette::Mid, mid);
    p.setColor(QPalette::Dark, shade);
    for (QPalette::ColorRole role : {QPalette::Text, QPalette::WindowText, QPalette::ButtonText})
        p.setColor(QPalette::Disabled, role, disabled);
    return p;
}

}  // namespace

bool isDark()
{
    const Qt::ColorScheme scheme = QGuiApplication::styleHints()->colorScheme();
    if (scheme == Qt::ColorScheme::Unknown)
        return QGuiApplication::palette().color(QPalette::Window).lightness() < 128;
    return scheme == Qt::ColorScheme::Dark;
}

void apply(QApplication &app)
{
    app.setStyle(QStringLiteral("Fusion"));
    app.setPalette(palette(isDark()));
    // style sheets resolve palette() colors when polished, so re-polish
    const QWidgetList widgets = QApplication::allWidgets();
    for (QWidget *widget : widgets) {
        if (!widget->styleSheet().isEmpty())
            widget->setStyleSheet(widget->styleSheet());
    }
}

void followSystem(QApplication &app)
{
    apply(app);
    QObject::connect(QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged, &app,
                     [&app] { apply(app); });
}

}  // namespace theme
