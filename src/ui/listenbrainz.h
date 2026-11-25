// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QObject>
#include <QUrl>
#include <QtQmlIntegration/qqmlintegration.h>

#include <functional>
#include <memory>

#include "scrobbler.h"

class QJSEngine;
class QQmlEngine;

namespace mmm {
class PlaybackController;
}

// ListenBrainz as seen by QML (singleton `ListenBrainz`, M14): the account, submitting on/off,
// the queue. Wraps core mmm::Scrobbler and feeds it the PlaybackController's plays. Keeps the
// user name and on/off in QSettings (the token is in the keyring). Construct it after Playback
// and before the QML engine.
class ListenBrainz : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    // "disconnected", "checking", "connected", "expired"
    Q_PROPERTY(QString state READ state NOTIFY changed FINAL)
    Q_PROPERTY(bool connected READ isConnected NOTIFY changed FINAL)
    Q_PROPERTY(bool enabled READ isEnabled WRITE setEnabled NOTIFY changed FINAL)
    Q_PROPERTY(QString user READ user NOTIFY changed FINAL)
    Q_PROPERTY(int queued READ queued NOTIFY queueChanged FINAL)
    Q_PROPERTY(QString errorText READ errorText NOTIFY changed FINAL)
    Q_PROPERTY(QString statusText READ statusText NOTIFY statusChanged FINAL)
    Q_PROPERTY(QString toolTip READ toolTip NOTIFY statusChanged FINAL)
    Q_PROPERTY(QString lastListen READ lastListen NOTIFY changed FINAL)

public:
    struct Options
    {
        QString databasePath;                       // the queue (its own file: main.cpp)
        bool persistSettings = true;                // QSettings; tests: false
        std::shared_ptr<mmm::SecretStore> secrets;  // default: the keyring
        QUrl baseUrl;                               // default: ListenBrainz (MMM_LISTENBRAINZ_URL)
        std::function<void(const QUrl &)> openUrl;  // default: the desktop's browser
        bool watchNetwork = true;
        mmm::PlaybackController *playback = nullptr; // default: Playback's
    };

    explicit ListenBrainz(Options options, QObject *parent = nullptr);
    ~ListenBrainz() override;

    static ListenBrainz *instance();
    static ListenBrainz *create(QQmlEngine *, QJSEngine *);

    QString state() const;
    bool isConnected() const { return m_s->isConnected(); }
    bool isEnabled() const { return m_s->isEnabled(); }
    void setEnabled(bool on) { m_s->setEnabled(on); }
    QString user() const { return m_s->user(); }
    int queued() const { return m_s->queued(); }
    QString errorText() const;
    QString statusText() const;
    QString toolTip() const;
    QString lastListen() const { return m_s->lastListen(); }

    Q_INVOKABLE void connectAccount(const QString &token) { m_s->connectAccount(token); }
    Q_INVOKABLE void cancelConnect() { m_s->cancelConnect(); }
    Q_INVOKABLE void disconnectAccount() { m_s->disconnectAccount(); }
    Q_INVOKABLE void retryNow() { m_s->retryNow(); }
    // Opens the ListenBrainz settings page (where the user token is) in the browser.
    Q_INVOKABLE void openTokenPage();

    mmm::Scrobbler *scrobbler() const { return m_s.get(); }

Q_SIGNALS:
    void changed();
    void queueChanged();
    void statusChanged();

private:
    void save();

    Options m_options;
    std::unique_ptr<mmm::Scrobbler> m_s;
};
