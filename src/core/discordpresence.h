// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QString>
#include <QElapsedTimer>
#include <QTimer>

#include <functional>
#include <optional>
#include <utility>

namespace mmm {

// What Discord shows about a track.
struct DiscordTrack
{
    QString title;
    QString artist;
    QString album;
    qint64 durationMs = 0; // <= 0: unknown
    friend bool operator==(const DiscordTrack &, const DiscordTrack &) = default;
};

// Discord status (M15): what to show for the playing track, and when to tell Discord. No
// sockets: it emits the SET_ACTIVITY arguments (`activityChanged`) for the caller to send.
//
// Shown: "Listening to <the Discord application's name>", "Song - Artist" (also the one-line
// status in the member list), the album and a time bar (start / end in Unix seconds, which every
// IPC client takes). Paused, stopped or nothing playing: cleared.
//
// Fed with update() on every playback change, position ticks included: changes within
// `coalesceMs` go as one, a time bar off by up to 2 s isn't re-sent, and at most
// `maxPerWindow` updates go out per `windowMs` (Discord allows 5 per 20 s): the latest state
// always goes out once the window allows.
class DiscordPresence : public QObject
{
    Q_OBJECT

public:
    struct Options
    {
        std::function<qint64()> nowMs; // wall clock (default: the system's)
        int coalesceMs = 250;
        int windowMs = 20'000;
        int maxPerWindow = 5;
        qint64 pid = 0; // default: this process
    };

    explicit DiscordPresence(Options options, QObject *parent = nullptr);

    // The activity for `track` played up to `positionMs` at `nowMs`; empty if it has no title.
    static QJsonObject activityFor(const DiscordTrack &track, qint64 positionMs, qint64 nowMs);
    // Discord's text rules: trimmed, at least 2 characters (a zero-width space pads one), at
    // most 128 (cut with "…", never inside a surrogate pair). "" stays "" (left out).
    static QString fit(const QString &text);

    // What's playing now (nullopt or !playing: nothing to show).
    void update(const std::optional<DiscordTrack> &track, bool playing, qint64 positionMs);
    // Connected: shows what's playing (a new connection shows nothing). Disconnected: forgets
    // what Discord had.
    void setConnected(bool connected);
    // The SET_ACTIVITY arguments that clear the activity.
    QJsonObject clearArgs() const;
    int sentCount() const { return m_sentCount; }

Q_SIGNALS:
    void activityChanged(const QJsonObject &setActivityArgs);

private:
    struct Shown
    {
        bool playing = false;
        DiscordTrack track;
        qint64 startS = 0;
        qint64 endS = 0;
    };
    bool same(const Shown &a, const Shown &b) const;
    // start / end (Unix s; end 0: unknown) of `track` played up to `positionMs` at `nowMs`.
    static std::pair<qint64, qint64> times(const DiscordTrack &track, qint64 positionMs, qint64 nowMs);
    void trySend();

    Options m_options;
    Shown m_wanted;
    Shown m_sent;                // what Discord has, if m_sentKnown
    bool m_sentKnown = false;
    bool m_connected = false;
    QElapsedTimer m_clock;       // the rate window's (monotonic: a wall clock change can't jam it)
    QList<qint64> m_sentAt;      // m_clock ms of recent sends
    QTimer m_coalesceTimer;
    QTimer m_windowTimer;
    int m_sentCount = 0;
};

} // namespace mmm
