// SPDX-License-Identifier: GPL-3.0-or-later
#include "desktop.h"

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QJSEngine>
#include <QPointer>

namespace {

Desktop *s_instance = nullptr;

} // namespace

Desktop::Desktop(Options options, QObject *parent)
    : QObject(parent)
    , m_options(std::move(options))
{
    Q_ASSERT_X(!s_instance, "Desktop", "only one Desktop may exist");
    s_instance = this;
    if (!m_options.openUrl)
        m_options.openUrl = [](const QUrl &url) { QDesktopServices::openUrl(url); };
}

Desktop::~Desktop()
{
    s_instance = nullptr;
}

Desktop *Desktop::instance()
{
    return s_instance;
}

Desktop *Desktop::create(QQmlEngine *, QJSEngine *)
{
    Q_ASSERT_X(s_instance, "Desktop", "construct Desktop before the QML engine");
    QJSEngine::setObjectOwnership(s_instance, QJSEngine::CppOwnership);
    return s_instance;
}

bool Desktop::openFolder(const QString &dir)
{
    if (dir.isEmpty() || !QFileInfo(dir).isDir())
        return false;
    m_options.openUrl(QUrl::fromLocalFile(dir));
    return true;
}

bool Desktop::showFile(const QString &filePath)
{
    if (filePath.isEmpty())
        return false;
    const QFileInfo file(filePath);
    if (!file.exists())
        return openFolder(file.absolutePath()); // gone: its folder, if that's still there
    if (!m_options.showItems)
        return openFolder(file.absolutePath());
    QPointer<Desktop> self(this);
    const QString dir = file.absolutePath();
    // A proper URI (spaces, #, % … encoded), as FileManager1 wants.
    const QString uri = QString::fromUtf8(QUrl::fromLocalFile(file.absoluteFilePath()).toEncoded());
    m_options.showItems({uri}, [self, dir](bool ok) {
        if (!ok && self)
            self->openFolder(dir);
    });
    return true;
}

void Desktop::fileManagerShowItems(const QStringList &uris, std::function<void(bool)> done)
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected()) {
        done(false);
        return;
    }
    QDBusMessage call = QDBusMessage::createMethodCall(QStringLiteral("org.freedesktop.FileManager1"),
                                                       QStringLiteral("/org/freedesktop/FileManager1"),
                                                       QStringLiteral("org.freedesktop.FileManager1"),
                                                       QStringLiteral("ShowItems"));
    call << uris << QString(); // (no startup id)
    // (Parented to the app: gone with it, even if it quits before the answer.)
    auto *watcher = new QDBusPendingCallWatcher(bus.asyncCall(call, 5000), QCoreApplication::instance());
    QObject::connect(watcher, &QDBusPendingCallWatcher::finished, watcher, [watcher, done] {
        watcher->deleteLater();
        done(!watcher->isError());
    });
}

QString Desktop::commonFolder(const QStringList &filePaths)
{
    if (filePaths.isEmpty())
        return {};
    const QString first = QFileInfo(filePaths.first()).absolutePath();
    QStringList common = first.split(QLatin1Char('/'));
    for (const QString &path : filePaths) {
        const QStringList parts = QFileInfo(path).absolutePath().split(QLatin1Char('/'));
        qsizetype n = 0;
        while (n < common.size() && n < parts.size() && common[n] == parts[n])
            ++n;
        common = common.first(n);
    }
    const QString dir = common.join(QLatin1Char('/'));
    // Nothing really shared (only "/" or the home folder): the first file's own folder.
    if (dir.isEmpty() || dir == QLatin1String("/") || QDir(dir) == QDir::home())
        return first;
    return dir;
}

bool Desktop::openCommonFolder(const QStringList &filePaths)
{
    if (filePaths.size() == 1)
        return showFile(filePaths.first());
    return openFolder(commonFolder(filePaths));
}
