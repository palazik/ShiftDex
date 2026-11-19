// ShiftDex - unpack, edit and repack APK and JAR files
// Copyright (C) 2026 palaziks
// SPDX-License-Identifier: GPL-3.0-or-later
#include "Tabs.h"

#include "CodeEditor.h"
#include "Engine.h"
#include "FindBar.h"
#include "Highlighter.h"

#include <QFile>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QImageReader>
#include <QJsonObject>
#include <QLabel>
#include <QLocale>
#include <QPushButton>
#include <QSaveFile>
#include <QScrollArea>
#include <QScrollBar>
#include <QVBoxLayout>

extern "C" {
#include "sdx_search.h"
}

namespace {
const qint64 kMaxTextSize = 24 * 1024 * 1024;
const qint64 kHexPreview = 256 * 1024;
}

// ------------------------------------------------------------------ DocTab

DocTab::DocTab(const QString &path, QWidget *parent) : QWidget(parent), m_path(path) {}

bool DocTab::save(QString *) { return true; }
void DocTab::showFind(bool) {}

// ----------------------------------------------------------------- TextTab

TextTab::TextTab(const QString &path, QWidget *parent) : DocTab(path, parent)
{
    build(QString());
    m_highlighter = new Highlighter(m_editor->document(), Highlighter::languageFor(path));
    m_kind = Highlighter::languageName(Highlighter::languageFor(path));
    reload();
}

TextTab::TextTab(const QString &path, const QString &text, const QString &notice, const QString &kind,
                 QWidget *parent)
    : DocTab(path, parent), m_generated(true), m_kind(kind)
{
    build(notice);
    m_editor->setReadOnly(true);
    if (kind == QLatin1String("Bytecode") || kind == QLatin1String("Java"))
        m_highlighter = new Highlighter(m_editor->document(), Highlighter::Language::Java);
    m_editor->setPlainText(text);
    m_editor->applySettings();
}

TextTab *TextTab::readOnlyFile(const QString &path, const QString &notice, const QString &kind, QWidget *parent)
{
    auto *tab = new TextTab(path, QString(), notice, kind, parent);
    tab->m_fromFile = true;
    tab->reload();
    return tab;
}

void TextTab::build(const QString &notice)
{
    m_editor = new CodeEditor;
    m_findBar = new FindBar(m_editor);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    if (!notice.isEmpty()) {
        auto *bar = new QFrame;
        bar->setObjectName(QStringLiteral("bar"));
        auto *barLayout = new QHBoxLayout(bar);
        barLayout->setContentsMargins(10, 5, 10, 5);
        auto *label = new QLabel(notice);
        label->setObjectName(QStringLiteral("muted"));
        barLayout->addWidget(label);
        layout->addWidget(bar);
    }
    layout->addWidget(m_editor, 1);
    layout->addWidget(m_findBar);
    setFocusProxy(m_editor);

    connect(m_editor->document(), &QTextDocument::modificationChanged, this, &DocTab::modifiedChanged);
    connect(m_editor, &QPlainTextEdit::cursorPositionChanged, this, &DocTab::cursorMoved);
}

bool TextTab::isModified() const
{
    return !m_generated && m_editor->document()->isModified();
}

bool TextTab::reload()
{
    if (m_generated && !m_fromFile)
        return QFileInfo::exists(path());
    QFile file(path());
    if (!file.open(QIODevice::ReadOnly))
        return false;
    const QByteArray data = file.readAll();
    m_crlf = data.contains("\r\n");
    QString text = QString::fromUtf8(data);
    if (m_crlf)
        text.replace(QLatin1String("\r\n"), QLatin1String("\n"));
    const int line = m_editor->textCursor().blockNumber() + 1;
    m_editor->setPlainText(text);
    m_editor->document()->setModified(false);
    if (line > 1)
        m_editor->goToLine(line);
    return true;
}

bool TextTab::save(QString *error)
{
    if (m_generated || !m_editor->document()->isModified())
        return true;
    QString text = m_editor->toPlainText();
    if (m_crlf)
        text.replace(QLatin1Char('\n'), QLatin1String("\r\n"));
    QSaveFile file(path());
    // keep the file's own permissions; a temp file would lose them
    file.setDirectWriteFallback(true);
    if (!file.open(QIODevice::WriteOnly) || file.write(text.toUtf8()) < 0 || !file.commit()) {
        if (error)
            *error = file.errorString();
        return false;
    }
    m_editor->document()->setModified(false);
    return true;
}

void TextTab::applySettings()
{
    m_editor->applySettings();
    if (m_highlighter)
        m_highlighter->reloadColors();
}

void TextTab::showFind(bool withReplace)
{
    m_findBar->open(withReplace);
}

QString TextTab::description() const
{
    if (m_generated && !m_fromFile)
        return m_kind;
    const QTextCursor cursor = m_editor->textCursor();
    if (m_generated)
        return tr("%1 (read-only)   Ln %2, Col %3").arg(m_kind).arg(cursor.blockNumber() + 1).arg(cursor.positionInBlock() + 1);
    return tr("%1   Ln %2, Col %3%4")
        .arg(m_kind)
        .arg(cursor.blockNumber() + 1)
        .arg(cursor.positionInBlock() + 1)
        .arg(m_crlf ? QStringLiteral("   CRLF") : QString());
}

// ---------------------------------------------------------------- ImageTab

ImageTab::ImageTab(const QString &path, QWidget *parent) : DocTab(path, parent)
{
    m_image = new QLabel;
    m_image->setAlignment(Qt::AlignCenter);
    m_scroll = new QScrollArea;
    m_scroll->setWidget(m_image);
    m_scroll->setWidgetResizable(true);
    m_scroll->setFrameShape(QFrame::NoFrame);
    m_scroll->setAlignment(Qt::AlignCenter);

    m_info = new QLabel;
    m_info->setObjectName(QStringLiteral("muted"));
    auto *replace = new QPushButton(tr("Replace Image…"));
    connect(replace, &QPushButton::clicked, this, [this] { emit replaceRequested(this->path()); });

    auto *bar = new QFrame;
    bar->setObjectName(QStringLiteral("bar"));
    auto *barLayout = new QHBoxLayout(bar);
    barLayout->setContentsMargins(10, 5, 10, 5);
    barLayout->addWidget(m_info, 1);
    barLayout->addWidget(replace);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(bar);
    layout->addWidget(m_scroll, 1);
    reload();
}

ImageTab::~ImageTab()
{
    delete m_pixmap;
}

bool ImageTab::reload()
{
    const QFileInfo info(path());
    if (!info.exists())
        return false;
    QImageReader reader(path());
    reader.setDecideFormatFromContent(true);
    const QString format = QString::fromLatin1(reader.format()).toUpper();
    const QImage image = reader.read();
    delete m_pixmap;
    m_pixmap = nullptr;
    const QString size = QLocale().formattedDataSize(info.size());
    if (image.isNull()) {
        m_image->setText(tr("This image cannot be displayed (%1).").arg(reader.errorString()));
        m_description = tr("Image");
        m_info->setText(size);
        return true;
    }
    m_pixmap = new QPixmap(QPixmap::fromImage(image));
    m_description = tr("%1 image").arg(format.isEmpty() ? tr("Unknown") : format);
    m_info->setText(tr("%1 × %2 px   %3   %4").arg(image.width()).arg(image.height()).arg(size, format));
    rescale();
    return true;
}

void ImageTab::resizeEvent(QResizeEvent *event)
{
    DocTab::resizeEvent(event);
    rescale();
}

void ImageTab::rescale()
{
    if (!m_pixmap)
        return;
    const QSize room = m_scroll->viewport()->size() - QSize(32, 32);
    if (room.width() < 8 || room.height() < 8)
        return;
    // large images shrink to fit; launcher icons are enlarged so they can be judged
    double scale = qMin(double(room.width()) / m_pixmap->width(), double(room.height()) / m_pixmap->height());
    scale = qMin(scale, m_pixmap->width() <= 96 ? 4.0 : (m_pixmap->width() <= 256 ? 2.0 : 1.0));
    const QSize target = (QSizeF(m_pixmap->size()) * scale).toSize().expandedTo(QSize(1, 1));
    m_image->setPixmap(m_pixmap->scaled(target, Qt::KeepAspectRatio,
                                        scale > 1.0 ? Qt::FastTransformation : Qt::SmoothTransformation));
}

// -------------------------------------------------------------------- Tabs

namespace Tabs {

bool isImage(const QString &path)
{
    static const QStringList extensions{QStringLiteral("png"), QStringLiteral("jpg"), QStringLiteral("jpeg"),
                                        QStringLiteral("webp"), QStringLiteral("gif"), QStringLiteral("bmp"),
                                        QStringLiteral("ico")};
    return extensions.contains(QFileInfo(path).suffix().toLower());
}

QString hexDump(const QByteArray &data)
{
    static const char digits[] = "0123456789abcdef";
    QString out;
    out.reserve(data.size() * 5);
    for (int offset = 0; offset < data.size(); offset += 16) {
        QString hex, ascii;
        for (int i = 0; i < 16; i++) {
            if (offset + i < data.size()) {
                const uchar byte = uchar(data.at(offset + i));
                hex += QLatin1Char(digits[byte >> 4]);
                hex += QLatin1Char(digits[byte & 15]);
                ascii += (byte >= 0x20 && byte < 0x7f) ? QLatin1Char(char(byte)) : QLatin1Char('.');
            } else {
                hex += QLatin1String("  ");
            }
            hex += QLatin1Char(i == 7 ? ' ' : ' ');
            if (i == 7)
                hex += QLatin1Char(' ');
        }
        out += QStringLiteral("%1  %2 %3\n").arg(offset, 8, 16, QLatin1Char('0')).arg(hex, ascii);
    }
    return out;
}

DocTab *open(const QString &path, QWidget *parent)
{
    const QFileInfo info(path);
    if (isImage(path))
        return new ImageTab(path, parent);

    if (info.suffix().toLower() == QLatin1String("class")) {
        QJsonValue result;
        QString error;
        if (Engine::call({QStringLiteral("disasm"), path}, &result, &error, 60000)) {
            return new TextTab(path, result.toObject().value(QStringLiteral("text")).toString(),
                               QObject::tr("Disassembled bytecode (read-only). Class files cannot be edited as text; "
                                           "replace the .class file to change it."),
                               QStringLiteral("Bytecode"), parent);
        }
        // fall through to the hex view when no JDK is around
    }

    QFile file(path);
    QByteArray head;
    if (file.open(QIODevice::ReadOnly))
        head = file.read(kHexPreview);
    const bool binary = sdx_is_binary(reinterpret_cast<const unsigned char *>(head.constData()), size_t(head.size()));
    if (binary || info.size() > kMaxTextSize) {
        const QString notice = info.size() > head.size()
            ? QObject::tr("Binary file, %1. Showing the first %2 (read-only).")
                  .arg(QLocale().formattedDataSize(info.size()), QLocale().formattedDataSize(head.size()))
            : QObject::tr("Binary file, %1 (read-only).").arg(QLocale().formattedDataSize(info.size()));
        return new TextTab(path, hexDump(head), notice, QStringLiteral("Binary"), parent);
    }
    return new TextTab(path, parent);
}

} // namespace Tabs
