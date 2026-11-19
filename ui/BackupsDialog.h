// ShiftDex - unpack, edit and repack APK and JAR files
// Copyright (C) 2026 palaziks
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QDialog>
#include <QJsonObject>

class QLabel;
class QPushButton;
class QTabWidget;
class QTreeWidget;

// Snapshots of the whole project, backups of single files, and the
// untouched original the project was created from.
class BackupsDialog : public QDialog {
    Q_OBJECT
public:
    // currentFile: project-relative path of the file open in the editor, may be empty
    BackupsDialog(const QJsonObject &project, const QString &currentFile, QWidget *parent = nullptr);

signals:
    // The files on disk were replaced; open editors need to reload.
    void projectReplaced(const QJsonObject &project);
    void fileRestored(const QString &relativePath);

private:
    QWidget *buildSnapshots();
    QWidget *buildFiles();
    QWidget *buildOriginal();
    void loadSnapshots();
    void loadFiles();
    QString selectedSnapshot() const;

    QJsonObject m_project;
    QString m_dir;
    QString m_currentFile;
    QTreeWidget *m_snapshots = nullptr;
    QTreeWidget *m_files = nullptr;
    QPushButton *m_restore = nullptr, *m_export = nullptr, *m_delete = nullptr;
    QPushButton *m_fileRestore = nullptr, *m_fileDelete = nullptr;
};
