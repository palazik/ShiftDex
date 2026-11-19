// ShiftDex - unpack, edit and repack APK and JAR files
// Copyright (C) 2026 palaziks
// SPDX-License-Identifier: GPL-3.0-or-later
#include "Keymap.h"

#include "Config.h"

#include <QAction>
#include <QCoreApplication>
#include <QJsonObject>

namespace Keymap {

namespace {
const QString kKey = QStringLiteral("keymap");
}

const QList<Entry> &entries()
{
    static const QList<Entry> list{
        {"open-file", QT_TRANSLATE_NOOP("Keymap", "Open an APK or JAR"), "Ctrl+O", "General"},
        {"go-back", QT_TRANSLATE_NOOP("Keymap", "Go back to the project list"), "Alt+Left", "General"},
        {"toggle-sidebar", QT_TRANSLATE_NOOP("Keymap", "Show or hide the side panel"), "F9", "General"},
        {"preferences", QT_TRANSLATE_NOOP("Keymap", "Preferences"), "Ctrl+,", "General"},
        {"quit", QT_TRANSLATE_NOOP("Keymap", "Quit"), "Ctrl+Q", "General"},
        {"save", QT_TRANSLATE_NOOP("Keymap", "Save the current file"), "Ctrl+S", "Editing"},
        {"save-all", QT_TRANSLATE_NOOP("Keymap", "Save all files"), "Ctrl+Shift+S", "Editing"},
        {"close-tab", QT_TRANSLATE_NOOP("Keymap", "Close the current tab"), "Ctrl+W", "Editing"},
        {"find", QT_TRANSLATE_NOOP("Keymap", "Find in file"), "Ctrl+F", "Editing"},
        {"replace", QT_TRANSLATE_NOOP("Keymap", "Find and replace"), "Ctrl+H", "Editing"},
        {"goto-line", QT_TRANSLATE_NOOP("Keymap", "Go to line"), "Ctrl+G", "Editing"},
        {"switch-code", QT_TRANSLATE_NOOP("Keymap", "Switch between smali and Java"), "Ctrl+J", "Project"},
        {"find-in-project", QT_TRANSLATE_NOOP("Keymap", "Search in all files"), "Ctrl+Shift+F", "Project"},
        {"quick-open", QT_TRANSLATE_NOOP("Keymap", "Find a file by name"), "Ctrl+P", "Project"},
        {"build", QT_TRANSLATE_NOOP("Keymap", "Build the APK or JAR"), "Ctrl+B", "Project"},
        {"snapshot", QT_TRANSLATE_NOOP("Keymap", "Create a snapshot"), "Ctrl+Shift+B", "Project"},
        {"backup-file", QT_TRANSLATE_NOOP("Keymap", "Back up the current file"), "Ctrl+Alt+B", "Project"},
    };
    return list;
}

QKeySequence defaultFor(const QString &id)
{
    for (const Entry &entry : entries()) {
        if (id == QLatin1String(entry.id))
            return QKeySequence(QString::fromLatin1(entry.fallback), QKeySequence::PortableText);
    }
    return {};
}

QKeySequence current(const QString &id)
{
    const QJsonObject custom = Config::instance().object(kKey);
    if (custom.contains(id) && custom.value(id).isString())
        return QKeySequence(custom.value(id).toString(), QKeySequence::PortableText);
    return defaultFor(id);
}

void assign(const QString &id, const QKeySequence &sequence)
{
    QJsonObject custom = Config::instance().object(kKey);
    if (sequence == defaultFor(id))
        custom.remove(id);
    else
        custom.insert(id, sequence.toString(QKeySequence::PortableText));
    Config::instance().set(kKey, custom);
}

void resetAll()
{
    Config::instance().set(kKey, QJsonObject());
}

QString conflict(const QString &id, const QKeySequence &sequence)
{
    if (sequence.isEmpty())
        return {};
    for (const Entry &entry : entries()) {
        const QString other = QString::fromLatin1(entry.id);
        if (other != id && current(other) == sequence)
            return other;
    }
    return {};
}

void bind(QAction *action, const QString &id)
{
    action->setShortcut(current(id));
    action->setShortcutContext(Qt::WindowShortcut);
    QObject::connect(&Config::instance(), &Config::changed, action, [action, id](const QString &key) {
        if (key == kKey)
            action->setShortcut(current(id));
    });
}

} // namespace Keymap
