// SPDX-License-Identifier: GPL-3.0-or-later
#include <QCommandLineParser>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QTimer>

#include "appinfo.h"

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
    parser.addOption(quitAfterLoad);
    parser.process(app);

    QQmlApplicationEngine engine;
    QObject::connect(
        &engine, &QQmlApplicationEngine::objectCreationFailed, &app,
        [] { QCoreApplication::exit(EXIT_FAILURE); }, Qt::QueuedConnection);
    engine.loadFromModule(QStringLiteral("MusicMdManager"), QStringLiteral("Main"));

    if (engine.rootObjects().isEmpty())
        return EXIT_FAILURE;

    if (parser.isSet(quitAfterLoad))
        QTimer::singleShot(0, &app, &QCoreApplication::quit);

    return QGuiApplication::exec();
}
