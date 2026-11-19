// ShiftDex - unpack, edit and repack APK and JAR files
// Copyright (C) 2026 palaziks
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QWidget>

class QLabel;
class QPushButton;
class QStackedWidget;
class ToolsPanel;

// First-run setup: welcome, tools, appearance.
class SetupPage : public QWidget {
    Q_OBJECT
public:
    explicit SetupPage(QWidget *parent = nullptr);

signals:
    void finished();

private:
    QWidget *buildWelcome();
    QWidget *buildTools();
    QWidget *buildAppearance();
    void showStep(int step);

    QStackedWidget *m_steps;
    QLabel *m_stepLabel;
    QPushButton *m_back, *m_next;
    ToolsPanel *m_tools = nullptr;
};
