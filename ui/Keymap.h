// ShiftDex - unpack, edit and repack APK and JAR files
// Copyright (C) 2026 palaziks
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QKeySequence>
#include <QList>
#include <QString>

class QAction;

// Keyboard shortcuts the user can change in Preferences.
namespace Keymap {

struct Entry {
    const char *id;       // stable name, used as the key in the config file
    const char *label;
    const char *fallback; // default shortcut, portable text
    const char *section;
};

const QList<Entry> &entries();

QKeySequence defaultFor(const QString &id);
QKeySequence current(const QString &id);
// An empty sequence removes the shortcut.
void assign(const QString &id, const QKeySequence &sequence);
void resetAll();
// Id of another action that already uses this shortcut, or empty.
QString conflict(const QString &id, const QKeySequence &sequence);

// Ties a QAction to an entry; its shortcut follows later changes.
void bind(QAction *action, const QString &id);

} // namespace Keymap
