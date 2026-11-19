// ShiftDex - unpack, edit and repack APK and JAR files
// Copyright (C) 2026 palaziks
// SPDX-License-Identifier: GPL-3.0-or-later
#include "Workspace.h"

#include "BackupsDialog.h"
#include "CodeEditor.h"
#include "Config.h"
#include "Engine.h"
#include "IdentityDialog.h"
#include "JobDialog.h"
#include "Keymap.h"
#include "PluginsDialog.h"
#include "SearchPanel.h"
#include "Tabs.h"
#include "Util.h"

#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QDir>
#include <QFileDialog>
#include <QFileSystemModel>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QMenu>
#include <QSplitter>
#include <QStackedWidget>
#include <QTabBar>
#include <QTabWidget>
#include <QTimer>
#include <QTreeView>
#include <QVBoxLayout>

namespace {

QString validName(const QString &name)
{
    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty() || trimmed == QLatin1String(".") || trimmed == QLatin1String("..")
        || trimmed.contains(QLatin1Char('/')) || trimmed.contains(QChar(0)))
        return {};
    return trimmed;
}

} // namespace

Workspace::Workspace(QWidget *parent) : QWidget(parent)
{
    // ---- project header
    m_icon = new QLabel;
    m_icon->setFixedSize(32, 32);
    m_title = new QLabel;
    m_title->setObjectName(QStringLiteral("subheading"));
    m_subtitle = new QLabel;
    m_subtitle->setObjectName(QStringLiteral("muted"));
    m_subtitle->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_kind = new QLabel;
    m_kind->setObjectName(QStringLiteral("tag"));
    m_codeTag = new QLabel;
    m_codeTag->setObjectName(QStringLiteral("tag"));
    m_codeTag->setToolTip(tr("How this project's code is shown. Change it with Code view in the side panel."));

    auto *header = new QFrame;
    header->setObjectName(QStringLiteral("bar"));
    auto *headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(12, 7, 12, 7);
    headerLayout->setSpacing(10);
    headerLayout->addWidget(m_icon);
    headerLayout->addWidget(m_title);
    headerLayout->addWidget(m_kind);
    headerLayout->addWidget(m_codeTag);
    headerLayout->addWidget(m_subtitle, 1);

    // ---- side panel
    m_tree = new QTreeView;
    m_tree->setObjectName(QStringLiteral("borderless"));
    m_tree->setHeaderHidden(true);
    m_tree->setUniformRowHeights(true);
    m_tree->setAnimated(false);
    m_tree->setIndentation(16);
    m_tree->setFrameShape(QFrame::NoFrame);
    m_tree->setContextMenuPolicy(Qt::CustomContextMenu);
    m_tree->setEditTriggers(QAbstractItemView::NoEditTriggers);
    connect(m_tree, &QTreeView::customContextMenuRequested, this, &Workspace::treeMenu);
    connect(m_tree, &QTreeView::activated, this, [this](const QModelIndex &index) {
        if (m_model && !m_model->isDir(index))
            openFile(m_model->filePath(index));
    });

    m_search = new SearchPanel;
    connect(m_search, &SearchPanel::openRequested, this, &Workspace::openFile);

    m_side = new QTabWidget;
    m_side->setDocumentMode(true);
    m_side->addTab(m_tree, tr("Files"));
    m_side->addTab(m_search, tr("Search"));

    // code view switch, always visible above the file list
    m_viewCombo = new QComboBox;
    m_viewCombo->setToolTip(tr("Smali is the code you edit and build. Java is the same code decompiled for "
                               "reading; it cannot be edited."));
    connect(m_viewCombo, &QComboBox::activated, this,
            [this](int index) { setCodeView(m_viewCombo->itemData(index).toString()); });
    auto *viewBar = new QFrame;
    viewBar->setObjectName(QStringLiteral("bar"));
    auto *viewLayout = new QHBoxLayout(viewBar);
    viewLayout->setContentsMargins(8, 6, 8, 6);
    viewLayout->addWidget(new QLabel(tr("Code view")));
    viewLayout->addWidget(m_viewCombo, 1);

    m_sidePanel = new QWidget;
    auto *sideLayout = new QVBoxLayout(m_sidePanel);
    sideLayout->setContentsMargins(0, 0, 0, 0);
    sideLayout->setSpacing(0);
    sideLayout->addWidget(viewBar);
    sideLayout->addWidget(m_side, 1);
    m_sidePanel->setMinimumWidth(200);

    // ---- editor area
    m_tabs = new QTabWidget;
    m_tabs->setDocumentMode(true);
    m_tabs->setTabsClosable(true);
    m_tabs->setMovable(true);
    m_tabs->tabBar()->setElideMode(Qt::ElideMiddle);
    m_tabs->tabBar()->setExpanding(false);
    connect(m_tabs, &QTabWidget::tabCloseRequested, this, [this](int index) { closeTab(index); });
    connect(m_tabs, &QTabWidget::currentChanged, this, [this] {
        m_stack->setCurrentIndex(m_tabs->count() ? 1 : 0);
        if (const DocTab *tab = currentTab())
            reveal(tab->path());
        emit stateChanged();
    });

    auto *empty = new QWidget;
    auto *emptyLayout = new QVBoxLayout(empty);
    auto *emptyTitle = new QLabel(tr("No file open"));
    emptyTitle->setObjectName(QStringLiteral("subheading"));
    emptyTitle->setAlignment(Qt::AlignCenter);
    auto *emptyText = new QLabel;
    emptyText->setObjectName(QStringLiteral("muted"));
    emptyText->setAlignment(Qt::AlignCenter);
    emptyText->setTextFormat(Qt::PlainText);
    auto refreshHint = [emptyText] {
        emptyText->setText(Workspace::tr("Pick a file from the list on the left.\n\n"
                                         "%1   find a file by name\n%2   search in all files\n%3   build")
                               .arg(Keymap::current(QStringLiteral("quick-open")).toString(QKeySequence::NativeText),
                                    Keymap::current(QStringLiteral("find-in-project")).toString(QKeySequence::NativeText),
                                    Keymap::current(QStringLiteral("build")).toString(QKeySequence::NativeText)));
    };
    refreshHint();
    connect(&Config::instance(), &Config::changed, emptyText, [refreshHint](const QString &key) {
        if (key == QLatin1String("keymap"))
            refreshHint();
    });
    emptyLayout->addStretch();
    emptyLayout->addWidget(emptyTitle);
    emptyLayout->addWidget(emptyText);
    emptyLayout->addStretch();

    m_stack = new QStackedWidget;
    m_stack->addWidget(empty);
    m_stack->addWidget(m_tabs);

    m_splitter = new QSplitter(Qt::Horizontal);
    m_splitter->addWidget(m_sidePanel);
    m_splitter->addWidget(m_stack);
    m_splitter->setStretchFactor(0, 0);
    m_splitter->setStretchFactor(1, 1);
    m_splitter->setChildrenCollapsible(false);
    m_splitter->setSizes({Config::instance().number(QStringLiteral("sidebar_width")), 900});
    connect(m_splitter, &QSplitter::splitterMoved, this, [this] {
        if (m_sidePanel->isVisible())
            Config::instance().set(QStringLiteral("sidebar_width"), m_splitter->sizes().value(0));
    });

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(header);
    layout->addWidget(m_splitter, 1);
}

// ------------------------------------------------------------------ project

QString Workspace::src() const { return m_project.value(QStringLiteral("src")).toString(); }

bool Workspace::isApk() const { return m_project.value(QStringLiteral("kind")).toString() == QLatin1String("apk"); }

QString Workspace::javaDir() const { return m_project.value(QStringLiteral("java_dir")).toString(); }

QString Workspace::treeRoot() const
{
    return m_codeView == QLatin1String("java") && !javaDir().isEmpty() ? javaDir() : src();
}

bool Workspace::isProjectFile(const QString &path) const
{
    return !src().isEmpty() && path.startsWith(src() + QLatin1Char('/'));
}

QString Workspace::relative(const QString &path) const
{
    if (!javaDir().isEmpty() && path.startsWith(javaDir() + QLatin1Char('/')))
        return QStringLiteral("java/") + QDir(javaDir()).relativeFilePath(path);
    return QDir(src()).relativeFilePath(path);
}

void Workspace::openProject(const QJsonObject &project)
{
    closeProject();
    m_project = project;
    m_backedUp.clear();
    // a project remembers its code view; fall back to smali if the Java is gone
    m_codeView = project.value(QStringLiteral("code_view")).toString() == QLatin1String("java") && !javaDir().isEmpty()
        ? QStringLiteral("java") : QStringLiteral("smali");
    m_viewCombo->clear();
    m_viewCombo->addItem(isApk() ? tr("Smali (editable)") : tr("Class files"), QStringLiteral("smali"));
    m_viewCombo->addItem(tr("Java (read-only)"), QStringLiteral("java"));
    syncViewControls();
    resetTree();
    m_search->setRoot(treeRoot());
    m_side->setCurrentIndex(0);
    m_sidePanel->show();
    updateHeader();

    // open the file people look at first
    const QString manifest = src() + (isApk() ? QStringLiteral("/AndroidManifest.xml")
                                              : QStringLiteral("/META-INF/MANIFEST.MF"));
    if (QFileInfo::exists(manifest))
        openFile(manifest);
    emit stateChanged();
}

void Workspace::closeProject()
{
    while (m_tabs->count())
        closeTab(0, false);
    m_project = QJsonObject();
    m_search->setRoot(QString());
    m_tree->setModel(nullptr);
    delete m_model;
    m_model = nullptr;
}

void Workspace::setProject(const QJsonObject &project)
{
    if (project.isEmpty())
        return;
    m_project = project;
    updateHeader();
    emit projectUpdated(project);
    emit stateChanged();
}

void Workspace::updateHeader()
{
    const QString label = m_project.value(QStringLiteral("label")).toString();
    const QString name = m_project.value(QStringLiteral("name")).toString();
    m_title->setText(label.isEmpty() ? name : label);
    m_kind->setText(m_project.value(QStringLiteral("kind")).toString().toUpper());
    syncViewControls();

    QStringList parts;
    const QString package = m_project.value(QStringLiteral("package")).toString();
    const QString version = m_project.value(QStringLiteral("version_name")).toString();
    const QString mainClass = m_project.value(QStringLiteral("main_class")).toString();
    if (!package.isEmpty())
        parts << package;
    if (!version.isEmpty())
        parts << tr("version %1").arg(version);
    if (!mainClass.isEmpty())
        parts << tr("main class %1").arg(mainClass);
    if (parts.isEmpty())
        parts << m_project.value(QStringLiteral("original_name")).toString();
    m_subtitle->setText(parts.join(QStringLiteral("   ·   ")));

    QPixmap pixmap(m_project.value(QStringLiteral("icon_path")).toString());
    if (pixmap.isNull())
        pixmap = QPixmap(QStringLiteral(":/icons/app.svg"));
    m_icon->setPixmap(pixmap.scaled(32, 32, Qt::KeepAspectRatio, Qt::SmoothTransformation));
}

// Keeps the file list pointing at the file being edited.
void Workspace::reveal(const QString &path)
{
    if (!m_model)
        return;
    const QModelIndex index = m_model->index(path);
    if (!index.isValid())
        return;
    m_tree->setCurrentIndex(index);
    m_tree->scrollTo(index, QAbstractItemView::EnsureVisible);
    // folders load in the background; scroll again once this one's rows exist
    m_pendingReveal = path;
}

void Workspace::resetTree()
{
    // a restored snapshot replaces the folder itself, which the old model
    // would keep watching by inode; start over with a fresh one
    QFileSystemModel *old = m_model;
    m_model = new QFileSystemModel(this);
    m_model->setReadOnly(true);
    m_model->setRootPath(treeRoot());
    m_tree->setModel(m_model);
    m_tree->setRootIndex(m_model->index(treeRoot()));
    for (int column = 1; column < m_model->columnCount(); column++)
        m_tree->hideColumn(column);
    m_tree->sortByColumn(0, Qt::AscendingOrder);
    connect(m_model, &QFileSystemModel::directoryLoaded, this, [this](const QString &directory) {
        if (m_pendingReveal.isEmpty() || !m_pendingReveal.startsWith(directory + QLatin1Char('/')))
            return;
        const bool last = QFileInfo(m_pendingReveal).absolutePath() == directory;
        const QString path = m_pendingReveal;
        if (last)
            m_pendingReveal.clear();
        // the model sorts and inserts the new rows a moment after this signal
        QTimer::singleShot(80, m_tree, [this, path] {
            const QModelIndex index = m_model ? m_model->index(path) : QModelIndex();
            if (index.isValid())
                m_tree->scrollTo(index, QAbstractItemView::PositionAtCenter);
        });
    });
    delete old;
}

// --------------------------------------------------------------------- tabs

DocTab *Workspace::tabAt(int index) const { return qobject_cast<DocTab *>(m_tabs->widget(index)); }
DocTab *Workspace::currentTab() const { return qobject_cast<DocTab *>(m_tabs->currentWidget()); }

DocTab *Workspace::tabFor(const QString &path) const
{
    for (int i = 0; i < m_tabs->count(); i++) {
        if (tabAt(i)->path() == path)
            return tabAt(i);
    }
    return nullptr;
}

bool Workspace::hasModified() const
{
    for (int i = 0; i < m_tabs->count(); i++) {
        if (tabAt(i)->isModified())
            return true;
    }
    return false;
}

void Workspace::updateTabTitle(DocTab *tab)
{
    const int index = m_tabs->indexOf(tab);
    if (index < 0)
        return;
    const QString name = QFileInfo(tab->path()).fileName();
    m_tabs->setTabText(index, tab->isModified() ? name + QStringLiteral(" •") : name);
    m_tabs->setTabToolTip(index, relative(tab->path()));
}

void Workspace::openFile(const QString &path, int line, int column)
{
    const QFileInfo info(path);
    if (!info.isFile()) {
        emit status(tr("%1 no longer exists.").arg(relative(path)));
        return;
    }
    DocTab *tab = tabFor(info.absoluteFilePath());
    if (!tab) {
        QApplication::setOverrideCursor(Qt::WaitCursor);
        if (isProjectFile(info.absoluteFilePath()) || javaDir().isEmpty()) {
            tab = Tabs::open(info.absoluteFilePath(), m_tabs);
        } else {
            const QString key = Keymap::current(QStringLiteral("switch-code")).toString(QKeySequence::NativeText);
            tab = TextTab::readOnlyFile(
                info.absoluteFilePath(),
                isApk() ? tr("Java decompiled by jadx, read-only. To change this code, edit its smali (%1 jumps there).").arg(key)
                        : tr("Java decompiled by jadx, read-only. Class files cannot be edited as text (%1 jumps to the class file).").arg(key),
                QStringLiteral("Java"), m_tabs);
        }
        QApplication::restoreOverrideCursor();
        m_tabs->addTab(tab, QString());
        updateTabTitle(tab);
        connect(tab, &DocTab::modifiedChanged, this, [this, tab] {
            updateTabTitle(tab);
            emit stateChanged();
        });
        connect(tab, &DocTab::cursorMoved, this, [this, tab] {
            if (tab == currentTab())
                emit stateChanged();
        });
        if (auto *image = qobject_cast<ImageTab *>(tab))
            connect(image, &ImageTab::replaceRequested, this, &Workspace::replaceFile);
    }
    m_tabs->setCurrentWidget(tab);
    if (line > 0 && tab->editor())
        tab->editor()->goToLine(line, qMax(1, column));
    tab->setFocus();
    emit stateChanged();
}

bool Workspace::saveTab(DocTab *tab)
{
    if (!tab || !tab->isModified())
        return true;
    if (Config::instance().flag(QStringLiteral("backup_on_save")) && !m_backedUp.contains(tab->path())) {
        // best effort: a failed backup must not block saving
        Engine::call({QStringLiteral("file-backup"), m_project.value(QStringLiteral("dir")).toString(),
                      relative(tab->path())});
        m_backedUp.insert(tab->path());
    }
    QString error;
    if (!tab->save(&error)) {
        Util::error(this, tr("Save"), tr("%1 could not be saved: %2").arg(relative(tab->path()), error));
        return false;
    }
    return true;
}

bool Workspace::saveCurrent()
{
    DocTab *tab = currentTab();
    if (!saveTab(tab))
        return false;
    if (tab)
        emit status(tr("Saved %1").arg(relative(tab->path())));
    return true;
}

bool Workspace::saveAll()
{
    int saved = 0;
    for (int i = 0; i < m_tabs->count(); i++) {
        if (!tabAt(i)->isModified())
            continue;
        if (!saveTab(tabAt(i)))
            return false;
        saved++;
    }
    if (saved)
        emit status(tr("Saved %1").arg(Util::count(saved, tr("file"), tr("files"))));
    return true;
}

bool Workspace::confirmClose()
{
    int modified = 0;
    for (int i = 0; i < m_tabs->count(); i++)
        modified += tabAt(i)->isModified() ? 1 : 0;
    if (!modified)
        return true;
    const auto choice = QMessageBox::question(
        this, tr("Unsaved Changes"), modified == 1 ? tr("1 file has unsaved changes. Save it?")
                                           : tr("%1 files have unsaved changes. Save them?").arg(modified),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
    if (choice == QMessageBox::Cancel)
        return false;
    return choice == QMessageBox::Discard || saveAll();
}

void Workspace::closeTab(int index, bool ask)
{
    DocTab *tab = tabAt(index);
    if (!tab)
        return;
    if (ask && tab->isModified()) {
        m_tabs->setCurrentIndex(index);
        const auto choice = QMessageBox::question(
            this, tr("Unsaved Changes"), tr("Save the changes to %1?").arg(QFileInfo(tab->path()).fileName()),
            QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
        if (choice == QMessageBox::Cancel || (choice == QMessageBox::Save && !saveTab(tab)))
            return;
    }
    m_tabs->removeTab(index);
    tab->deleteLater();
    m_stack->setCurrentIndex(m_tabs->count() ? 1 : 0);
    emit stateChanged();
}

void Workspace::closeCurrentTab()
{
    if (m_tabs->count())
        closeTab(m_tabs->currentIndex());
}

void Workspace::reloadTabs()
{
    for (int i = m_tabs->count() - 1; i >= 0; i--) {
        if (!QFileInfo::exists(tabAt(i)->path()) || !tabAt(i)->reload())
            closeTab(i, false);
        else
            updateTabTitle(tabAt(i));
    }
    emit stateChanged();
}

void Workspace::applySettings()
{
    for (int i = 0; i < m_tabs->count(); i++)
        tabAt(i)->applySettings();
}

// ------------------------------------------------------------- edit actions

void Workspace::find(bool withReplace)
{
    if (DocTab *tab = currentTab())
        tab->showFind(withReplace);
}

void Workspace::goToLine()
{
    DocTab *tab = currentTab();
    if (!tab || !tab->editor())
        return;
    bool ok = false;
    const int lines = tab->editor()->blockCount();
    const int line = QInputDialog::getInt(this, tr("Go to Line"), tr("Line number (1 – %1):").arg(lines),
                                          tab->editor()->textCursor().blockNumber() + 1, 1, lines, 1, &ok);
    if (ok) {
        tab->editor()->goToLine(line);
        tab->editor()->setFocus();
    }
}

void Workspace::searchProject(bool fileNames)
{
    m_sidePanel->show();
    m_side->setCurrentWidget(m_search);
    m_search->activate(fileNames);
}

void Workspace::toggleSidebar()
{
    m_sidePanel->setVisible(!m_sidePanel->isVisible());
}

// ---------------------------------------------------------- project actions

void Workspace::build()
{
    if (m_project.isEmpty() || !saveAll())
        return;
    Config &config = Config::instance();
    QString folder = config.str(QStringLiteral("last_export_dir"));
    if (folder.isEmpty() || !QDir(folder).exists())
        folder = QFileInfo(m_project.value(QStringLiteral("source_path")).toString()).absolutePath();
    if (!QDir(folder).exists())
        folder = QDir::homePath();
    const QString suggested = folder + QLatin1Char('/') + m_project.value(QStringLiteral("default_output")).toString();
    const QString suffix = QFileInfo(suggested).suffix();
    QString dest = QFileDialog::getSaveFileName(
        this, tr("Build"), suggested,
        isApk() ? tr("Android packages (*.apk)") : tr("Java archives (*.%1)").arg(suffix));
    if (dest.isEmpty())
        return;
    if (QFileInfo(dest).suffix().isEmpty())
        dest += QLatin1Char('.') + suffix;
    config.set(QStringLiteral("last_export_dir"), QFileInfo(dest).absolutePath());

    JobDialog::run(this, tr("Building %1").arg(QFileInfo(dest).fileName()),
                   {QStringLiteral("build"), m_project.value(QStringLiteral("dir")).toString(), dest},
                   [this](bool ok, const QJsonValue &value) {
                       if (!ok)
                           return;
                       const QJsonObject result = value.toObject();
                       setProject(result.value(QStringLiteral("project")).toObject());
                       const QString path = result.value(QStringLiteral("path")).toString();
                       QString text = tr("%1\n%2").arg(path, Util::size(result.value(QStringLiteral("size")).toDouble()));
                       if (isApk()) {
                           text += QStringLiteral("\n\n");
                           text += result.value(QStringLiteral("signed")).toBool()
                               ? tr("The APK is signed and ready to install. Because it is signed with a "
                                    "different key than the original, uninstall the original app first.")
                               : tr("The APK is not signed. Android will not install it until it is signed.");
                       }
                       QMessageBox box(QMessageBox::Information, tr("Build Finished"), text, QMessageBox::Ok, this);
                       QPushButton *show = box.addButton(tr("Open Folder"), QMessageBox::ActionRole);
                       box.exec();
                       if (box.clickedButton() == static_cast<QAbstractButton *>(show))
                           Util::openFolder(QFileInfo(path).absolutePath());
                   });
}

void Workspace::createSnapshot()
{
    if (m_project.isEmpty() || !saveAll())
        return;
    bool ok = false;
    const QString note = QInputDialog::getText(this, tr("Create Snapshot"),
                                               tr("Note (optional), so you can tell snapshots apart:"),
                                               QLineEdit::Normal, QString(), &ok);
    if (!ok)
        return;
    QStringList arguments{QStringLiteral("snapshot"), m_project.value(QStringLiteral("dir")).toString()};
    if (!note.trimmed().isEmpty())
        arguments << QStringLiteral("--note") << note.trimmed();
    JobDialog::run(this, tr("Creating Snapshot"), arguments, [this](bool done, const QJsonValue &) {
        if (done)
            emit status(tr("Snapshot created. Find it under Project > Backups."));
    });
}

void Workspace::backupCurrentFile()
{
    DocTab *tab = currentTab();
    if (!tab || !isProjectFile(tab->path()) || !saveTab(tab))
        return;
    QString error;
    if (!Engine::call({QStringLiteral("file-backup"), m_project.value(QStringLiteral("dir")).toString(),
                       relative(tab->path())},
                      nullptr, &error)) {
        Util::error(this, tr("Back Up File"), error);
        return;
    }
    m_backedUp.insert(tab->path());
    emit status(tr("Backed up %1").arg(relative(tab->path())));
}

void Workspace::showBackups()
{
    if (m_project.isEmpty() || !saveAll())
        return;
    const DocTab *tab = currentTab();
    BackupsDialog dialog(m_project, tab && isProjectFile(tab->path()) ? relative(tab->path()) : QString(), this);
    connect(&dialog, &BackupsDialog::projectReplaced, this, [this](const QJsonObject &project) {
        setProject(project);
        resetTree();
        reloadTabs();
    });
    connect(&dialog, &BackupsDialog::fileRestored, this, [this](const QString &rel) {
        if (DocTab *open = tabFor(src() + QLatin1Char('/') + rel))
            open->reload();
        emit status(tr("Restored %1").arg(rel));
    });
    dialog.exec();
}

void Workspace::showPlugins()
{
    if (!m_project.isEmpty() && !saveAll())
        return;
    PluginsDialog dialog(m_project, this);
    connect(&dialog, &PluginsDialog::projectChanged, this, [this](const QJsonObject &project) {
        setProject(project);
        reloadTabs();
    });
    dialog.exec();
}

void Workspace::showIdentity()
{
    if (m_project.isEmpty())
        return;
    if (!m_project.value(QStringLiteral("can_edit_identity")).toBool()) {
        Util::error(this, tr("App Name, Icon and Package"),
                    isApk() ? tr("This APK was unpacked without an XML manifest, so its name, icon and package "
                                 "cannot be changed here. Set “Decode resources as” to XML in Preferences > "
                                 "Building and open the APK again.")
                            : tr("Only Android apps have a name, icon and package name to change. For a JAR, "
                                 "edit META-INF/MANIFEST.MF."));
        return;
    }
    if (!saveAll())
        return;
    IdentityDialog dialog(m_project, this);
    connect(&dialog, &IdentityDialog::applied, this, [this](const QJsonObject &project) {
        setProject(project);
        reloadTabs();
        emit status(tr("App details updated. Build the project to get the new APK."));
    });
    dialog.exec();
}

void Workspace::openProjectFolder()
{
    if (!m_project.isEmpty())
        Util::openFolder(src());
}

// ---------------------------------------------------------------- code view

void Workspace::syncViewControls()
{
    const bool java = m_codeView == QLatin1String("java");
    m_viewCombo->setCurrentIndex(qMax(0, m_viewCombo->findData(m_codeView)));
    m_codeTag->setText(java ? tr("JAVA VIEW") : (isApk() ? tr("SMALI") : tr("BYTECODE")));
    m_codeTag->setVisible(!m_project.isEmpty());
}

void Workspace::setCodeView(const QString &view)
{
    if (m_project.isEmpty() || view == m_codeView) {
        syncViewControls();
        return;
    }
    if (view == QLatin1String("java") && javaDir().isEmpty()) {
        // nothing to show yet; come back here once the Java exists
        syncViewControls();
        ensureJava([this] { setCodeView(QStringLiteral("java")); });
        return;
    }
    m_codeView = view;
    QJsonValue updated;
    if (Engine::call({QStringLiteral("code-view"), m_project.value(QStringLiteral("dir")).toString(), view}, &updated))
        m_project = updated.toObject();
    applyCodeView();
    emit projectUpdated(m_project);
    emit status(view == QLatin1String("java")
                    ? tr("Java view: read-only. Switch back to edit the code.")
                    : (isApk() ? tr("Smali view: this is the code that gets built.") : tr("Class file view.")));
}

void Workspace::applyCodeView()
{
    syncViewControls();
    resetTree();
    m_search->setRoot(treeRoot());
    if (const DocTab *tab = currentTab())
        reveal(tab->path());
    emit stateChanged();
}

void Workspace::decompileJava(bool fromCurrent, std::function<void()> then)
{
    QStringList arguments{QStringLiteral("java-decompile"), m_project.value(QStringLiteral("dir")).toString()};
    if (fromCurrent)
        arguments << QStringLiteral("--current");
    JobDialog::run(this, tr("Decompiling to Java"), arguments, [this, then](bool ok, const QJsonValue &result) {
        if (!ok)
            return;
        setProject(result.toObject());
        if (then)
            then();
    });
}

void Workspace::ensureJava(std::function<void()> then)
{
    QJsonValue status;
    Engine::call({QStringLiteral("status")}, &status);
    if (!status.toObject().value(QStringLiteral("jadx")).toString().isEmpty()) {
        decompileJava(false, then);
        return;
    }
    QMessageBox box(QMessageBox::Question, tr("Java View"),
                    tr("The Java view is made by jadx, which is not installed yet.\n\n"
                       "It is a 72 MB download from github.com/skylot/jadx, stored in your user folder and "
                       "only needed for the Java view. Smali works without it."),
                    QMessageBox::Cancel, this);
    QPushButton *download = box.addButton(tr("Download jadx"), QMessageBox::AcceptRole);
    box.exec();
    if (box.clickedButton() != static_cast<QAbstractButton *>(download))
        return;
    JobDialog::run(this, tr("Downloading jadx"), {QStringLiteral("install-jadx")},
                   [this, then](bool ok, const QJsonValue &) {
                       if (ok)
                           decompileJava(false, then);
                   });
}

// The same class in the other form: a/b/C$D.smali <-> a/b/C.java
QString Workspace::counterpart(const QString &path) const
{
    const QString java = javaDir();
    if (!java.isEmpty() && path.startsWith(java + QLatin1Char('/'))) {
        if (!path.endsWith(QLatin1String(".java")))
            return {};
        const QString stem = QDir(java).relativeFilePath(path).chopped(5);
        if (!isApk()) {
            const QString classFile = src() + QLatin1Char('/') + stem + QStringLiteral(".class");
            return QFileInfo::exists(classFile) ? classFile : QString();
        }
        // one folder per dex file: smali/classes, smali/classes2, ...
        const QDir smali(src() + QStringLiteral("/smali"));
        for (const QString &dex : smali.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
            const QString candidate = smali.filePath(dex + QLatin1Char('/') + stem + QStringLiteral(".smali"));
            if (QFileInfo::exists(candidate))
                return candidate;
        }
        return {};
    }
    if (java.isEmpty() || !isProjectFile(path))
        return {};
    QString stem = QDir(src()).relativeFilePath(path);
    if (isApk()) {
        if (!stem.startsWith(QLatin1String("smali/")) || !stem.endsWith(QLatin1String(".smali")))
            return {};
        stem = stem.section(QLatin1Char('/'), 2).chopped(6);
    } else {
        if (!stem.endsWith(QLatin1String(".class")))
            return {};
        stem.chop(6);
    }
    // inner and synthetic classes live in their outer class's Java file
    const int slash = stem.lastIndexOf(QLatin1Char('/'));
    const int dollar = stem.indexOf(QLatin1Char('$'), slash + 1);
    if (dollar > slash + 1)
        stem.truncate(dollar);
    const QString candidate = java + QLatin1Char('/') + stem + QStringLiteral(".java");
    return QFileInfo::exists(candidate) ? candidate : QString();
}

void Workspace::switchCode()
{
    if (m_project.isEmpty())
        return;
    const DocTab *tab = currentTab();
    if (tab && javaDir().isEmpty() && isProjectFile(tab->path())) {
        ensureJava([this] { switchCode(); });
        return;
    }
    const QString other = tab ? counterpart(tab->path()) : QString();
    if (other.isEmpty()) {
        // no matching file to jump to: just flip the view
        setCodeView(m_codeView == QLatin1String("java") ? QStringLiteral("smali") : QStringLiteral("java"));
        return;
    }
    setCodeView(isProjectFile(other) ? QStringLiteral("smali") : QStringLiteral("java"));
    openFile(other);
}

void Workspace::updateJavaView()
{
    if (m_project.isEmpty() || !saveAll())
        return;
    if (javaDir().isEmpty()) {
        ensureJava([this] { setCodeView(QStringLiteral("java")); });
        return;
    }
    QMessageBox box(QMessageBox::Question, tr("Update Java View"),
                    tr("Decompile the Java again?\n\n"
                       "From the current project, the Java shows the changes you made. This builds the "
                       "project first, so it takes longer.\n"
                       "From the original file, it shows the code as it was when you opened it."),
                    QMessageBox::Cancel, this);
    QPushButton *current = box.addButton(tr("Current Project"), QMessageBox::AcceptRole);
    QPushButton *original = box.addButton(tr("Original File"), QMessageBox::ActionRole);
    box.setDefaultButton(current);
    box.exec();
    if (box.clickedButton() != static_cast<QAbstractButton *>(current)
        && box.clickedButton() != static_cast<QAbstractButton *>(original))
        return;
    decompileJava(box.clickedButton() == static_cast<QAbstractButton *>(current), [this] {
        // the Java folder was replaced as a whole
        if (m_codeView == QLatin1String("java"))
            applyCodeView();
        reloadTabs();
        emit status(tr("Java view updated."));
    });
}

// ---------------------------------------------------------------- file tree

void Workspace::pathChanged(const QString &from, const QString &to)
{
    for (int i = 0; i < m_tabs->count(); i++) {
        DocTab *tab = tabAt(i);
        if (tab->path() == from)
            tab->setPath(to);
        else if (tab->path().startsWith(from + QLatin1Char('/')))
            tab->setPath(to + tab->path().mid(from.length()));
        else
            continue;
        updateTabTitle(tab);
    }
}

void Workspace::pathRemoved(const QString &path)
{
    for (int i = m_tabs->count() - 1; i >= 0; i--) {
        const QString open = tabAt(i)->path();
        if (open == path || open.startsWith(path + QLatin1Char('/')))
            closeTab(i, false);
    }
}

void Workspace::replaceFile(const QString &target)
{
    const QFileInfo info(target);
    const QString source = QFileDialog::getOpenFileName(this, tr("Replace %1").arg(info.fileName()), QDir::homePath());
    if (source.isEmpty())
        return;
    if (QFileInfo(source).suffix().toLower() != info.suffix().toLower()
        && !Util::confirm(this, tr("Replace File"),
                          tr("%1 is not a .%2 file. The content is copied as it is and the name stays %3. Continue?")
                              .arg(QFileInfo(source).fileName(), info.suffix(), info.fileName()),
                          tr("Replace")))
        return;
    if (DocTab *open = tabFor(target); open && open->isModified()) {
        if (!Util::confirm(this, tr("Replace File"), tr("Unsaved changes to %1 will be lost.").arg(info.fileName()),
                           tr("Replace")))
            return;
    }
    QFile::remove(target);
    if (!QFile::copy(source, target)) {
        Util::error(this, tr("Replace File"), tr("%1 could not be written.").arg(relative(target)));
        return;
    }
    QFile::setPermissions(target, QFile::permissions(target) | QFile::WriteOwner);
    if (DocTab *open = tabFor(target))
        open->reload();
    emit status(tr("Replaced %1").arg(relative(target)));
}

void Workspace::treeMenu(const QPoint &position)
{
    if (!m_model)
        return;
    const QModelIndex index = m_tree->indexAt(position);
    const QString path = index.isValid() ? m_model->filePath(index) : treeRoot();
    const QFileInfo info(path);

    if (m_codeView == QLatin1String("java")) {
        // the Java view is read-only: nothing here can change files
        QMenu menu(this);
        QAction *open = info.isFile() ? menu.addAction(tr("Open")) : nullptr;
        const QString other = info.isFile() ? counterpart(path) : QString();
        QAction *smali = other.isEmpty() ? nullptr : menu.addAction(isApk() ? tr("Open Its Smali") : tr("Open Its Class File"));
        if (open)
            menu.addSeparator();
        QAction *copyPath = menu.addAction(tr("Copy Path"));
        QAction *chosen = menu.exec(m_tree->viewport()->mapToGlobal(position));
        if (chosen && chosen == open)
            openFile(path);
        else if (chosen && chosen == smali)
            openFile(other);
        else if (chosen == copyPath)
            QApplication::clipboard()->setText(relative(path));
        return;
    }

    const bool isRoot = !index.isValid();
    const QString folder = info.isDir() ? path : info.absolutePath();

    QMenu menu(this);
    QAction *open = info.isFile() ? menu.addAction(tr("Open")) : nullptr;
    if (open)
        menu.addSeparator();
    QAction *newFile = menu.addAction(tr("New File…"));
    QAction *newFolder = menu.addAction(tr("New Folder…"));
    QAction *addFiles = menu.addAction(tr("Add Files…"));
    menu.addSeparator();
    QAction *replace = info.isFile() ? menu.addAction(tr("Replace With…")) : nullptr;
    QAction *backup = info.isFile() ? menu.addAction(tr("Back Up File")) : nullptr;
    QAction *rename = isRoot ? nullptr : menu.addAction(tr("Rename…"));
    QAction *remove = isRoot ? nullptr : menu.addAction(tr("Delete"));
    menu.addSeparator();
    QAction *copyPath = menu.addAction(tr("Copy Path"));
    QAction *reveal = menu.addAction(tr("Open Folder in File Manager"));

    QAction *chosen = menu.exec(m_tree->viewport()->mapToGlobal(position));
    if (!chosen)
        return;

    if (chosen == open) {
        openFile(path);
    } else if (chosen == newFile || chosen == newFolder) {
        const bool makeFolder = chosen == newFolder;
        bool ok = false;
        const QString name = validName(QInputDialog::getText(
            this, makeFolder ? tr("New Folder") : tr("New File"),
            tr("Name, inside %1:").arg(relative(folder) == QLatin1String(".") ? tr("the project") : relative(folder)),
            QLineEdit::Normal, QString(), &ok));
        if (!ok)
            return;
        const QString target = folder + QLatin1Char('/') + name;
        if (name.isEmpty() || QFileInfo::exists(target)) {
            Util::error(this, makeFolder ? tr("New Folder") : tr("New File"), name.isEmpty() ? tr("That name cannot be used.")
                                                           : tr("%1 already exists.").arg(name));
            return;
        }
        if (makeFolder) {
            QDir(folder).mkdir(name);
        } else {
            QFile file(target);
            if (file.open(QIODevice::WriteOnly)) {
                file.close();
                openFile(target);
            }
        }
        m_tree->expand(m_model->index(folder));
    } else if (chosen == addFiles) {
        const QStringList files = QFileDialog::getOpenFileNames(this, tr("Add Files to %1").arg(QFileInfo(folder).fileName()),
                                                                QDir::homePath());
        int added = 0;
        for (const QString &file : files) {
            const QString target = folder + QLatin1Char('/') + QFileInfo(file).fileName();
            if (QFileInfo::exists(target)) {
                if (!Util::confirm(this, tr("Add Files"), tr("%1 already exists here. Replace it?").arg(QFileInfo(file).fileName()),
                                   tr("Replace")))
                    continue;
                QFile::remove(target);
            }
            if (QFile::copy(file, target)) {
                QFile::setPermissions(target, QFile::permissions(target) | QFile::WriteOwner);
                added++;
                if (DocTab *tab = tabFor(target))
                    tab->reload();
            }
        }
        m_tree->expand(m_model->index(folder));
        if (added)
            emit status(tr("Added %1").arg(Util::count(added, tr("file"), tr("files"))));
    } else if (chosen == replace) {
        replaceFile(path);
    } else if (chosen == backup) {
        if (DocTab *tab = tabFor(path); tab && !saveTab(tab))
            return;
        QString error;
        if (Engine::call({QStringLiteral("file-backup"), m_project.value(QStringLiteral("dir")).toString(), relative(path)},
                         nullptr, &error))
            emit status(tr("Backed up %1").arg(relative(path)));
        else
            Util::error(this, tr("Back Up File"), error);
    } else if (chosen == rename) {
        bool ok = false;
        const QString name = validName(QInputDialog::getText(this, tr("Rename"), tr("New name:"), QLineEdit::Normal,
                                                             info.fileName(), &ok));
        if (!ok || name == info.fileName())
            return;
        const QString target = info.absolutePath() + QLatin1Char('/') + name;
        if (name.isEmpty() || QFileInfo::exists(target) || !QFile::rename(path, target)) {
            Util::error(this, tr("Rename"), tr("%1 could not be renamed to %2.").arg(info.fileName(), name));
            return;
        }
        pathChanged(path, target);
    } else if (chosen == remove) {
        if (!Util::confirm(this, tr("Delete"),
                           info.isDir() ? tr("Delete the folder %1 and everything in it?").arg(relative(path))
                                        : tr("Delete %1?").arg(relative(path)),
                           tr("Delete")))
            return;
        const bool gone = info.isDir() && !info.isSymLink() ? QDir(path).removeRecursively() : QFile::remove(path);
        if (!gone) {
            Util::error(this, tr("Delete"), tr("%1 could not be deleted.").arg(relative(path)));
            return;
        }
        pathRemoved(path);
    } else if (chosen == copyPath) {
        QApplication::clipboard()->setText(isRoot ? src() : relative(path));
    } else if (chosen == reveal) {
        Util::openFolder(folder);
    }
}
