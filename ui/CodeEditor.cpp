// ShiftDex - unpack, edit and repack APK and JAR files
// Copyright (C) 2026 palaziks
// SPDX-License-Identifier: GPL-3.0-or-later
#include "CodeEditor.h"

#include "Config.h"
#include "Theme.h"

#include <QFontDatabase>
#include <QPainter>
#include <QTextBlock>

namespace {

class Gutter : public QWidget {
public:
    explicit Gutter(CodeEditor *editor) : QWidget(editor), m_editor(editor) {}
    QSize sizeHint() const override { return {m_editor->gutterWidth(), 0}; }

protected:
    void paintEvent(QPaintEvent *event) override { m_editor->paintGutter(event); }

private:
    CodeEditor *m_editor;
};

} // namespace

CodeEditor::CodeEditor(QWidget *parent) : QPlainTextEdit(parent), m_gutter(new Gutter(this))
{
    setObjectName(QStringLiteral("borderless"));
    setFrameShape(QFrame::NoFrame);
    connect(this, &QPlainTextEdit::blockCountChanged, this, &CodeEditor::updateGutterWidth);
    connect(this, &QPlainTextEdit::updateRequest, this, &CodeEditor::updateGutter);
    connect(this, &QPlainTextEdit::cursorPositionChanged, this, &CodeEditor::refreshSelections);
    applySettings();
}

void CodeEditor::applySettings()
{
    const Config &config = Config::instance();
    m_lineNumbers = config.flag(QStringLiteral("editor_line_numbers"));
    m_highlightLine = config.flag(QStringLiteral("editor_highlight_line"));
    m_spaces = config.flag(QStringLiteral("editor_spaces"));
    m_tabWidth = qBound(1, config.number(QStringLiteral("editor_tab_width")), 16);

    // ask fontconfig for its monospace alias; the "system fixed font" is not
    // reliably fixed-width on every desktop
    QFont font(QStringLiteral("monospace"));
    font.setStyleHint(QFont::Monospace);
    font.setFixedPitch(true);
    font.setPointSize(qBound(6, config.number(QStringLiteral("editor_font_size")), 40));
    setFont(font);
    setTabStopDistance(QFontMetricsF(font).horizontalAdvance(QLatin1Char(' ')) * m_tabWidth);
    setLineWrapMode(config.flag(QStringLiteral("editor_wrap")) ? QPlainTextEdit::WidgetWidth
                                                                : QPlainTextEdit::NoWrap);

    const Theme::EditorColors &colors = Theme::editorColors();
    QPalette pal = palette();
    pal.setColor(QPalette::Base, colors.background);
    pal.setColor(QPalette::Text, colors.text);
    setPalette(pal);

    m_gutter->setVisible(m_lineNumbers);
    updateGutterWidth();
    refreshSelections();
    m_gutter->update();
}

int CodeEditor::gutterWidth() const
{
    if (!m_lineNumbers)
        return 0;
    int digits = 1;
    for (int max = qMax(1, blockCount()); max >= 10; max /= 10)
        digits++;
    return 18 + fontMetrics().horizontalAdvance(QLatin1Char('9')) * qMax(3, digits);
}

void CodeEditor::updateGutterWidth()
{
    setViewportMargins(gutterWidth(), 0, 0, 0);
}

void CodeEditor::updateGutter(const QRect &rect, int dy)
{
    if (dy)
        m_gutter->scroll(0, dy);
    else
        m_gutter->update(0, rect.y(), m_gutter->width(), rect.height());
    if (rect.contains(viewport()->rect()))
        updateGutterWidth();
}

void CodeEditor::resizeEvent(QResizeEvent *event)
{
    QPlainTextEdit::resizeEvent(event);
    // as tall as the text area only, so it stays clear of the horizontal scroll bar
    const QRect area = contentsRect();
    m_gutter->setGeometry(QRect(area.left(), area.top(), gutterWidth(), viewport()->height()));
}

void CodeEditor::paintGutter(QPaintEvent *event)
{
    const Theme::EditorColors &colors = Theme::editorColors();
    QPainter painter(m_gutter);
    painter.fillRect(event->rect(), colors.lineNumbersBg);
    painter.setFont(font());

    QTextBlock block = firstVisibleBlock();
    int number = block.blockNumber();
    int top = qRound(blockBoundingGeometry(block).translated(contentOffset()).top());
    int bottom = top + qRound(blockBoundingRect(block).height());
    const int currentLine = textCursor().blockNumber();

    while (block.isValid() && top <= event->rect().bottom()) {
        if (block.isVisible() && bottom >= event->rect().top()) {
            painter.setPen(number == currentLine ? colors.text : colors.lineNumbers);
            painter.drawText(0, top, m_gutter->width() - 9, fontMetrics().height(), Qt::AlignRight,
                             QString::number(number + 1));
        }
        block = block.next();
        top = bottom;
        bottom = top + qRound(blockBoundingRect(block).height());
        number++;
    }
}

void CodeEditor::setMatchSelections(const QList<QTextEdit::ExtraSelection> &selections)
{
    m_matches = selections;
    refreshSelections();
}

void CodeEditor::refreshSelections()
{
    QList<QTextEdit::ExtraSelection> selections;
    if (m_highlightLine && !isReadOnly()) {
        QTextEdit::ExtraSelection line;
        line.format.setBackground(Theme::editorColors().currentLine);
        line.format.setProperty(QTextFormat::FullWidthSelection, true);
        line.cursor = textCursor();
        line.cursor.clearSelection();
        selections.append(line);
    }
    selections += m_matches;
    setExtraSelections(selections);
    m_gutter->update();
}

void CodeEditor::goToLine(int line, int column)
{
    const QTextBlock block = document()->findBlockByNumber(qMax(0, line - 1));
    if (!block.isValid())
        return;
    QTextCursor cursor(block);
    cursor.movePosition(QTextCursor::Right, QTextCursor::MoveAnchor, qBound(0, column - 1, block.length() - 1));
    setTextCursor(cursor);
    centerCursor();
}

void CodeEditor::keyPressEvent(QKeyEvent *event)
{
    if (isReadOnly()) {
        QPlainTextEdit::keyPressEvent(event);
        return;
    }
    QTextCursor cursor = textCursor();
    const bool plain = !(event->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier));

    if ((event->key() == Qt::Key_Tab || event->key() == Qt::Key_Backtab) && plain) {
        const bool unindent = event->key() == Qt::Key_Backtab;
        const QString unit = m_spaces ? QString(m_tabWidth, QLatin1Char(' ')) : QStringLiteral("\t");
        if (cursor.hasSelection() || unindent) {
            // indent or unindent every selected line
            const int start = cursor.selectionStart();
            const int end = cursor.selectionEnd();
            QTextBlock block = document()->findBlock(start);
            const QTextBlock last = document()->findBlock(qMax(start, end - (end > start ? 1 : 0)));
            cursor.beginEditBlock();
            while (block.isValid()) {
                QTextCursor line(block);
                if (unindent) {
                    const QString text = block.text();
                    int remove = 0;
                    if (text.startsWith(QLatin1Char('\t')))
                        remove = 1;
                    else
                        while (remove < m_tabWidth && remove < text.length() && text.at(remove) == QLatin1Char(' '))
                            remove++;
                    line.movePosition(QTextCursor::Right, QTextCursor::KeepAnchor, remove);
                    line.removeSelectedText();
                } else {
                    line.insertText(unit);
                }
                if (block == last)
                    break;
                block = block.next();
            }
            cursor.endEditBlock();
            return;
        }
        if (m_spaces) {
            const int column = cursor.positionInBlock();
            cursor.insertText(QString(m_tabWidth - column % m_tabWidth, QLatin1Char(' ')));
            return;
        }
    }

    if ((event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) && plain
        && !(event->modifiers() & Qt::ShiftModifier)) {
        // carry the current line's indentation over to the new line
        const QString text = cursor.block().text();
        int indent = 0;
        while (indent < text.length() && indent < cursor.positionInBlock()
               && (text.at(indent) == QLatin1Char(' ') || text.at(indent) == QLatin1Char('\t')))
            indent++;
        cursor.insertText(QLatin1Char('\n') + text.left(indent));
        ensureCursorVisible();
        return;
    }
    QPlainTextEdit::keyPressEvent(event);
}
