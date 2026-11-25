// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QObject>
#include <QStringList>
#include <QUrl>
#include <QtQmlIntegration/qqmlintegration.h>

#include <functional>

class QJSEngine;
class QQmlEngine;

// The desktop as seen by QML (singleton `Desktop`, Q1 context menus): opens a track's folder
// or an album's in the file manager. Construct it before the QML engine; tests pass an
// `openUrl` that only records, so no file manager opens.
class Desktop : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

public:
    struct Options
    {
        std::function<void(const QUrl &)> openUrl; // default: QDesktopServices
    };

    explicit Desktop(Options options, QObject *parent = nullptr);
    ~Desktop() override;

    static Desktop *instance();
    static Desktop *create(QQmlEngine *, QJSEngine *);

    // The folder holding `filePath`; false if it's gone.
    Q_INVOKABLE bool openContainingFolder(const QString &filePath);
    // The folder the files share (an album with disc folders: the album's), else the first
    // file's folder.
    Q_INVOKABLE bool openCommonFolder(const QStringList &filePaths);
    // That folder, for tests and the above.
    static QString commonFolder(const QStringList &filePaths);

private:
    bool openFolder(const QString &dir);

    Options m_options;
};
