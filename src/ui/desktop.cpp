// SPDX-License-Identifier: GPL-3.0-or-later
#include "desktop.h"

#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QJSEngine>

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

bool Desktop::openContainingFolder(const QString &filePath)
{
    return !filePath.isEmpty() && openFolder(QFileInfo(filePath).absolutePath());
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
    return openFolder(commonFolder(filePaths));
}
