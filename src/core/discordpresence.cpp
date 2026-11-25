// SPDX-License-Identifier: GPL-3.0-or-later
#include "discordpresence.h"

#include <QCoreApplication>
#include <QDateTime>

#include <algorithm>
#include <cstdlib>

namespace mmm {

namespace {

constexpr int kMaxText = 128;
constexpr qint64 kToleranceS = 2;

} // namespace

DiscordPresence::DiscordPresence(Options options, QObject *parent)
    : QObject(parent)
    , m_options(std::move(options))
{
    if (!m_options.nowMs)
        m_options.nowMs = [] { return QDateTime::currentMSecsSinceEpoch(); };
    if (m_options.pid <= 0)
        m_options.pid = QCoreApplication::applicationPid();
    m_clock.start();
    m_coalesceTimer.setSingleShot(true);
    connect(&m_coalesceTimer, &QTimer::timeout, this, &DiscordPresence::trySend);
    m_windowTimer.setSingleShot(true);
    connect(&m_windowTimer, &QTimer::timeout, this, &DiscordPresence::trySend);
}

QString DiscordPresence::fit(const QString &text)
{
    QString t = text.trimmed();
    if (t.isEmpty())
        return t;
    if (t.toUcs4().size() == 1) // one character (an emoji is two UTF-16 units, still one)
        return t + QChar(0x200B);
    if (t.size() > kMaxText) {
        t.truncate(kMaxText - 1);
        if (t.back().isHighSurrogate())
            t.chop(1);
        t += QChar(0x2026);
    }
    return t;
}

QJsonObject DiscordPresence::activityFor(const DiscordTrack &track, qint64 positionMs, qint64 nowMs)
{
    if (fit(track.title).isEmpty())
        return {};
    // "Song - Artist" on the first line (and the one-line status), the album under it.
    const QString artist = track.artist.trimmed();
    const QString song = fit(artist.isEmpty() ? track.title : track.title.trimmed() + QStringLiteral(" - ") + artist);
    const QString album = fit(track.album);
    const auto [startS, endS] = times(track, positionMs, nowMs);
    QJsonObject timestamps{{QStringLiteral("start"), startS}};
    if (endS > 0)
        timestamps.insert(QStringLiteral("end"), endS);

    QJsonObject activity{{QStringLiteral("type"), 2}, // Listening
                         {QStringLiteral("details"), song},
                         {QStringLiteral("status_display_type"), 2}, // the one-line status: details
                         {QStringLiteral("timestamps"), timestamps}};
    if (!album.isEmpty())
        activity.insert(QStringLiteral("state"), album);
    return activity;
}

std::pair<qint64, qint64> DiscordPresence::times(const DiscordTrack &track, qint64 positionMs, qint64 nowMs)
{
    qint64 position = std::max<qint64>(0, positionMs);
    if (track.durationMs > 0)
        position = std::min(position, track.durationMs);
    const qint64 startS = (nowMs - position) / 1000;
    return {startS, track.durationMs > 0 ? startS + (track.durationMs + 500) / 1000 : 0};
}

QJsonObject DiscordPresence::clearArgs() const
{
    return QJsonObject{{QStringLiteral("pid"), m_options.pid}};
}

void DiscordPresence::update(const std::optional<DiscordTrack> &track, bool playing, qint64 positionMs)
{
    Shown wanted;
    if (track && playing && !fit(track->title).isEmpty()) {
        const auto [startS, endS] = times(*track, positionMs, m_options.nowMs());
        wanted = Shown{true, *track, startS, endS};
    }
    m_wanted = wanted;
    if (m_connected && !(m_sentKnown && same(m_sent, m_wanted)) && !m_coalesceTimer.isActive() && !m_windowTimer.isActive())
        m_coalesceTimer.start(m_options.coalesceMs);
}

void DiscordPresence::setConnected(bool connected)
{
    m_connected = connected;
    m_coalesceTimer.stop();
    m_windowTimer.stop();
    m_sent = Shown{};
    m_sentKnown = connected; // a new connection shows nothing
    if (connected)
        trySend();
}

bool DiscordPresence::same(const Shown &a, const Shown &b) const
{
    if (a.playing != b.playing)
        return false;
    if (!a.playing)
        return true;
    // (Not the length itself: the player's can jitter; the end time covers it.)
    return a.track.title == b.track.title && a.track.artist == b.track.artist && a.track.album == b.track.album
        && std::llabs(a.startS - b.startS) <= kToleranceS && std::llabs(a.endS - b.endS) <= kToleranceS;
}

void DiscordPresence::trySend()
{
    if (!m_connected || (m_sentKnown && same(m_sent, m_wanted)))
        return;
    const qint64 now = m_clock.elapsed();
    m_sentAt.erase(std::remove_if(m_sentAt.begin(), m_sentAt.end(), [&](qint64 t) { return t <= now - m_options.windowMs; }),
                   m_sentAt.end());
    if (m_sentAt.size() >= m_options.maxPerWindow) {
        // The window is full: the latest state goes when the oldest send leaves it.
        m_windowTimer.start(static_cast<int>(std::clamp<qint64>(m_sentAt.first() + m_options.windowMs - now + 1, 1, m_options.windowMs)));
        return;
    }
    QJsonObject args = clearArgs();
    if (m_wanted.playing) {
        // Re-made from the stored times (not a fresh position): what's sent is what's compared.
        QJsonObject activity = activityFor(m_wanted.track, 0, m_wanted.startS * 1000);
        QJsonObject ts{{QStringLiteral("start"), m_wanted.startS}};
        if (m_wanted.endS > 0)
            ts.insert(QStringLiteral("end"), m_wanted.endS);
        activity.insert(QStringLiteral("timestamps"), ts);
        args.insert(QStringLiteral("activity"), activity);
    }
    m_sentAt << now;
    m_sent = m_wanted;
    m_sentKnown = true;
    ++m_sentCount;
    Q_EMIT activityChanged(args);
}

} // namespace mmm
