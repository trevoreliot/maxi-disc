// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QFile>
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

    NetMdError uploadTrack(const MdEncodedAudio &audio, const QString &title, const UploadProgress &progress,
                           int *trackIndex) override
    {
        int stepMs = 0;
        NetMdError error = NetMdError::None;
        {
            const auto l = step(QStringLiteral("uploadTrack:%1:%2").arg(title).arg(audio.bytes));
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

// "Decodes" by writing a small file (a second of silence per track, in the mode's format: PCM, or
// whole 384 / 192-byte ATRAC3 frames), no GStreamer or atracdenc. Can fail
// for a path containing "broken"; waits `delayMs` (polling cancel) to test canceling a decode.
struct FakeEncoder final : MdAudioEncoder
{
    int delayMs = 0;
    bool encode(const QString &source, MdRecordMode mode, const QString &destination, const std::atomic_bool &cancel,
                MdEncodedAudio *out, QString *error) override
    {
        for (int waited = 0; waited < delayMs; waited += 10) {
            if (cancel.load()) {
                if (error)
                    *error = kEncodeCanceled;
                return false;
            }
            QThread::msleep(10);
        }
        if (source.contains(QLatin1String("broken"))) {
            if (error)
                *error = QStringLiteral("cannot decode %1").arg(source);
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
        return true;
    }
};

inline MdAudioEncoderFactory fakeEncoder(int delayMs = 0)
{
    return [delayMs] {
        auto e = std::make_unique<FakeEncoder>();
        e->delayMs = delayMs;
        return e;
    };
}

inline NetMdBackendFactory fakeNetMd(std::shared_ptr<FakeRecorder> recorder)
{
    return [recorder] { return std::make_unique<FakeNetMdBackend>(recorder); };
}

} // namespace mmm::test
