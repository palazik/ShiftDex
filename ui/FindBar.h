// ShiftDex - unpack, edit and repack APK and JAR files
// Copyright (C) 2026 palaziks
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QFrame>
#include <QTextDocument>

class CodeEditor;
class QCheckBox;
class QLabel;
class QLineEdit;
class QPushButton;

// Find and replace strip shown under an editor.
class FindBar : public QFrame {
    Q_OBJECT
public:
    explicit FindBar(CodeEditor *editor, QWidget *parent = nullptr);

    void open(bool withReplace);
    void close();

protected:
    void keyPressEvent(QKeyEvent *event) override;

private:
    bool find(bool backward, bool wrap = true);
    void replaceOne();
    void replaceAll();
    void refresh();
    QTextCursor search(const QTextCursor &from, bool backward) const;
    QTextDocument::FindFlags flags(bool backward) const;
    QString replacementFor(const QTextCursor &match) const;

    CodeEditor *m_editor;
    QLineEdit *m_find;
    QLineEdit *m_replace;
    QCheckBox *m_case;
    QCheckBox *m_regex;
    QLabel *m_count;
    QWidget *m_replaceRow;
};
