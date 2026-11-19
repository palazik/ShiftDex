// ShiftDex - unpack, edit and repack APK and JAR files
// Copyright (C) 2026 palaziks
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QJsonObject>
#include <QWidget>

class QLabel;
class QPushButton;

// Shows whether Java, APKEditor and an APK signer are available and lets
// the user download the missing ones. Used by first-run setup and Preferences.
class ToolsPanel : public QWidget {
    Q_OBJECT
public:
    explicit ToolsPanel(QWidget *parent = nullptr);

    void refresh();
    // Downloads APKEditor without being asked when it is missing (first-run setup).
    void fetchMissing();
    QJsonObject status() const { return m_status; }
    bool ready() const;

signals:
    void statusChanged();

private:
    void download(const QString &command, const QString &title);

    QJsonObject m_status;
    QLabel *m_java, *m_apkeditor, *m_signer, *m_jadx;
    QLabel *m_javaMark, *m_apkeditorMark, *m_signerMark, *m_jadxMark;
    QPushButton *m_getApkeditor, *m_localApkeditor, *m_getSigner, *m_getJadx;
    bool m_fetchTried = false;
};
