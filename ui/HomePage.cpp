// ShiftDex - unpack, edit and repack APK and JAR files
// Copyright (C) 2026 palaziks
// SPDX-License-Identifier: GPL-3.0-or-later
#include "HomePage.h"

#include "Engine.h"
#include "Util.h"

#include <QHBoxLayout>
#include <QHeaderView>
#include <QIcon>
#include <QJsonArray>
#include <QLabel>
#include <QMenu>
#include <QPushButton>
#include <QStackedWidget>
#include <QTreeWidget>
#include <QVBoxLayout>

HomePage::HomePage(QWidget *parent) : QWidget(parent)
{
    auto *icon = new QLabel;
    icon->setPixmap(QIcon(QStringLiteral(":/icons/app.svg")).pixmap(56, 56));
    auto *title = new QLabel(QStringLiteral("ShiftDex"));
    title->setObjectName(QStringLiteral("heading"));
    auto *subtitle = new QLabel(tr("Unpack, edit and repack APK and JAR files"));
    subtitle->setObjectName(QStringLiteral("muted"));
    auto *titles = new QVBoxLayout;
    titles->setSpacing(2);
    titles->addWidget(title);
    titles->addWidget(subtitle);

    auto *openFile = new QPushButton(tr("Open APK or JAR…"));
    openFile->setObjectName(QStringLiteral("primary"));
    openFile->setMinimumHeight(34);
    auto *plugins = new QPushButton(tr("Plugins"));
    plugins->setMinimumHeight(34);
    connect(openFile, &QPushButton::clicked, this, &HomePage::openFileRequested);
    connect(plugins, &QPushButton::clicked, this, &HomePage::pluginsRequested);

    auto *top = new QHBoxLayout;
    top->setSpacing(14);
    top->addWidget(icon);
    top->addLayout(titles, 1);
    top->addWidget(plugins, 0, Qt::AlignVCenter);
    top->addWidget(openFile, 0, Qt::AlignVCenter);

    auto *listTitle = new QLabel(tr("Projects"));
    listTitle->setObjectName(QStringLiteral("subheading"));

    m_list = new QTreeWidget;
    m_list->setHeaderLabels({tr("Name"), tr("Package or main class"), tr("Version"), tr("Type"), tr("Code"), tr("Last changed")});
    m_list->setRootIsDecorated(false);
    m_list->setUniformRowHeights(true);
    m_list->setAlternatingRowColors(true);
    m_list->setIconSize(QSize(28, 28));
    m_list->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_list->header()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_list->header()->setStretchLastSection(false);
    m_list->setContextMenuPolicy(Qt::CustomContextMenu);

    auto *empty = new QFrame;
    empty->setObjectName(QStringLiteral("card"));
    auto *emptyTitle = new QLabel(tr("No projects yet"));
    emptyTitle->setObjectName(QStringLiteral("subheading"));
    emptyTitle->setAlignment(Qt::AlignCenter);
    auto *emptyText = new QLabel(tr("Open an .apk or .jar file to unpack it into a project.\n"
                                    "You can also drop a file anywhere on this window."));
    emptyText->setObjectName(QStringLiteral("muted"));
    emptyText->setAlignment(Qt::AlignCenter);
    auto *emptyLayout = new QVBoxLayout(empty);
    emptyLayout->addStretch();
    emptyLayout->addWidget(emptyTitle);
    emptyLayout->addWidget(emptyText);
    emptyLayout->addStretch();

    m_stack = new QStackedWidget;
    m_stack->addWidget(empty);
    m_stack->addWidget(m_list);

    m_open = new QPushButton(tr("Open Project"));
    m_folder = new QPushButton(tr("Show Folder"));
    m_remove = new QPushButton(tr("Delete…"));
    m_remove->setObjectName(QStringLiteral("danger"));
    auto *credit = new QLabel(tr("ShiftDex %1  ·  Free software under the GNU GPL v3  ·  Made by palaziks")
                                  .arg(QStringLiteral(SHIFTDEX_VERSION)));
    credit->setObjectName(QStringLiteral("muted"));

    credit->setAlignment(Qt::AlignCenter);
    auto *listHeader = new QHBoxLayout;
    listHeader->addWidget(listTitle, 1, Qt::AlignBottom);
    listHeader->addWidget(m_remove);
    listHeader->addWidget(m_folder);
    listHeader->addWidget(m_open);

    auto *column = new QVBoxLayout;
    column->setSpacing(12);
    column->addLayout(top);
    column->addSpacing(10);
    column->addLayout(listHeader);
    column->addWidget(m_stack, 1);
    column->addWidget(credit);

    // keep the content from stretching edge to edge on wide windows
    auto *holder = new QWidget;
    holder->setLayout(column);
    holder->setMaximumWidth(980);
    auto *outer = new QHBoxLayout(this);
    outer->setContentsMargins(28, 24, 28, 18);
    outer->addWidget(holder);

    auto updateButtons = [this] {
        const bool has = !selected().isEmpty();
        m_open->setEnabled(has);
        m_folder->setEnabled(has);
        m_remove->setEnabled(has);
    };
    connect(m_list, &QTreeWidget::itemSelectionChanged, this, updateButtons);
    connect(m_list, &QTreeWidget::itemActivated, this, [this] {
        if (!selected().isEmpty())
            emit projectChosen(selected());
    });
    connect(m_open, &QPushButton::clicked, this, [this] { emit projectChosen(selected()); });
    connect(m_folder, &QPushButton::clicked, this,
            [this] { Util::openFolder(selected().value(QStringLiteral("dir")).toString()); });
    connect(m_remove, &QPushButton::clicked, this, &HomePage::removeSelected);
    connect(m_list, &QTreeWidget::customContextMenuRequested, this, [this](const QPoint &position) {
        if (!m_list->itemAt(position))
            return;
        QMenu menu(this);
        QAction *open = menu.addAction(tr("Open Project"));
        QAction *folder = menu.addAction(tr("Show Folder"));
        menu.addSeparator();
        QAction *remove = menu.addAction(tr("Delete…"));
        QAction *chosen = menu.exec(m_list->viewport()->mapToGlobal(position));
        if (chosen == open)
            emit projectChosen(selected());
        else if (chosen == folder)
            Util::openFolder(selected().value(QStringLiteral("dir")).toString());
        else if (chosen == remove)
            removeSelected();
    });
    updateButtons();
}

QJsonObject HomePage::selected() const
{
    const QTreeWidgetItem *item = m_list->currentItem();
    return item && item->isSelected() ? item->data(0, Qt::UserRole).toJsonObject() : QJsonObject();
}

void HomePage::removeSelected()
{
    const QJsonObject project = selected();
    if (project.isEmpty())
        return;
    if (!Util::confirm(this, tr("Delete Project"),
                       tr("Delete the project “%1” with its backups?\n\nThe file you originally opened is not touched.")
                           .arg(project.value(QStringLiteral("name")).toString()),
                       tr("Delete")))
        return;
    QString error;
    if (!Engine::call({QStringLiteral("delete"), project.value(QStringLiteral("dir")).toString()}, nullptr, &error))
        Util::error(this, tr("Delete Project"), error);
    refresh();
}

void HomePage::refresh()
{
    m_list->clear();
    QJsonValue result;
    Engine::call({QStringLiteral("projects")}, &result);
    const QIcon fallback(QStringLiteral(":/icons/app.svg"));
    for (const QJsonValue &value : result.toArray()) {
        const QJsonObject project = value.toObject();
        auto *item = new QTreeWidgetItem(m_list);
        const QString label = project.value(QStringLiteral("label")).toString();
        const QString name = project.value(QStringLiteral("name")).toString();
        item->setText(0, label.isEmpty() || label == name ? name : QStringLiteral("%1  (%2)").arg(label, name));
        QString package = project.value(QStringLiteral("package")).toString();
        if (package.isEmpty())
            package = project.value(QStringLiteral("main_class")).toString();
        item->setText(1, package);
        item->setText(2, project.value(QStringLiteral("version_name")).toString());
        item->setText(3, project.value(QStringLiteral("kind")).toString().toUpper());
        const bool javaView = project.value(QStringLiteral("code_view")).toString() == QLatin1String("java");
        const bool isApk = project.value(QStringLiteral("kind")).toString() == QLatin1String("apk");
        item->setText(4, javaView ? tr("Java") : (isApk ? tr("Smali") : tr("Bytecode")));
        item->setText(5, Util::time(project.value(QStringLiteral("modified")).toDouble()));
        const QPixmap icon(project.value(QStringLiteral("icon_path")).toString());
        item->setIcon(0, icon.isNull() ? fallback : QIcon(icon));
        item->setData(0, Qt::UserRole, project);
    }
    for (int column : {2, 3, 4, 5})
        m_list->resizeColumnToContents(column);
    m_stack->setCurrentIndex(m_list->topLevelItemCount() ? 1 : 0);
    if (m_list->topLevelItemCount())
        m_list->setCurrentItem(m_list->topLevelItem(0));
}
