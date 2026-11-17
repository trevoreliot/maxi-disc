// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QDBusConnection>
#include <QObject>
#include <QString>
#include <QVariantMap>

namespace mmm {

class PlaybackController;

// MPRIS 2 (org.mpris.MediaPlayer2 + .Player) on the session bus: media keys, desktop
// players, playerctl, and the base of the Emacs client. Commands go through the
// PlaybackController, the same path the UI uses.
class MprisService : public QObject
{
    Q_OBJECT

public:
    explicit MprisService(PlaybackController *controller, QObject *parent = nullptr);
    ~MprisService() override;

    // Claims org.mpris.MediaPlayer2.musicmdmanager (or a ".instance<pid>" variant if another
    // instance has it) and exports /org/mpris/MediaPlayer2. Returns false if there's no bus.
    bool registerOn(const QDBusConnection &connection, QString *error = nullptr);
    QString serviceName() const { return m_serviceName; }

    // Directory for extracted embedded art (mpris:artUrl). Default: AppPaths::artCacheDir().
    void setArtCacheDir(const QString &dir) { m_artCacheDir = dir; }

    // --- Used by the D-Bus adaptors ---
    PlaybackController *controller() const { return m_controller; }
    QVariantMap metadata() const;
    QString playbackStatus() const;
    QString loopStatus() const;
    void setLoopStatus(const QString &status);

Q_SIGNALS:
    void raiseRequested();
    void quitRequested();

private:
    void notifyPlayer(const QVariantMap &changed);
    void emitAll();

    PlaybackController *m_controller;
    QDBusConnection m_connection;
    QString m_serviceName;
    QString m_artCacheDir;
    // Built once per track (may extract embedded art from the file).
    mutable QVariantMap m_cachedMetadata;
    mutable qint64 m_metadataTrackId = -2;
};

} // namespace mmm
