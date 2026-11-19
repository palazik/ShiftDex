// ShiftDex - unpack, edit and repack APK and JAR files
// Copyright (C) 2026 palaziks
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QDialog>
#include <QJsonObject>

class QLabel;
class QLineEdit;
class QCheckBox;
class QComboBox;
class QPushButton;
class QTableWidget;
class QTreeWidget;

// Installed plugins: apply them to the open project, import, export,
// remove, and create new ones.
class PluginsDialog : public QDialog {
    Q_OBJECT
public:
    // project is empty when opened from the project list
    PluginsDialog(const QJsonObject &project, QWidget *parent = nullptr);

    static void openGuide(QWidget *parent);
    // "This is a smali plugin: …" - shown wherever a plugin is installed or applied.
    static QString codeSentence(const QJsonObject &plugin);

signals:
    void projectChanged(const QJsonObject &project);

private:
    void reload(const QString &selectId = QString());
    QJsonObject selected() const;
    void updateDetails();
    void applySelected();
    void installFromFile();
    void exportSelected();
    void removeSelected();
    void createNew();

    QJsonObject m_project;
    QTreeWidget *m_list;
    QLabel *m_details;
    QPushButton *m_apply, *m_export, *m_remove, *m_create;
};

// Builds a plugin from the changes made to a project and/or replace rules.
class PluginCreator : public QDialog {
    Q_OBJECT
public:
    PluginCreator(const QJsonObject &project, QWidget *parent = nullptr);

    QString createdId() const { return m_createdId; }
    QString createdCodeLabel() const { return m_createdCode; }

private:
    void loadChanges();
    void addRule(const QString &files = QString(), const QString &find = QString(),
                 const QString &replace = QString());
    void create();

    QJsonObject m_project;
    QLineEdit *m_id, *m_name, *m_version, *m_author;
    QLineEdit *m_description;
    QCheckBox *m_restrict;
    QComboBox *m_code;
    QString m_createdCode;
    QTreeWidget *m_changes;
    QLabel *m_changesNote;
    QTableWidget *m_rules;
    QString m_createdId;
};
