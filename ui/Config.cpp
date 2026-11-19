// ShiftDex - unpack, edit and repack APK and JAR files
// Copyright (C) 2026 palaziks
// SPDX-License-Identifier: GPL-3.0-or-later
#include "Config.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QSaveFile>
#include <QStandardPaths>

Config &Config::instance()
{
    static Config config;
    return config;
}

Config::Config()
{
    m_defaults = QJsonObject{
        {QStringLiteral("setup_done"), false},
        {QStringLiteral("theme"), QStringLiteral("system")},
        {QStringLiteral("workspace_dir"), QString()},
        {QStringLiteral("window_width"), 1180},
        {QStringLiteral("window_height"), 760},
        {QStringLiteral("sidebar_width"), 280},
        {QStringLiteral("java_path"), QString()},
        {QStringLiteral("java_heap"), QString()},
        {QStringLiteral("decode_type"), QStringLiteral("xml")},
        {QStringLiteral("decode_smali"), true},
        {QStringLiteral("sign_after_build"), true},
        {QStringLiteral("keystore_path"), QString()},
        {QStringLiteral("keystore_alias"), QString()},
        {QStringLiteral("keystore_pass"), QString()},
        {QStringLiteral("key_pass"), QString()},
        {QStringLiteral("jar_strip_signatures"), true},
        {QStringLiteral("last_export_dir"), QString()},
        {QStringLiteral("snapshot_before_plugin"), true},
        {QStringLiteral("snapshot_before_restore"), true},
        {QStringLiteral("backup_on_save"), false},
        {QStringLiteral("editor_font_size"), 11},
        {QStringLiteral("editor_wrap"), false},
        {QStringLiteral("editor_line_numbers"), true},
        {QStringLiteral("editor_highlight_line"), true},
        {QStringLiteral("editor_tab_width"), 4},
        {QStringLiteral("editor_spaces"), true},
        {QStringLiteral("keymap"), QJsonObject()},
    };
    m_values = m_defaults;
    load();
}

QString Config::filePath() const
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
        + QStringLiteral("/shiftdex/config.json");
}

QString Config::dataDir()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
        + QStringLiteral("/shiftdex");
}

void Config::load()
{
    QFile file(filePath());
    if (!file.open(QIODevice::ReadOnly))
        return;
    const QJsonObject stored = QJsonDocument::fromJson(file.readAll()).object();
    for (auto it = m_defaults.constBegin(); it != m_defaults.constEnd(); ++it) {
        const QJsonValue value = stored.value(it.key());
        if (value.type() == it.value().type())
            m_values.insert(it.key(), value);
    }
}

void Config::save() const
{
    QDir().mkpath(QFileInfo(filePath()).absolutePath());
    QSaveFile file(filePath());
    if (!file.open(QIODevice::WriteOnly))
        return;
    file.write(QJsonDocument(m_values).toJson(QJsonDocument::Indented));
    file.commit();
    // the keystore password may be in here
    QFile::setPermissions(filePath(), QFile::ReadOwner | QFile::WriteOwner);
}

QString Config::str(const QString &key) const { return m_values.value(key).toString(); }
bool Config::flag(const QString &key) const { return m_values.value(key).toBool(); }
int Config::number(const QString &key) const { return m_values.value(key).toInt(); }
QJsonObject Config::object(const QString &key) const { return m_values.value(key).toObject(); }

void Config::set(const QString &key, const QJsonValue &value)
{
    if (!m_defaults.contains(key) || m_values.value(key) == value)
        return;
    m_values.insert(key, value);
    save();
    emit changed(key);
}

QString Config::workspaceDir() const
{
    const QString custom = str(QStringLiteral("workspace_dir"));
    return custom.isEmpty() ? dataDir() + QStringLiteral("/projects") : custom;
}
