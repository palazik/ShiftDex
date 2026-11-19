// ShiftDex - unpack, edit and repack APK and JAR files
// Copyright (C) 2026 palaziks
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QPlainTextEdit>

// Plain text editor with a line number gutter, current line highlight and
// the indentation behaviour expected from a code editor.
class CodeEditor : public QPlainTextEdit {
    Q_OBJECT
public:
    explicit CodeEditor(QWidget *parent = nullptr);

    // Re-reads font, wrapping, tab and colour settings.
    void applySettings();
    void goToLine(int line, int column = 1);
    // Extra selections marking search matches; cleared with an empty list.
    void setMatchSelections(const QList<QTextEdit::ExtraSelection> &selections);

    int gutterWidth() const;
    void paintGutter(QPaintEvent *event);

protected:
    void resizeEvent(QResizeEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

private:
    void updateGutterWidth();
    void updateGutter(const QRect &rect, int dy);
    void refreshSelections();

    QWidget *m_gutter;
    QList<QTextEdit::ExtraSelection> m_matches;
    bool m_lineNumbers = true;
    bool m_highlightLine = true;
    bool m_spaces = true;
    int m_tabWidth = 4;
};
