// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QObject>
#include <QStringList>
#include <QUrl>
#include <QtQmlIntegration/qqmlintegration.h>

#include <functional>

class QJSEngine;
class QQmlEngine;

// The desktop as seen by QML (singleton `Desktop`, Q1 / Q2 context menus): shows a file in
// the file manager (selected, through the FileManager1 D-Bus interface Dolphin, Nautilus and
// others offer; else its folder opens) or opens a folder. Construct it before the QML engine.
// Only the app passes `showItems` (fileManagerShowItems): without it, a file's folder opens.
// Tests pass an `openUrl` that only records, so no file manager opens.
class Desktop : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

public:
    using ShowItems = std::function<void(const QStringList &uris, std::function<void(bool ok)> done)>;
    struct Options
    {
        std::function<void(const QUrl &)> openUrl; // default: QDesktopServices
        ShowItems showItems;                       // none: open the folder instead
    };

    explicit Desktop(Options options, QObject *parent = nullptr);
    ~Desktop() override;

    static Desktop *instance();
    static Desktop *create(QQmlEngine *, QJSEngine *);

    // `filePath` selected in the file manager (its folder if that can't be done); false if gone.
    Q_INVOKABLE bool showFile(const QString &filePath);
    // A folder in the file manager; false if it isn't one.
    Q_INVOKABLE bool openFolder(const QString &dir);
    // The same as showFile (Q1's name).
    Q_INVOKABLE bool openContainingFolder(const QString &filePath) { return showFile(filePath); }
    // The folder the files share (an album with disc folders: the album's), else the first
    // file's folder. One file: shown, like showFile.
    Q_INVOKABLE bool openCommonFolder(const QStringList &filePaths);
    // FileManager1.ShowItems on the session bus (async, 5 s): what the app passes as showItems.
    static void fileManagerShowItems(const QStringList &uris, std::function<void(bool ok)> done);
    // That folder, for tests and the above.
    static QString commonFolder(const QStringList &filePaths);

private:
    Options m_options;
};
