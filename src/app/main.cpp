// SPDX-License-Identifier: GPL-3.0-or-later
#include <QCommandLineParser>
#include <QDir>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QTimer>

#include "appinfo.h"
#include "apppaths.h"
#include "coverimageprovider.h"
#include "devices.h"
#include "library.h"
#include "playback.h"

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    QGuiApplication::setApplicationName(mmm::AppInfo::name());
    QGuiApplication::setApplicationDisplayName(mmm::AppInfo::displayName());
    QGuiApplication::setOrganizationName(mmm::AppInfo::organization());
    QGuiApplication::setApplicationVersion(mmm::AppInfo::version());
    QGuiApplication::setDesktopFileName(mmm::AppInfo::name());

    QCommandLineParser parser;
    parser.setApplicationDescription(mmm::AppInfo::displayName());
    parser.addHelpOption();
    parser.addVersionOption();
    const QCommandLineOption quitAfterLoad(
        QStringLiteral("quit-after-load"),
        QStringLiteral("Load the UI, then exit immediately (smoke test)."));
    const QCommandLineOption databaseOpt(
        QStringLiteral("database"),
        QStringLiteral("Library database to use (default: %1).").arg(mmm::AppPaths::defaultDatabasePath()),
        QStringLiteral("FILE"), mmm::AppPaths::defaultDatabasePath());
    const QCommandLineOption noScanOpt(
        QStringLiteral("no-scan"), QStringLiteral("Don't scan or watch the library folders."));
    const QCommandLineOption noMprisOpt(
        QStringLiteral("no-mpris"), QStringLiteral("Don't register on D-Bus (no media keys / desktop controls)."));
    const QCommandLineOption noDevicesOpt(
        QStringLiteral("no-devices"), QStringLiteral("Don't look for USB drives / DAPs (no UDisks2)."));
    QCommandLineOption screenshotOpt(
        QStringLiteral("screenshot"), QStringLiteral("Save a PNG of the window after loading, then exit."),
        QStringLiteral("FILE"));
    screenshotOpt.setFlags(QCommandLineOption::HiddenFromHelp); // development / testing aid
    QCommandLineOption screenshotDelayOpt(
        QStringLiteral("screenshot-delay"), QStringLiteral("Milliseconds to wait before --screenshot."),
        QStringLiteral("MS"), QStringLiteral("1500"));
    screenshotDelayOpt.setFlags(QCommandLineOption::HiddenFromHelp);
    parser.addOptions({quitAfterLoad, databaseOpt, noScanOpt, noMprisOpt, noDevicesOpt, screenshotOpt, screenshotDelayOpt});
    parser.process(app);

    // Fusion looks the same everywhere; its colours come from Theme (Main.qml's palette).
    if (qEnvironmentVariableIsEmpty("QT_QUICK_CONTROLS_STYLE"))
        QQuickStyle::setStyle(QStringLiteral("Fusion"));

    const bool scan = !parser.isSet(noScanOpt);
    const QString databasePath = parser.value(databaseOpt);
    QDir().mkpath(QFileInfo(databasePath).absolutePath());

    // Created before (and so destroyed after) the QML engine that uses it.
    Library library({.databasePath = databasePath, .scanOnStart = scan, .watch = scan});
    Playback playback({.player = {}, .mpris = !parser.isSet(noMprisOpt), .persistSettings = true});
    Devices devices({.databasePath = databasePath, .monitor = !parser.isSet(noDevicesOpt)});
    QObject::connect(&playback, &Playback::quitRequested, &app, &QCoreApplication::quit);

    QQmlApplicationEngine engine;
    engine.addImageProvider(QStringLiteral("cover"), new CoverImageProvider(mmm::AppPaths::coverCacheDir()));
    QObject::connect(
        &engine, &QQmlApplicationEngine::objectCreationFailed, &app,
        [] { QCoreApplication::exit(EXIT_FAILURE); }, Qt::QueuedConnection);
    engine.loadFromModule(QStringLiteral("MusicMdManager"), QStringLiteral("Main"));

    if (engine.rootObjects().isEmpty())
        return EXIT_FAILURE;

    if (auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().constFirst())) {
        QObject::connect(&playback, &Playback::raiseRequested, window, [window] {
            window->show();
            window->raise();
            window->requestActivate();
        });
    }

    if (parser.isSet(quitAfterLoad))
        QTimer::singleShot(0, &app, &QCoreApplication::quit);

    if (parser.isSet(screenshotOpt)) {
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().constFirst());
        const QString file = parser.value(screenshotOpt);
        QTimer::singleShot(parser.value(screenshotDelayOpt).toInt(), &app, [window, file] {
            const bool ok = window && window->grabWindow().save(file);
            QCoreApplication::exit(ok ? EXIT_SUCCESS : EXIT_FAILURE);
        });
    }

    return QGuiApplication::exec();
}
