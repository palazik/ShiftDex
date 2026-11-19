// ShiftDex - unpack, edit and repack APK and JAR files
// Copyright (C) 2026 palaziks
// SPDX-License-Identifier: GPL-3.0-or-later
#include "Engine.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>

namespace Engine {

QString libDir()
{
    static QString cached;
    if (!cached.isEmpty())
        return cached;
    QStringList candidates;
    const QString fromEnv = qEnvironmentVariable("SHIFTDEX_LIBDIR");
    if (!fromEnv.isEmpty())
        candidates << fromEnv;
    // installed and build tree share this layout: bin/shiftdex, lib/shiftdex/
    candidates << QDir::cleanPath(QCoreApplication::applicationDirPath()
                                  + QStringLiteral("/../" SHIFTDEX_LIBDIR));
    for (const QString &candidate : candidates) {
        if (QFileInfo::exists(candidate + QStringLiteral("/engine/shiftdex/cli.py"))) {
            cached = candidate;
            return cached;
        }
    }
    return candidates.last();
}

QString docFile(const QString &name)
{
    const QString appDir = QCoreApplication::applicationDirPath();
    const QStringList candidates{
        QDir::cleanPath(appDir + QStringLiteral("/../share/doc/shiftdex/") + name),
        // build tree: lib/shiftdex/engine links back into the source tree
        QFileInfo(libDir() + QStringLiteral("/engine")).canonicalFilePath()
            + QStringLiteral("/../docs/") + name,
    };
    for (const QString &candidate : candidates) {
        if (QFileInfo::exists(candidate))
            return QFileInfo(candidate).canonicalFilePath();
    }
    return QString();
}

QProcessEnvironment environment()
{
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    const QString engine = libDir() + QStringLiteral("/engine");
    const QString existing = env.value(QStringLiteral("PYTHONPATH"));
    env.insert(QStringLiteral("PYTHONPATH"), existing.isEmpty() ? engine : engine + QLatin1Char(':') + existing);
    env.insert(QStringLiteral("SHIFTDEX_JARTOOL"), libDir() + QStringLiteral("/shiftdex-jartool.jar"));
    env.insert(QStringLiteral("PYTHONIOENCODING"), QStringLiteral("utf-8"));
    env.insert(QStringLiteral("PYTHONUNBUFFERED"), QStringLiteral("1"));
    return env;
}

QStringList baseArguments()
{
    return {QStringLiteral("-m"), QStringLiteral("shiftdex.cli"), QStringLiteral("--json")};
}

bool call(const QStringList &arguments, QJsonValue *result, QString *error, int timeoutMs)
{
    QProcess process;
    process.setProcessEnvironment(environment());
    process.setProcessChannelMode(QProcess::SeparateChannels);
    process.start(QStringLiteral("python3"), baseArguments() + arguments, QIODevice::ReadOnly);
    if (!process.waitForStarted(5000)) {
        if (error)
            *error = QObject::tr("python3 could not be started. Is Python 3 installed?");
        return false;
    }
    if (!process.waitForFinished(timeoutMs)) {
        process.kill();
        process.waitForFinished(2000);
        if (error)
            *error = QObject::tr("The engine did not answer in time.");
        return false;
    }
    const QList<QByteArray> lines = process.readAllStandardOutput().split('\n');
    for (auto it = lines.crbegin(); it != lines.crend(); ++it) {
        const QJsonObject object = QJsonDocument::fromJson(*it).object();
        if (!object.contains(QStringLiteral("ok")))
            continue;
        if (object.value(QStringLiteral("ok")).toBool()) {
            if (result)
                *result = object.value(QStringLiteral("result"));
            return true;
        }
        if (error)
            *error = object.value(QStringLiteral("error")).toString();
        return false;
    }
    if (error) {
        const QString detail = QString::fromUtf8(process.readAllStandardError()).trimmed();
        *error = detail.isEmpty() ? QObject::tr("The engine stopped unexpectedly.") : detail.right(600);
    }
    return false;
}

} // namespace Engine

EngineJob::EngineJob(QObject *parent) : QObject(parent)
{
    m_process.setProcessEnvironment(Engine::environment());
    connect(&m_process, &QProcess::readyReadStandardOutput, this, &EngineJob::readOutput);
    connect(&m_process, &QProcess::finished, this, &EngineJob::finish);
    connect(&m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError failure) {
        if (failure == QProcess::FailedToStart && !m_done) {
            m_done = true;
            emit finished(false, QJsonValue(), tr("python3 could not be started. Is Python 3 installed?"));
        }
    });
}

EngineJob::~EngineJob()
{
    if (m_process.state() != QProcess::NotRunning) {
        m_process.disconnect(this);
        m_process.terminate();
        if (!m_process.waitForFinished(3000))
            m_process.kill();
        m_process.waitForFinished(1000);
    }
}

void EngineJob::start(const QStringList &arguments)
{
    m_process.start(QStringLiteral("python3"), Engine::baseArguments() + arguments, QIODevice::ReadOnly);
}

void EngineJob::cancel()
{
    // SIGTERM: the engine stops its child process and cleans up half-written folders
    if (m_process.state() != QProcess::NotRunning)
        m_process.terminate();
}

bool EngineJob::isRunning() const
{
    return m_process.state() != QProcess::NotRunning;
}

void EngineJob::readOutput()
{
    m_buffer += m_process.readAllStandardOutput();
    int newline;
    while ((newline = m_buffer.indexOf('\n')) >= 0) {
        handleLine(m_buffer.left(newline));
        m_buffer.remove(0, newline + 1);
    }
}

void EngineJob::handleLine(const QByteArray &line)
{
    if (line.trimmed().isEmpty())
        return;
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(line, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        emit log(QString::fromUtf8(line));
        return;
    }
    const QJsonObject object = document.object();
    if (object.contains(QStringLiteral("log"))) {
        emit log(object.value(QStringLiteral("log")).toString());
    } else if (object.contains(QStringLiteral("progress"))) {
        emit progress(object.value(QStringLiteral("progress")).toDouble());
    } else if (object.contains(QStringLiteral("ok"))) {
        m_gotFinal = true;
        m_ok = object.value(QStringLiteral("ok")).toBool();
        m_result = object.value(QStringLiteral("result"));
        m_error = object.value(QStringLiteral("error")).toString();
    }
}

void EngineJob::finish(int, QProcess::ExitStatus)
{
    if (m_done)
        return;
    m_done = true;
    readOutput();
    if (!m_buffer.isEmpty())
        handleLine(m_buffer);
    if (!m_gotFinal) {
        const QString detail = QString::fromUtf8(m_process.readAllStandardError()).trimmed();
        for (const QString &line : detail.split(QLatin1Char('\n'), Qt::SkipEmptyParts))
            emit log(line);
        emit finished(false, QJsonValue(), tr("The engine stopped unexpectedly."));
        return;
    }
    emit finished(m_ok, m_result, m_ok ? QString() : m_error);
}
