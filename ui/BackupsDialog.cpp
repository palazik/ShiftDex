// ShiftDex - unpack, edit and repack APK and JAR files
// Copyright (C) 2026 palaziks
// SPDX-License-Identifier: GPL-3.0-or-later
#include "BackupsDialog.h"

#include "Config.h"
#include "Engine.h"
#include "JobDialog.h"
#include "Util.h"

#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QJsonArray>
#include <QLabel>
#include <QPushButton>
#include <QTabWidget>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace {

QTreeWidget *makeList(const QStringList &headers)
{
    auto *list = new QTreeWidget;
    list->setHeaderLabels(headers);
    list->setRootIsDecorated(false);
    list->setUniformRowHeights(true);
    list->setAlternatingRowColors(true);
    list->setSelectionMode(QAbstractItemView::SingleSelection);
    list->header()->setStretchLastSection(false);
    return list;
}

QLabel *hint(const QString &text)
{
    auto *label = new QLabel(text);
    label->setObjectName(QStringLiteral("muted"));
    label->setWordWrap(true);
    return label;
}

} // namespace

BackupsDialog::BackupsDialog(const QJsonObject &project, const QString &currentFile, QWidget *parent)
    : QDialog(parent), m_project(project), m_dir(project.value(QStringLiteral("dir")).toString()),
      m_currentFile(currentFile)
{
    setWindowTitle(tr("Backups — %1").arg(project.value(QStringLiteral("name")).toString()));
    resize(720, 480);

    auto *tabs = new QTabWidget;
    tabs->setDocumentMode(true);
    tabs->addTab(buildSnapshots(), tr("Snapshots"));
    tabs->addTab(buildFiles(), tr("File Backups"));
    tabs->addTab(buildOriginal(), tr("Original File"));

    auto *close = new QPushButton(tr("Close"));
    connect(close, &QPushButton::clicked, this, &QDialog::accept);
    auto *bottom = new QHBoxLayout;
    bottom->setContentsMargins(12, 8, 12, 12);
    bottom->addStretch();
    bottom->addWidget(close);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 6, 0, 0);
    layout->addWidget(tabs, 1);
    layout->addLayout(bottom);

    loadSnapshots();
    loadFiles();
}

// --------------------------------------------------------------- snapshots

QWidget *BackupsDialog::buildSnapshots()
{
    auto *page = new QWidget;
    m_snapshots = makeList({tr("Created"), tr("Note"), tr("Files"), tr("Size")});
    m_snapshots->header()->setSectionResizeMode(1, QHeaderView::Stretch);

    auto *create = new QPushButton(tr("Create Snapshot…"));
    create->setObjectName(QStringLiteral("primary"));
    m_restore = new QPushButton(tr("Restore"));
    m_export = new QPushButton(tr("Export…"));
    m_delete = new QPushButton(tr("Delete"));
    m_delete->setObjectName(QStringLiteral("danger"));

    auto *buttons = new QHBoxLayout;
    buttons->addWidget(create);
    buttons->addStretch();
    buttons->addWidget(m_restore);
    buttons->addWidget(m_export);
    buttons->addWidget(m_delete);

    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(12, 12, 12, 4);
    layout->addWidget(hint(tr("A snapshot stores every file of the unpacked project. Restoring one puts the "
                              "whole project back to that moment.")));
    layout->addWidget(m_snapshots, 1);
    layout->addLayout(buttons);

    auto updateButtons = [this] {
        const bool selected = !selectedSnapshot().isEmpty();
        m_restore->setEnabled(selected);
        m_export->setEnabled(selected);
        m_delete->setEnabled(selected);
    };
    connect(m_snapshots, &QTreeWidget::itemSelectionChanged, this, updateButtons);
    updateButtons();

    connect(create, &QPushButton::clicked, this, [this] {
        bool ok = false;
        const QString note = QInputDialog::getText(this, tr("Create Snapshot"),
                                                   tr("Note (optional), so you can tell snapshots apart:"),
                                                   QLineEdit::Normal, QString(), &ok);
        if (!ok)
            return;
        QStringList arguments{QStringLiteral("snapshot"), m_dir};
        if (!note.trimmed().isEmpty())
            arguments << QStringLiteral("--note") << note.trimmed();
        JobDialog::run(this, tr("Creating Snapshot"), arguments, [this](bool, const QJsonValue &) { loadSnapshots(); });
    });

    connect(m_restore, &QPushButton::clicked, this, [this] {
        const QString id = selectedSnapshot();
        const QString extra = Config::instance().flag(QStringLiteral("snapshot_before_restore"))
            ? tr("The current state is saved as a snapshot first.")
            : tr("Changes made since then will be lost.");
        if (!Util::confirm(this, tr("Restore Snapshot"),
                           tr("Replace all project files with this snapshot?\n\n%1").arg(extra), tr("Restore")))
            return;
        JobDialog::run(this, tr("Restoring Snapshot"), {QStringLiteral("snapshot-restore"), m_dir, id},
                       [this](bool ok, const QJsonValue &result) {
                           loadSnapshots();
                           if (ok) {
                               m_project = result.toObject();
                               emit projectReplaced(m_project);
                           }
                       });
    });

    connect(m_export, &QPushButton::clicked, this, [this] {
        const QString id = selectedSnapshot();
        const QString suggested = QStringLiteral("%1/%2-%3.tar.gz")
            .arg(QDir::homePath(), m_project.value(QStringLiteral("id")).toString(), id);
        const QString dest = QFileDialog::getSaveFileName(this, tr("Export Snapshot"), suggested,
                                                          tr("Compressed archive (*.tar.gz)"));
        if (dest.isEmpty())
            return;
        QString error;
        if (!Engine::call({QStringLiteral("snapshot-export"), m_dir, id, dest}, nullptr, &error, 300000))
            Util::error(this, tr("Export Snapshot"), error);
    });

    connect(m_delete, &QPushButton::clicked, this, [this] {
        const QString id = selectedSnapshot();
        if (!Util::confirm(this, tr("Delete Snapshot"), tr("Delete this snapshot? This cannot be undone."),
                           tr("Delete")))
            return;
        QString error;
        if (!Engine::call({QStringLiteral("snapshot-delete"), m_dir, id}, nullptr, &error))
            Util::error(this, tr("Delete Snapshot"), error);
        loadSnapshots();
    });
    return page;
}

QString BackupsDialog::selectedSnapshot() const
{
    const QTreeWidgetItem *item = m_snapshots->currentItem();
    return item && item->isSelected() ? item->data(0, Qt::UserRole).toString() : QString();
}

void BackupsDialog::loadSnapshots()
{
    m_snapshots->clear();
    QJsonValue result;
    QString error;
    if (!Engine::call({QStringLiteral("snapshots"), m_dir}, &result, &error)) {
        Util::error(this, tr("Backups"), error);
        return;
    }
    for (const QJsonValue &value : result.toArray()) {
        const QJsonObject snapshot = value.toObject();
        auto *item = new QTreeWidgetItem(m_snapshots);
        QString note = snapshot.value(QStringLiteral("note")).toString();
        if (snapshot.value(QStringLiteral("kind")).toString() == QLatin1String("auto"))
            note = note.isEmpty() ? tr("Automatic") : tr("%1 (automatic)").arg(note);
        item->setText(0, Util::time(snapshot.value(QStringLiteral("time")).toDouble()));
        item->setText(1, note);
        item->setText(2, QString::number(snapshot.value(QStringLiteral("files")).toInt()));
        item->setText(3, Util::size(snapshot.value(QStringLiteral("size")).toDouble()));
        item->setData(0, Qt::UserRole, snapshot.value(QStringLiteral("id")).toString());
    }
    for (int column : {0, 2, 3})
        m_snapshots->resizeColumnToContents(column);
}

// ------------------------------------------------------------ file backups

QWidget *BackupsDialog::buildFiles()
{
    auto *page = new QWidget;
    m_files = makeList({tr("File"), tr("Backed up"), tr("Size")});
    m_files->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_files->setTextElideMode(Qt::ElideLeft);

    auto *backup = new QPushButton(m_currentFile.isEmpty()
                                       ? tr("Back Up Current File")
                                       : tr("Back Up %1").arg(QFileInfo(m_currentFile).fileName()));
    backup->setEnabled(!m_currentFile.isEmpty());
    backup->setToolTip(m_currentFile.isEmpty() ? tr("Open a file in the editor first.") : m_currentFile);
    m_fileRestore = new QPushButton(tr("Restore"));
    m_fileDelete = new QPushButton(tr("Delete"));
    m_fileDelete->setObjectName(QStringLiteral("danger"));

    auto *buttons = new QHBoxLayout;
    buttons->addWidget(backup);
    buttons->addStretch();
    buttons->addWidget(m_fileRestore);
    buttons->addWidget(m_fileDelete);

    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(12, 12, 12, 4);
    layout->addWidget(hint(tr("Copies of single files. Use them before a risky edit; restoring only touches "
                              "that one file.")));
    layout->addWidget(m_files, 1);
    layout->addLayout(buttons);

    auto updateButtons = [this] {
        const QTreeWidgetItem *item = m_files->currentItem();
        const bool selected = item && item->isSelected();
        m_fileRestore->setEnabled(selected);
        m_fileDelete->setEnabled(selected);
    };
    connect(m_files, &QTreeWidget::itemSelectionChanged, this, updateButtons);
    updateButtons();

    connect(backup, &QPushButton::clicked, this, [this] {
        QString error;
        if (!Engine::call({QStringLiteral("file-backup"), m_dir, m_currentFile}, nullptr, &error))
            Util::error(this, tr("Back Up File"), error);
        loadFiles();
    });
    connect(m_fileRestore, &QPushButton::clicked, this, [this] {
        const QTreeWidgetItem *item = m_files->currentItem();
        const QString rel = item->data(0, Qt::UserRole).toString();
        if (!Util::confirm(this, tr("Restore File"),
                           tr("Replace %1 with the backup from %2?").arg(rel, item->text(1).simplified()),
                           tr("Restore")))
            return;
        QString error;
        if (!Engine::call({QStringLiteral("file-restore"), m_dir, rel, item->data(0, Qt::UserRole + 1).toString()},
                          nullptr, &error)) {
            Util::error(this, tr("Restore File"), error);
            return;
        }
        emit fileRestored(rel);
    });
    connect(m_fileDelete, &QPushButton::clicked, this, [this] {
        const QTreeWidgetItem *item = m_files->currentItem();
        QString error;
        if (!Engine::call({QStringLiteral("file-backup-delete"), m_dir, item->data(0, Qt::UserRole).toString(),
                           item->data(0, Qt::UserRole + 1).toString()},
                          nullptr, &error))
            Util::error(this, tr("Delete Backup"), error);
        loadFiles();
    });
    return page;
}

void BackupsDialog::loadFiles()
{
    m_files->clear();
    QJsonValue result;
    if (!Engine::call({QStringLiteral("file-backups"), m_dir}, &result))
        return;
    for (const QJsonValue &value : result.toArray()) {
        const QJsonObject backup = value.toObject();
        auto *item = new QTreeWidgetItem(m_files);
        const QString rel = backup.value(QStringLiteral("rel")).toString();
        item->setText(0, rel);
        item->setToolTip(0, rel);
        item->setText(1, Util::time(backup.value(QStringLiteral("time")).toDouble()));
        item->setText(2, Util::size(backup.value(QStringLiteral("size")).toDouble()));
        item->setData(0, Qt::UserRole, rel);
        item->setData(0, Qt::UserRole + 1, backup.value(QStringLiteral("id")).toString());
    }
    m_files->resizeColumnToContents(1);
    m_files->resizeColumnToContents(2);
}

// ----------------------------------------------------------------- original

QWidget *BackupsDialog::buildOriginal()
{
    auto *page = new QWidget;
    const QString original = m_project.value(QStringLiteral("original_file")).toString();
    const QFileInfo info(original);

    auto *name = new QLabel(info.exists() ? info.fileName() : tr("The original file is missing."));
    name->setObjectName(QStringLiteral("subheading"));
    auto *details = hint(info.exists()
                             ? tr("%1, opened from %2")
                                   .arg(Util::size(double(info.size())),
                                        m_project.value(QStringLiteral("source_path")).toString())
                             : QString());

    auto *save = new QPushButton(tr("Save a Copy…"));
    auto *reset = new QPushButton(tr("Reset Project to Original…"));
    reset->setObjectName(QStringLiteral("danger"));
    save->setEnabled(info.exists());
    reset->setEnabled(info.exists());

    auto *buttons = new QHBoxLayout;
    buttons->addWidget(save);
    buttons->addWidget(reset);
    buttons->addStretch();

    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(12, 12, 12, 4);
    layout->setSpacing(8);
    layout->addWidget(hint(tr("ShiftDex keeps an untouched copy of the file you opened. It is never modified, "
                              "whatever you do to the project.")));
    layout->addSpacing(6);
    layout->addWidget(name);
    layout->addWidget(details);
    layout->addLayout(buttons);
    layout->addStretch();

    connect(save, &QPushButton::clicked, this, [this, original, info] {
        const QString dest = QFileDialog::getSaveFileName(this, tr("Save a Copy of the Original"),
                                                          QDir::homePath() + QLatin1Char('/') + info.fileName());
        if (dest.isEmpty())
            return;
        QFile::remove(dest);
        if (!QFile::copy(original, dest))
            Util::error(this, tr("Save a Copy"), tr("The file could not be written to %1.").arg(dest));
    });
    connect(reset, &QPushButton::clicked, this, [this] {
        const QString extra = Config::instance().flag(QStringLiteral("snapshot_before_restore"))
            ? tr("The current state is saved as a snapshot first.")
            : tr("All your changes will be lost.");
        if (!Util::confirm(this, tr("Reset Project"),
                           tr("Unpack the original file again and discard the current project files?\n\n%1").arg(extra),
                           tr("Reset")))
            return;
        JobDialog::run(this, tr("Resetting Project"), {QStringLiteral("reset"), m_dir},
                       [this](bool ok, const QJsonValue &result) {
                           loadSnapshots();
                           if (ok) {
                               m_project = result.toObject();
                               emit projectReplaced(m_project);
                           }
                       });
    });
    return page;
}
