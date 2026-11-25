// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QObject>
#include <QStringList>
#include <QUrl>
#include <QtQmlIntegration/qqmlintegration.h>

#include <functional>
#include <optional>

#include "discordipc.h"
#include "discordpresence.h"

class QJSEngine;
class QQmlEngine;

namespace mmm {
class PlaybackController;
}

// Discord status as seen by QML (singleton `Discord`, M15): shows what's playing on the user's
// Discord profile through the Discord desktop client's local socket (core DiscordIpc +
// DiscordPresence), fed by the PlaybackController and its Player. Needs the user's Discord
// application id (its name is what Discord shows: "Listening to <name>"); kept with on/off in
// QSettings (it isn't secret). Construct it after Playback and before the QML engine.
class Discord : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    // "off", "no-app-id", "not-running", "connecting", "connected", "error"
    Q_PROPERTY(QString state READ state NOTIFY changed FINAL)
    Q_PROPERTY(bool enabled READ isEnabled WRITE setEnabled NOTIFY changed FINAL)
    Q_PROPERTY(QString appId READ appId NOTIFY changed FINAL)
    Q_PROPERTY(bool hasAppId READ hasAppId NOTIFY changed FINAL)
    Q_PROPERTY(QString statusText READ statusText NOTIFY changed FINAL)
    Q_PROPERTY(QString toolTip READ toolTip NOTIFY changed FINAL)
    Q_PROPERTY(QString errorText READ errorText NOTIFY changed FINAL)

public:
    struct Options
    {
        bool persistSettings = true;                // QSettings; tests: false
        QString appId;                              // when not persisted (tests)
        std::optional<QStringList> ipcDirs;         // where Discord's socket is (tests: a fake's)
        std::function<void(const QUrl &)> openUrl;  // default: the desktop's browser
        mmm::PlaybackController *playback = nullptr; // default: Playback's
        int firstRetryMs = 5000;
        int coalesceMs = 250;
    };

    explicit Discord(Options options, QObject *parent = nullptr);
    ~Discord() override;

    static Discord *instance();
    static Discord *create(QQmlEngine *, QJSEngine *);

    QString state() const;
    bool isEnabled() const { return m_enabled; }
    void setEnabled(bool on);
    QString appId() const { return m_appId; }
    bool hasAppId() const { return !m_appId.isEmpty(); }
    QString statusText() const;
    QString toolTip() const;
    QString errorText() const;

    // Saves the application id (a first one also switches the status on).
    Q_INVOKABLE void setAppId(const QString &id);
    // The Discord Developer Portal (where applications are made) in the browser.
    Q_INVOKABLE void openDeveloperPortal();

    mmm::DiscordIpc *ipc() { return &m_ipc; }
    mmm::DiscordPresence *presence() { return &m_presence; }

Q_SIGNALS:
    void changed();

private:
    void feed();
    void apply(); // connect / disconnect for the current id and on/off
    void save();

    Options m_options;
    mmm::DiscordIpc m_ipc;
    mmm::DiscordPresence m_presence;
    mmm::PlaybackController *m_pc = nullptr;
    QString m_appId;
    bool m_enabled = false;
    QString m_commandError;
};
