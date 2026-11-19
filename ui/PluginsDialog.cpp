// ShiftDex - unpack, edit and repack APK and JAR files
// Copyright (C) 2026 palaziks
// SPDX-License-Identifier: GPL-3.0-or-later
#include "PluginsDialog.h"

#include "Config.h"
#include "Engine.h"
#include "JobDialog.h"
#include "Util.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QTableWidget>
#include <QTemporaryFile>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace {
const QString kGuideUrl = QStringLiteral("https://github.com/palazik/ShiftDex/blob/main/docs/PLUGINS.md");
}

void PluginsDialog::openGuide(QWidget *)
{
    const QString local = Engine::docFile(QStringLiteral("PLUGINS.md"));
    QDesktopServices::openUrl(local.isEmpty() ? QUrl(kGuideUrl) : QUrl::fromLocalFile(local));
}

QString PluginsDialog::codeSentence(const QJsonObject &plugin)
{
    const QString code = plugin.value(QStringLiteral("code")).toString();
    if (code == QLatin1String("smali"))
        return tr("This is a smali plugin: it changes the app's smali code.");
    if (code == QLatin1String("java"))
        return tr("This is a Java plugin: it changes Java class files, so it is for JAR projects.");
    return tr("This plugin does not change code (neither smali nor Java), only other files.");
}

PluginsDialog::PluginsDialog(const QJsonObject &project, QWidget *parent) : QDialog(parent), m_project(project)
{
    setWindowTitle(tr("Plugins"));
    resize(760, 500);
    const bool hasProject = !m_project.isEmpty();

    auto *intro = new QLabel(tr("A plugin is a set of changes made by you or another developer: files to add or "
                                "replace, files to delete, and text to replace. Plugins only describe changes; "
                                "they never run code."));
    intro->setObjectName(QStringLiteral("muted"));
    intro->setWordWrap(true);

    m_list = new QTreeWidget;
    m_list->setHeaderLabels({tr("Plugin"), tr("Code"), tr("Version"), tr("Author"), tr("For")});
    m_list->setRootIsDecorated(false);
    m_list->setUniformRowHeights(true);
    m_list->setAlternatingRowColors(true);
    m_list->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_list->header()->setStretchLastSection(false);

    m_details = new QLabel;
    m_details->setWordWrap(true);
    m_details->setTextFormat(Qt::PlainText);
    m_details->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_details->setMinimumHeight(104);
    m_details->setAlignment(Qt::AlignTop | Qt::AlignLeft);

    m_apply = new QPushButton(tr("Apply to Project"));
    m_apply->setObjectName(QStringLiteral("primary"));
    auto *install = new QPushButton(tr("Install…"));
    m_create = new QPushButton(tr("New Plugin…"));
    m_create->setEnabled(hasProject);
    m_create->setToolTip(hasProject ? tr("Make a plugin from the changes in this project")
                                    : tr("Open a project first: a plugin is made from its changes."));
    m_export = new QPushButton(tr("Export…"));
    m_remove = new QPushButton(tr("Remove"));
    m_remove->setObjectName(QStringLiteral("danger"));
    auto *guide = new QPushButton(tr("Plugin Guide"));
    auto *folder = new QPushButton(tr("Open Folder"));
    auto *close = new QPushButton(tr("Close"));
    for (QPushButton *button : {m_apply, install, m_create, m_export, m_remove, guide, folder, close})
        button->setAutoDefault(false);

    auto *side = new QVBoxLayout;
    side->setSpacing(6);
    side->addWidget(m_apply);
    side->addSpacing(8);
    side->addWidget(install);
    side->addWidget(m_create);
    side->addWidget(m_export);
    side->addWidget(m_remove);
    side->addStretch();
    side->addWidget(guide);
    side->addWidget(folder);

    auto *left = new QVBoxLayout;
    left->addWidget(m_list, 1);
    left->addWidget(m_details);

    auto *middle = new QHBoxLayout;
    middle->setSpacing(12);
    middle->addLayout(left, 1);
    middle->addLayout(side);

    auto *bottom = new QHBoxLayout;
    bottom->addStretch();
    bottom->addWidget(close);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(14, 14, 14, 12);
    layout->setSpacing(10);
    layout->addWidget(intro);
    layout->addLayout(middle, 1);
    layout->addLayout(bottom);

    connect(m_list, &QTreeWidget::itemSelectionChanged, this, &PluginsDialog::updateDetails);
    connect(m_list, &QTreeWidget::itemDoubleClicked, this, [this] {
        if (m_apply->isEnabled())
            applySelected();
    });
    connect(m_apply, &QPushButton::clicked, this, &PluginsDialog::applySelected);
    connect(install, &QPushButton::clicked, this, &PluginsDialog::installFromFile);
    connect(m_create, &QPushButton::clicked, this, &PluginsDialog::createNew);
    connect(m_export, &QPushButton::clicked, this, &PluginsDialog::exportSelected);
    connect(m_remove, &QPushButton::clicked, this, &PluginsDialog::removeSelected);
    connect(guide, &QPushButton::clicked, this, [this] { openGuide(this); });
    connect(folder, &QPushButton::clicked, this, [] {
        const QString path = Config::dataDir() + QStringLiteral("/plugins");
        QDir().mkpath(path);
        Util::openFolder(path);
    });
    connect(close, &QPushButton::clicked, this, &QDialog::accept);

    reload();
}

void PluginsDialog::reload(const QString &selectId)
{
    m_list->clear();
    QStringList arguments{QStringLiteral("plugins")};
    if (!m_project.isEmpty())
        arguments << QStringLiteral("--project") << m_project.value(QStringLiteral("dir")).toString();
    QJsonValue result;
    QString error;
    if (!Engine::call(arguments, &result, &error)) {
        m_details->setText(error);
        return;
    }
    const QJsonObject data = result.toObject();
    for (const QJsonValue &value : data.value(QStringLiteral("plugins")).toArray()) {
        const QJsonObject plugin = value.toObject();
        const QJsonObject target = plugin.value(QStringLiteral("target")).toObject();
        QString scope = target.value(QStringLiteral("type")).toString().toUpper();
        if (scope == QLatin1String("ANY"))
            scope = tr("APK and JAR");
        auto *item = new QTreeWidgetItem(m_list);
        item->setText(0, plugin.value(QStringLiteral("name")).toString());
        item->setText(1, plugin.value(QStringLiteral("code_label")).toString());
        item->setText(2, plugin.value(QStringLiteral("version")).toString());
        item->setText(3, plugin.value(QStringLiteral("author")).toString());
        item->setText(4, scope);
        item->setData(0, Qt::UserRole, plugin);
        if (plugin.value(QStringLiteral("id")).toString() == selectId)
            m_list->setCurrentItem(item);
    }
    for (const QJsonValue &value : data.value(QStringLiteral("broken")).toArray()) {
        const QJsonObject broken = value.toObject();
        auto *item = new QTreeWidgetItem(m_list);
        item->setText(0, tr("%1 (cannot be loaded)").arg(broken.value(QStringLiteral("folder")).toString()));
        item->setData(0, Qt::UserRole + 1, broken.value(QStringLiteral("error")).toString());
        item->setDisabled(false);
    }
    for (int column : {1, 2, 3, 4})
        m_list->resizeColumnToContents(column);
    if (!m_list->currentItem() && m_list->topLevelItemCount() > 0)
        m_list->setCurrentItem(m_list->topLevelItem(0));
    updateDetails();
}

QJsonObject PluginsDialog::selected() const
{
    const QTreeWidgetItem *item = m_list->currentItem();
    return item ? item->data(0, Qt::UserRole).toJsonObject() : QJsonObject();
}

void PluginsDialog::updateDetails()
{
    const QTreeWidgetItem *item = m_list->currentItem();
    const QJsonObject plugin = selected();
    const bool valid = !plugin.isEmpty();
    m_export->setEnabled(valid);
    m_remove->setEnabled(valid);

    if (!item) {
        m_details->setText(tr("No plugins installed yet. Install a .sdplugin file from another developer, or "
                              "make your own with New Plugin."));
        m_apply->setEnabled(false);
        return;
    }
    if (!valid) {
        m_details->setText(item->data(0, Qt::UserRole + 1).toString());
        m_apply->setEnabled(false);
        return;
    }
    QStringList lines;
    const QString description = plugin.value(QStringLiteral("description")).toString();
    if (!description.isEmpty())
        lines << description;
    lines << codeSentence(plugin);
    lines << tr("Contains: %1.   ID: %2").arg(plugin.value(QStringLiteral("summary")).toString(),
                                              plugin.value(QStringLiteral("id")).toString());
    const QString onlyFor = plugin.value(QStringLiteral("target")).toObject().value(QStringLiteral("package")).toString();
    if (!onlyFor.isEmpty())
        lines << tr("Only for the package %1.").arg(onlyFor);
    QStringList problems;
    for (const QJsonValue &problem : plugin.value(QStringLiteral("problems")).toArray())
        problems << problem.toString();
    if (!problems.isEmpty())
        lines << tr("Not applicable here: %1").arg(problems.join(QLatin1Char(' ')));
    else if (m_project.isEmpty())
        lines << tr("Open a project to apply this plugin.");
    m_details->setText(lines.join(QLatin1Char('\n')));
    m_apply->setEnabled(!m_project.isEmpty() && problems.isEmpty());
}

void PluginsDialog::applySelected()
{
    const QJsonObject plugin = selected();
    if (plugin.isEmpty() || m_project.isEmpty())
        return;
    const QString name = plugin.value(QStringLiteral("name")).toString();
    const QString safety = Config::instance().flag(QStringLiteral("snapshot_before_plugin"))
        ? tr("A snapshot is taken first, so this can be undone from Backups.")
        : tr("Automatic snapshots are turned off, so this cannot be undone.");
    QMessageBox box(QMessageBox::Question, tr("Apply Plugin"),
                    tr("Apply “%1” to this project?\n\n%2\nContains: %3.\n\n%4")
                        .arg(name, codeSentence(plugin), plugin.value(QStringLiteral("summary")).toString(), safety),
                    QMessageBox::Cancel, this);
    QPushButton *go = box.addButton(tr("Apply"), QMessageBox::AcceptRole);
    box.exec();
    if (box.clickedButton() != static_cast<QAbstractButton *>(go))
        return;
    JobDialog::run(this, tr("Applying %1").arg(name),
                   {QStringLiteral("plugin-apply"), m_project.value(QStringLiteral("dir")).toString(),
                    plugin.value(QStringLiteral("id")).toString()},
                   [this, name, plugin](bool ok, const QJsonValue &result) {
                       if (!ok)
                           return;
                       const QJsonObject data = result.toObject();
                       m_project = data.value(QStringLiteral("project")).toObject();
                       emit projectChanged(m_project);
                       QStringList report;
                       const QJsonArray lines = data.value(QStringLiteral("report")).toArray();
                       for (int i = 0; i < lines.size() && i < 12; i++)
                           report << lines.at(i).toString();
                       if (lines.size() > 12)
                           report << tr("… and %1 more").arg(lines.size() - 12);
                       QMessageBox::information(this, tr("Plugin Applied"),
                                                tr("“%1” was applied.\n%2\n\n%3")
                                                    .arg(name, codeSentence(plugin), report.join(QLatin1Char('\n'))));
                   });
}

void PluginsDialog::installFromFile()
{
    const QString file = QFileDialog::getOpenFileName(this, tr("Install Plugin"), QDir::homePath(),
                                                      tr("ShiftDex plugins (*.sdplugin *.zip);;All files (*)"));
    if (file.isEmpty())
        return;
    QJsonValue result;
    QString error;
    if (!Engine::call({QStringLiteral("plugin-install"), file}, &result, &error)) {
        Util::error(this, tr("Install Plugin"), error);
        return;
    }
    const QJsonObject installed = result.toObject();
    reload(installed.value(QStringLiteral("id")).toString());
    QMessageBox::information(this, tr("Plugin Installed"),
                             tr("“%1” was installed.\n\n%2")
                                 .arg(installed.value(QStringLiteral("name")).toString(), codeSentence(installed)));
}

void PluginsDialog::exportSelected()
{
    const QJsonObject plugin = selected();
    if (plugin.isEmpty())
        return;
    const QString id = plugin.value(QStringLiteral("id")).toString();
    QString dest = QFileDialog::getSaveFileName(this, tr("Export Plugin"),
                                                QStringLiteral("%1/%2.sdplugin").arg(QDir::homePath(), id),
                                                tr("ShiftDex plugins (*.sdplugin)"));
    if (dest.isEmpty())
        return;
    if (!dest.endsWith(QLatin1String(".sdplugin")))
        dest += QStringLiteral(".sdplugin");
    QString error;
    if (!Engine::call({QStringLiteral("plugin-export"), id, dest}, nullptr, &error))
        Util::error(this, tr("Export Plugin"), error);
}

void PluginsDialog::removeSelected()
{
    const QJsonObject plugin = selected();
    if (plugin.isEmpty())
        return;
    if (!Util::confirm(this, tr("Remove Plugin"),
                       tr("Remove “%1”? Projects it was already applied to stay as they are.")
                           .arg(plugin.value(QStringLiteral("name")).toString()),
                       tr("Remove")))
        return;
    QString error;
    if (!Engine::call({QStringLiteral("plugin-remove"), plugin.value(QStringLiteral("id")).toString()}, nullptr, &error))
        Util::error(this, tr("Remove Plugin"), error);
    reload();
}

void PluginsDialog::createNew()
{
    PluginCreator creator(m_project, this);
    if (creator.exec() != QDialog::Accepted)
        return;
    reload(creator.createdId());
    QMessageBox::information(this, tr("Plugin Created"),
                             tr("The plugin was created and installed.\n\n%1\n\nUse Export to share it.")
                                 .arg(codeSentence(selected())));
}

// ------------------------------------------------------------ PluginCreator

PluginCreator::PluginCreator(const QJsonObject &project, QWidget *parent) : QDialog(parent), m_project(project)
{
    setWindowTitle(tr("New Plugin"));
    resize(720, 660);

    m_name = new QLineEdit;
    m_name->setPlaceholderText(tr("Remove splash screen"));
    m_id = new QLineEdit;
    m_id->setPlaceholderText(tr("yourname.remove-splash"));
    m_version = new QLineEdit(QStringLiteral("1.0"));
    m_version->setMaximumWidth(120);
    m_author = new QLineEdit(qEnvironmentVariable("USER"));
    m_description = new QLineEdit;
    m_description->setPlaceholderText(tr("What the plugin changes, in a sentence"));
    const QString package = m_project.value(QStringLiteral("package")).toString();
    m_restrict = new QCheckBox(package.isEmpty() ? tr("Only allow this plugin on the same package")
                                                 : tr("Only allow this plugin on %1").arg(package));
    m_restrict->setEnabled(!package.isEmpty());
    m_restrict->setChecked(!package.isEmpty());

    // suggest an id from the name until the user edits the id by hand
    connect(m_name, &QLineEdit::textEdited, this, [this](const QString &text) {
        if (m_id->isModified())
            return;
        QString slug = text.toLower();
        slug.replace(QRegularExpression(QStringLiteral("[^a-z0-9]+")), QStringLiteral("-"));
        slug.remove(QRegularExpression(QStringLiteral("^-+|-+$")));
        const QString author = m_author->text().toLower().remove(QRegularExpression(QStringLiteral("[^a-z0-9]")));
        m_id->setText(slug.isEmpty() ? QString() : (author.isEmpty() ? slug : author + QLatin1Char('.') + slug));
    });

    auto *form = new QFormLayout;
    form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
    form->addRow(tr("Name"), m_name);
    form->addRow(tr("ID"), m_id);
    form->addRow(tr("Version"), m_version);
    form->addRow(tr("Author"), m_author);
    form->addRow(tr("Description"), m_description);
    m_code = new QComboBox;
    m_code->addItem(tr("Detect from the changes below"), QString());
    m_code->addItem(tr("Smali — changes an APK's smali code"), QStringLiteral("smali"));
    m_code->addItem(tr("Java — changes a JAR's class files"), QStringLiteral("java"));
    m_code->addItem(tr("No code — resources and other files only"), QStringLiteral("none"));
    m_code->setToolTip(tr("Every plugin states what kind of code it changes. People see this before they "
                          "install or apply it."));
    form->addRow(tr("Code type"), m_code);
    form->addRow(QString(), m_restrict);

    auto *changesTitle = new QLabel(tr("Changed files to include"));
    changesTitle->setObjectName(QStringLiteral("subheading"));
    m_changesNote = new QLabel(tr("Comparing the project with the original …"));
    m_changesNote->setObjectName(QStringLiteral("muted"));
    m_changesNote->setWordWrap(true);
    m_changes = new QTreeWidget;
    m_changes->setHeaderLabels({tr("File"), tr("Change")});
    m_changes->setRootIsDecorated(false);
    m_changes->setUniformRowHeights(true);
    m_changes->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_changes->header()->setStretchLastSection(false);
    m_changes->setTextElideMode(Qt::ElideLeft);
    auto *all = new QPushButton(tr("Select All"));
    auto *none = new QPushButton(tr("Select None"));
    auto setAll = [this](Qt::CheckState state) {
        for (int i = 0; i < m_changes->topLevelItemCount(); i++)
            m_changes->topLevelItem(i)->setCheckState(0, state);
    };
    connect(all, &QPushButton::clicked, this, [setAll] { setAll(Qt::Checked); });
    connect(none, &QPushButton::clicked, this, [setAll] { setAll(Qt::Unchecked); });
    auto *changeButtons = new QHBoxLayout;
    changeButtons->addWidget(m_changesNote, 1);
    changeButtons->addWidget(all);
    changeButtons->addWidget(none);

    auto *rulesTitle = new QLabel(tr("Replace rules"));
    rulesTitle->setObjectName(QStringLiteral("subheading"));
    auto *rulesNote = new QLabel(tr("Optional. A rule replaces text in every file matching a pattern, which keeps "
                                    "working across app versions where whole files would not. "
                                    "Example pattern: smali/**/*.smali"));
    rulesNote->setObjectName(QStringLiteral("muted"));
    rulesNote->setWordWrap(true);
    m_rules = new QTableWidget(0, 4);
    m_rules->setHorizontalHeaderLabels({tr("Files"), tr("Find"), tr("Replace with"), tr("Regex")});
    m_rules->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_rules->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_rules->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    m_rules->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    m_rules->verticalHeader()->hide();
    m_rules->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_rules->setMaximumHeight(130);
    auto *addRuleButton = new QPushButton(tr("Add Rule"));
    auto *removeRule = new QPushButton(tr("Remove Rule"));
    connect(addRuleButton, &QPushButton::clicked, this, [this] { addRule(QStringLiteral("smali/**/*.smali")); });
    connect(removeRule, &QPushButton::clicked, this, [this] {
        if (m_rules->currentRow() >= 0)
            m_rules->removeRow(m_rules->currentRow());
    });
    auto *ruleButtons = new QHBoxLayout;
    ruleButtons->addWidget(addRuleButton);
    ruleButtons->addWidget(removeRule);
    ruleButtons->addStretch();

    auto *createButton = new QPushButton(tr("Create Plugin"));
    createButton->setObjectName(QStringLiteral("primary"));
    auto *cancel = new QPushButton(tr("Cancel"));
    auto *guide = new QPushButton(tr("Plugin Guide"));
    for (QPushButton *button : {all, none, addRuleButton, removeRule, createButton, cancel, guide})
        button->setAutoDefault(false);
    connect(createButton, &QPushButton::clicked, this, &PluginCreator::create);
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(guide, &QPushButton::clicked, this, [this] { PluginsDialog::openGuide(this); });
    auto *bottom = new QHBoxLayout;
    bottom->addWidget(guide);
    bottom->addStretch();
    bottom->addWidget(cancel);
    bottom->addWidget(createButton);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(16, 16, 16, 12);
    layout->setSpacing(8);
    layout->addLayout(form);
    layout->addSpacing(6);
    layout->addWidget(changesTitle);
    layout->addWidget(m_changes, 1);
    layout->addLayout(changeButtons);
    layout->addSpacing(6);
    layout->addWidget(rulesTitle);
    layout->addWidget(rulesNote);
    layout->addWidget(m_rules);
    layout->addLayout(ruleButtons);
    layout->addSpacing(4);
    layout->addLayout(bottom);

    loadChanges();
}

void PluginCreator::loadChanges()
{
    auto *job = new EngineJob(this);
    connect(job, &EngineJob::finished, this, [this, job](bool ok, const QJsonValue &result, const QString &error) {
        job->deleteLater();
        if (!ok) {
            m_changesNote->setText(error);
            return;
        }
        const QJsonObject changes = result.toObject();
        const QList<QPair<QString, QString>> kinds{
            {QStringLiteral("added"), tr("Added")},
            {QStringLiteral("modified"), tr("Modified")},
            {QStringLiteral("deleted"), tr("Deleted")},
        };
        int total = 0;
        for (const auto &kind : kinds) {
            for (const QJsonValue &value : changes.value(kind.first).toArray()) {
                auto *item = new QTreeWidgetItem(m_changes);
                item->setText(0, value.toString());
                item->setToolTip(0, value.toString());
                item->setText(1, kind.second);
                item->setData(0, Qt::UserRole, kind.first);
                item->setCheckState(0, Qt::Checked);
                total++;
            }
        }
        m_changes->resizeColumnToContents(1);
        m_changesNote->setText(total ? (total == 1 ? tr("1 file differs from the original.") : tr("%1 files differ from the original.").arg(total))
                                     : tr("No files were changed yet. The plugin can still carry replace rules."));
    });
    job->start({QStringLiteral("changes"), m_project.value(QStringLiteral("dir")).toString()});
}

void PluginCreator::addRule(const QString &files, const QString &find, const QString &replace)
{
    const int row = m_rules->rowCount();
    m_rules->insertRow(row);
    m_rules->setItem(row, 0, new QTableWidgetItem(files));
    m_rules->setItem(row, 1, new QTableWidgetItem(find));
    m_rules->setItem(row, 2, new QTableWidgetItem(replace));
    auto *regex = new QTableWidgetItem;
    regex->setFlags(Qt::ItemIsUserCheckable | Qt::ItemIsEnabled | Qt::ItemIsSelectable);
    regex->setCheckState(Qt::Unchecked);
    m_rules->setItem(row, 3, regex);
    m_rules->setCurrentCell(row, 1);
    m_rules->editItem(m_rules->item(row, 1));
}

void PluginCreator::create()
{
    // commit a cell that is still being edited
    m_rules->setCurrentCell(-1, -1);

    QJsonArray files, deleted, rules;
    for (int i = 0; i < m_changes->topLevelItemCount(); i++) {
        const QTreeWidgetItem *item = m_changes->topLevelItem(i);
        if (item->checkState(0) != Qt::Checked)
            continue;
        if (item->data(0, Qt::UserRole).toString() == QLatin1String("deleted"))
            deleted.append(item->text(0));
        else
            files.append(item->text(0));
    }
    for (int row = 0; row < m_rules->rowCount(); row++) {
        const QString pattern = m_rules->item(row, 0)->text().trimmed();
        const QString find = m_rules->item(row, 1)->text();
        if (pattern.isEmpty() && find.isEmpty())
            continue;
        if (pattern.isEmpty() || find.isEmpty()) {
            Util::error(this, windowTitle(), tr("Rule %1 needs both a file pattern and the text to find.").arg(row + 1));
            return;
        }
        rules.append(QJsonObject{
            {QStringLiteral("files"), pattern},
            {QStringLiteral("find"), find},
            {QStringLiteral("replace"), m_rules->item(row, 2)->text()},
            {QStringLiteral("regex"), m_rules->item(row, 3)->checkState() == Qt::Checked},
        });
    }
    if (m_name->text().trimmed().isEmpty() || m_id->text().trimmed().isEmpty()) {
        Util::error(this, windowTitle(), tr("Give the plugin a name and an ID."));
        return;
    }

    QJsonObject spec{
        {QStringLiteral("info"), QJsonObject{
            {QStringLiteral("id"), m_id->text().trimmed()},
            {QStringLiteral("name"), m_name->text().trimmed()},
            {QStringLiteral("version"), m_version->text().trimmed()},
            {QStringLiteral("author"), m_author->text().trimmed()},
            {QStringLiteral("description"), m_description->text().trimmed()},
            {QStringLiteral("code"), m_code->currentData().toString()},
        }},
        {QStringLiteral("files"), files},
        {QStringLiteral("deleted"), deleted},
        {QStringLiteral("rules"), rules},
        {QStringLiteral("restrict_package"), m_restrict->isChecked()},
    };

    auto send = [this](QJsonObject request, QString *error) {
        QTemporaryFile file;
        if (!file.open()) {
            *error = file.errorString();
            return false;
        }
        file.write(QJsonDocument(request).toJson());
        file.flush();
        return Engine::call({QStringLiteral("plugin-create"), QStringLiteral("--project"),
                             m_project.value(QStringLiteral("dir")).toString(), QStringLiteral("--spec"),
                             file.fileName()},
                            nullptr, error, 120000);
    };

    QString error;
    if (!send(spec, &error)) {
        if (!error.contains(QLatin1String("already installed"))) {
            Util::error(this, windowTitle(), error);
            return;
        }
        if (!Util::confirm(this, windowTitle(),
                           tr("A plugin with the ID %1 is already installed. Replace it?").arg(m_id->text().trimmed()),
                           tr("Replace")))
            return;
        spec.insert(QStringLiteral("overwrite"), true);
        if (!send(spec, &error)) {
            Util::error(this, windowTitle(), error);
            return;
        }
    }
    m_createdId = m_id->text().trimmed();
    accept();
}
