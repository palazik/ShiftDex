// ShiftDex - unpack, edit and repack APK and JAR files
// Copyright (C) 2026 palaziks
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QDateTime>
#include <QDesktopServices>
#include <QJsonObject>
#include <QLocale>
#include <QMessageBox>
#include <QPushButton>
#include <QString>
#include <QUrl>

namespace Util {

inline QString size(double bytes)
{
    return QLocale().formattedDataSize(qint64(bytes), 1, QLocale::DataSizeTraditionalFormat);
}

inline QString time(double secondsSinceEpoch)
{
    const QDateTime when = QDateTime::fromSecsSinceEpoch(qint64(secondsSinceEpoch));
    return QLocale().toString(when, QStringLiteral("yyyy-MM-dd  HH:mm"));
}

// "1 file" / "3 files": the app ships without translation catalogues, so
// Qt's %n plural handling has nothing to work with.
inline QString count(qint64 n, const QString &one, const QString &many)
{
    return QStringLiteral("%1 %2").arg(n).arg(n == 1 ? one : many);
}

inline void error(QWidget *parent, const QString &title, const QString &text)
{
    QMessageBox::warning(parent, title, text);
}

// Asks before something that cannot be undone. Returns true to go ahead.
inline bool confirm(QWidget *parent, const QString &title, const QString &text, const QString &action)
{
    QMessageBox box(QMessageBox::Question, title, text, QMessageBox::Cancel, parent);
    QPushButton *go = box.addButton(action, QMessageBox::DestructiveRole);
    box.setDefaultButton(QMessageBox::Cancel);
    box.exec();
    return box.clickedButton() == static_cast<QAbstractButton *>(go);
}

inline void openFolder(const QString &path)
{
    QDesktopServices::openUrl(QUrl::fromLocalFile(path));
}

} // namespace Util
