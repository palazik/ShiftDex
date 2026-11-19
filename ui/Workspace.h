// ShiftDex - unpack, edit and repack APK and JAR files
// Copyright (C) 2026 palaziks
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QJsonObject>
#include <QSet>
#include <QWidget>
#include <functional>

class DocTab;
class QComboBox;
class QFileSystemModel;
class QLabel;
class QSplitter;
class QStackedWidget;
class QTabWidget;
class QTreeView;
class SearchPanel;

// The editing view of one project: file tree and search on the left,
// open files in tabs on the right.
class Workspace : public QWidget {
    Q_OBJECT
public:
    explicit Workspace(QWidget *parent = nullptr);

    void openProject(const QJsonObject &project);
    void closeProject();
    QJsonObject project() const { return m_project; }
    bool isApk() const;

    DocTab *currentTab() const;
    bool hasModified() const;
    // Offers to save unsaved files. False when the user cancelled.
    bool confirmClose();

    void openFile(const QString &path, int line = 0, int column = 0);
    bool saveCurrent();
    bool saveAll();
    void closeCurrentTab();
    void find(bool withReplace);
    void goToLine();
    void searchProject(bool fileNames);
    void toggleSidebar();

    void build();
    void createSnapshot();
    void backupCurrentFile();
    void showBackups();
    void showPlugins();
    void showIdentity();
    void openProjectFolder();

    // "smali" (the editable code, class files for a JAR) or "java" (read-only, by jadx)
    QString codeView() const { return m_codeView; }
    // Jumps between a class's smali and its Java; without one, switches the view.
    void switchCode();
    void updateJavaView();
    // False for the read-only Java view, which lives outside the editable tree.
    bool isProjectFile(const QString &path) const;

    void applySettings();

signals:
    // tab switched, modified state changed, project loaded: refresh menus
    void stateChanged();
    void status(const QString &message);
    void projectUpdated(const QJsonObject &project);

private:
    QString src() const;
    QString javaDir() const;
    QString treeRoot() const;
    void setCodeView(const QString &view);
    void applyCodeView();
    void syncViewControls();
    void decompileJava(bool fromCurrent, std::function<void()> then);
    void ensureJava(std::function<void()> then);
    QString counterpart(const QString &path) const;
    QString relative(const QString &path) const;
    DocTab *tabFor(const QString &path) const;
    DocTab *tabAt(int index) const;
    bool saveTab(DocTab *tab);
    void closeTab(int index, bool ask = true);
    void updateTabTitle(DocTab *tab);
    void updateHeader();
    void resetTree();
    void reveal(const QString &path);
    void reloadTabs();
    void setProject(const QJsonObject &project);
    void treeMenu(const QPoint &position);
    void pathChanged(const QString &from, const QString &to);
    void pathRemoved(const QString &path);
    void replaceFile(const QString &target);

    QJsonObject m_project;
    QSet<QString> m_backedUp;
    QString m_pendingReveal;
    QString m_codeView = QStringLiteral("smali");
    QLabel *m_icon, *m_title, *m_subtitle, *m_kind, *m_codeTag;
    QWidget *m_sidePanel;
    QComboBox *m_viewCombo;
    QSplitter *m_splitter;
    QTabWidget *m_side;
    QTreeView *m_tree;
    QFileSystemModel *m_model = nullptr;
    SearchPanel *m_search;
    QStackedWidget *m_stack;
    QTabWidget *m_tabs;
};
