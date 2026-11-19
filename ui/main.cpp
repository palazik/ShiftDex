// ShiftDex - unpack, edit and repack APK and JAR files
// Copyright (C) 2026 palaziks
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <https://www.gnu.org/licenses/>.
#include "BackupsDialog.h"
#include "Config.h"
#include "Engine.h"
#include "IdentityDialog.h"
#include "MainWindow.h"
#include "PluginsDialog.h"
#include "Theme.h"
#include "Workspace.h"

#include <QAbstractButton>
#include <QApplication>
#include <QCommandLineParser>
#include <QFileInfo>
#include <QIcon>
#include <QLineEdit>
#include <QTimer>

namespace {

// Developer aid (see docs/BUILDING.md): run a few UI steps, save a picture
// of the window or of the dialog on top of it, and quit. Works with
// QT_QPA_PLATFORM=offscreen, so the interface can be checked without a display.
//
//   --screenshot out.png --view "click:Get Started;click:Next"
//   --project <dir> --screenshot out.png --view "file:AndroidManifest.xml;dialog:backups"
void runStep(MainWindow *window, const QString &step)
{
    const QString kind = step.section(QLatin1Char(':'), 0, 0);
    const QString value = step.section(QLatin1Char(':'), 1);
    const QJsonObject project = window->workspace()->project();
    if (kind == QLatin1String("click")) {
        QWidget *scope = QApplication::activeModalWidget() ? QApplication::activeModalWidget() : window;
        const auto buttons = scope->findChildren<QAbstractButton *>();
        for (QAbstractButton *button : buttons) {
            if (button->isVisible() && button->isEnabled() && button->text().remove(QLatin1Char('&')) == value) {
                button->click();
                return;
            }
        }
        qWarning("screenshot: no button \"%s\"", qPrintable(value));
    } else if (kind == QLatin1String("file")) {
        window->workspace()->openFile(project.value(QStringLiteral("src")).toString() + QLatin1Char('/') + value);
    } else if (kind == QLatin1String("type")) {
        if (auto *edit = qobject_cast<QLineEdit *>(QApplication::focusWidget()))
            edit->setText(value);
    } else if (kind == QLatin1String("search")) {
        window->workspace()->searchProject(value == QLatin1String("names"));
    } else if (kind == QLatin1String("find")) {
        window->workspace()->find(value == QLatin1String("replace"));
    } else if (kind == QLatin1String("codeview")) {
        if (window->workspace()->codeView() != value)
            window->workspace()->switchCode();
    } else if (kind == QLatin1String("switchcode")) {
        window->workspace()->switchCode();
    } else if (kind == QLatin1String("theme")) {
        Config::instance().set(QStringLiteral("theme"), value);
    } else if (kind == QLatin1String("dialog")) {
        // exec() blocks until the dialog closes, so later steps run from its event loop
        if (value == QLatin1String("preferences"))
            window->showPreferences(0);
        else if (value == QLatin1String("editor"))
            window->showPreferences(1);
        else if (value == QLatin1String("building"))
            window->showPreferences(2);
        else if (value == QLatin1String("tools"))
            window->showPreferences(3);
        else if (value == QLatin1String("shortcuts"))
            window->showPreferences(4);
        else if (value == QLatin1String("about"))
            window->showAbout();
        else if (value == QLatin1String("plugins"))
            PluginsDialog(project, window).exec();
        else if (value == QLatin1String("new-plugin"))
            PluginCreator(project, window).exec();
        else if (value == QLatin1String("backups") && !project.isEmpty())
            BackupsDialog(project, QString(), window).exec();
        else if (value == QLatin1String("identity") && !project.isEmpty())
            IdentityDialog(project, window).exec();
    }
}

void scheduleScreenshot(MainWindow *window, const QString &file, const QString &script)
{
    const QStringList steps = script.split(QLatin1Char(';'), Qt::SkipEmptyParts);
    int delay = 800;
    for (const QString &step : steps) {
        QTimer::singleShot(delay, window, [window, step] { runStep(window, step.trimmed()); });
        delay += 700;
    }
    QTimer::singleShot(delay + 600, window, [file] {
        QWidget *target = QApplication::activeModalWidget();
        if (!target)
            target = QApplication::activeWindow();
        if (!target && !QApplication::topLevelWidgets().isEmpty())
            target = QApplication::topLevelWidgets().constFirst();
        if (target)
            target->grab().save(file);
        QApplication::exit(0);
    });
}

} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("shiftdex"));
    QApplication::setApplicationDisplayName(QStringLiteral("ShiftDex"));
    QApplication::setApplicationVersion(QStringLiteral(SHIFTDEX_VERSION));
    QApplication::setOrganizationName(QStringLiteral("palaziks"));
    QGuiApplication::setDesktopFileName(QStringLiteral("io.github.palazik.ShiftDex"));
    QApplication::setWindowIcon(QIcon(QStringLiteral(":/icons/app.svg")));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral(
        "Unpack, edit and repack APK and JAR files.\n"
        "Free software under the GNU GPL v3 or later. Made by palaziks."));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument(QStringLiteral("file"), QStringLiteral("APK or JAR file to open."), QStringLiteral("[file]"));
    const QCommandLineOption projectOption(QStringLiteral("project"),
                                           QStringLiteral("Open an existing project folder."), QStringLiteral("folder"));
    QCommandLineOption screenshotOption(QStringLiteral("screenshot"), QStringLiteral("Save a screenshot and quit."),
                                        QStringLiteral("png"));
    QCommandLineOption viewOption(QStringLiteral("view"), QStringLiteral("Steps to run before --screenshot."),
                                  QStringLiteral("steps"));
    screenshotOption.setFlags(QCommandLineOption::HiddenFromHelp);
    viewOption.setFlags(QCommandLineOption::HiddenFromHelp);
    parser.addOptions({projectOption, screenshotOption, viewOption});
    parser.process(app);

    Theme::apply(app, Config::instance().str(QStringLiteral("theme")));

    MainWindow window;
    window.show();

    if (parser.isSet(projectOption)) {
        QJsonValue project;
        if (Engine::call({QStringLiteral("info"), QFileInfo(parser.value(projectOption)).absoluteFilePath()}, &project))
            window.openProject(project.toObject());
    } else if (!parser.positionalArguments().isEmpty()) {
        const QString file = QFileInfo(parser.positionalArguments().constFirst()).absoluteFilePath();
        QTimer::singleShot(0, &window, [&window, file] { window.openPath(file); });
    }
    if (parser.isSet(screenshotOption))
        scheduleScreenshot(&window, parser.value(screenshotOption), parser.value(viewOption));

    return QApplication::exec();
}
