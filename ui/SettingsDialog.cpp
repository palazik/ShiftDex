// ShiftDex - unpack, edit and repack APK and JAR files
// Copyright (C) 2026 palaziks
// SPDX-License-Identifier: GPL-3.0-or-later
#include "SettingsDialog.h"

#include "Config.h"
#include "Keymap.h"
#include "ToolsPanel.h"
#include "Util.h"

#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDir>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QTabWidget>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace {

Config &config() { return Config::instance(); }

QCheckBox *toggle(const QString &text, const QString &key)
{
    auto *box = new QCheckBox(text);
    box->setChecked(config().flag(key));
    QObject::connect(box, &QCheckBox::toggled, box, [key](bool on) { config().set(key, on); });
    return box;
}

QLineEdit *textField(const QString &key, const QString &placeholder = QString())
{
    auto *edit = new QLineEdit(config().str(key));
    edit->setPlaceholderText(placeholder);
    QObject::connect(edit, &QLineEdit::editingFinished, edit, [edit, key] { config().set(key, edit->text().trimmed()); });
    return edit;
}

QSpinBox *numberField(const QString &key, int min, int max)
{
    auto *spin = new QSpinBox;
    spin->setRange(min, max);
    spin->setValue(config().number(key));
    QObject::connect(spin, &QSpinBox::valueChanged, spin, [key](int value) { config().set(key, value); });
    return spin;
}

// "/home/me/.config/x" -> "~/.config/x", which also wraps better in a label
QString homeRelative(const QString &path)
{
    const QString home = QDir::homePath();
    return path.startsWith(home + QLatin1Char('/')) ? QLatin1Char('~') + path.mid(home.length()) : path;
}

QLabel *note(const QString &text)
{
    auto *label = new QLabel(text);
    label->setObjectName(QStringLiteral("muted"));
    label->setWordWrap(true);
    return label;
}

// line edit with a Browse button; pickFolder chooses between folder and file dialogs
QWidget *pathField(QWidget *owner, const QString &key, const QString &placeholder, bool pickFolder,
                   const QString &filter = QString())
{
    auto *edit = textField(key, placeholder);
    auto *browse = new QPushButton(QObject::tr("Browse…"));
    browse->setAutoDefault(false);
    QObject::connect(browse, &QPushButton::clicked, owner, [owner, edit, key, pickFolder, filter] {
        const QString start = edit->text().isEmpty() ? QDir::homePath() : edit->text();
        const QString chosen = pickFolder
            ? QFileDialog::getExistingDirectory(owner, QObject::tr("Choose a Folder"), start)
            : QFileDialog::getOpenFileName(owner, QObject::tr("Choose a File"), start, filter);
        if (!chosen.isEmpty()) {
            edit->setText(chosen);
            config().set(key, chosen);
        }
    });
    auto *row = new QWidget;
    auto *layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(edit, 1);
    layout->addWidget(browse);
    return row;
}

// pages scroll, so nothing gets squeezed on a small screen
QWidget *page(QLayout *content)
{
    auto *widget = new QWidget;
    auto *layout = new QVBoxLayout(widget);
    layout->setContentsMargins(16, 16, 16, 8);
    layout->setSpacing(10);
    layout->addLayout(content);
    layout->addStretch();
    auto *scroll = new QScrollArea;
    scroll->setWidget(widget);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    return scroll;
}

} // namespace

SettingsDialog::SettingsDialog(QWidget *parent, int initialPage) : QDialog(parent)
{
    setWindowTitle(tr("Preferences"));
    resize(720, 620);

    auto *tabs = new QTabWidget;
    tabs->setDocumentMode(true);
    tabs->addTab(buildGeneral(), tr("General"));
    tabs->addTab(buildEditor(), tr("Editor"));
    tabs->addTab(buildBuilding(), tr("Building"));
    tabs->addTab(buildTools(), tr("Tools"));
    tabs->addTab(buildShortcuts(), tr("Shortcuts"));
    tabs->setCurrentIndex(initialPage);

    auto *close = new QPushButton(tr("Close"));
    close->setDefault(true);
    connect(close, &QPushButton::clicked, this, &QDialog::accept);
    auto *bottom = new QHBoxLayout;
    bottom->setContentsMargins(12, 6, 12, 12);
    bottom->addWidget(note(tr("Changes are saved as you make them.")), 1);
    bottom->addWidget(close);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 6, 0, 0);
    layout->addWidget(tabs, 1);
    layout->addLayout(bottom);
}

QWidget *SettingsDialog::buildGeneral()
{
    auto *theme = new QComboBox;
    const QList<QPair<QString, QString>> themes{
        {QStringLiteral("system"), tr("Follow the system")},
        {QStringLiteral("light"), tr("White")},
        {QStringLiteral("dark"), tr("Black")},
        {QStringLiteral("amoled"), tr("AMOLED (pure black)")},
    };
    for (const auto &entry : themes)
        theme->addItem(entry.second, entry.first);
    theme->setCurrentIndex(qMax(0, theme->findData(config().str(QStringLiteral("theme")))));
    connect(theme, &QComboBox::currentIndexChanged, this,
            [theme] { config().set(QStringLiteral("theme"), theme->currentData().toString()); });

    auto *form = new QFormLayout;
    form->setVerticalSpacing(10);
    form->addRow(tr("Theme"), theme);
    form->addRow(tr("Projects folder"),
                 pathField(this, QStringLiteral("workspace_dir"), Config::dataDir() + QStringLiteral("/projects"), true));
    form->addRow(QString(), note(tr("Unpacked projects and their backups are stored here. Existing projects are "
                                    "not moved when you change the folder.")));

    auto *safety = new QGroupBox(tr("Safety"));
    auto *safetyLayout = new QVBoxLayout(safety);
    safetyLayout->addWidget(toggle(tr("Take a snapshot before applying a plugin"), QStringLiteral("snapshot_before_plugin")));
    safetyLayout->addWidget(toggle(tr("Take a snapshot before restoring a snapshot or resetting the project"),
                                   QStringLiteral("snapshot_before_restore")));
    safetyLayout->addWidget(toggle(tr("Back up a file the first time it is saved in a session"),
                                   QStringLiteral("backup_on_save")));

    auto *layout = new QVBoxLayout;
    layout->setSpacing(14);
    layout->addLayout(form);
    layout->addWidget(safety);
    return page(layout);
}

QWidget *SettingsDialog::buildEditor()
{
    auto *form = new QFormLayout;
    form->setVerticalSpacing(10);
    form->addRow(tr("Font size"), numberField(QStringLiteral("editor_font_size"), 6, 40));
    form->addRow(tr("Tab width"), numberField(QStringLiteral("editor_tab_width"), 1, 16));
    form->addRow(QString(), toggle(tr("Insert spaces instead of tabs"), QStringLiteral("editor_spaces")));
    form->addRow(QString(), toggle(tr("Show line numbers"), QStringLiteral("editor_line_numbers")));
    form->addRow(QString(), toggle(tr("Highlight the current line"), QStringLiteral("editor_highlight_line")));
    form->addRow(QString(), toggle(tr("Wrap long lines"), QStringLiteral("editor_wrap")));
    return page(form);
}

QWidget *SettingsDialog::buildBuilding()
{
    auto *decode = new QComboBox;
    decode->addItem(tr("XML (recommended, editable resources)"), QStringLiteral("xml"));
    decode->addItem(tr("JSON (for obfuscated apps XML cannot decode)"), QStringLiteral("json"));
    decode->addItem(tr("Raw (binary resources left as they are)"), QStringLiteral("raw"));
    decode->setCurrentIndex(qMax(0, decode->findData(config().str(QStringLiteral("decode_type")))));
    connect(decode, &QComboBox::currentIndexChanged, this,
            [decode] { config().set(QStringLiteral("decode_type"), decode->currentData().toString()); });

    auto *unpack = new QGroupBox(tr("Unpacking APKs"));
    auto *unpackForm = new QFormLayout(unpack);
    unpackForm->addRow(tr("Decode resources as"), decode);
    unpackForm->addRow(QString(), toggle(tr("Decode DEX code to smali"),
                                         QStringLiteral("decode_smali")));
    unpackForm->addRow(QString(), note(tr("Applies to APKs opened from now on. Changing the app name, icon and "
                                          "package needs the XML format. Without smali, classes.dex files are "
                                          "kept as they are and code cannot be edited.")));

    auto *sign = new QGroupBox(tr("Signing"));
    auto *signForm = new QFormLayout(sign);
    signForm->addRow(QString(), toggle(tr("Sign APKs after building"), QStringLiteral("sign_after_build")));
    signForm->addRow(tr("Keystore"), pathField(this, QStringLiteral("keystore_path"),
                                               tr("Leave empty to use ShiftDex's own key"), false,
                                               tr("Keystores (*.jks *.keystore *.p12 *.pfx);;All files (*)")));
    signForm->addRow(tr("Key alias"), textField(QStringLiteral("keystore_alias")));
    auto *storePass = textField(QStringLiteral("keystore_pass"));
    storePass->setEchoMode(QLineEdit::Password);
    auto *keyPass = textField(QStringLiteral("key_pass"), tr("Same as the keystore password"));
    keyPass->setEchoMode(QLineEdit::Password);
    signForm->addRow(tr("Keystore password"), storePass);
    signForm->addRow(tr("Key password"), keyPass);
    signForm->addRow(QString(), note(tr("Passwords are stored unencrypted in %1, readable only by your user.")
                                         .arg(homeRelative(config().filePath()))));
    signForm->addRow(QString(), toggle(tr("Leave old signature files out of rebuilt JARs"),
                                       QStringLiteral("jar_strip_signatures")));

    auto *layout = new QVBoxLayout;
    layout->setSpacing(14);
    layout->addWidget(unpack);
    layout->addWidget(sign);
    return page(layout);
}

QWidget *SettingsDialog::buildTools()
{
    auto *panel = new ToolsPanel;
    // a custom Java path changes what the panel finds
    connect(&config(), &Config::changed, panel, [panel](const QString &key) {
        if (key == QLatin1String("java_path"))
            panel->refresh();
    });
    auto *layout = new QVBoxLayout;
    layout->addWidget(note(tr("ShiftDex relies on these tools. APKEditor and the signer are downloaded from "
                              "their official GitHub releases into your user folder; nothing is installed "
                              "system-wide.")));
    layout->addSpacing(8);
    layout->addWidget(panel);

    auto *java = new QGroupBox(tr("Java options"));
    auto *javaForm = new QFormLayout(java);
    javaForm->addRow(tr("Java binary"), pathField(this, QStringLiteral("java_path"), tr("Found automatically"), false));
    auto *heap = textField(QStringLiteral("java_heap"), tr("Default"));
    heap->setMaximumWidth(120);
    javaForm->addRow(tr("Memory limit"), heap);
    javaForm->addRow(QString(), note(tr("For very large apps, for example 4g. Leave empty to let Java decide.")));
    layout->addSpacing(8);
    layout->addWidget(java);
    return page(layout);
}

// ---------------------------------------------------------------- shortcuts

QWidget *SettingsDialog::buildShortcuts()
{
    m_shortcuts = new QTreeWidget;
    m_shortcuts->setHeaderLabels({tr("Action"), tr("Shortcut")});
    m_shortcuts->setRootIsDecorated(false);
    m_shortcuts->setUniformRowHeights(true);
    m_shortcuts->setAlternatingRowColors(true);
    m_shortcuts->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_shortcuts->header()->setStretchLastSection(false);

    m_sequence = new QKeySequenceEdit;
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    m_sequence->setMaximumSequenceLength(1);
#endif
    m_assign = new QPushButton(tr("Assign"));
    m_clear = new QPushButton(tr("Remove"));
    m_resetOne = new QPushButton(tr("Default"));
    auto *resetAll = new QPushButton(tr("Reset All"));
    m_shortcutNote = note(QString());

    auto *row = new QHBoxLayout;
    row->addWidget(new QLabel(tr("New shortcut:")));
    row->addWidget(m_sequence, 1);
    row->addWidget(m_assign);
    row->addWidget(m_clear);
    row->addWidget(m_resetOne);
    row->addSpacing(12);
    row->addWidget(resetAll);

    auto *widget = new QWidget;
    auto *layout = new QVBoxLayout(widget);
    layout->setContentsMargins(16, 16, 16, 8);
    layout->addWidget(note(tr("Select an action, click the field below and press the keys you want.")));
    layout->addWidget(m_shortcuts, 1);
    layout->addLayout(row);
    layout->addWidget(m_shortcutNote);

    auto updateState = [this] {
        const QString id = selectedAction();
        const bool selected = !id.isEmpty();
        m_sequence->setEnabled(selected);
        m_clear->setEnabled(selected && !Keymap::current(id).isEmpty());
        m_resetOne->setEnabled(selected && Keymap::current(id) != Keymap::defaultFor(id));
        const QKeySequence wanted = m_sequence->keySequence();
        const QString clash = selected ? Keymap::conflict(id, wanted) : QString();
        m_assign->setEnabled(selected && !wanted.isEmpty() && clash.isEmpty() && wanted != Keymap::current(id));
        QString message;
        if (!clash.isEmpty()) {
            for (const Keymap::Entry &entry : Keymap::entries()) {
                if (clash == QLatin1String(entry.id))
                    message = tr("%1 is already used by “%2”.")
                                  .arg(wanted.toString(QKeySequence::NativeText),
                                       QCoreApplication::translate("Keymap", entry.label));
            }
        }
        m_shortcutNote->setText(message);
    };

    connect(m_shortcuts, &QTreeWidget::itemSelectionChanged, this, [this, updateState] {
        m_sequence->clear();
        updateState();
    });
    connect(m_sequence, &QKeySequenceEdit::keySequenceChanged, this, updateState);
    connect(m_assign, &QPushButton::clicked, this, [this, updateState] {
        Keymap::assign(selectedAction(), m_sequence->keySequence());
        m_sequence->clear();
        loadShortcuts();
        updateState();
    });
    connect(m_clear, &QPushButton::clicked, this, [this, updateState] {
        Keymap::assign(selectedAction(), QKeySequence());
        loadShortcuts();
        updateState();
    });
    connect(m_resetOne, &QPushButton::clicked, this, [this, updateState] {
        const QString id = selectedAction();
        const QString clash = Keymap::conflict(id, Keymap::defaultFor(id));
        if (!clash.isEmpty()) {
            m_shortcutNote->setText(tr("The default shortcut is in use by another action. Change that one first."));
            return;
        }
        Keymap::assign(id, Keymap::defaultFor(id));
        loadShortcuts();
        updateState();
    });
    connect(resetAll, &QPushButton::clicked, this, [this, updateState] {
        if (!Util::confirm(this, tr("Reset Shortcuts"), tr("Put every shortcut back to its default?"), tr("Reset All")))
            return;
        Keymap::resetAll();
        loadShortcuts();
        updateState();
    });

    loadShortcuts();
    updateState();
    return widget;
}

QString SettingsDialog::selectedAction() const
{
    const QTreeWidgetItem *item = m_shortcuts->currentItem();
    return item ? item->data(0, Qt::UserRole).toString() : QString();
}

void SettingsDialog::loadShortcuts()
{
    const QString keep = selectedAction();
    m_shortcuts->clear();
    QString section;
    for (const Keymap::Entry &entry : Keymap::entries()) {
        if (section != QLatin1String(entry.section)) {
            section = QString::fromLatin1(entry.section);
            auto *header = new QTreeWidgetItem(m_shortcuts);
            header->setText(0, section);
            header->setFlags(Qt::ItemIsEnabled);
            QFont bold = header->font(0);
            bold.setBold(true);
            header->setFont(0, bold);
        }
        const QString id = QString::fromLatin1(entry.id);
        auto *item = new QTreeWidgetItem(m_shortcuts);
        item->setText(0, QStringLiteral("    ") + QCoreApplication::translate("Keymap", entry.label));
        const QKeySequence sequence = Keymap::current(id);
        item->setText(1, sequence.isEmpty() ? tr("None") : sequence.toString(QKeySequence::NativeText));
        if (sequence != Keymap::defaultFor(id)) {
            QFont bold = item->font(1);
            bold.setBold(true);
            item->setFont(1, bold);
        }
        item->setData(0, Qt::UserRole, id);
        if (id == keep)
            m_shortcuts->setCurrentItem(item);
    }
    m_shortcuts->resizeColumnToContents(1);
}
