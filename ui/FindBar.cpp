// ShiftDex - unpack, edit and repack APK and JAR files
// Copyright (C) 2026 palaziks
// SPDX-License-Identifier: GPL-3.0-or-later
#include "FindBar.h"

#include "CodeEditor.h"
#include "Theme.h"
#include "Util.h"

#include <QCheckBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QToolButton>

FindBar::FindBar(CodeEditor *editor, QWidget *parent) : QFrame(parent), m_editor(editor)
{
    setObjectName(QStringLiteral("findBar"));

    m_find = new QLineEdit;
    m_find->setPlaceholderText(tr("Find"));
    m_find->setClearButtonEnabled(true);
    m_replace = new QLineEdit;
    m_replace->setPlaceholderText(tr("Replace with"));
    m_case = new QCheckBox(tr("Match case"));
    m_regex = new QCheckBox(tr("Regex"));
    m_count = new QLabel;
    m_count->setObjectName(QStringLiteral("muted"));
    m_count->setMinimumWidth(90);

    auto *previous = new QPushButton(tr("Previous"));
    auto *next = new QPushButton(tr("Next"));
    auto *replaceOne = new QPushButton(tr("Replace"));
    auto *replaceAll = new QPushButton(tr("Replace All"));
    auto *closeButton = new QToolButton;
    closeButton->setText(QStringLiteral("✕"));
    closeButton->setAutoRaise(true);
    closeButton->setToolTip(tr("Close (Esc)"));
    for (QPushButton *button : {previous, next, replaceOne, replaceAll})
        button->setAutoDefault(false);

    auto *layout = new QGridLayout(this);
    layout->setContentsMargins(8, 6, 8, 6);
    layout->setHorizontalSpacing(6);
    layout->setVerticalSpacing(5);
    layout->addWidget(m_find, 0, 0);
    layout->addWidget(previous, 0, 1);
    layout->addWidget(next, 0, 2);
    layout->addWidget(m_case, 0, 3);
    layout->addWidget(m_regex, 0, 4);
    layout->addWidget(m_count, 0, 5);
    layout->addWidget(closeButton, 0, 6);
    layout->setColumnStretch(0, 1);

    // the replace row is its own widget so it can be hidden as a unit
    m_replaceRow = new QWidget;
    auto *row = new QHBoxLayout(m_replaceRow);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(6);
    row->addWidget(m_replace, 1);
    row->addWidget(replaceOne);
    row->addWidget(replaceAll);
    layout->addWidget(m_replaceRow, 1, 0, 1, 3);

    connect(m_find, &QLineEdit::textChanged, this, [this] {
        // search as you type, starting from where the selection began
        QTextCursor cursor = m_editor->textCursor();
        cursor.setPosition(cursor.selectionStart());
        m_editor->setTextCursor(cursor);
        find(false);
        refresh();
    });
    connect(m_find, &QLineEdit::returnPressed, this, [this] { find(false); });
    connect(m_replace, &QLineEdit::returnPressed, this, &FindBar::replaceOne);
    connect(next, &QPushButton::clicked, this, [this] { find(false); });
    connect(previous, &QPushButton::clicked, this, [this] { find(true); });
    connect(replaceOne, &QPushButton::clicked, this, &FindBar::replaceOne);
    connect(replaceAll, &QPushButton::clicked, this, &FindBar::replaceAll);
    connect(closeButton, &QToolButton::clicked, this, &FindBar::close);
    connect(m_case, &QCheckBox::toggled, this, &FindBar::refresh);
    connect(m_regex, &QCheckBox::toggled, this, &FindBar::refresh);
    connect(m_editor->document(), &QTextDocument::contentsChanged, this, [this] {
        if (isVisible())
            refresh();
    });
    hide();
}

void FindBar::open(bool withReplace)
{
    const bool canReplace = withReplace && !m_editor->isReadOnly();
    m_replaceRow->setVisible(canReplace);
    show();
    const QString selected = m_editor->textCursor().selectedText();
    if (!selected.isEmpty() && !selected.contains(QChar::ParagraphSeparator))
        m_find->setText(selected);
    m_find->setFocus();
    m_find->selectAll();
    refresh();
}

void FindBar::close()
{
    hide();
    m_editor->setMatchSelections({});
    m_editor->setFocus();
}

void FindBar::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape) {
        close();
        return;
    }
    QFrame::keyPressEvent(event);
}

QTextDocument::FindFlags FindBar::flags(bool backward) const
{
    QTextDocument::FindFlags result;
    if (backward)
        result |= QTextDocument::FindBackward;
    if (m_case->isChecked())
        result |= QTextDocument::FindCaseSensitively;
    return result;
}

QTextCursor FindBar::search(const QTextCursor &from, bool backward) const
{
    const QString text = m_find->text();
    if (text.isEmpty())
        return {};
    if (m_regex->isChecked()) {
        QRegularExpression expression(text, m_case->isChecked() ? QRegularExpression::NoPatternOption
                                                                 : QRegularExpression::CaseInsensitiveOption);
        if (!expression.isValid())
            return {};
        return m_editor->document()->find(expression, from, flags(backward));
    }
    return m_editor->document()->find(text, from, flags(backward));
}

bool FindBar::find(bool backward, bool wrap)
{
    QTextCursor found = search(m_editor->textCursor(), backward);
    if (found.isNull() && wrap) {
        QTextCursor edge(m_editor->document());
        edge.movePosition(backward ? QTextCursor::End : QTextCursor::Start);
        found = search(edge, backward);
    }
    if (found.isNull())
        return false;
    m_editor->setTextCursor(found);
    m_editor->centerCursor();
    return true;
}

QString FindBar::replacementFor(const QTextCursor &match) const
{
    if (!m_regex->isChecked())
        return m_replace->text();
    // let \1 style references in the replacement work
    QRegularExpression expression(m_find->text(), m_case->isChecked() ? QRegularExpression::NoPatternOption
                                                                       : QRegularExpression::CaseInsensitiveOption);
    QString selected = match.selectedText();
    selected.replace(QChar::ParagraphSeparator, QLatin1Char('\n'));
    const QRegularExpressionMatch exact = expression.match(selected, 0, QRegularExpression::NormalMatch,
                                                           QRegularExpression::AnchorAtOffsetMatchOption);
    return exact.hasMatch() ? selected.replace(expression, m_replace->text()) : m_replace->text();
}

void FindBar::replaceOne()
{
    if (m_editor->isReadOnly() || m_find->text().isEmpty())
        return;
    QTextCursor cursor = m_editor->textCursor();
    // only replace when the selection is a real match
    QTextCursor start = cursor;
    start.setPosition(cursor.selectionStart());
    const QTextCursor match = search(start, false);
    if (!match.isNull() && match.selectionStart() == cursor.selectionStart()
        && match.selectionEnd() == cursor.selectionEnd() && cursor.hasSelection()) {
        cursor.insertText(replacementFor(match));
        m_editor->setTextCursor(cursor);
    }
    find(false);
}

void FindBar::replaceAll()
{
    if (m_editor->isReadOnly() || m_find->text().isEmpty())
        return;
    QTextCursor cursor(m_editor->document());
    cursor.beginEditBlock();
    int count = 0;
    QTextCursor match = search(cursor, false);
    while (!match.isNull()) {
        const bool empty = !match.hasSelection();
        match.insertText(replacementFor(match));
        count++;
        if (empty && !match.movePosition(QTextCursor::NextCharacter))
            break;
        match = search(match, false);
    }
    cursor.endEditBlock();
    m_count->setText(tr("%1 replaced").arg(count));
}

void FindBar::refresh()
{
    QList<QTextEdit::ExtraSelection> selections;
    int count = 0;
    bool capped = false;
    if (!m_find->text().isEmpty()) {
        QTextCursor match = search(QTextCursor(m_editor->document()), false);
        while (!match.isNull()) {
            if (!match.hasSelection()) {
                if (!match.movePosition(QTextCursor::NextCharacter))
                    break;
            } else {
                QTextEdit::ExtraSelection selection;
                selection.cursor = match;
                selection.format.setBackground(Theme::editorColors().match);
                selections.append(selection);
                if (++count >= 2000) {
                    capped = true;
                    break;
                }
            }
            match = search(match, false);
        }
    }
    m_editor->setMatchSelections(selections);
    if (m_find->text().isEmpty())
        m_count->clear();
    else if (count == 0)
        m_count->setText(tr("No matches"));
    else
        m_count->setText(capped ? tr("2000+ matches") : Util::count(count, tr("match"), tr("matches")));
}
