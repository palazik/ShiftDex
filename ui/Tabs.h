// ShiftDex - unpack, edit and repack APK and JAR files
// Copyright (C) 2026 palaziks
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QWidget>

class CodeEditor;
class FindBar;
class Highlighter;
class QLabel;
class QScrollArea;

// One open file. Subclasses decide how the file is shown.
class DocTab : public QWidget {
    Q_OBJECT
public:
    explicit DocTab(const QString &path, QWidget *parent = nullptr);

    QString path() const { return m_path; }
    void setPath(const QString &path) { m_path = path; }

    virtual bool isModified() const { return false; }
    virtual bool save(QString *error);
    // Reads the file again, dropping unsaved changes. False if it is gone.
    virtual bool reload() = 0;
    virtual CodeEditor *editor() const { return nullptr; }
    virtual void applySettings() {}
    virtual void showFind(bool withReplace);
    // Short description for the status bar, e.g. "Smali" or "PNG image".
    virtual QString description() const = 0;

signals:
    void modifiedChanged(bool modified);
    void cursorMoved();

private:
    QString m_path;
};

class TextTab : public DocTab {
    Q_OBJECT
public:
    // Editable file on disk.
    explicit TextTab(const QString &path, QWidget *parent = nullptr);
    // Read-only generated text (disassembly, hex dump) with a notice above it.
    TextTab(const QString &path, const QString &text, const QString &notice, const QString &kind,
            QWidget *parent = nullptr);
    // Read-only view of a file on disk (decompiled Java). reload() reads it again.
    static TextTab *readOnlyFile(const QString &path, const QString &notice, const QString &kind,
                                 QWidget *parent = nullptr);

    bool isModified() const override;
    bool save(QString *error) override;
    bool reload() override;
    CodeEditor *editor() const override { return m_editor; }
    void applySettings() override;
    void showFind(bool withReplace) override;
    QString description() const override;

private:
    void build(const QString &notice);

    CodeEditor *m_editor = nullptr;
    FindBar *m_findBar = nullptr;
    Highlighter *m_highlighter = nullptr;
    bool m_generated = false;
    bool m_fromFile = false;
    bool m_crlf = false;
    QString m_kind;
};

class ImageTab : public DocTab {
    Q_OBJECT
public:
    explicit ImageTab(const QString &path, QWidget *parent = nullptr);

    bool reload() override;
    QString description() const override { return m_description; }

signals:
    void replaceRequested(const QString &path);

protected:
    void resizeEvent(QResizeEvent *event) override;

private:
    void rescale();

    QLabel *m_image;
    QLabel *m_info;
    QScrollArea *m_scroll;
    QPixmap *m_pixmap = nullptr;
    QString m_description;

public:
    ~ImageTab() override;
};

namespace Tabs {

// Picks the right tab type for a file. Never returns null.
DocTab *open(const QString &path, QWidget *parent);
bool isImage(const QString &path);
QString hexDump(const QByteArray &data);

} // namespace Tabs
