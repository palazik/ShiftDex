// ShiftDex - unpack, edit and repack APK and JAR files
// Copyright (C) 2026 palaziks
// SPDX-License-Identifier: GPL-3.0-or-later
#include "Theme.h"

#include <QApplication>
#include <QPalette>
#include <QStyleFactory>
#include <QStyleHints>

namespace Theme {

namespace {

Kind g_kind = Kind::Light;
// what the desktop asked for, read once before our own palette replaces it
int g_systemDark = -1;

struct Scheme {
    QColor window, base, alternate, text, muted, button, border, accent, accentText, tooltip;
};

Scheme scheme(Kind kind)
{
    switch (kind) {
    case Kind::Dark:
        return {"#242424", "#1b1b1b", "#212121", "#e3e3e3", "#9a9a9a", "#313131", "#3c3c3c",
                "#2f9e6e", "#ffffff", "#303030"};
    case Kind::Amoled:
        return {"#000000", "#000000", "#080808", "#e6e6e6", "#8c8c8c", "#121212", "#262626",
                "#2f9e6e", "#ffffff", "#141414"};
    case Kind::Light:
    default:
        return {"#f2f2f2", "#ffffff", "#f7f7f7", "#1d1d1d", "#6b6b6b", "#e9e9e9", "#cfcfcf",
                "#1c8457", "#ffffff", "#ffffff"};
    }
}

EditorColors editor(Kind kind)
{
    if (kind == Kind::Light) {
        return {QColor("#ffffff"), QColor("#1d1d1d"), QColor("#9a9a9a"), QColor("#f5f5f5"),
                QColor("#f0f4f1"), QColor("#ffe08a"),
                QColor("#7a8288"), QColor("#a3541a"), QColor("#7b3fb3"), QColor("#0b6fb8"),
                QColor("#00796b"), QColor("#b0257a"), QColor("#1565c0"), QColor("#8a5a00"),
                QColor("#c2410c"), QColor("#37474f")};
    }
    const bool amoled = kind == Kind::Amoled;
    return {QColor(amoled ? "#000000" : "#1b1b1b"), QColor("#e3e3e3"), QColor("#6f6f6f"),
            QColor(amoled ? "#000000" : "#202020"), QColor(amoled ? "#111111" : "#262a27"),
            QColor("#6b5a16"),
            QColor("#7f8a91"), QColor("#e0a36b"), QColor("#c792ea"), QColor("#79c0ff"),
            QColor("#5fd3bc"), QColor("#f07ab8"), QColor("#7fb4ff"), QColor("#e6c36a"),
            QColor("#ffa56b"), QColor("#b8c4cc")};
}

EditorColors g_editor = editor(Kind::Light);

QString styleSheet(const Scheme &s)
{
    // Fusion draws nearly everything from the palette; this only tightens
    // the places where it looks dated.
    QString css = QStringLiteral(R"(
QToolTip { color: %TEXT%; background: %TOOLTIP%; border: 1px solid %BORDER%; padding: 3px 6px; }
QMainWindow::separator, QSplitter::handle { background: %BORDER%; }
QSplitter::handle:horizontal { width: 1px; }
QSplitter::handle:vertical { height: 1px; }
QMenuBar { background: %WINDOW%; border-bottom: 1px solid %BORDER%; }
QMenuBar::item { padding: 4px 9px; background: transparent; }
QMenuBar::item:selected { background: %BUTTON%; }
QMenu { background: %BASE%; border: 1px solid %BORDER%; padding: 4px 0; }
QMenu::item { padding: 5px 26px 5px 22px; }
QMenu::item:selected { background: %ACCENT%; color: %ACCENTTEXT%; }
QMenu::item:disabled { color: %MUTED%; }
QMenu::separator { height: 1px; background: %BORDER%; margin: 4px 0; }
QToolBar { background: %WINDOW%; border: none; border-bottom: 1px solid %BORDER%; padding: 3px 6px; spacing: 2px; }
QToolBar QToolButton { padding: 4px 10px; border: 1px solid transparent; border-radius: 4px; }
QToolBar QToolButton:hover { background: %BUTTON%; border-color: %BORDER%; }
QToolBar QToolButton:pressed { background: %BORDER%; }
QToolBar QToolButton:disabled { color: %MUTED%; }
QToolBar QToolButton#primaryAction { background: %ACCENT%; color: %ACCENTTEXT%; font-weight: 600; }
QToolBar QToolButton#primaryAction:hover { border-color: %ACCENT%; }
QToolBar QToolButton#primaryAction:disabled { background: %BUTTON%; color: %MUTED%; }
QToolBar::separator { width: 1px; background: %BORDER%; margin: 4px 6px; }
QStatusBar { background: %WINDOW%; border-top: 1px solid %BORDER%; }
QStatusBar::item { border: none; }
QTabWidget::pane { border: none; border-top: 1px solid %BORDER%; }
QTabBar::tab { background: transparent; color: %MUTED%; padding: 6px 12px; border: none; border-bottom: 2px solid transparent; }
QTabBar::tab:selected { color: %TEXT%; border-bottom-color: %ACCENT%; }
QTabBar::tab:hover:!selected { color: %TEXT%; }
QTabBar::close-button { subcontrol-position: right; image: url(:/icons/close.svg); margin: 2px; }
QTabBar::close-button:hover { image: url(:/icons/close-hover.svg); }
QTreeView, QListView, QTableView, QPlainTextEdit, QTextEdit { border: 1px solid %BORDER%; }
QTreeView#borderless, QPlainTextEdit#borderless { border: none; }
QAbstractItemView { outline: none; }
QTreeView::item, QListView::item { padding: 3px 2px; }
QHeaderView::section { background: %WINDOW%; color: %MUTED%; border: none; border-bottom: 1px solid %BORDER%; padding: 4px 8px; }
QLineEdit, QSpinBox, QComboBox, QKeySequenceEdit { padding: 4px 6px; border: 1px solid %BORDER%; border-radius: 4px; background: %BASE%; }
QLineEdit:focus, QSpinBox:focus, QComboBox:focus { border-color: %ACCENT%; }
QComboBox::drop-down { border: none; width: 20px; }
QPushButton { padding: 5px 14px; border: 1px solid %BORDER%; border-radius: 4px; background: %BUTTON%; }
QPushButton:hover { border-color: %MUTED%; }
QPushButton:pressed { background: %BORDER%; }
QPushButton:disabled { color: %MUTED%; }
QPushButton:default { border-color: %ACCENT%; }
QPushButton#primary { background: %ACCENT%; color: %ACCENTTEXT%; border-color: %ACCENT%; font-weight: 600; }
QPushButton#primary:disabled { background: %BUTTON%; color: %MUTED%; border-color: %BORDER%; }
QPushButton#danger { color: %DANGER%; }
QGroupBox { border: 1px solid %BORDER%; border-radius: 6px; margin-top: 10px; padding: 12px 10px 8px 10px; }
QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 4px; color: %MUTED%; }
QProgressBar { border: 1px solid %BORDER%; border-radius: 4px; background: %BASE%; height: 8px; text-align: center; }
QProgressBar::chunk { background: %ACCENT%; border-radius: 3px; }
QScrollBar:vertical { background: transparent; width: 11px; margin: 0; }
QScrollBar:horizontal { background: transparent; height: 11px; margin: 0; }
QScrollBar::handle { background: %BORDER%; border-radius: 4px; margin: 2px; }
QScrollBar::handle:vertical { min-height: 28px; }
QScrollBar::handle:horizontal { min-width: 28px; }
QScrollBar::handle:hover { background: %MUTED%; }
QScrollBar::add-line, QScrollBar::sub-line { width: 0; height: 0; }
QScrollBar::add-page, QScrollBar::sub-page { background: transparent; }
QCheckBox, QRadioButton { spacing: 8px; }
QCheckBox::indicator, QRadioButton::indicator { width: 14px; height: 14px; border: 1px solid %MUTED%; background: %BASE%; }
QCheckBox::indicator { border-radius: 3px; }
QRadioButton::indicator { border-radius: 8px; }
QCheckBox::indicator:hover, QRadioButton::indicator:hover { border-color: %ACCENT%; }
QCheckBox::indicator:checked { background: %ACCENT%; border-color: %ACCENT%; image: url(:/icons/check.svg); }
QRadioButton::indicator:checked { border: 5px solid %ACCENT%; background: %ACCENTTEXT%; width: 6px; height: 6px; }
QCheckBox::indicator:disabled, QRadioButton::indicator:disabled { border-color: %BORDER%; }
QTreeView::indicator, QTableView::indicator { width: 14px; height: 14px; border: 1px solid %MUTED%; border-radius: 3px; background: %BASE%; }
QTreeView::indicator:checked, QTableView::indicator:checked { background: %ACCENT%; border-color: %ACCENT%; image: url(:/icons/check.svg); }
QLabel#muted, QCheckBox#muted { color: %MUTED%; }
QLabel#heading { font-size: 17pt; font-weight: 600; }
QLabel#subheading { font-size: 11pt; font-weight: 600; }
QLabel#tag { color: %ACCENT%; border: 1px solid %ACCENT%; border-radius: 3px; padding: 0 5px; font-size: 8pt; font-weight: 700; }
QFrame#bar { background: %WINDOW%; border: none; border-bottom: 1px solid %BORDER%; }
QFrame#findBar { background: %WINDOW%; border: none; border-top: 1px solid %BORDER%; }
QFrame#card { background: %BASE%; border: 1px solid %BORDER%; border-radius: 6px; }
)");
    css.replace(QLatin1String("%WINDOW%"), s.window.name());
    css.replace(QLatin1String("%BASE%"), s.base.name());
    css.replace(QLatin1String("%TEXT%"), s.text.name());
    css.replace(QLatin1String("%MUTED%"), s.muted.name());
    css.replace(QLatin1String("%BUTTON%"), s.button.name());
    css.replace(QLatin1String("%BORDER%"), s.border.name());
    css.replace(QLatin1String("%ACCENTTEXT%"), s.accentText.name());
    css.replace(QLatin1String("%ACCENT%"), s.accent.name());
    css.replace(QLatin1String("%TOOLTIP%"), s.tooltip.name());
    css.replace(QLatin1String("%DANGER%"), g_kind == Kind::Light ? QStringLiteral("#b3261e") : QStringLiteral("#ff8a80"));
    return css;
}

} // namespace

Kind resolve(const QString &setting)
{
    if (setting == QLatin1String("light"))
        return Kind::Light;
    if (setting == QLatin1String("dark"))
        return Kind::Dark;
    if (setting == QLatin1String("amoled"))
        return Kind::Amoled;
    return g_systemDark > 0 ? Kind::Dark : Kind::Light;
}

Kind current() { return g_kind; }

void apply(QApplication &app, const QString &setting)
{
    if (g_systemDark < 0) {
        bool dark = false;
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
        dark = QGuiApplication::styleHints()->colorScheme() == Qt::ColorScheme::Dark;
#endif
        if (!dark) {
            const QPalette system = QGuiApplication::palette();
            dark = system.color(QPalette::Window).lightness() < system.color(QPalette::WindowText).lightness();
        }
        g_systemDark = dark ? 1 : 0;
    }

    g_kind = resolve(setting);
    g_editor = editor(g_kind);
    const Scheme s = scheme(g_kind);

    // Fusion follows the palette on every desktop, which the native styles do not.
    app.setStyle(QStyleFactory::create(QStringLiteral("Fusion")));

    QPalette palette;
    palette.setColor(QPalette::Window, s.window);
    palette.setColor(QPalette::WindowText, s.text);
    palette.setColor(QPalette::Base, s.base);
    palette.setColor(QPalette::AlternateBase, s.alternate);
    palette.setColor(QPalette::Text, s.text);
    palette.setColor(QPalette::Button, s.button);
    palette.setColor(QPalette::ButtonText, s.text);
    palette.setColor(QPalette::BrightText, Qt::white);
    palette.setColor(QPalette::ToolTipBase, s.tooltip);
    palette.setColor(QPalette::ToolTipText, s.text);
    palette.setColor(QPalette::PlaceholderText, s.muted);
    palette.setColor(QPalette::Highlight, s.accent);
    palette.setColor(QPalette::HighlightedText, s.accentText);
    palette.setColor(QPalette::Link, g_kind == Kind::Light ? s.accent : QColor("#5fd3a2"));
    palette.setColor(QPalette::Light, s.border.lighter(120));
    palette.setColor(QPalette::Midlight, s.border);
    palette.setColor(QPalette::Mid, s.border);
    palette.setColor(QPalette::Dark, s.border.darker(130));
    palette.setColor(QPalette::Shadow, QColor(0, 0, 0, 90));
    for (QPalette::ColorRole role : {QPalette::WindowText, QPalette::Text, QPalette::ButtonText})
        palette.setColor(QPalette::Disabled, role, s.muted);
    app.setPalette(palette);
    app.setStyleSheet(styleSheet(s));
}

const EditorColors &editorColors() { return g_editor; }
QColor accent() { return scheme(g_kind).accent; }
QColor mutedText() { return scheme(g_kind).muted; }
QColor warning() { return g_kind == Kind::Light ? QColor("#9a6700") : QColor("#e3b341"); }
QColor good() { return g_kind == Kind::Light ? QColor("#1c8457") : QColor("#56d19a"); }

} // namespace Theme
