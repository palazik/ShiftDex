// ShiftDex - unpack, edit and repack APK and JAR files
// Copyright (C) 2026 palaziks
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QDialog>
#include <QJsonObject>

class QCheckBox;
class QLabel;
class QLineEdit;

// Change an app's name, icon, package name and version.
class IdentityDialog : public QDialog {
    Q_OBJECT
public:
    IdentityDialog(const QJsonObject &project, QWidget *parent = nullptr);

signals:
    void applied(const QJsonObject &project);

private:
    void chooseIcon();
    void apply();
    void showIcon(const QString &path);
    bool renderIcons(const QString &directory, QString *error) const;

    QJsonObject m_project;
    QJsonObject m_current;
    QString m_newIcon;
    QLabel *m_icon;
    QLabel *m_iconNote;
    QLineEdit *m_name, *m_package, *m_versionName, *m_versionCode;
    QCheckBox *m_allLocales, *m_renameRefs;
};
