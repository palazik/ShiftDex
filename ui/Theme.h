// ShiftDex - unpack, edit and repack APK and JAR files
// Copyright (C) 2026 palaziks
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QColor>
#include <QString>

class QApplication;

namespace Theme {

enum class Kind { Light, Dark, Amoled };

// Colours of the code editor and the syntax highlighter.
struct EditorColors {
    QColor background, text, lineNumbers, lineNumbersBg, currentLine, match;
    QColor comment, string, keyword, number, type, directive, tag, attribute, label, reg;
};

// "system", "light", "dark" or "amoled" -> the theme to actually use.
Kind resolve(const QString &setting);
Kind current();

void apply(QApplication &app, const QString &setting);
const EditorColors &editorColors();

QColor accent();
QColor mutedText();
QColor warning();
QColor good();

} // namespace Theme
