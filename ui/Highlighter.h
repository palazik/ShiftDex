// ShiftDex - unpack, edit and repack APK and JAR files
// Copyright (C) 2026 palaziks
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QRegularExpression>
#include <QSyntaxHighlighter>
#include <QTextCharFormat>
#include <QVector>

// Syntax colouring for the file types found in unpacked APKs and JARs.
class Highlighter : public QSyntaxHighlighter {
    Q_OBJECT
public:
    enum class Language { None, Smali, Xml, Json, Java, Properties };

    Highlighter(QTextDocument *document, Language language);

    static Language languageFor(const QString &fileName);
    static QString languageName(Language language);

    // Call after the theme changed.
    void reloadColors();

protected:
    void highlightBlock(const QString &text) override;

private:
    struct Rule {
        QRegularExpression pattern;
        QTextCharFormat format;
        int group = 0;
    };

    void buildRules();
    void highlightXml(const QString &text);
    void highlightBlockComments(const QString &text);

    Language m_language;
    QVector<Rule> m_rules;
    QTextCharFormat m_comment, m_string, m_tag, m_attribute;
};
