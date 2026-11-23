// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QStringList>
#include <QThread>

#include <memory>
#include <mutex>

#include "netmdbackend.h"

namespace mmm::test {

// An in-memory NetMD recorder for tests (no USB). The backend runs on the NetMD worker
// thread while the test looks on: everything is behind the mutex.
struct FakeRecorder
{
    std::mutex mutex;
    QString header = QStringLiteral("0;Scratch//1-2;Side A//");
    QList<MdTrack> tracks;
    MdCapacity capacity{1'000'000, 4'800'000, 3'800'000, true};
    bool noDisc = false;
    NetMdError openError = NetMdError::None;
    NetMdError writeError = NetMdError::None;
    NetMdError headerReadError = NetMdError::None; // readHeader fails (a USB glitch)
    int delayMs = 0;            // per command, to test busy states
    QStringList calls;          // "open", "readDisc", "readHeader", "writeHeader:<raw>", ...

    FakeRecorder()
    {
        tracks = {MdTrack{0, QStringLiteral("Intro"), 61'000, MdEncoding::Sp, false, false},
                  MdTrack{1, QString(), 245'500, MdEncoding::Lp2, false, false},
                  MdTrack{2, QStringLiteral("Outro"), 3'725'000, MdEncoding::Lp4, true, true}};
    }
    QStringList takeCalls()
    {
        std::lock_guard lock(mutex);
        return std::exchange(calls, {});
    }
};

class FakeNetMdBackend final : public NetMdBackend
{
public:
    explicit FakeNetMdBackend(std::shared_ptr<FakeRecorder> recorder) : m_r(std::move(recorder)) {}

    NetMdError open(const NetMdDevice &) override
    {
        const auto l = step(QStringLiteral("open"));
        m_open = m_r->openError == NetMdError::None;
        return m_r->openError;
    }
    void close() override
    {
        if (m_open) {
            std::lock_guard lock(m_r->mutex);
            m_r->calls << QStringLiteral("close");
        }
        m_open = false;
    }
    bool isOpen() const override { return m_open; }

    NetMdError readDisc(MdDisc *disc) override
    {
        const auto l = step(QStringLiteral("readDisc"));
        if (m_r->noDisc)
            return NetMdError::NoDisc;
        disc->rawHeader = m_r->header;
        disc->tracks = m_r->tracks;
        disc->capacity = m_r->capacity;
        return NetMdError::None;
    }
    NetMdError readHeader(QString *raw) override
    {
        const auto l = step(QStringLiteral("readHeader"));
        if (m_r->headerReadError != NetMdError::None)
            return m_r->headerReadError;
        *raw = m_r->header;
        return NetMdError::None;
    }
    NetMdError readTrackTitle(int index, QString *title) override
    {
        const auto l = step(QStringLiteral("readTrackTitle:%1").arg(index));
        if (index < 0 || index >= m_r->tracks.size())
            return NetMdError::Failed;
        *title = m_r->tracks[index].title;
        return NetMdError::None;
    }
    NetMdError writeHeader(const QString &raw) override
    {
        const auto l = step(QStringLiteral("writeHeader:") + raw);
        if (m_r->writeError == NetMdError::None)
            m_r->header = raw;
        return m_r->writeError;
    }
    NetMdError writeTrackTitle(int index, const QString &title) override
    {
        const auto l = step(QStringLiteral("writeTrackTitle:%1:%2").arg(index).arg(title));
        if (m_r->writeError == NetMdError::None && index >= 0 && index < m_r->tracks.size())
            m_r->tracks[index].title = title;
        return m_r->writeError;
    }

private:
    // Logs the call, waits the configured delay (outside the lock), then holds the lock.
    std::unique_lock<std::mutex> step(const QString &call)
    {
        int delay = 0;
        {
            std::lock_guard lock(m_r->mutex);
            m_r->calls << call;
            delay = m_r->delayMs;
        }
        if (delay > 0)
            QThread::msleep(static_cast<unsigned long>(delay));
        return std::unique_lock(m_r->mutex);
    }

    std::shared_ptr<FakeRecorder> m_r;
    bool m_open = false;
};

inline NetMdBackendFactory fakeNetMd(std::shared_ptr<FakeRecorder> recorder)
{
    return [recorder] { return std::make_unique<FakeNetMdBackend>(recorder); };
}

} // namespace mmm::test
