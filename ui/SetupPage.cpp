// ShiftDex - unpack, edit and repack APK and JAR files
// Copyright (C) 2026 palaziks
// SPDX-License-Identifier: GPL-3.0-or-later
#include "SetupPage.h"

#include "Config.h"
#include "ToolsPanel.h"

#include <QButtonGroup>
#include <QDir>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QStackedWidget>
#include <QTimer>
#include <QVBoxLayout>

namespace {

QLabel *heading(const QString &text)
{
    auto *label = new QLabel(text);
    label->setObjectName(QStringLiteral("heading"));
    return label;
}

QLabel *paragraph(const QString &text, bool muted = false)
{
    auto *label = new QLabel(text);
    label->setWordWrap(true);
    label->setOpenExternalLinks(true);
    if (muted)
        label->setObjectName(QStringLiteral("muted"));
    return label;
}

} // namespace

SetupPage::SetupPage(QWidget *parent) : QWidget(parent)
{
    m_steps = new QStackedWidget;
    m_steps->addWidget(buildWelcome());
    m_steps->addWidget(buildTools());
    m_steps->addWidget(buildAppearance());

    m_stepLabel = new QLabel;
    m_stepLabel->setObjectName(QStringLiteral("muted"));
    m_back = new QPushButton(tr("Back"));
    m_next = new QPushButton;
    m_next->setObjectName(QStringLiteral("primary"));
    m_next->setDefault(true);
    m_next->setMinimumWidth(120);

    auto *buttons = new QHBoxLayout;
    buttons->setContentsMargins(9, 0, 9, 0);
    buttons->addWidget(m_stepLabel, 1);
    buttons->addWidget(m_back);
    buttons->addWidget(m_next);

    auto *column = new QVBoxLayout;
    column->addWidget(m_steps, 1);
    column->addLayout(buttons);

    auto *holder = new QWidget;
    holder->setLayout(column);
    holder->setMaximumWidth(640);
    auto *outer = new QHBoxLayout(this);
    outer->setContentsMargins(28, 36, 28, 24);
    outer->addWidget(holder);

    connect(m_back, &QPushButton::clicked, this, [this] { showStep(m_steps->currentIndex() - 1); });
    connect(m_next, &QPushButton::clicked, this, [this] {
        if (m_steps->currentIndex() + 1 < m_steps->count()) {
            showStep(m_steps->currentIndex() + 1);
            return;
        }
        Config::instance().set(QStringLiteral("setup_done"), true);
        emit finished();
    });
    showStep(0);
}

void SetupPage::showStep(int step)
{
    m_steps->setCurrentIndex(step);
    const bool last = step == m_steps->count() - 1;
    m_back->setVisible(step > 0);
    m_next->setText(step == 0 ? tr("Get Started") : (last ? tr("Finish") : tr("Next")));
    m_stepLabel->setText(step == 0 ? QString() : tr("Step %1 of %2").arg(step).arg(m_steps->count() - 1));
    if (step == 1 && m_tools) {
        m_tools->refresh();
        // let the page appear before the download dialog opens on top of it
        QTimer::singleShot(250, m_tools, &ToolsPanel::fetchMissing);
    }
}

QWidget *SetupPage::buildWelcome()
{
    auto *icon = new QLabel;
    icon->setPixmap(QIcon(QStringLiteral(":/icons/app.svg")).pixmap(88, 88));

    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);
    layout->setSpacing(12);
    layout->addStretch();
    layout->addWidget(icon);
    layout->addWidget(heading(tr("Welcome to ShiftDex")));
    layout->addWidget(paragraph(tr("ShiftDex unpacks Android apps (.apk) and Java archives (.jar), lets you browse "
                                   "and edit what is inside, and packs them again.")));
    layout->addWidget(paragraph(tr("You can change an app's name, icon and package name, keep backups while you "
                                   "work, and share your changes with others as plugins.")));
    layout->addSpacing(8);
    layout->addWidget(paragraph(tr("ShiftDex is free and open source software, released under the "
                                   "<a href=\"https://www.gnu.org/licenses/gpl-3.0.html\">GNU General Public "
                                   "License, version 3</a>. It comes with no warranty.<br>Made by palaziks."),
                                true));
    layout->addWidget(paragraph(tr("Only modify software you have the right to modify."), true));
    layout->addStretch();
    return page;
}

QWidget *SetupPage::buildTools()
{
    m_tools = new ToolsPanel;
    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);
    layout->setSpacing(12);
    layout->addWidget(heading(tr("Tools")));
    layout->addWidget(paragraph(tr("ShiftDex uses APKEditor to decode and build APK files. It is fetched "
                                   "automatically from its official GitHub releases into your user folder, so "
                                   "you always get the current version."),
                                true));
    layout->addSpacing(10);
    layout->addWidget(m_tools);
    layout->addStretch();
    layout->addWidget(paragraph(tr("You can continue without these and install them later from "
                                   "Preferences > Tools. JAR files only need Java."),
                                true));
    return page;
}

QWidget *SetupPage::buildAppearance()
{
    Config &config = Config::instance();
    auto *group = new QButtonGroup(this);
    auto *themes = new QVBoxLayout;
    themes->setSpacing(6);
    const QList<QPair<QString, QString>> choices{
        {QStringLiteral("system"), tr("Follow the system")},
        {QStringLiteral("light"), tr("White")},
        {QStringLiteral("dark"), tr("Black")},
        {QStringLiteral("amoled"), tr("AMOLED — pure black, easy on OLED screens")},
    };
    for (const auto &choice : choices) {
        auto *radio = new QRadioButton(choice.second);
        radio->setChecked(config.str(QStringLiteral("theme")) == choice.first);
        const QString value = choice.first;
        connect(radio, &QRadioButton::toggled, this, [value](bool on) {
            if (on)
                Config::instance().set(QStringLiteral("theme"), value);
        });
        group->addButton(radio);
        themes->addWidget(radio);
    }

    auto *folder = new QLineEdit(config.workspaceDir());
    folder->setReadOnly(true);
    auto *browse = new QPushButton(tr("Change…"));
    connect(browse, &QPushButton::clicked, this, [this, folder] {
        const QString chosen = QFileDialog::getExistingDirectory(this, tr("Choose the Projects Folder"), QDir::homePath());
        if (chosen.isEmpty())
            return;
        Config::instance().set(QStringLiteral("workspace_dir"), chosen);
        folder->setText(chosen);
    });
    auto *folderRow = new QHBoxLayout;
    folderRow->addWidget(folder, 1);
    folderRow->addWidget(browse);

    auto *themeTitle = new QLabel(tr("Theme"));
    themeTitle->setObjectName(QStringLiteral("subheading"));
    auto *folderTitle = new QLabel(tr("Projects folder"));
    folderTitle->setObjectName(QStringLiteral("subheading"));

    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);
    layout->setSpacing(10);
    layout->addWidget(heading(tr("Make it yours")));
    layout->addSpacing(6);
    layout->addWidget(themeTitle);
    layout->addLayout(themes);
    layout->addSpacing(14);
    layout->addWidget(folderTitle);
    layout->addWidget(paragraph(tr("Unpacked apps and their backups are kept here. Decoded apps can take several "
                                   "hundred megabytes each."),
                                true));
    layout->addLayout(folderRow);
    layout->addStretch();
    layout->addWidget(paragraph(tr("Everything here, including keyboard shortcuts, can be changed later in "
                                   "Preferences."),
                                true));
    return page;
}
