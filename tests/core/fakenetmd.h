// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QFile>
#include <QFileInfo>
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
    NetMdError uploadError = NetMdError::None;  // uploadTrack fails (disc full, unplugged)
    int failUploadAt = -1;      // ... only for the upload with this number (0-based); -1: all
    int uploads = 0;            // uploads attempted
    QList<MdRecordMode> uploadModes; // the mode of each upload attempted
    int uploadStepMs = 0;       // per progress step of an upload (4 steps)
    NetMdError editError = NetMdError::None; // deleteTrack / moveTrack / eraseDisc fail (M12b)
    int failEditAt = -1;        // ... only the edit with this number (0-based); -1: all
    int edits = 0;              // edits attempted
    QStringList calls;          // "open", "readDisc", "readHeader", "writeHeader:<raw>", ...
    // M12c: what ran when, across the two threads: "encode:<file>", "encoded:<file>",
    // "encode-failed:<file>", "encode-canceled:<file>" (the encoder, if given this recorder),
    // "upload:<title>", "uploaded:<title>".
    QStringList events;

    FakeRecorder() { tracks = startTracks(); }
    static QList<MdTrack> startTracks()
    {
        return {MdTrack{0, QStringLiteral("Intro"), 61'000, MdEncoding::Sp, false, false},
                MdTrack{1, QString(), 245'500, MdEncoding::Lp2, false, false},
                MdTrack{2, QStringLiteral("Outro"), 3'725'000, MdEncoding::Lp4, true, true}};
    }
    // The disc it starts with, no errors (tests sharing one recorder put it back).
    void reset()
    {
        std::lock_guard lock(mutex);
        header = QStringLiteral("0;Scratch//1-2;Side A//");
        tracks = startTracks();
        capacity = MdCapacity{1'000'000, 4'800'000, 3'800'000, true};
        noDisc = false;
        events.clear();
        openError = writeError = headerReadError = uploadError = editError = NetMdError::None;
        failUploadAt = failEditAt = -1;
        delayMs = uploadStepMs = 0;
    }
    QStringList takeCalls()
    {
        std::lock_guard lock(mutex);
        return std::exchange(calls, {});
    }
    QStringList takeEvents()
    {
        std::lock_guard lock(mutex);
        return std::exchange(events, {});
    }
    void event(const QString &e)
    {
        std::lock_guard lock(mutex);
        events << e;
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

    NetMdError uploadTrack(const MdEncodedAudio &audio, const QString &title, const UploadProgress &progress,
                           int *trackIndex) override
    {
        int stepMs = 0;
        NetMdError error = NetMdError::None;
        {
            const auto l = step(QStringLiteral("uploadTrack:%1:%2").arg(title).arg(audio.bytes));
            m_r->events << QStringLiteral("upload:") + title;
            stepMs = m_r->uploadStepMs;
            m_r->uploadModes << audio.mode;
            if (m_r->failUploadAt < 0 || m_r->failUploadAt == m_r->uploads)
                error = m_r->uploadError;
            ++m_r->uploads;
        }
        // Progress in four steps, like the real one's 64 KB pieces.
        for (int i = 1; i <= 4; ++i) {
            if (stepMs > 0)
                QThread::msleep(static_cast<unsigned long>(stepMs));
            if (progress)
                progress(audio.bytes * i / 4, audio.bytes);
        }
        m_r->event(QStringLiteral("uploaded:") + title);
        if (error != NetMdError::None)
            return error;
        std::lock_guard lock(m_r->mutex);
        const int index = static_cast<int>(m_r->tracks.size());
        const MdEncoding encoding = audio.mode == MdRecordMode::Lp4 ? MdEncoding::Lp4
            : audio.mode == MdRecordMode::Lp2                      ? MdEncoding::Lp2
                                                                   : MdEncoding::Sp;
        m_r->tracks << MdTrack{index, title, audio.durationMs, encoding, false, false};
        const qint64 discMs = audio.durationMs / mdModeFactor(audio.mode); // capacity is SP time
        m_r->capacity.recordedMs += discMs;
        m_r->capacity.availableMs -= discMs;
        if (trackIndex)
            *trackIndex = index;
        return NetMdError::None;
    }

    // Like a recorder: the tracks change, the header (groups) doesn't.
    NetMdError deleteTrack(int index) override
    {
        const auto l = step(QStringLiteral("deleteTrack:%1").arg(index));
        if (const NetMdError e = editFails(); e != NetMdError::None)
            return e;
        if (index < 0 || index >= m_r->tracks.size())
            return NetMdError::Rejected;
        const qint64 ms = m_r->tracks.at(index).durationMs;
        m_r->tracks.removeAt(index);
        renumber();
        m_r->capacity.recordedMs -= ms;
        m_r->capacity.availableMs += ms;
        return NetMdError::None;
    }
    NetMdError moveTrack(int from, int to) override
    {
        const auto l = step(QStringLiteral("moveTrack:%1:%2").arg(from).arg(to));
        if (const NetMdError e = editFails(); e != NetMdError::None)
            return e;
        if (from < 0 || from >= m_r->tracks.size() || to < 0 || to >= m_r->tracks.size())
            return NetMdError::Rejected;
        m_r->tracks.move(from, to);
        renumber();
        return NetMdError::None;
    }
    NetMdError eraseDisc() override
    {
        const auto l = step(QStringLiteral("eraseDisc"));
        if (const NetMdError e = editFails(); e != NetMdError::None)
            return e;
        m_r->tracks.clear();
        m_r->header.clear();
        m_r->capacity.recordedMs = 0;
        m_r->capacity.availableMs = m_r->capacity.totalMs;
        return NetMdError::None;
    }

private:
    // Under the lock.
    NetMdError editFails()
    {
        const bool fails = m_r->failEditAt < 0 || m_r->failEditAt == m_r->edits;
        ++m_r->edits;
        return fails ? m_r->editError : NetMdError::None;
    }
    void renumber()
    {
        for (int i = 0; i < m_r->tracks.size(); ++i)
            m_r->tracks[i].index = i;
    }

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

// "Decodes" by writing a small file (a second of silence per track, in the mode's format: PCM, or
// whole 384 / 192-byte ATRAC3 frames), no GStreamer or atracdenc. Can fail
// for a path containing "broken"; waits `delayMs` (polling cancel) to test canceling a decode,
// 600 ms for a path containing "slow". Logs to `log`'s events if given.
struct FakeEncoder final : MdAudioEncoder
{
    int delayMs = 0;
    std::shared_ptr<FakeRecorder> log;
    bool encode(const QString &source, MdRecordMode mode, const QString &destination, const std::atomic_bool &cancel,
                MdEncodedAudio *out, QString *error) override
    {
        const QString name = QFileInfo(source).completeBaseName();
        const auto note = [&](const char *what) {
            if (log)
                log->event(QLatin1String(what) + name);
        };
        note("encode:");
        const int wait = source.contains(QLatin1String("slow")) ? 600 : delayMs;
        for (int waited = 0; waited < wait; waited += 10) {
            if (cancel.load()) {
                if (error)
                    *error = kEncodeCanceled;
                note("encode-canceled:");
                return false;
            }
            QThread::msleep(10);
        }
        if (source.contains(QLatin1String("broken"))) {
            if (error)
                *error = QStringLiteral("cannot decode %1").arg(source);
            note("encode-failed:");
            return false;
        }
        QFile f(destination);
        if (!f.open(QIODevice::WriteOnly))
            return false;
        const qint64 bytes = mode == MdRecordMode::Sp ? 176400 : 43 * mdWireSpec(mode).frameBytes;
        f.write(QByteArray(bytes, '\0'));
        f.close();
        if (out)
            *out = MdEncodedAudio{mode, destination, bytes, 1000};
        note("encoded:");
        return true;
    }
};

inline MdAudioEncoderFactory fakeEncoder(int delayMs = 0, std::shared_ptr<FakeRecorder> log = {})
{
    return [delayMs, log] {
        auto e = std::make_unique<FakeEncoder>();
        e->delayMs = delayMs;
        e->log = log;
        return e;
    };
}

inline NetMdBackendFactory fakeNetMd(std::shared_ptr<FakeRecorder> recorder)
{
    return [recorder] { return std::make_unique<FakeNetMdBackend>(recorder); };
}

} // namespace mmm::test
