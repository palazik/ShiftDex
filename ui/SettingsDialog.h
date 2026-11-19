// ShiftDex - unpack, edit and repack APK and JAR files
// Copyright (C) 2026 palaziks
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QDialog>

class QKeySequenceEdit;
class QLabel;
class QPushButton;
class QTreeWidget;

// Preferences. Every control writes its setting as soon as it changes.
class SettingsDialog : public QDialog {
    Q_OBJECT
public:
    explicit SettingsDialog(QWidget *parent = nullptr, int page = 0);

private:
    QWidget *buildGeneral();
    QWidget *buildEditor();
    QWidget *buildBuilding();
    QWidget *buildTools();
    QWidget *buildShortcuts();
    void loadShortcuts();
    QString selectedAction() const;

    QTreeWidget *m_shortcuts = nullptr;
    QKeySequenceEdit *m_sequence = nullptr;
    QLabel *m_shortcutNote = nullptr;
    QPushButton *m_assign = nullptr, *m_clear = nullptr, *m_resetOne = nullptr;
};
