// ShiftDex - unpack, edit and repack APK and JAR files
// Copyright (C) 2026 palaziks
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QJsonValue>
#include <QObject>
#include <QProcess>
#include <QStringList>

// One running engine command. The engine (engine/shiftdex/cli.py) prints a
// JSON object per line: log lines, progress, and a final result or error.
class EngineJob : public QObject {
    Q_OBJECT
public:
    explicit EngineJob(QObject *parent = nullptr);
    ~EngineJob() override;

    void start(const QStringList &arguments);
    void cancel();
    bool isRunning() const;

signals:
    void log(const QString &line);
    void progress(double fraction);
    // error is empty on success; "Cancelled" when the user stopped it
    void finished(bool ok, const QJsonValue &result, const QString &error);

private:
    void readOutput();
    void handleLine(const QByteArray &line);
    void finish(int exitCode, QProcess::ExitStatus status);

    QProcess m_process;
    QByteArray m_buffer;
    QJsonValue m_result;
    QString m_error;
    bool m_gotFinal = false;
    bool m_ok = false;
    bool m_done = false;
};

namespace Engine {

// Folder with the engine and jartool: <prefix>/lib/shiftdex.
QString libDir();
QString docFile(const QString &name);

// Runs a short command and waits for it. Only for commands that return at
// once (listings, single file operations); everything slow goes through
// JobDialog so the window stays responsive.
bool call(const QStringList &arguments, QJsonValue *result = nullptr, QString *error = nullptr,
          int timeoutMs = 30000);

QProcessEnvironment environment();
QStringList baseArguments();

} // namespace Engine
