// ShiftDex - unpack, edit and repack APK and JAR files
// Copyright (C) 2026 palaziks
// SPDX-License-Identifier: GPL-3.0-or-later
#include "IdentityDialog.h"

#include "Engine.h"
#include "JobDialog.h"
#include "Util.h"

#include <QCheckBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPushButton>
#include <QRegularExpressionValidator>
#include <QTemporaryDir>
#include <QVBoxLayout>

IdentityDialog::IdentityDialog(const QJsonObject &project, QWidget *parent)
    : QDialog(parent), m_project(project)
{
    setWindowTitle(tr("App Name, Icon and Package"));
    setMinimumWidth(560);

    QJsonValue result;
    QString error;
    if (Engine::call({QStringLiteral("identity"), project.value(QStringLiteral("dir")).toString()}, &result, &error))
        m_current = result.toObject();

    m_icon = new QLabel;
    m_icon->setFixedSize(96, 96);
    m_icon->setAlignment(Qt::AlignCenter);
    m_icon->setFrameShape(QFrame::StyledPanel);
    m_iconNote = new QLabel;
    m_iconNote->setObjectName(QStringLiteral("muted"));
    m_iconNote->setWordWrap(true);
    auto *choose = new QPushButton(tr("Choose Image…"));
    choose->setAutoDefault(false);
    choose->setEnabled(m_current.value(QStringLiteral("has_icon")).toBool());
    connect(choose, &QPushButton::clicked, this, &IdentityDialog::chooseIcon);

    auto *iconSide = new QVBoxLayout;
    iconSide->addWidget(choose, 0, Qt::AlignLeft);
    iconSide->addWidget(m_iconNote);
    iconSide->addStretch();
    auto *iconRow = new QHBoxLayout;
    iconRow->setSpacing(14);
    iconRow->addWidget(m_icon);
    iconRow->addLayout(iconSide, 1);

    m_name = new QLineEdit(m_current.value(QStringLiteral("label")).toString());
    m_package = new QLineEdit(m_current.value(QStringLiteral("package")).toString());
    m_versionName = new QLineEdit(m_current.value(QStringLiteral("version_name")).toString());
    m_versionCode = new QLineEdit(m_current.value(QStringLiteral("version_code")).toString());
    m_versionCode->setValidator(new QRegularExpressionValidator(QRegularExpression(QStringLiteral("\\d{0,10}")), this));
    m_versionCode->setMaximumWidth(140);

    m_allLocales = new QCheckBox(tr("Rename in all translations"));
    m_allLocales->setChecked(true);
    m_allLocales->setToolTip(tr("When off, only the default language gets the new name."));
    m_renameRefs = new QCheckBox(tr("Also rename authorities and permissions"));
    m_renameRefs->setChecked(true);
    m_renameRefs->setToolTip(tr("Needed to install the app next to the original. Code that hard-codes the "
                                "old authority may need a matching edit."));

    auto *form = new QFormLayout;
    form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
    form->setVerticalSpacing(8);
    form->addRow(tr("App name"), m_name);
    form->addRow(QString(), m_allLocales);
    form->addRow(tr("Package name"), m_package);
    form->addRow(QString(), m_renameRefs);
    form->addRow(tr("Version name"), m_versionName);
    form->addRow(tr("Version code"), m_versionCode);

    auto *applyButton = new QPushButton(tr("Apply"));
    applyButton->setObjectName(QStringLiteral("primary"));
    applyButton->setDefault(true);
    auto *cancel = new QPushButton(tr("Cancel"));
    connect(applyButton, &QPushButton::clicked, this, &IdentityDialog::apply);
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    auto *buttons = new QHBoxLayout;
    buttons->addStretch();
    buttons->addWidget(cancel);
    buttons->addWidget(applyButton);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(18, 18, 18, 14);
    layout->setSpacing(14);
    layout->addLayout(iconRow);
    layout->addLayout(form);
    if (m_current.isEmpty()) {
        auto *problem = new QLabel(error.isEmpty() ? tr("The manifest could not be read.") : error);
        problem->setWordWrap(true);
        layout->addWidget(problem);
        applyButton->setEnabled(false);
    }
    layout->addLayout(buttons);

    showIcon(m_current.value(QStringLiteral("icon_path")).toString());
    if (!m_current.value(QStringLiteral("has_icon")).toBool())
        m_iconNote->setText(tr("This app's manifest does not declare an icon."));
    else
        m_iconNote->setText(tr("PNG, JPEG, WebP or SVG. A square image of 512 × 512 or larger works best."));
}

void IdentityDialog::showIcon(const QString &path)
{
    QImageReader reader(path);
    reader.setDecideFormatFromContent(true);
    if (reader.format() == "svg")
        reader.setScaledSize(QSize(192, 192));
    const QImage image = reader.read();
    if (image.isNull()) {
        m_icon->setText(tr("No icon"));
        return;
    }
    m_icon->setPixmap(QPixmap::fromImage(image).scaled(84, 84, Qt::KeepAspectRatio, Qt::SmoothTransformation));
}

void IdentityDialog::chooseIcon()
{
    const QString file = QFileDialog::getOpenFileName(this, tr("Choose the New Icon"), QDir::homePath(),
                                                      tr("Images (*.png *.jpg *.jpeg *.webp *.svg *.bmp)"));
    if (file.isEmpty())
        return;
    QImageReader reader(file);
    reader.setDecideFormatFromContent(true);
    if (!reader.canRead()) {
        Util::error(this, tr("Choose Icon"), tr("That file is not an image ShiftDex can read."));
        return;
    }
    m_newIcon = file;
    showIcon(file);
    m_iconNote->setText(tr("New icon: %1").arg(QFileInfo(file).fileName()));
}

bool IdentityDialog::renderIcons(const QString &directory, QString *error) const
{
    // The engine has no image library; it gets the icon pre-rendered in
    // every size the app's resources use.
    QImageReader reader(m_newIcon);
    reader.setDecideFormatFromContent(true);
    if (reader.format() == "svg")
        reader.setScaledSize(QSize(512, 512));
    const QImage source = reader.read();
    if (source.isNull()) {
        *error = reader.errorString();
        return false;
    }
    for (const QJsonValue &value : m_current.value(QStringLiteral("icon_sizes")).toArray()) {
        const int size = value.toInt();
        if (size <= 0)
            continue;
        QImage canvas(size, size, QImage::Format_ARGB32);
        canvas.fill(Qt::transparent);
        const QImage scaled = source.scaled(size, size, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        QPainter painter(&canvas);
        painter.drawImage((size - scaled.width()) / 2, (size - scaled.height()) / 2, scaled);
        painter.end();
        if (!canvas.save(QStringLiteral("%1/%2.png").arg(directory).arg(size), "PNG")) {
            *error = tr("The icon could not be rendered at %1 px.").arg(size);
            return false;
        }
    }
    return true;
}

void IdentityDialog::apply()
{
    QJsonObject spec;
    const QString name = m_name->text().trimmed();
    const QString package = m_package->text().trimmed();
    if (name.isEmpty()) {
        Util::error(this, windowTitle(), tr("The app name cannot be empty."));
        return;
    }
    if (name != m_current.value(QStringLiteral("label")).toString())
        spec.insert(QStringLiteral("label"), name);
    if (package != m_current.value(QStringLiteral("package")).toString()) {
        static const QRegularExpression valid(QStringLiteral("^[A-Za-z][A-Za-z0-9_]*(\\.[A-Za-z][A-Za-z0-9_]*)+$"));
        if (!valid.match(package).hasMatch()) {
            Util::error(this, windowTitle(),
                        tr("A package name needs at least two parts separated by dots, such as "
                           "com.example.app. Each part starts with a letter and may contain letters, digits "
                           "and underscores."));
            return;
        }
        spec.insert(QStringLiteral("package"), package);
    }
    if (m_versionName->text() != m_current.value(QStringLiteral("version_name")).toString())
        spec.insert(QStringLiteral("version_name"), m_versionName->text());
    if (!m_versionCode->text().isEmpty()
        && m_versionCode->text() != m_current.value(QStringLiteral("version_code")).toString())
        spec.insert(QStringLiteral("version_code"), m_versionCode->text());

    // lives until the job is done
    auto temp = std::make_shared<QTemporaryDir>();
    if (!temp->isValid()) {
        Util::error(this, windowTitle(), tr("A temporary folder could not be created."));
        return;
    }
    if (!m_newIcon.isEmpty()) {
        QString error;
        if (!renderIcons(temp->path(), &error)) {
            Util::error(this, windowTitle(), error);
            return;
        }
        spec.insert(QStringLiteral("icon_dir"), temp->path());
    }
    if (spec.isEmpty()) {
        reject();
        return;
    }
    spec.insert(QStringLiteral("all_locales"), m_allLocales->isChecked());
    spec.insert(QStringLiteral("rename_refs"), m_renameRefs->isChecked());

    const QString specFile = temp->filePath(QStringLiteral("request.json"));
    QFile file(specFile);
    if (!file.open(QIODevice::WriteOnly)) {
        Util::error(this, windowTitle(), file.errorString());
        return;
    }
    file.write(QJsonDocument(spec).toJson());
    file.close();

    JobDialog::run(this, tr("Applying Changes"),
                   {QStringLiteral("identity-apply"), m_project.value(QStringLiteral("dir")).toString(),
                    QStringLiteral("--spec"), specFile},
                   [this, temp](bool ok, const QJsonValue &result) {
                       if (!ok)
                           return;
                       emit applied(result.toObject().value(QStringLiteral("project")).toObject());
                       accept();
                   });
}
