// ShiftDex - unpack, edit and repack APK and JAR files
// Copyright (C) 2026 palaziks
// SPDX-License-Identifier: GPL-3.0-or-later
#include "MainWindow.h"

#include "CodeEditor.h"
#include "Config.h"
#include "Engine.h"
#include "HomePage.h"
#include "JobDialog.h"
#include "Keymap.h"
#include "PluginsDialog.h"
#include "SettingsDialog.h"
#include "SetupPage.h"
#include "Tabs.h"
#include "Theme.h"
#include "Util.h"
#include "Workspace.h"

#include <QActionGroup>
#include <QApplication>
#include <QCloseEvent>
#include <QDragEnterEvent>
#include <QFileDialog>
#include <QFontDatabase>
#include <QLabel>
#include <QMenuBar>
#include <QMimeData>
#include <QPlainTextEdit>
#include <QStackedWidget>
#include <QStatusBar>
#include <QToolBar>
#include <QToolButton>
#include <QVBoxLayout>

namespace {

const QStringList kExtensions{QStringLiteral("apk"), QStringLiteral("xapk"), QStringLiteral("apks"),
                              QStringLiteral("apkm"), QStringLiteral("jar"), QStringLiteral("aar"),
                              QStringLiteral("war")};

bool isApkLike(const QString &path)
{
    return QFileInfo(path).suffix().toLower().contains(QLatin1String("apk"));
}

} // namespace

MainWindow::MainWindow()
{
    setWindowTitle(QStringLiteral("ShiftDex"));
    setAcceptDrops(true);
    Config &config = Config::instance();
    resize(qMax(820, config.number(QStringLiteral("window_width"))),
           qMax(540, config.number(QStringLiteral("window_height"))));

    m_setup = new SetupPage;
    m_home = new HomePage;
    m_workspace = new Workspace;
    m_stack = new QStackedWidget;
    m_stack->addWidget(m_setup);
    m_stack->addWidget(m_home);
    m_stack->addWidget(m_workspace);
    setCentralWidget(m_stack);

    m_fileInfo = new QLabel;
    m_fileInfo->setContentsMargins(0, 0, 10, 0);
    statusBar()->addPermanentWidget(m_fileInfo);
    statusBar()->setSizeGripEnabled(false);

    buildActions();

    connect(m_setup, &SetupPage::finished, this, &MainWindow::showHome);
    connect(m_home, &HomePage::openFileRequested, this, &MainWindow::chooseFile);
    connect(m_home, &HomePage::projectChosen, this, &MainWindow::openProject);
    connect(m_home, &HomePage::pluginsRequested, this, [this] {
        PluginsDialog dialog(QJsonObject(), this);
        dialog.exec();
    });
    connect(m_workspace, &Workspace::stateChanged, this, &MainWindow::updateActions);
    connect(m_workspace, &Workspace::status, this, [this](const QString &message) {
        statusBar()->showMessage(message, 6000);
    });

    connect(&config, &Config::changed, this, [this](const QString &key) {
        if (key == QLatin1String("theme"))
            Theme::apply(*qApp, Config::instance().str(QStringLiteral("theme")));
        if (key == QLatin1String("theme") || key.startsWith(QLatin1String("editor_")))
            m_workspace->applySettings();
    });

    if (config.flag(QStringLiteral("setup_done"))) {
        showHome();
    } else {
        m_stack->setCurrentWidget(m_setup);
        menuBar()->hide();
        m_toolbar->hide();
        statusBar()->hide();
    }
    updateActions();
}

QAction *MainWindow::action(const QString &text, const QString &keymapId)
{
    auto *created = new QAction(text, this);
    if (!keymapId.isEmpty())
        Keymap::bind(created, keymapId);
    // shortcuts must work even though the action only lives in a menu
    addAction(created);
    return created;
}

void MainWindow::buildActions()
{
    m_open = action(tr("&Open APK or JAR…"), QStringLiteral("open-file"));
    m_save = action(tr("&Save"), QStringLiteral("save"));
    m_saveAll = action(tr("Save &All"), QStringLiteral("save-all"));
    m_closeTab = action(tr("&Close Tab"), QStringLiteral("close-tab"));
    m_back = action(tr("&Back to Projects"), QStringLiteral("go-back"));
    QAction *quit = action(tr("&Quit"), QStringLiteral("quit"));
    quit->setMenuRole(QAction::QuitRole);

    m_find = action(tr("&Find…"), QStringLiteral("find"));
    m_replace = action(tr("&Replace…"), QStringLiteral("replace"));
    m_goto = action(tr("&Go to Line…"), QStringLiteral("goto-line"));
    QAction *preferences = action(tr("&Preferences…"), QStringLiteral("preferences"));
    preferences->setMenuRole(QAction::PreferencesRole);

    m_build = action(tr("&Build…"), QStringLiteral("build"));
    m_identity = action(tr("App &Name, Icon and Package…"));
    m_backups = action(tr("Back&ups…"));
    m_snapshot = action(tr("Create &Snapshot…"), QStringLiteral("snapshot"));
    m_backupFile = action(tr("Back Up Current &File"), QStringLiteral("backup-file"));
    m_plugins = action(tr("&Plugins…"));
    m_searchFiles = action(tr("Search in &All Files…"), QStringLiteral("find-in-project"));
    m_quickOpen = action(tr("Find File by &Name…"), QStringLiteral("quick-open"));
    m_projectFolder = action(tr("Open Project F&older"));
    m_switchCode = action(tr("S&witch Between Smali and Java"), QStringLiteral("switch-code"));
    m_updateJava = action(tr("Update &Java View…"));
    m_sidebar = action(tr("Show or Hide the &Side Panel"), QStringLiteral("toggle-sidebar"));

    QAction *guide = action(tr("&Plugin Guide"));
    QAction *lastLog = action(tr("&Last Task Log"));
    QAction *about = action(tr("&About ShiftDex"));
    about->setMenuRole(QAction::AboutRole);
    QAction *aboutQt = action(tr("About &Qt"));
    aboutQt->setMenuRole(QAction::AboutQtRole);

    // the editor's own undo/redo/clipboard, reachable from the menu
    auto editorAction = [this](const QString &text, QKeySequence::StandardKey key, void (QPlainTextEdit::*slot)()) {
        auto *created = new QAction(text, this);
        created->setShortcut(key);
        // the editor already handles the key itself; this is only for the menu
        created->setShortcutContext(Qt::WidgetShortcut);
        connect(created, &QAction::triggered, this, [this, slot] {
            const DocTab *tab = m_workspace->currentTab();
            if (tab && tab->editor())
                (tab->editor()->*slot)();
        });
        m_editActions.append(created);
        return created;
    };
    QAction *undo = editorAction(tr("&Undo"), QKeySequence::Undo, &QPlainTextEdit::undo);
    QAction *redo = editorAction(tr("Re&do"), QKeySequence::Redo, &QPlainTextEdit::redo);
    QAction *cut = editorAction(tr("Cu&t"), QKeySequence::Cut, &QPlainTextEdit::cut);
    QAction *copy = editorAction(tr("&Copy"), QKeySequence::Copy, &QPlainTextEdit::copy);
    QAction *paste = editorAction(tr("&Paste"), QKeySequence::Paste, &QPlainTextEdit::paste);
    QAction *selectAll = editorAction(tr("Select &All"), QKeySequence::SelectAll, &QPlainTextEdit::selectAll);

    QMenu *file = menuBar()->addMenu(tr("&File"));
    file->addAction(m_open);
    file->addSeparator();
    file->addAction(m_save);
    file->addAction(m_saveAll);
    file->addAction(m_closeTab);
    file->addSeparator();
    file->addAction(m_back);
    file->addAction(quit);

    QMenu *edit = menuBar()->addMenu(tr("&Edit"));
    edit->addAction(undo);
    edit->addAction(redo);
    edit->addSeparator();
    edit->addAction(cut);
    edit->addAction(copy);
    edit->addAction(paste);
    edit->addAction(selectAll);
    edit->addSeparator();
    edit->addAction(m_find);
    edit->addAction(m_replace);
    edit->addAction(m_goto);
    edit->addSeparator();
    edit->addAction(preferences);

    QMenu *project = menuBar()->addMenu(tr("&Project"));
    project->addAction(m_build);
    project->addSeparator();
    project->addAction(m_identity);
    project->addAction(m_plugins);
    project->addSeparator();
    project->addAction(m_switchCode);
    project->addAction(m_updateJava);
    project->addSeparator();
    project->addAction(m_backups);
    project->addAction(m_snapshot);
    project->addAction(m_backupFile);
    project->addSeparator();
    project->addAction(m_searchFiles);
    project->addAction(m_quickOpen);
    project->addAction(m_projectFolder);

    QMenu *view = menuBar()->addMenu(tr("&View"));
    view->addAction(m_sidebar);
    view->addSeparator();
    QMenu *themes = view->addMenu(tr("&Theme"));
    auto *themeGroup = new QActionGroup(this);
    const QList<QPair<QString, QString>> themeChoices{
        {QStringLiteral("system"), tr("Follow the System")},
        {QStringLiteral("light"), tr("White")},
        {QStringLiteral("dark"), tr("Black")},
        {QStringLiteral("amoled"), tr("AMOLED")},
    };
    for (const auto &choice : themeChoices) {
        QAction *item = themes->addAction(choice.second);
        item->setCheckable(true);
        item->setData(choice.first);
        themeGroup->addAction(item);
        connect(item, &QAction::triggered, this,
                [choice] { Config::instance().set(QStringLiteral("theme"), choice.first); });
    }
    auto syncTheme = [themeGroup] {
        for (QAction *item : themeGroup->actions())
            item->setChecked(item->data().toString() == Config::instance().str(QStringLiteral("theme")));
    };
    syncTheme();
    connect(&Config::instance(), &Config::changed, this, [syncTheme](const QString &key) {
        if (key == QLatin1String("theme"))
            syncTheme();
    });

    QMenu *help = menuBar()->addMenu(tr("&Help"));
    help->addAction(guide);
    help->addAction(lastLog);
    help->addSeparator();
    help->addAction(about);
    help->addAction(aboutQt);

    m_toolbar = addToolBar(tr("Main Toolbar"));
    m_toolbar->setMovable(false);
    m_toolbar->setFloatable(false);
    m_toolbar->setToolButtonStyle(Qt::ToolButtonTextOnly);
    m_toolbar->setContextMenuPolicy(Qt::PreventContextMenu);
    auto addTool = [this](QAction *source, const QString &text) {
        // toolbar labels are shorter than the menu texts
        auto *button = new QToolButton;
        button->setText(text);
        button->setToolTip(source->text().remove(QLatin1Char('&')).remove(QStringLiteral("…")));
        button->setFocusPolicy(Qt::NoFocus);
        connect(button, &QToolButton::clicked, source, &QAction::trigger);
        connect(source, &QAction::changed, button, [button, source] { button->setEnabled(source->isEnabled()); });
        m_toolbar->addWidget(button);
        return button;
    };
    addTool(m_back, tr("← Projects"));
    m_toolbar->addSeparator();
    addTool(m_save, tr("Save"));
    addTool(m_saveAll, tr("Save All"));
    m_toolbar->addSeparator();
    addTool(m_identity, tr("Name, Icon, Package"));
    addTool(m_plugins, tr("Plugins"));
    addTool(m_backups, tr("Backups"));
    m_toolbar->addSeparator();
    addTool(m_switchCode, tr("Smali ⇄ Java"));
    auto *spacer = new QWidget;
    spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    m_toolbar->addWidget(spacer);
    addTool(m_build, tr("Build"))->setObjectName(QStringLiteral("primaryAction"));

    connect(m_open, &QAction::triggered, this, &MainWindow::chooseFile);
    connect(m_save, &QAction::triggered, m_workspace, &Workspace::saveCurrent);
    connect(m_saveAll, &QAction::triggered, m_workspace, &Workspace::saveAll);
    connect(m_closeTab, &QAction::triggered, m_workspace, &Workspace::closeCurrentTab);
    connect(m_back, &QAction::triggered, this, &MainWindow::goHome);
    connect(quit, &QAction::triggered, this, &QWidget::close);
    connect(m_find, &QAction::triggered, this, [this] { m_workspace->find(false); });
    connect(m_replace, &QAction::triggered, this, [this] { m_workspace->find(true); });
    connect(m_goto, &QAction::triggered, m_workspace, &Workspace::goToLine);
    connect(preferences, &QAction::triggered, this, [this] { showPreferences(); });
    connect(m_build, &QAction::triggered, m_workspace, &Workspace::build);
    connect(m_identity, &QAction::triggered, m_workspace, &Workspace::showIdentity);
    connect(m_backups, &QAction::triggered, m_workspace, &Workspace::showBackups);
    connect(m_snapshot, &QAction::triggered, m_workspace, &Workspace::createSnapshot);
    connect(m_backupFile, &QAction::triggered, m_workspace, &Workspace::backupCurrentFile);
    connect(m_plugins, &QAction::triggered, m_workspace, &Workspace::showPlugins);
    connect(m_searchFiles, &QAction::triggered, this, [this] { m_workspace->searchProject(false); });
    connect(m_quickOpen, &QAction::triggered, this, [this] { m_workspace->searchProject(true); });
    connect(m_projectFolder, &QAction::triggered, m_workspace, &Workspace::openProjectFolder);
    connect(m_sidebar, &QAction::triggered, m_workspace, &Workspace::toggleSidebar);
    connect(m_switchCode, &QAction::triggered, m_workspace, &Workspace::switchCode);
    connect(m_updateJava, &QAction::triggered, m_workspace, &Workspace::updateJavaView);
    connect(guide, &QAction::triggered, this, [this] { PluginsDialog::openGuide(this); });
    connect(lastLog, &QAction::triggered, this, &MainWindow::showLastLog);
    connect(about, &QAction::triggered, this, &MainWindow::showAbout);
    connect(aboutQt, &QAction::triggered, qApp, &QApplication::aboutQt);
}

bool MainWindow::inWorkspace() const
{
    return m_stack->currentWidget() == m_workspace;
}

void MainWindow::updateActions()
{
    const bool project = inWorkspace();
    const DocTab *tab = project ? m_workspace->currentTab() : nullptr;
    const bool editor = tab && tab->editor();
    const bool writable = editor && !tab->editor()->isReadOnly();

    m_save->setEnabled(tab && tab->isModified());
    m_saveAll->setEnabled(project && m_workspace->hasModified());
    m_closeTab->setEnabled(tab);
    m_back->setEnabled(project);
    m_find->setEnabled(editor);
    m_replace->setEnabled(writable);
    m_goto->setEnabled(editor);
    for (QAction *item : {m_build, m_backups, m_snapshot, m_searchFiles, m_quickOpen, m_projectFolder, m_sidebar,
                          m_switchCode, m_updateJava})
        item->setEnabled(project);
    m_identity->setEnabled(project && m_workspace->isApk());
    m_backupFile->setEnabled(tab && m_workspace->isProjectFile(tab->path()));
    m_plugins->setEnabled(m_stack->currentWidget() != m_setup);
    m_open->setEnabled(m_stack->currentWidget() != m_setup);
    for (QAction *item : std::as_const(m_editActions))
        item->setEnabled(editor);

    m_fileInfo->setText(tab ? tab->description() : QString());
    QString title = QStringLiteral("ShiftDex");
    if (project) {
        const QString name = m_workspace->project().value(QStringLiteral("name")).toString();
        title = tab ? QStringLiteral("%1 — %2 — ShiftDex").arg(QFileInfo(tab->path()).fileName(), name)
                    : QStringLiteral("%1 — ShiftDex").arg(name);
    }
    setWindowTitle(title);
}

// ---------------------------------------------------------------- navigation

void MainWindow::showHome()
{
    menuBar()->show();
    statusBar()->show();
    m_toolbar->hide();
    m_home->refresh();
    m_stack->setCurrentWidget(m_home);
    updateActions();
}

void MainWindow::goHome()
{
    if (!inWorkspace() || !m_workspace->confirmClose())
        return;
    m_workspace->closeProject();
    showHome();
}

void MainWindow::openProject(const QJsonObject &project)
{
    if (project.isEmpty())
        return;
    if (inWorkspace()) {
        if (!m_workspace->confirmClose())
            return;
        m_workspace->closeProject();
    }
    if (!QFileInfo(project.value(QStringLiteral("src")).toString()).isDir()) {
        Util::error(this, tr("Open Project"),
                    tr("The files of “%1” are missing. Use Backups to restore a snapshot or reset the project.")
                        .arg(project.value(QStringLiteral("name")).toString()));
    }
    m_stack->setCurrentWidget(m_workspace);
    m_toolbar->show();
    m_workspace->openProject(project);
    updateActions();
}

void MainWindow::chooseFile()
{
    QString folder = Config::instance().str(QStringLiteral("last_export_dir"));
    if (folder.isEmpty() || !QDir(folder).exists())
        folder = QDir::homePath();
    const QString file = QFileDialog::getOpenFileName(
        this, tr("Open APK or JAR"), folder,
        tr("Apps and archives (*.apk *.xapk *.apks *.apkm *.jar *.aar *.war);;Android apps (*.apk *.xapk *.apks *.apkm);;"
           "Java archives (*.jar *.aar *.war);;All files (*)"));
    if (!file.isEmpty())
        openPath(file);
}

void MainWindow::openPath(const QString &path)
{
    if (m_stack->currentWidget() == m_setup)
        return;
    const QFileInfo info(path);
    if (!info.isFile() || !kExtensions.contains(info.suffix().toLower())) {
        Util::error(this, tr("Open"), tr("%1 is not an APK or JAR file.").arg(info.fileName()));
        return;
    }
    if (inWorkspace() && !m_workspace->confirmClose())
        return;

    QJsonValue status;
    if (Engine::call({QStringLiteral("status")}, &status)) {
        const QJsonObject tools = status.toObject();
        QString missing;
        if (!tools.value(QStringLiteral("java_ok")).toBool())
            missing = tr("Java was not found. ShiftDex needs a Java runtime to unpack files.");
        else if (isApkLike(path) && tools.value(QStringLiteral("apkeditor")).toString().isEmpty()) {
            // fetch it and carry on; no reason to make the user go looking for a button
            const QString file = info.absoluteFilePath();
            JobDialog::run(this, tr("Downloading APKEditor"), {QStringLiteral("install-apkeditor")},
                           [this, file](bool ok, const QJsonValue &) {
                               if (ok)
                                   unpack(file);
                           });
            return;
        }
        if (!missing.isEmpty()) {
            QMessageBox box(QMessageBox::Warning, tr("Open"), missing, QMessageBox::Cancel, this);
            QPushButton *tools = box.addButton(tr("Open Tools Settings"), QMessageBox::AcceptRole);
            box.exec();
            if (box.clickedButton() == static_cast<QAbstractButton *>(tools))
                showPreferences(3);
            return;
        }
    }

    unpack(info.absoluteFilePath());
}

void MainWindow::unpack(const QString &path)
{
    JobDialog::run(this, tr("Unpacking %1").arg(QFileInfo(path).fileName()),
                   {QStringLiteral("create"), path},
                   [this](bool ok, const QJsonValue &result) {
                       if (ok)
                           openProject(result.toObject());
                       else if (!inWorkspace())
                           m_home->refresh();
                   });
}

// -------------------------------------------------------------------- dialogs

void MainWindow::showPreferences(int page)
{
    SettingsDialog dialog(this, page);
    dialog.exec();
}

void MainWindow::showLastLog()
{
    QDialog dialog(this);
    dialog.setWindowTitle(JobDialog::lastTitle().isEmpty() ? tr("Last Task Log") : JobDialog::lastTitle());
    dialog.resize(760, 480);
    auto *text = new QPlainTextEdit(JobDialog::lastLog().isEmpty() ? tr("No task has run yet.") : JobDialog::lastLog());
    text->setReadOnly(true);
    text->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    text->setLineWrapMode(QPlainTextEdit::NoWrap);
    auto *close = new QPushButton(tr("Close"));
    connect(close, &QPushButton::clicked, &dialog, &QDialog::accept);
    auto *layout = new QVBoxLayout(&dialog);
    layout->addWidget(text);
    layout->addWidget(close, 0, Qt::AlignRight);
    dialog.exec();
}

void MainWindow::showAbout()
{
    QDialog dialog(this);
    dialog.setWindowTitle(tr("About ShiftDex"));
    dialog.setMinimumWidth(480);

    auto *icon = new QLabel;
    icon->setPixmap(QIcon(QStringLiteral(":/icons/app.svg")).pixmap(72, 72));
    icon->setAlignment(Qt::AlignTop);
    auto *title = new QLabel(QStringLiteral("ShiftDex"));
    title->setObjectName(QStringLiteral("heading"));
    auto *version = new QLabel(tr("Version %1").arg(QStringLiteral(SHIFTDEX_VERSION)));
    version->setObjectName(QStringLiteral("muted"));
    auto *text = new QLabel(tr(
        "<p>Unpack, edit and repack APK and JAR files.</p>"
        "<p><b>Made by palaziks.</b><br>"
        "<a href=\"https://github.com/palazik/ShiftDex\">github.com/palazik/ShiftDex</a></p>"
        "<p>Copyright © 2026 palaziks. ShiftDex is free software: you can redistribute it and/or modify it "
        "under the terms of the <a href=\"https://www.gnu.org/licenses/gpl-3.0.html\">GNU General Public "
        "License</a> as published by the Free Software Foundation, either version 3 of the License, or (at "
        "your option) any later version.</p>"
        "<p>This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without "
        "even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.</p>"
        "<p>APK decoding and building is done by "
        "<a href=\"https://github.com/REAndroid/APKEditor\">APKEditor</a> by REAndroid (Apache-2.0). "
        "Signing can use <a href=\"https://github.com/patrickfav/uber-apk-signer\">uber-apk-signer</a> by "
        "Patrick Favre-Bulle (Apache-2.0). The interface is built with Qt %1.</p>").arg(QString::fromLatin1(qVersion())));
    text->setWordWrap(true);
    text->setOpenExternalLinks(true);
    text->setTextInteractionFlags(Qt::TextBrowserInteraction);
    // a wrapped label does not tell the dialog how tall it needs to be
    text->setFixedWidth(430);
    text->setMinimumHeight(text->heightForWidth(430));

    auto *right = new QVBoxLayout;
    right->setSpacing(2);
    right->addWidget(title);
    right->addWidget(version);
    right->addSpacing(6);
    right->addWidget(text);
    auto *top = new QHBoxLayout;
    top->setSpacing(18);
    top->addWidget(icon);
    top->addLayout(right, 1);

    auto *close = new QPushButton(tr("Close"));
    close->setDefault(true);
    connect(close, &QPushButton::clicked, &dialog, &QDialog::accept);
    auto *layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(20, 20, 20, 14);
    layout->addLayout(top);
    layout->addWidget(close, 0, Qt::AlignRight);
    dialog.exec();
}

// --------------------------------------------------------------------- events

void MainWindow::closeEvent(QCloseEvent *event)
{
    if (inWorkspace() && !m_workspace->confirmClose()) {
        event->ignore();
        return;
    }
    if (!isMaximized() && !isFullScreen()) {
        Config::instance().set(QStringLiteral("window_width"), width());
        Config::instance().set(QStringLiteral("window_height"), height());
    }
    event->accept();
}

void MainWindow::dragEnterEvent(QDragEnterEvent *event)
{
    if (m_stack->currentWidget() == m_setup || !event->mimeData()->hasUrls())
        return;
    for (const QUrl &url : event->mimeData()->urls()) {
        if (url.isLocalFile() && kExtensions.contains(QFileInfo(url.toLocalFile()).suffix().toLower())) {
            event->acceptProposedAction();
            return;
        }
    }
}

void MainWindow::dropEvent(QDropEvent *event)
{
    for (const QUrl &url : event->mimeData()->urls()) {
        if (url.isLocalFile() && kExtensions.contains(QFileInfo(url.toLocalFile()).suffix().toLower())) {
            event->acceptProposedAction();
            // let the drop finish before a modal dialog opens
            const QString path = url.toLocalFile();
            QMetaObject::invokeMethod(this, [this, path] { openPath(path); }, Qt::QueuedConnection);
            return;
        }
    }
}
