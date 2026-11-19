// ShiftDex - unpack, edit and repack APK and JAR files
// Copyright (C) 2026 palaziks
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QJsonObject>
#include <QWidget>

class QLabel;
class QPushButton;
class QStackedWidget;
class QTreeWidget;

// Start page: the list of projects and the way to open a new file.
class HomePage : public QWidget {
    Q_OBJECT
public:
    explicit HomePage(QWidget *parent = nullptr);

    void refresh();

signals:
    void openFileRequested();
    void projectChosen(const QJsonObject &project);
    void pluginsRequested();

private:
    QJsonObject selected() const;
    void removeSelected();

    QTreeWidget *m_list;
    QStackedWidget *m_stack;
    QPushButton *m_open, *m_remove, *m_folder;
};
