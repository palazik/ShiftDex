// ShiftDex - unpack, edit and repack APK and JAR files
// Copyright (C) 2026 palaziks
// SPDX-License-Identifier: GPL-3.0-or-later
#include "Highlighter.h"

#include "Theme.h"

namespace {

QTextCharFormat colored(const QColor &color, bool bold = false, bool italic = false)
{
    QTextCharFormat format;
    format.setForeground(color);
    if (bold)
        format.setFontWeight(QFont::DemiBold);
    format.setFontItalic(italic);
    return format;
}

// block states
enum { Normal = -1, InComment = 1, InTag = 2, InDoubleQuote = 3, InSingleQuote = 4 };

} // namespace

Highlighter::Highlighter(QTextDocument *document, Language language)
    : QSyntaxHighlighter(document), m_language(language)
{
    buildRules();
}

Highlighter::Language Highlighter::languageFor(const QString &fileName)
{
    const QString name = fileName.toLower();
    const QString ext = name.section(QLatin1Char('.'), -1);
    if (ext == QLatin1String("smali"))
        return Language::Smali;
    if (ext == QLatin1String("xml") || ext == QLatin1String("html") || ext == QLatin1String("htm")
        || ext == QLatin1String("svg") || ext == QLatin1String("pom"))
        return Language::Xml;
    if (ext == QLatin1String("json"))
        return Language::Json;
    if (ext == QLatin1String("java") || ext == QLatin1String("kt") || ext == QLatin1String("class")
        || ext == QLatin1String("gradle") || ext == QLatin1String("js"))
        return Language::Java;
    if (ext == QLatin1String("properties") || ext == QLatin1String("mf") || ext == QLatin1String("cfg")
        || ext == QLatin1String("ini") || ext == QLatin1String("textproto") || ext == QLatin1String("yml")
        || ext == QLatin1String("yaml") || ext == QLatin1String("toml") || ext == QLatin1String("sf"))
        return Language::Properties;
    return Language::None;
}

QString Highlighter::languageName(Language language)
{
    switch (language) {
    case Language::Smali: return QStringLiteral("Smali");
    case Language::Xml: return QStringLiteral("XML");
    case Language::Json: return QStringLiteral("JSON");
    case Language::Java: return QStringLiteral("Java");
    case Language::Properties: return QStringLiteral("Properties");
    case Language::None: break;
    }
    return QStringLiteral("Plain text");
}

void Highlighter::reloadColors()
{
    buildRules();
    rehighlight();
}

void Highlighter::buildRules()
{
    const Theme::EditorColors &c = Theme::editorColors();
    m_rules.clear();
    m_comment = colored(c.comment, false, true);
    m_string = colored(c.string);
    m_tag = colored(c.tag);
    m_attribute = colored(c.attribute);

    auto add = [this](const QString &pattern, const QTextCharFormat &format, int group = 0) {
        m_rules.append({QRegularExpression(pattern), format, group});
    };

    switch (m_language) {
    case Language::Smali:
        // order matters: later rules paint over earlier ones
        add(QStringLiteral("(?<![\\w$/;])[vp]\\d+\\b"), colored(c.reg));
        add(QStringLiteral("(?<![\\w$.])-?(?:0x[0-9a-fA-F]+|\\d+(?:\\.\\d+)?)[LlSsTtFfDd]?\\b"), colored(c.number));
        add(QStringLiteral("^\\s*((?:[a-z]+-)*[a-z]+(?:/[a-z0-9]+)?)(?=\\s|$)"), colored(c.keyword), 1);
        add(QStringLiteral("\\b(?:public|private|protected|static|final|synchronized|bridge|varargs|native|"
                           "abstract|strictfp|synthetic|constructor|declared-synchronized|interface|enum|"
                           "annotation|volatile|transient)\\b"),
            colored(c.keyword));
        add(QStringLiteral("\\[*L[\\w$/\\-]+;"), colored(c.type));
        add(QStringLiteral("->([\\w$<>\\-]+)"), colored(c.tag), 1);
        add(QStringLiteral("(?<![\\w$:]):[A-Za-z_][\\w$]*"), colored(c.label));
        add(QStringLiteral("^\\s*(\\.(?:end\\s+)?[a-z][a-z\\-]*)"), colored(c.directive, true), 1);
        add(QStringLiteral("\"(?:\\\\.|[^\"\\\\])*\""), m_string);
        add(QStringLiteral("#(?=(?:[^\"]*\"[^\"]*\")*[^\"]*$).*$"), m_comment);
        break;
    case Language::Json:
        add(QStringLiteral("-?\\b\\d+(?:\\.\\d+)?(?:[eE][+-]?\\d+)?\\b"), colored(c.number));
        add(QStringLiteral("\\b(?:true|false|null)\\b"), colored(c.keyword));
        add(QStringLiteral("\"(?:\\\\.|[^\"\\\\])*\""), m_string);
        add(QStringLiteral("(\"(?:\\\\.|[^\"\\\\])*\")\\s*:"), colored(c.attribute), 1);
        break;
    case Language::Java:
        add(QStringLiteral("\\b[A-Z][A-Za-z0-9_]*\\b"), colored(c.type));
        add(QStringLiteral("\\b(?:0x[0-9a-fA-F_]+|\\d[\\d_]*(?:\\.\\d+)?)[LlFfDd]?\\b"), colored(c.number));
        add(QStringLiteral("\\b(?:abstract|assert|boolean|break|byte|case|catch|char|class|const|continue|"
                           "default|do|double|else|enum|extends|final|finally|float|for|goto|if|implements|"
                           "import|instanceof|int|interface|long|native|new|package|private|protected|public|"
                           "return|short|static|strictfp|super|switch|synchronized|this|throw|throws|transient|"
                           "try|void|volatile|while|var|record|sealed|permits|true|false|null|fun|val|when|"
                           "object|override)\\b"),
            colored(c.keyword));
        add(QStringLiteral("@[A-Za-z_][\\w.]*"), colored(c.directive));
        add(QStringLiteral("\"(?:\\\\.|[^\"\\\\])*\""), m_string);
        add(QStringLiteral("'(?:\\\\.|[^'\\\\])+'"), m_string);
        add(QStringLiteral("//.*$"), m_comment);
        break;
    case Language::Properties:
        add(QStringLiteral("^\\s*([^#!=:\\s][^=:]*?)\\s*[=:]"), colored(c.attribute), 1);
        add(QStringLiteral("^\\s*\\[[^\\]]+\\]"), colored(c.directive, true));
        add(QStringLiteral("^\\s*[#!].*$"), m_comment);
        break;
    case Language::Xml:
    case Language::None:
        break;
    }
}

void Highlighter::highlightBlock(const QString &text)
{
    if (m_language == Language::None)
        return;
    if (m_language == Language::Xml) {
        highlightXml(text);
        return;
    }
    for (const Rule &rule : std::as_const(m_rules)) {
        auto it = rule.pattern.globalMatch(text);
        while (it.hasNext()) {
            const QRegularExpressionMatch match = it.next();
            setFormat(match.capturedStart(rule.group), match.capturedLength(rule.group), rule.format);
        }
    }
    if (m_language == Language::Java)
        highlightBlockComments(text);
}

void Highlighter::highlightBlockComments(const QString &text)
{
    setCurrentBlockState(Normal);
    int start = 0;
    if (previousBlockState() != InComment) {
        start = text.indexOf(QLatin1String("/*"));
        // ignore a comment opener inside a string or after a line comment
        while (start >= 0 && format(start) == m_string)
            start = text.indexOf(QLatin1String("/*"), start + 2);
        if (start >= 0 && format(start) == m_comment)
            start = -1;
    }
    while (start >= 0) {
        const int end = text.indexOf(QLatin1String("*/"), start == 0 && previousBlockState() == InComment ? 0 : start + 2);
        int length;
        if (end < 0) {
            setCurrentBlockState(InComment);
            length = text.length() - start;
        } else {
            length = end - start + 2;
        }
        setFormat(start, length, m_comment);
        start = end < 0 ? -1 : text.indexOf(QLatin1String("/*"), start + length);
    }
}

void Highlighter::highlightXml(const QString &text)
{
    // A small state machine: XML tags and comments often span lines in
    // decoded manifests, which line-based rules cannot follow.
    int state = previousBlockState();
    int i = 0;
    const int n = text.length();
    while (i < n) {
        if (state == InComment) {
            const int end = text.indexOf(QLatin1String("-->"), i);
            const int stop = end < 0 ? n : end + 3;
            setFormat(i, stop - i, m_comment);
            i = stop;
            state = end < 0 ? InComment : Normal;
        } else if (state == InDoubleQuote || state == InSingleQuote) {
            const QChar quote = state == InDoubleQuote ? QLatin1Char('"') : QLatin1Char('\'');
            const int end = text.indexOf(quote, i);
            const int stop = end < 0 ? n : end + 1;
            setFormat(i, stop - i, m_string);
            i = stop;
            if (end >= 0)
                state = InTag;
        } else if (state == InTag) {
            const QChar ch = text.at(i);
            if (ch == QLatin1Char('>')) {
                setFormat(i, 1, m_tag);
                state = Normal;
                i++;
            } else if (ch == QLatin1Char('"') || ch == QLatin1Char('\'')) {
                setFormat(i, 1, m_string);
                state = ch == QLatin1Char('"') ? InDoubleQuote : InSingleQuote;
                i++;
            } else if (ch.isLetter() || ch == QLatin1Char('_')) {
                int end = i;
                while (end < n && (text.at(end).isLetterOrNumber() || QStringLiteral("_:.-").contains(text.at(end))))
                    end++;
                setFormat(i, end - i, m_attribute);
                i = end;
            } else {
                if (ch == QLatin1Char('/') || ch == QLatin1Char('?'))
                    setFormat(i, 1, m_tag);
                i++;
            }
        } else {
            const int open = text.indexOf(QLatin1Char('<'), i);
            if (open < 0)
                break;
            if (text.mid(open, 4) == QLatin1String("<!--")) {
                state = InComment;
                i = open;
                continue;
            }
            int end = open + 1;
            while (end < n && (text.at(end).isLetterOrNumber() || QStringLiteral("_:.-/?!").contains(text.at(end))))
                end++;
            setFormat(open, end - open, m_tag);
            i = end;
            state = InTag;
        }
    }
    setCurrentBlockState(state);
}
