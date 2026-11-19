// ShiftDex - unpack, edit and repack APK and JAR files
// Copyright (C) 2026 palaziks
// SPDX-License-Identifier: GPL-3.0-or-later
#include "SearchPanel.h"

#include "Theme.h"
#include "Util.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFile>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QTimer>
#include <QTreeWidget>
#include <QVBoxLayout>

#include "sdx_search.h"

namespace {

const int kMaxResults = 5000;

int onMatch(const char *relPath, long line, long column, const char *text, size_t length, void *user)
{
    auto *worker = static_cast<SearchWorker *>(user);
    SearchHit hit;
    hit.path = QString::fromUtf8(relPath);
    hit.line = int(line);
    hit.column = int(column);
    hit.text = QString::fromUtf8(text, qsizetype(length));
    worker->push(hit);
    return worker->stopped() ? 1 : 0;
}

int shouldStop(void *user)
{
    return static_cast<SearchWorker *>(user)->stopped() ? 1 : 0;
}

} // namespace

SearchWorker::SearchWorker(const QString &root, const QString &pattern, bool ignoreCase, bool regex,
                           bool namesOnly, QObject *parent)
    : QThread(parent), m_root(root), m_pattern(pattern), m_ignoreCase(ignoreCase), m_regex(regex),
      m_namesOnly(namesOnly)
{
}

void SearchWorker::push(const SearchHit &hit)
{
    m_batch.append(hit);
    if (m_batch.size() >= 200)
        flush();
}

void SearchWorker::flush()
{
    if (m_batch.isEmpty())
        return;
    emit hits(m_batch);
    m_batch.clear();
}

void SearchWorker::run()
{
    const QByteArray root = QFile::encodeName(m_root);
    const QByteArray pattern = m_pattern.toUtf8();
    sdx_search_options options{};
    options.pattern = pattern.constData();
    options.ignore_case = m_ignoreCase;
    options.regex = m_regex;
    options.names_only = m_namesOnly;
    options.max_results = kMaxResults;
    options.skip_top = ".cache";
    sdx_search_stats stats{};
    const int code = sdx_search(root.constData(), &options, onMatch, shouldStop, this, &stats);
    flush();
    emit done(code, int(stats.files_scanned), int(stats.matches), stats.truncated != 0);
}

SearchPanel::SearchPanel(QWidget *parent) : QWidget(parent)
{
    qRegisterMetaType<QList<SearchHit>>("QList<SearchHit>");

    m_input = new QLineEdit;
    m_input->setPlaceholderText(tr("Search"));
    m_input->setClearButtonEnabled(true);
    m_mode = new QComboBox;
    m_mode->addItem(tr("In file contents"));
    m_mode->addItem(tr("In file names"));
    m_case = new QCheckBox(tr("Match case"));
    m_regex = new QCheckBox(tr("Regex"));
    m_status = new QLabel(tr("Type at least two characters."));
    m_status->setObjectName(QStringLiteral("muted"));
    m_status->setWordWrap(true);

    m_results = new QTreeWidget;
    m_results->setObjectName(QStringLiteral("borderless"));
    m_results->setHeaderHidden(true);
    m_results->setUniformRowHeights(true);
    m_results->setIndentation(14);
    m_results->setTextElideMode(Qt::ElideRight);
    m_results->setFrameShape(QFrame::NoFrame);

    auto *options = new QHBoxLayout;
    options->addWidget(m_case);
    options->addWidget(m_regex);
    options->addStretch();

    auto *top = new QVBoxLayout;
    top->setContentsMargins(8, 8, 8, 6);
    top->setSpacing(6);
    top->addWidget(m_input);
    top->addWidget(m_mode);
    top->addLayout(options);
    top->addWidget(m_status);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addLayout(top);
    layout->addWidget(m_results, 1);

    m_debounce = new QTimer(this);
    m_debounce->setSingleShot(true);
    m_debounce->setInterval(280);
    connect(m_debounce, &QTimer::timeout, this, &SearchPanel::start);
    connect(m_input, &QLineEdit::textChanged, m_debounce, qOverload<>(&QTimer::start));
    connect(m_input, &QLineEdit::returnPressed, this, &SearchPanel::start);
    connect(m_mode, &QComboBox::currentIndexChanged, this, &SearchPanel::start);
    connect(m_case, &QCheckBox::toggled, this, &SearchPanel::start);
    connect(m_regex, &QCheckBox::toggled, this, &SearchPanel::start);
    connect(m_results, &QTreeWidget::itemActivated, this, [this](QTreeWidgetItem *item) {
        const QString path = item->data(0, Qt::UserRole).toString();
        if (!path.isEmpty())
            emit openRequested(m_root + QLatin1Char('/') + path, item->data(0, Qt::UserRole + 1).toInt(),
                               item->data(0, Qt::UserRole + 2).toInt());
    });
    connect(m_results, &QTreeWidget::itemClicked, m_results, &QTreeWidget::itemActivated);
}

SearchPanel::~SearchPanel()
{
    cancel();
}

void SearchPanel::setRoot(const QString &root)
{
    cancel();
    m_root = root;
    m_results->clear();
    m_fileItems.clear();
    m_input->clear();
    m_status->setText(tr("Type at least two characters."));
}

void SearchPanel::activate(bool fileNames)
{
    m_mode->setCurrentIndex(fileNames ? 1 : 0);
    m_input->setFocus();
    m_input->selectAll();
}

void SearchPanel::cancel()
{
    if (!m_worker)
        return;
    m_worker->disconnect(this);
    m_worker->stop();
    m_worker->wait();
    m_worker->deleteLater();
    m_worker = nullptr;
}

void SearchPanel::start()
{
    m_debounce->stop();
    cancel();
    m_results->clear();
    m_fileItems.clear();
    const QString pattern = m_input->text();
    if (m_root.isEmpty() || pattern.length() < 2) {
        m_status->setText(tr("Type at least two characters."));
        return;
    }
    m_status->setText(tr("Searching …"));
    const bool names = m_mode->currentIndex() == 1;
    // file names are matched without regard to case unless asked otherwise
    m_worker = new SearchWorker(m_root, pattern, !m_case->isChecked(), m_regex->isChecked(), names, this);
    connect(m_worker, &SearchWorker::hits, this, &SearchPanel::addHits);
    connect(m_worker, &SearchWorker::done, this, [this, names](int code, int files, int matches, bool truncated) {
        if (code == SDX_ERR_PATTERN)
            m_status->setText(tr("That regular expression is not valid."));
        else if (matches == 0)
            m_status->setText(tr("Nothing found in %1.").arg(Util::count(files, tr("file"), tr("files"))));
        else if (names)
            m_status->setText(truncated ? tr("First %1 files shown.").arg(matches) : Util::count(matches, tr("file"), tr("files")));
        else
            m_status->setText(truncated
                                  ? tr("First %1 matches shown. Narrow the search to see the rest.").arg(matches)
                                  : tr("%1 in %2").arg(Util::count(matches, tr("match"), tr("matches")),
                                                     Util::count(m_fileItems.size(), tr("file"), tr("files"))));
    });
    m_worker->start();
}

void SearchPanel::addHits(const QList<SearchHit> &batch)
{
    m_results->setUpdatesEnabled(false);
    for (const SearchHit &hit : batch) {
        if (hit.line == 0) {
            auto *item = new QTreeWidgetItem(m_results);
            const int slash = hit.path.lastIndexOf(QLatin1Char('/'));
            item->setText(0, slash < 0 ? hit.path
                                       : QStringLiteral("%1  —  %2").arg(hit.path.mid(slash + 1), hit.path.left(slash)));
            item->setToolTip(0, hit.path);
            item->setData(0, Qt::UserRole, hit.path);
            continue;
        }
        QTreeWidgetItem *fileItem = m_fileItems.value(hit.path);
        if (!fileItem) {
            fileItem = new QTreeWidgetItem(m_results);
            const int slash = hit.path.lastIndexOf(QLatin1Char('/'));
            fileItem->setText(0, hit.path.mid(slash + 1));
            fileItem->setToolTip(0, hit.path);
            QFont bold = fileItem->font(0);
            bold.setBold(true);
            fileItem->setFont(0, bold);
            fileItem->setData(0, Qt::UserRole, hit.path);
            fileItem->setData(0, Qt::UserRole + 1, hit.line);
            fileItem->setData(0, Qt::UserRole + 2, hit.column);
            // the first few files open expanded; a long list stays scannable
            fileItem->setExpanded(m_fileItems.size() < 40);
            m_fileItems.insert(hit.path, fileItem);
        }
        auto *item = new QTreeWidgetItem(fileItem);
        item->setText(0, QStringLiteral("%1:  %2").arg(hit.line).arg(hit.text));
        item->setData(0, Qt::UserRole, hit.path);
        item->setData(0, Qt::UserRole + 1, hit.line);
        item->setData(0, Qt::UserRole + 2, hit.column);
    }
    m_results->setUpdatesEnabled(true);
}
