// ShiftDex - unpack, edit and repack APK and JAR files
// Copyright (C) 2026 palaziks
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QJsonObject>
#include <QMainWindow>

class HomePage;
class QAction;
class QLabel;
class QStackedWidget;
class QToolBar;
class SetupPage;
class Workspace;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    MainWindow();

    // Unpacks an APK or JAR into a new project and opens it.
    void openPath(const QString &path);
    void openProject(const QJsonObject &project);
    void showPreferences(int page = 0);
    void showAbout();
    Workspace *workspace() const { return m_workspace; }

protected:
    void closeEvent(QCloseEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;

private:
    void buildActions();
    void chooseFile();
    void goHome();
    void showHome();
    void updateActions();
    void showLastLog();
    bool inWorkspace() const;
    QAction *action(const QString &text, const QString &keymapId = QString());

    QStackedWidget *m_stack;
    SetupPage *m_setup;
    HomePage *m_home;
    Workspace *m_workspace;
    QToolBar *m_toolbar;
    QLabel *m_fileInfo;

    QAction *m_save, *m_saveAll, *m_closeTab, *m_back, *m_find, *m_replace, *m_goto;
    QAction *m_build, *m_identity, *m_backups, *m_snapshot, *m_backupFile, *m_plugins;
    QAction *m_searchFiles, *m_quickOpen, *m_sidebar, *m_projectFolder, *m_open;
    QAction *m_switchCode, *m_updateJava;
    void unpack(const QString &path);
    QList<QAction *> m_editActions;
};
