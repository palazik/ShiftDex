// ShiftDex - unpack, edit and repack APK and JAR files
// Copyright (C) 2026 palaziks
// SPDX-License-Identifier: GPL-3.0-or-later
#include "JobDialog.h"

#include "Engine.h"

#include <QCloseEvent>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QRegularExpression>
#include <QVBoxLayout>

namespace {
QString g_lastLog;
QString g_lastTitle;
}

QString JobDialog::lastLog() { return g_lastLog; }
QString JobDialog::lastTitle() { return g_lastTitle; }

void JobDialog::run(QWidget *parent, const QString &title, const QStringList &arguments, Callback callback)
{
    auto *dialog = new JobDialog(parent, title);
    dialog->m_callback = std::move(callback);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    g_lastTitle = title;
    g_lastLog.clear();
    dialog->m_job->start(arguments);
    dialog->open();
}

JobDialog::JobDialog(QWidget *parent, const QString &title) : QDialog(parent)
{
    setWindowTitle(title);
    setModal(true);
    setMinimumWidth(520);

    m_status = new QLabel(tr("Starting …"));
    m_status->setWordWrap(true);
    m_status->setTextInteractionFlags(Qt::TextSelectableByMouse);

    m_bar = new QProgressBar;
    m_bar->setRange(0, 0);
    m_bar->setTextVisible(false);

    m_log = new QPlainTextEdit;
    m_log->setReadOnly(true);
    m_log->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    m_log->setLineWrapMode(QPlainTextEdit::NoWrap);
    m_log->setMaximumBlockCount(5000);
    m_log->setMinimumHeight(240);
    m_log->hide();

    m_details = new QPushButton(tr("Show Details"));
    m_details->setCheckable(true);
    m_details->setAutoDefault(false);
    connect(m_details, &QPushButton::toggled, this, &JobDialog::setDetailsVisible);

    m_button = new QPushButton(tr("Cancel"));
    m_button->setAutoDefault(false);
    connect(m_button, &QPushButton::clicked, this, &JobDialog::reject);

    auto *buttons = new QHBoxLayout;
    buttons->addWidget(m_details);
    buttons->addStretch();
    buttons->addWidget(m_button);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(18, 18, 18, 14);
    layout->setSpacing(10);
    layout->addWidget(m_status);
    layout->addWidget(m_bar);
    layout->addWidget(m_log, 1);
    layout->addLayout(buttons);

    m_job = new EngineJob(this);
    connect(m_job, &EngineJob::log, this, &JobDialog::append);
    connect(m_job, &EngineJob::progress, this, [this](double fraction) {
        m_bar->setRange(0, 1000);
        m_bar->setValue(int(fraction * 1000));
    });
    connect(m_job, &EngineJob::finished, this, &JobDialog::done);
}

void JobDialog::setDetailsVisible(bool visible)
{
    m_log->setVisible(visible);
    m_details->setText(visible ? tr("Hide Details") : tr("Show Details"));
    if (!visible)
        adjustSize();
}

void JobDialog::append(const QString &line)
{
    m_log->appendPlainText(line);
    g_lastLog += line + QLatin1Char('\n');
    // APKEditor prefixes its lines with a timestamp and level; show the rest
    QString text = line.trimmed();
    static const QRegularExpression prefix(QStringLiteral("^\\d+\\.\\d+ [A-Z]: (\\[[A-Z ]+\\] )?"));
    text.remove(prefix);
    if (!text.isEmpty() && !text.startsWith(QLatin1Char('$')) && !text.startsWith(QLatin1Char('-'))
        && !text.startsWith(QLatin1Char('_')) && m_running) {
        m_status->setText(m_status->fontMetrics().elidedText(text, Qt::ElideMiddle, 470));
    }
}

void JobDialog::done(bool ok, const QJsonValue &result, const QString &error)
{
    m_running = false;
    m_result = result;
    if (ok || m_cancelRequested) {
        m_reported = true;
        const Callback callback = m_callback;
        const bool success = ok;
        accept();
        if (callback)
            callback(success, result);
        return;
    }
    // keep the dialog open so the log can be read
    g_lastLog += tr("Error: %1").arg(error) + QLatin1Char('\n');
    m_bar->hide();
    m_status->setText(error);
    m_log->appendPlainText(tr("Error: %1").arg(error));
    m_button->setText(tr("Close"));
    m_button->setDefault(true);
    m_details->setChecked(true);
}

void JobDialog::reject()
{
    if (m_running) {
        if (!m_cancelRequested) {
            m_cancelRequested = true;
            m_status->setText(tr("Cancelling …"));
            m_button->setEnabled(false);
            m_job->cancel();
        }
        return;
    }
    const Callback callback = m_callback;
    const bool report = !m_reported;
    m_reported = true;
    QDialog::reject();
    if (report && callback)
        callback(false, QJsonValue());
}

void JobDialog::closeEvent(QCloseEvent *event)
{
    if (m_running) {
        event->ignore();
        reject();
        return;
    }
    QDialog::closeEvent(event);
}
