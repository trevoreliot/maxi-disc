// SPDX-License-Identifier: GPL-3.0-or-later
#include "netmdmanager.h"

#include "apppaths.h"
#include "atracencoder.h"
#include "mdtitle.h"
#include "netmdmonitor.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QTemporaryDir>

#include <QLoggingCategory>
#include <QMetaObject>
#include <QThread>

#include <atomic>

Q_DECLARE_LOGGING_CATEGORY(lcNetMd)

namespace mmm {

// Owns the backend; lives on the NetMD thread. Every slot runs one command to the end
// (blocking) and reports through a signal (queued to the manager, so a manager that's gone
// simply doesn't get it).
class NetMdWorker : public QObject
{
    Q_OBJECT

public:
    NetMdWorker(NetMdBackendFactory factory, MdAudioEncoderFactory encoder, std::shared_ptr<std::atomic<quint64>> current,
                std::shared_ptr<std::atomic_bool> cancelDecode)
        : m_factory(std::move(factory))
        , m_encoder(std::move(encoder))
        , m_current(std::move(current))
        , m_cancelDecode(std::move(cancelDecode))
    {
    }

    // Records one track: decode, upload (title + commit), re-read the disc. Outcome: 0 done,
    // 1 canceled while decoding, 2 decoding failed, 3 the recorder failed.
    void uploadOne(quint64 generation, int index, const QString &source, const QString &title, const QString &temp,
                   MdRecordMode mode)
    {
        if (stale(generation))
            return;
        if (!m_backend) {
            Q_EMIT uploadStep(generation, index, 3, NetMdError::NotFound, {}, {}, false);
            return;
        }
        Q_EMIT itemState(generation, index, int(MdItemState::Decoding));
        MdEncodedAudio audio;
        QString why;
        const std::unique_ptr<MdAudioEncoder> encoder = m_encoder();
        if (!encoder->encode(source, mode, temp, *m_cancelDecode, &audio, &why)) {
            QFile::remove(temp);
            Q_EMIT uploadStep(generation, index, why == kEncodeCanceled ? 1 : 2, NetMdError::None, why, {}, false);
            return;
        }
        Q_EMIT itemState(generation, index, int(MdItemState::Uploading));
        QElapsedTimer sinceProgress;
        sinceProgress.start();
        int track = -1;
        const NetMdError error = m_backend->uploadTrack(audio, title, [&](qint64 sent, qint64 total) {
            if (sent >= total || sinceProgress.elapsed() >= 150) {
                Q_EMIT uploadProgress(generation, sent, total);
                sinceProgress.restart();
            }
        }, &track);
        QFile::remove(temp);
        MdDisc disc;
        const bool haveDisc = error != NetMdError::Disconnected && m_backend->readDisc(&disc) == NetMdError::None;
        // A failure after which the disc can't even be read: the recorder went away mid-session.
        const NetMdError reported = error != NetMdError::None && !haveDisc ? NetMdError::Disconnected : error;
        Q_EMIT uploadStep(generation, index, error == NetMdError::None ? 0 : 3, reported, {}, disc, haveDisc);
    }
    ~NetMdWorker() override = default;

    // Commands queued before a newer selection (or the shutdown) are skipped without touching
    // the recorder: a slow disc can't make the app wait through a backlog.
    bool stale(quint64 generation) const { return generation != m_current->load(); }

    void open(quint64 generation, const mmm::NetMdDevice &device)
    {
        if (stale(generation))
            return;
        close();
        m_backend = m_factory();
        NetMdError error = m_backend->open(device);
        MdDisc disc;
        if (error == NetMdError::None)
            error = m_backend->readDisc(&disc);
        Q_EMIT opened(generation, error, disc);
    }

    void read(quint64 generation)
    {
        if (stale(generation))
            return;
        MdDisc disc;
        const NetMdError error = m_backend ? m_backend->readDisc(&disc) : NetMdError::NotFound;
        Q_EMIT opened(generation, error, disc);
    }

    // Reads the header fresh (never overwrites groups someone else changed), puts the new
    // title in, writes it, reads it back.
    void writeDiscTitle(quint64 generation, const QString &cleanTitle)
    {
        if (stale(generation))
            return;
        if (!m_backend) {
            Q_EMIT headerWritten(generation, NetMdError::NotFound, {});
            return;
        }
        QString raw;
        if (const NetMdError e = m_backend->readHeader(&raw); e != NetMdError::None) {
            // Without the current header the groups would be lost: don't write.
            Q_EMIT headerWritten(generation, e, {});
            return;
        }
        const QString header = MdTitle::withTitle(raw, cleanTitle);
        if (header.toLatin1().size() > MdTitle::kMaxHeaderBytes) {
            Q_EMIT headerWritten(generation, NetMdError::Rejected, raw); // the groups alone don't fit
            return;
        }
        const NetMdError error = m_backend->writeHeader(header);
        QString after = error == NetMdError::None ? header : raw;
        QString readBack;
        if (m_backend->readHeader(&readBack) == NetMdError::None)
            after = readBack;
        if (after.isNull())
            after = QStringLiteral(""); // read fine, untitled (null means "couldn't read")
        Q_EMIT headerWritten(generation, error, after);
    }

    void writeTrackTitle(quint64 generation, int index, const QString &cleanTitle)
    {
        if (stale(generation))
            return;
        if (!m_backend) {
            Q_EMIT trackWritten(generation, NetMdError::NotFound, index, {});
            return;
        }
        const NetMdError error = m_backend->writeTrackTitle(index, cleanTitle);
        // What the recorder has now; if it can't be read back, what was written (or nothing).
        QString after;
        if (m_backend->readTrackTitle(index, &after) != NetMdError::None)
            after = error == NetMdError::None ? cleanTitle : QString();
        Q_EMIT trackWritten(generation, error, index, after);
    }

    void close()
    {
        if (m_backend)
            m_backend->close();
        m_backend.reset();
    }

Q_SIGNALS:
    void opened(quint64 generation, mmm::NetMdError error, const mmm::MdDisc &disc);
    void uploadStep(quint64 generation, int index, int outcome, mmm::NetMdError error, const QString &message,
                    const mmm::MdDisc &disc, bool haveDisc);
    void uploadProgress(quint64 generation, qint64 sent, qint64 total);
    void itemState(quint64 generation, int index, int state);
    void headerWritten(quint64 generation, mmm::NetMdError error, const QString &rawHeader);
    void trackWritten(quint64 generation, mmm::NetMdError error, int index, const QString &title);

private:
    NetMdBackendFactory m_factory;
    MdAudioEncoderFactory m_encoder;
    std::shared_ptr<std::atomic<quint64>> m_current;
    std::shared_ptr<std::atomic_bool> m_cancelDecode;
    std::unique_ptr<NetMdBackend> m_backend;
};

NetMdManager::NetMdManager(Options options, QObject *parent)
    : QObject(parent), m_options(std::move(options))
{
    qRegisterMetaType<mmm::NetMdDevice>();
    qRegisterMetaType<mmm::MdDisc>();
    qRegisterMetaType<mmm::NetMdError>();
    qRegisterMetaType<mmm::MdTransferProgress>();
    if (!m_options.backend)
        m_options.backend = makeLibNetMdBackend;
    if (!m_options.encoder)
        // atracdenc is looked up once, here (the factory runs on the worker thread).
        m_options.encoder = [atracdenc = AtracEncoder::findBinary()] { return makeDefaultMdEncoder(atracdenc); };
    m_thread = new QThread;
    m_thread->setObjectName(QStringLiteral("netmd"));
    m_worker = new NetMdWorker(m_options.backend, m_options.encoder, m_current, m_cancelDecode);
    m_worker->moveToThread(m_thread);
    connect(m_worker, &NetMdWorker::opened, this, &NetMdManager::onOpened, Qt::QueuedConnection);
    connect(m_worker, &NetMdWorker::headerWritten, this, &NetMdManager::onHeaderWritten, Qt::QueuedConnection);
    connect(m_worker, &NetMdWorker::trackWritten, this, &NetMdManager::onTrackWritten, Qt::QueuedConnection);
    connect(m_worker, &NetMdWorker::uploadStep, this, &NetMdManager::onUploadStep, Qt::QueuedConnection);
    connect(m_worker, &NetMdWorker::uploadProgress, this, &NetMdManager::onUploadProgress, Qt::QueuedConnection);
    connect(m_worker, &NetMdWorker::itemState, this, &NetMdManager::onItemState, Qt::QueuedConnection);
    m_thread->start();
    if (m_options.monitor) {
        m_monitor = std::make_unique<NetMdMonitor>(m_options.pollIntervalMs);
        connect(m_monitor.get(), &NetMdMonitor::devicesChanged, this, &NetMdManager::deviceListChanged);
    }
}

NetMdManager::~NetMdManager()
{
    m_monitor.reset();
    disconnect(m_worker, nullptr, this, nullptr);
    *m_current = ~quint64(0); // everything still queued is stale
    *m_cancelDecode = true;   // a decode in progress stops (an upload can't)
    // Close, delete the worker and end the thread, in that order, on that thread.
    NetMdWorker *worker = m_worker;
    QMetaObject::invokeMethod(worker, [worker] {
        worker->close();
        delete worker;
        QThread::currentThread()->quit();
    }, Qt::QueuedConnection);
    // A USB command can block for tens of seconds: don't hang the app's exit on one. A stuck
    // thread is left behind (the process is ending anyway).
    if (m_thread->wait(5000)) {
        delete m_thread;
    } else {
        qCWarning(lcNetMd) << "the recorder didn't answer in time: leaving its thread behind";
    }
}

void NetMdManager::start()
{
    if (m_monitor)
        m_monitor->start();
}

QList<NetMdDevice> NetMdManager::devices() const
{
    if (m_testMode)
        return m_testDevices;
    return m_monitor ? m_monitor->devices() : QList<NetMdDevice>();
}

void NetMdManager::setDevicesForTesting(const QList<NetMdDevice> &devices)
{
    m_testMode = true;
    m_testDevices = devices;
    deviceListChanged();
}

QString NetMdManager::errorString() const
{
    return describe(m_error);
}

void NetMdManager::deviceListChanged()
{
    // The selected recorder went away: let go of it (its handle is useless now).
    if (!m_selected.isEmpty()) {
        bool present = false;
        for (const NetMdDevice &d : devices())
            present = present || d.id == m_selected;
        if (!present) {
            nextGeneration();
            QMetaObject::invokeMethod(m_worker, [w = m_worker] { w->close(); }, Qt::QueuedConnection);
            m_hasDisc = false;
            m_disc = {};
            if (m_uploading) {
                *m_cancelDecode = true; // a decode in progress stops; the handle is useless now
                // The track being recorded is lost (not one already done); the rest can't be recorded.
                if (m_uploadIndex >= 0 && m_uploadIndex < m_items.size() && m_items[m_uploadIndex].state != MdItemState::Done) {
                    m_items[m_uploadIndex].state = MdItemState::Failed;
                    m_items[m_uploadIndex].error = describe(NetMdError::Disconnected);
                }
                skipRemaining(m_uploadIndex + 1);
                m_uploadFailed = true;
                finishUpload(false);
            }
            setError(NetMdError::Disconnected);
            setState(State::Ready);
            Q_EMIT discChanged();
        }
    }
    Q_EMIT devicesChanged();
}

void NetMdManager::select(const QString &deviceId)
{
    // A recording goes on while the user looks at another device (like a USB sync).
    if (m_uploading)
        return;
    // The same recorder again: only worth it if opening it failed (fixed since, maybe).
    if (deviceId == m_selected && !(m_state == State::Ready && needsReopen()))
        return;
    m_selected = deviceId;
    const quint64 generation = nextGeneration();
    m_hasDisc = false;
    m_disc = {};
    setError(NetMdError::None);
    Q_EMIT discChanged();
    if (deviceId.isEmpty()) {
        QMetaObject::invokeMethod(m_worker, [w = m_worker] { w->close(); }, Qt::QueuedConnection);
        setState(State::Idle);
        return;
    }
    NetMdDevice device;
    bool found = false;
    for (const NetMdDevice &d : devices()) {
        if (d.id == deviceId) {
            device = d;
            found = true;
        }
    }
    if (!found) {
        setError(NetMdError::NotFound);
        setState(State::Ready);
        return;
    }
    setState(State::Opening);
    QMetaObject::invokeMethod(m_worker, [w = m_worker, generation, device] { w->open(generation, device); },
                              Qt::QueuedConnection);
}

void NetMdManager::refresh()
{
    if (m_selected.isEmpty() || isBusy())
        return;
    if (needsReopen()) {
        // Couldn't open it, or lost it: open it again.
        const QString id = m_selected;
        m_selected.clear();
        select(id);
        return;
    }
    const quint64 generation = nextGeneration();
    setError(NetMdError::None);
    setState(State::Opening);
    QMetaObject::invokeMethod(m_worker, [w = m_worker, generation] { w->read(generation); }, Qt::QueuedConnection);
}

void NetMdManager::renameDisc(const QString &title)
{
    if (!m_hasDisc || isBusy())
        return;
    const QString clean = MdTitle::clean(title, MdTitle::kMaxHeaderBytes).text;
    const quint64 generation = nextGeneration();
    setError(NetMdError::None);
    setState(State::Writing);
    QMetaObject::invokeMethod(m_worker, [w = m_worker, generation, clean] { w->writeDiscTitle(generation, clean); },
                              Qt::QueuedConnection);
}

void NetMdManager::renameTrack(int index, const QString &title)
{
    if (!m_hasDisc || isBusy() || index < 0 || index >= m_disc.tracks.size())
        return;
    const QString clean = MdTitle::clean(title, MdTitle::kMaxTrackTitleBytes).text;
    const quint64 generation = nextGeneration();
    setError(NetMdError::None);
    setState(State::Writing);
    QMetaObject::invokeMethod(m_worker, [w = m_worker, generation, index, clean] { w->writeTrackTitle(generation, index, clean); },
                              Qt::QueuedConnection);
}

void NetMdManager::onOpened(quint64 generation, NetMdError error, const MdDisc &disc)
{
    if (generation != m_generation)
        return;
    m_hasDisc = error == NetMdError::None;
    m_disc = disc;
    if (m_hasDisc) {
        const MdTitle::Header h = MdTitle::parseHeader(disc.rawHeader);
        m_disc.title = h.title;
        m_disc.groups = h.groups;
    }
    setError(error);
    setState(State::Ready);
    Q_EMIT discChanged();
}

void NetMdManager::onHeaderWritten(quint64 generation, NetMdError error, const QString &rawHeader)
{
    if (generation != m_generation)
        return;
    if (m_titlingDisc) {
        // The disc title after a recording: done either way (a failed title isn't a failed recording).
        if (!rawHeader.isNull()) {
            const MdTitle::Header h = MdTitle::parseHeader(rawHeader);
            m_disc.rawHeader = rawHeader;
            m_disc.title = h.title;
            m_disc.groups = h.groups;
            Q_EMIT discChanged();
        }
        setError(error);
        finishUpload(true);
        return;
    }
    if (!rawHeader.isNull()) { // null: the header couldn't be read, keep what's shown
        const MdTitle::Header h = MdTitle::parseHeader(rawHeader);
        m_disc.rawHeader = rawHeader;
        m_disc.title = h.title;
        m_disc.groups = h.groups;
    }
    setError(error);
    setState(State::Ready);
    Q_EMIT discChanged();
}

void NetMdManager::onTrackWritten(quint64 generation, NetMdError error, int index, const QString &title)
{
    if (generation != m_generation)
        return;
    if (index >= 0 && index < m_disc.tracks.size() && !title.isNull())
        m_disc.tracks[index].title = title;
    setError(error);
    setState(State::Ready);
    Q_EMIT discChanged();
}

bool NetMdManager::startUpload(const QList<MdUploadItem> &items, const QString &discTitle, MdRecordMode mode)
{
    if (m_uploading || isBusy() || !m_hasDisc || items.isEmpty())
        return false;
    const QString base = m_options.tempDir.isEmpty() ? AppPaths::mdRecordDir() : m_options.tempDir;
    QDir().mkpath(base);
    m_tempDir = std::make_unique<QTemporaryDir>(QDir(base).filePath(QStringLiteral("record-XXXXXX")));
    if (!m_tempDir->isValid()) {
        m_tempDir.reset();
        return false;
    }
    m_items = items;
    for (MdUploadItem &item : m_items) {
        item.state = MdItemState::Pending;
        item.error.clear();
    }
    m_uploading = true;
    m_uploadMode = mode;
    m_cancelRequested = false;
    m_uploadFailed = false;
    m_titlingDisc = false;
    m_discTitleAfter = MdTitle::clean(discTitle, MdTitle::kMaxHeaderBytes).text;
    *m_cancelDecode = false;
    m_uploadIndex = -1;
    setError(NetMdError::None);
    setState(State::Writing);
    uploadNext();
    return true;
}

void NetMdManager::cancelUpload()
{
    if (!m_uploading || m_cancelRequested)
        return;
    m_cancelRequested = true;
    *m_cancelDecode = true; // a track still decoding stops now; one uploading finishes
    Q_EMIT uploadChanged();
}

void NetMdManager::uploadNext()
{
    m_progress = {};
    Q_EMIT uploadProgressChanged();
    const int next = m_uploadIndex + 1;
    if (m_cancelRequested || next >= m_items.size()) {
        skipRemaining(next);
        if (!m_cancelRequested && !m_uploadFailed && !m_discTitleAfter.isEmpty()) {
            // Everything's on the disc: title it, then finish (onHeaderWritten).
            m_titlingDisc = true;
            m_uploadIndex = -1; // every track is done: an unplug now fails none of them
            const quint64 generation = nextGeneration();
            QMetaObject::invokeMethod(m_worker, [w = m_worker, generation, t = m_discTitleAfter] { w->writeDiscTitle(generation, t); },
                                      Qt::QueuedConnection);
            Q_EMIT uploadChanged();
            return;
        }
        finishUpload(!m_cancelRequested && !m_uploadFailed);
        return;
    }
    m_uploadIndex = next;
    const MdUploadItem &item = m_items.at(next);
    const QString temp = m_tempDir->filePath(
        QStringLiteral("track-%1.%2").arg(next + 1).arg(m_uploadMode == MdRecordMode::Sp ? QStringLiteral("pcm") : QStringLiteral("atrac")));
    const quint64 generation = nextGeneration();
    QMetaObject::invokeMethod(m_worker, [w = m_worker, generation, next, source = item.track.path, title = item.title, temp,
                                         mode = m_uploadMode] {
        w->uploadOne(generation, next, source, title, temp, mode);
    }, Qt::QueuedConnection);
    Q_EMIT uploadChanged();
}

void NetMdManager::onItemState(quint64 generation, int index, int state)
{
    if (generation != m_generation || index < 0 || index >= m_items.size())
        return;
    m_items[index].state = MdItemState(state);
    Q_EMIT uploadChanged();
}

void NetMdManager::onUploadProgress(quint64 generation, qint64 sent, qint64 total)
{
    if (generation != m_generation)
        return;
    m_progress = MdTransferProgress{sent, total};
    Q_EMIT uploadProgressChanged();
}

void NetMdManager::onUploadStep(quint64 generation, int index, int outcome, NetMdError error, const QString &message,
                                const MdDisc &disc, bool haveDisc)
{
    if (generation != m_generation || index < 0 || index >= m_items.size())
        return;
    if (haveDisc) {
        m_disc = disc;
        const MdTitle::Header h = MdTitle::parseHeader(disc.rawHeader);
        m_disc.title = h.title;
        m_disc.groups = h.groups;
        Q_EMIT discChanged();
    }
    MdUploadItem &item = m_items[index];
    switch (outcome) {
    case 0:
        item.state = MdItemState::Done;
        break;
    case 1:
        item.state = MdItemState::Skipped; // canceled before it reached the recorder
        break;
    case 2:
        item.state = MdItemState::Failed;
        item.error = message;
        m_uploadFailed = true;
        break;
    default:
        item.state = MdItemState::Failed;
        item.error = describe(error);
        m_uploadFailed = true;
        setError(error);
        if (error == NetMdError::Disconnected) {
            m_hasDisc = false; // what's shown is gone
            m_disc = {};
            Q_EMIT discChanged();
        }
        break;
    }
    // A failed track stops the rest: an album with a hole is worse than half an album.
    if (m_uploadFailed) {
        skipRemaining(index + 1);
        finishUpload(false);
        // The recorder stopped answering (still plugged in): reopen it, which resets its USB
        // port first (LibNetMdBackend::close), instead of leaving it to a replug.
        if (m_error == NetMdError::Disconnected && !m_selected.isEmpty())
            refresh();
        return;
    }
    uploadNext();
}

void NetMdManager::skipRemaining(int from)
{
    for (int i = std::max(0, from); i < m_items.size(); ++i) {
        if (m_items[i].state == MdItemState::Pending)
            m_items[i].state = MdItemState::Skipped;
    }
}

void NetMdManager::finishUpload(bool ok)
{
    m_uploading = false;
    m_titlingDisc = false;
    m_uploadIndex = -1;
    m_tempDir.reset();
    m_progress = {};
    setState(m_selected.isEmpty() ? State::Idle : State::Ready);
    Q_EMIT uploadProgressChanged();
    Q_EMIT uploadChanged();
    Q_EMIT uploadFinished(ok);
}

quint64 NetMdManager::nextGeneration()
{
    *m_current = ++m_generation;
    return m_generation;
}

bool NetMdManager::needsReopen() const
{
    return m_error == NetMdError::PermissionDenied || m_error == NetMdError::Busy || m_error == NetMdError::NotFound
        || m_error == NetMdError::Disconnected || (m_error == NetMdError::Failed && !m_hasDisc);
}

void NetMdManager::setState(State state)
{
    if (m_state == state)
        return;
    m_state = state;
    Q_EMIT stateChanged();
}

void NetMdManager::setError(NetMdError error)
{
    if (m_error == error)
        return;
    m_error = error;
    Q_EMIT errorChanged();
}

} // namespace mmm

#include "netmdmanager.moc"
