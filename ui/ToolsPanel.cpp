// ShiftDex - unpack, edit and repack APK and JAR files
// Copyright (C) 2026 palaziks
// SPDX-License-Identifier: GPL-3.0-or-later
#include "ToolsPanel.h"

#include "Engine.h"
#include "JobDialog.h"
#include "Theme.h"
#include "Util.h"

#include <QDir>
#include <QFileDialog>
#include <QGridLayout>
#include <QLabel>
#include <QPushButton>

namespace {

QLabel *title(const QString &text)
{
    auto *label = new QLabel(text);
    QFont font = label->font();
    font.setBold(true);
    label->setFont(font);
    return label;
}

QLabel *detail()
{
    auto *label = new QLabel;
    label->setWordWrap(true);
    label->setObjectName(QStringLiteral("muted"));
    label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    return label;
}

void mark(QLabel *label, int state)
{
    // 0 = missing, 1 = ok, 2 = optional and absent
    label->setText(state == 1 ? QStringLiteral("✓") : (state == 0 ? QStringLiteral("✗") : QStringLiteral("–")));
    QPalette palette = label->palette();
    palette.setColor(QPalette::WindowText, state == 1 ? Theme::good() : (state == 0 ? Theme::warning() : Theme::mutedText()));
    label->setPalette(palette);
}

} // namespace

ToolsPanel::ToolsPanel(QWidget *parent) : QWidget(parent)
{
    m_java = detail();
    m_apkeditor = detail();
    m_signer = detail();
    m_jadx = detail();
    m_javaMark = new QLabel;
    m_apkeditorMark = new QLabel;
    m_signerMark = new QLabel;
    m_jadxMark = new QLabel;
    for (QLabel *label : {m_javaMark, m_apkeditorMark, m_signerMark, m_jadxMark}) {
        QFont font = label->font();
        font.setBold(true);
        font.setPointSize(font.pointSize() + 2);
        label->setFont(font);
        label->setFixedWidth(22);
        label->setAlignment(Qt::AlignTop | Qt::AlignHCenter);
    }
    m_getApkeditor = new QPushButton(tr("Download"));
    m_localApkeditor = new QPushButton(tr("Use Local File…"));
    m_getSigner = new QPushButton(tr("Download"));
    m_getJadx = new QPushButton(tr("Download"));
    for (QPushButton *button : {m_getApkeditor, m_localApkeditor, m_getSigner, m_getJadx})
        button->setAutoDefault(false);

    auto *grid = new QGridLayout(this);
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setHorizontalSpacing(10);
    grid->setVerticalSpacing(2);
    grid->setColumnStretch(1, 1);

    int row = 0;
    auto addTool = [&](QLabel *markLabel, const QString &name, QLabel *text, QList<QPushButton *> buttons) {
        grid->addWidget(markLabel, row, 0, 2, 1);
        grid->addWidget(title(name), row, 1);
        grid->addWidget(text, row + 1, 1);
        int column = 2;
        for (QPushButton *button : buttons)
            grid->addWidget(button, row, column++, 2, 1, Qt::AlignVCenter);
        grid->setRowMinimumHeight(row + 2, 12);
        row += 3;
    };
    addTool(m_javaMark, tr("Java"), m_java, {});
    addTool(m_apkeditorMark, tr("APKEditor"), m_apkeditor, {m_getApkeditor, m_localApkeditor});
    addTool(m_signerMark, tr("APK signer"), m_signer, {m_getSigner});
    addTool(m_jadxMark, tr("Java view (jadx)"), m_jadx, {m_getJadx});
    connect(m_getJadx, &QPushButton::clicked, this,
            [this] { download(QStringLiteral("install-jadx"), tr("Downloading jadx")); });

    connect(m_getApkeditor, &QPushButton::clicked, this,
            [this] { download(QStringLiteral("install-apkeditor"), tr("Downloading APKEditor")); });
    connect(m_getSigner, &QPushButton::clicked, this,
            [this] { download(QStringLiteral("install-signer"), tr("Downloading the APK Signer")); });
    connect(m_localApkeditor, &QPushButton::clicked, this, [this] {
        const QString file = QFileDialog::getOpenFileName(this, tr("Choose the APKEditor Jar"), QDir::homePath(),
                                                          tr("Java archives (*.jar)"));
        if (file.isEmpty())
            return;
        QString error;
        if (!Engine::call({QStringLiteral("use-apkeditor"), file}, nullptr, &error))
            Util::error(this, tr("APKEditor"), error);
        refresh();
    });
    refresh();
}

void ToolsPanel::download(const QString &command, const QString &title)
{
    JobDialog::run(this, title, {command}, [this](bool, const QJsonValue &) { refresh(); });
}

void ToolsPanel::fetchMissing()
{
    if (m_fetchTried)
        return;
    if (!m_status.value(QStringLiteral("java_ok")).toBool()
        || !m_status.value(QStringLiteral("apkeditor")).toString().isEmpty())
        return;
    // once per session: a failed download must not loop
    m_fetchTried = true;
    download(QStringLiteral("install-apkeditor"), tr("Downloading APKEditor"));
}

bool ToolsPanel::ready() const
{
    return m_status.value(QStringLiteral("java_ok")).toBool()
        && !m_status.value(QStringLiteral("apkeditor")).toString().isEmpty();
}

void ToolsPanel::refresh()
{
    QJsonValue result;
    QString error;
    if (!Engine::call({QStringLiteral("status")}, &result, &error)) {
        m_java->setText(error);
        mark(m_javaMark, 0);
        return;
    }
    m_status = result.toObject();

    const QString javaVersion = m_status.value(QStringLiteral("java_version")).toString();
    if (javaVersion.isEmpty()) {
        m_java->setText(tr("Not found. Install a Java runtime (OpenJDK 11 or newer) with your package manager, "
                           "or set its path in Preferences."));
        mark(m_javaMark, 0);
    } else {
        QString text = tr("Version %1 at %2").arg(javaVersion, m_status.value(QStringLiteral("java")).toString());
        if (!m_status.value(QStringLiteral("jdk")).toBool())
            text += tr(". This is a runtime only; install a full JDK to view .class bytecode.");
        m_java->setText(text);
        mark(m_javaMark, m_status.value(QStringLiteral("java_ok")).toBool() ? 1 : 0);
    }

    const QString apkeditor = m_status.value(QStringLiteral("apkeditor")).toString();
    if (apkeditor.isEmpty()) {
        m_apkeditor->setText(tr("Not installed. Needed to unpack and build APK files; JAR files work without it. "
                                "ShiftDex fetches it from github.com/REAndroid/APKEditor when it is needed."));
        m_getApkeditor->setText(tr("Download"));
        mark(m_apkeditorMark, 0);
    } else {
        m_apkeditor->setText(apkeditor == QLatin1String("local")
                                 ? tr("Installed from a local file. Unpacks and builds APK files.")
                                 : tr("%1 installed. Unpacks and builds APK files.").arg(apkeditor));
        m_getApkeditor->setText(tr("Check for Update"));
        mark(m_apkeditorMark, 1);
    }

    const QString signer = m_status.value(QStringLiteral("signer")).toString();
    if (signer.isEmpty()) {
        m_signer->setText(tr("None found. Android only installs signed APKs, so rebuilt apps would not install. "
                             "Download uber-apk-signer, or install the Android SDK build tools."));
        mark(m_signerMark, 2);
    } else {
        m_signer->setText(tr("Using %1.").arg(signer));
        mark(m_signerMark, 1);
    }
    m_getSigner->setVisible(!m_status.value(QStringLiteral("apksigner")).toBool());
    m_getSigner->setText(signer.isEmpty() ? tr("Download") : tr("Check for Update"));

    const QString jadx = m_status.value(QStringLiteral("jadx")).toString();
    if (jadx.isEmpty()) {
        m_jadx->setText(tr("Optional, not installed. Shows an app's code as read-only Java next to the smali. "
                           "A 72 MB download from github.com/skylot/jadx; ShiftDex also offers it the first "
                           "time you switch a project to Java."));
        mark(m_jadxMark, 2);
    } else {
        m_jadx->setText(tr("jadx %1 installed. Shows code as read-only Java.").arg(jadx));
        mark(m_jadxMark, 1);
    }
    m_getJadx->setText(jadx.isEmpty() ? tr("Download") : tr("Check for Update"));
    emit statusChanged();
}
