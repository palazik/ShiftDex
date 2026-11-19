// ShiftDex - unpack, edit and repack APK and JAR files
// Copyright (C) 2026 palaziks
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QJsonObject>
#include <QObject>
#include <QString>

// Settings shared with the engine: one JSON file, written here and read by
// engine/shiftdex/config.py. Keys and value types must stay in sync with
// DEFAULTS in that file.
class Config : public QObject {
    Q_OBJECT
public:
    static Config &instance();

    QString filePath() const;

    QString str(const QString &key) const;
    bool flag(const QString &key) const;
    int number(const QString &key) const;
    QJsonObject object(const QString &key) const;

    void set(const QString &key, const QJsonValue &value);

    // Folder that holds the projects (the custom one or the default).
    QString workspaceDir() const;
    static QString dataDir();

signals:
    void changed(const QString &key);

private:
    Config();
    void load();
    void save() const;

    QJsonObject m_defaults;
    QJsonObject m_values;
};
