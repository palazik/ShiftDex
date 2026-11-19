// ShiftDex - unpack, edit and repack APK and JAR files
// Copyright (C) 2026 palaziks
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QDialog>
#include <QJsonValue>
#include <functional>

class EngineJob;
class QLabel;
class QPlainTextEdit;
class QProgressBar;
class QPushButton;

// Runs one engine command while showing what it is doing. Closes by itself
// on success; stays open with the log when the command failed.
class JobDialog : public QDialog {
    Q_OBJECT
public:
    using Callback = std::function<void(bool ok, const QJsonValue &result)>;

    // Starts the command and returns at once; callback runs when it ended.
    static void run(QWidget *parent, const QString &title, const QStringList &arguments,
                    Callback callback = nullptr);

    // Output of the most recent task, for Help > Last Task Log.
    static QString lastLog();
    static QString lastTitle();

protected:
    void closeEvent(QCloseEvent *event) override;
    void reject() override;

private:
    JobDialog(QWidget *parent, const QString &title);
    void append(const QString &line);
    void done(bool ok, const QJsonValue &result, const QString &error);
    void setDetailsVisible(bool visible);

    EngineJob *m_job = nullptr;
    QLabel *m_status = nullptr;
    QProgressBar *m_bar = nullptr;
    QPlainTextEdit *m_log = nullptr;
    QPushButton *m_details = nullptr;
    QPushButton *m_button = nullptr;
    Callback m_callback;
    bool m_running = true;
    bool m_cancelRequested = false;
    bool m_reported = false;
    QJsonValue m_result;
};
