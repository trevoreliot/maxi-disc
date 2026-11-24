// SPDX-License-Identifier: GPL-3.0-or-later
#include "netmdmanager.h"

#include "apppaths.h"
#include "atracencoder.h"
#include "mdencodeworker.h"
#include "mdtitle.h"
#include "netmdmonitor.h"

#include <QDeadlineTimer>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QTemporaryDir>

#include <QLoggingCategory>
#include <QMetaObject>
#include <QThread>

#include <algorithm>
#include <atomic>
#include <functional>

Q_DECLARE_LOGGING_CATEGORY(lcNetMd)

namespace mmm {

// Owns the backend; lives on the NetMD thread. Every slot runs one command to the end
// (blocking) and reports through a signal (queued to the manager, so a manager that's gone
// simply doesn't get it).
class NetMdWorker : public QObject
{
    Q_OBJECT

public:
    NetMdWorker(NetMdBackendFactory factory, std::shared_ptr<std::atomic<quint64>> current)
        : m_factory(std::move(factory))
        , m_current(std::move(current))
    {
    }

    // Records one prepared track (title + commit), removes its file, re-reads the disc.
    // Outcome: 0 done, 3 the recorder failed. `dir` (the recording's temporary folder) is kept
    // until this returns.
    void uploadOne(quint64 generation, int index, const MdEncodedAudio &audio, const QString &title,
                   const std::shared_ptr<QTemporaryDir> &dir)
    {
        Q_UNUSED(dir)
        if (stale(generation)) {
            QFile::remove(audio.path);
            return;
        }
        if (!m_backend) {
            QFile::remove(audio.path);
            Q_EMIT uploadStep(generation, index, 3, NetMdError::NotFound, {}, {}, false);
            return;
        }
        QElapsedTimer sinceProgress;
        sinceProgress.start();
        int track = -1;
        const NetMdError error = m_backend->uploadTrack(audio, title, [&](qint64 sent, qint64 total) {
            if (sent >= total || sinceProgress.elapsed() >= 150) {
                Q_EMIT uploadProgress(generation, sent, total);
                sinceProgress.restart();
            }
        }, &track);
        QFile::remove(audio.path);
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

    // --- Editing (M12b) ---
    void deleteTracks(quint64 generation, QList<int> indices)
    {
        if (stale(generation))
            return;
        if (!m_backend) {
            Q_EMIT edited(generation, NetMdError::NotFound, {}, false);
            return;
        }
        // The groups as they are now (another program may have changed them): without them
        // they couldn't be kept right, so nothing is deleted.
        QString raw;
        if (const NetMdError e = m_backend->readHeader(&raw); e != NetMdError::None) {
            Q_EMIT edited(generation, e, {}, false);
            return;
        }
        // The last first: the others keep their numbers.
        std::sort(indices.begin(), indices.end(), std::greater<>());
        indices.erase(std::unique(indices.begin(), indices.end()), indices.end());
        // The groups as they'll be must fit (written in our form, which can be longer than the
        // recorder's): else nothing is deleted.
        {
            const MdTitle::Header before = MdTitle::parseHeader(raw);
            const MdTitle::Header after = MdTitle::headerAfterDelete(before, indices);
            if (after != before && MdTitle::formatHeader(after).toLatin1().size() > MdTitle::kMaxHeaderBytes) {
                Q_EMIT edited(generation, NetMdError::Rejected, {}, false);
                return;
            }
        }
        QList<int> deleted;
        NetMdError error = NetMdError::None;
        for (const int index : std::as_const(indices)) {
            error = m_backend->deleteTrack(index);
            if (error != NetMdError::None)
                break;
            deleted << index;
        }
        if (!deleted.isEmpty() && error != NetMdError::Disconnected) {
            const MdTitle::Header before = MdTitle::parseHeader(raw);
            const MdTitle::Header after = MdTitle::headerAfterDelete(before, deleted);
            if (after != before) {
                const NetMdError e = m_backend->writeHeader(MdTitle::formatHeader(after));
                if (error == NetMdError::None)
                    error = e;
            }
        }
        reportEdit(generation, error);
    }

    // `disc`: the manager's, to move the track in when it's done.
    void moveTrack(quint64 generation, int from, int to, MdDisc disc)
    {
        if (stale(generation))
            return;
        if (!m_backend) {
            Q_EMIT edited(generation, NetMdError::NotFound, {}, false);
            return;
        }
        QString raw;
        if (const NetMdError e = m_backend->readHeader(&raw); e != NetMdError::None) {
            Q_EMIT edited(generation, e, {}, false);
            return;
        }
        const MdTitle::Header before = MdTitle::parseHeader(raw);
        const MdTitle::Header after = MdTitle::headerAfterMove(before, from, to);
        const bool groupsChange = after != before;
        const QString next = groupsChange ? MdTitle::formatHeader(after) : raw;
        if (groupsChange && next.toLatin1().size() > MdTitle::kMaxHeaderBytes) {
            // The new group numbers wouldn't fit the header: don't move it.
            Q_EMIT edited(generation, NetMdError::Rejected, {}, false);
            return;
        }
        NetMdError error = m_backend->moveTrack(from, to);
        if (error == NetMdError::None && groupsChange)
            error = m_backend->writeHeader(next);
        if (error != NetMdError::None || from < 0 || to < 0 || from >= disc.tracks.size() || to >= disc.tracks.size()) {
            reportEdit(generation, error);
            return;
        }
        disc.tracks.move(from, to);
        for (int i = 0; i < disc.tracks.size(); ++i)
            disc.tracks[i].index = i;
        disc.rawHeader = next;
        Q_EMIT edited(generation, NetMdError::None, disc, true);
    }

    void eraseDisc(quint64 generation)
    {
        if (stale(generation))
            return;
        if (!m_backend) {
            Q_EMIT edited(generation, NetMdError::NotFound, {}, false);
            return;
        }
        reportEdit(generation, m_backend->eraseDisc());
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
    void headerWritten(quint64 generation, mmm::NetMdError error, const QString &rawHeader);
    void trackWritten(quint64 generation, mmm::NetMdError error, int index, const QString &title);
    void edited(quint64 generation, mmm::NetMdError error, const mmm::MdDisc &disc, bool haveDisc);

private:
    // After an edit (done or not): what's on the disc now, unless the recorder's gone.
    void reportEdit(quint64 generation, NetMdError error)
    {
        MdDisc disc;
        const NetMdError read = error == NetMdError::Disconnected ? error : m_backend->readDisc(&disc);
        const bool haveDisc = read == NetMdError::None;
        // Can't read what's on it after an edit: the list shown may be wrong (track numbers too),
        // so start over: the manager reopens the recorder.
        Q_EMIT edited(generation, haveDisc ? error : NetMdError::Disconnected, disc, haveDisc);
    }

    NetMdBackendFactory m_factory;
    std::shared_ptr<std::atomic<quint64>> m_current;
    std::unique_ptr<NetMdBackend> m_backend;
};

NetMdManager::NetMdManager(Options options, QObject *parent)
    : QObject(parent), m_options(std::move(options))
{
    qRegisterMetaType<mmm::NetMdDevice>();
    qRegisterMetaType<mmm::MdDisc>();
    qRegisterMetaType<mmm::NetMdError>();
    qRegisterMetaType<mmm::MdTransferProgress>();
    qRegisterMetaType<mmm::MdEncodedAudio>();
    if (!m_options.backend)
        m_options.backend = makeLibNetMdBackend;
    if (!m_options.encoder)
        // atracdenc is looked up once, here (the factory runs on the encode thread).
        m_options.encoder = [atracdenc = AtracEncoder::findBinary()] { return makeDefaultMdEncoder(atracdenc); };
    m_thread = new QThread;
    m_thread->setObjectName(QStringLiteral("netmd"));
    m_worker = new NetMdWorker(m_options.backend, m_current);
    m_worker->moveToThread(m_thread);
    connect(m_worker, &NetMdWorker::opened, this, &NetMdManager::onOpened, Qt::QueuedConnection);
    connect(m_worker, &NetMdWorker::headerWritten, this, &NetMdManager::onHeaderWritten, Qt::QueuedConnection);
    connect(m_worker, &NetMdWorker::trackWritten, this, &NetMdManager::onTrackWritten, Qt::QueuedConnection);
    connect(m_worker, &NetMdWorker::uploadStep, this, &NetMdManager::onUploadStep, Qt::QueuedConnection);
    connect(m_worker, &NetMdWorker::uploadProgress, this, &NetMdManager::onUploadProgress, Qt::QueuedConnection);
    connect(m_worker, &NetMdWorker::edited, this, &NetMdManager::onEdited, Qt::QueuedConnection);
    m_thread->start();
    // Tracks are prepared on their own thread (M12c): the next one while one uploads.
    m_encodeThread = new QThread;
    m_encodeThread->setObjectName(QStringLiteral("netmd-encode"));
    m_encodeWorker = new MdEncodeWorker(m_options.encoder);
    m_encodeWorker->moveToThread(m_encodeThread);
    connect(m_encodeWorker, &MdEncodeWorker::encoded, this, &NetMdManager::onEncoded, Qt::QueuedConnection);
    m_encodeThread->start();
    if (m_options.monitor) {
        m_monitor = std::make_unique<NetMdMonitor>(m_options.pollIntervalMs);
        connect(m_monitor.get(), &NetMdMonitor::devicesChanged, this, &NetMdManager::deviceListChanged);
    }
}

NetMdManager::~NetMdManager()
{
    m_monitor.reset();
    disconnect(m_worker, nullptr, this, nullptr);
    disconnect(m_encodeWorker, nullptr, this, nullptr);
    *m_current = ~quint64(0); // everything still queued is stale
    if (m_cancelEncode)
        *m_cancelEncode = true; // a track being prepared stops (an upload can't)
    dropPrepared();
    // Both threads wind down together: delete each worker and end its thread, on that thread
    // (the recorder's closed first).
    NetMdWorker *worker = m_worker;
    QMetaObject::invokeMethod(worker, [worker] {
        worker->close();
        delete worker;
        QThread::currentThread()->quit();
    }, Qt::QueuedConnection);
    MdEncodeWorker *encoder = m_encodeWorker;
    QMetaObject::invokeMethod(encoder, [encoder] {
        delete encoder;
        QThread::currentThread()->quit();
    }, Qt::QueuedConnection);
    m_tempDir.reset(); // the threads still using it keep it until they're done
    // A USB command can block for tens of seconds: don't hang the app's exit on one. A stuck
    // thread is left behind (the process is ending anyway). One 5 s budget for both, but each
    // gets at least half a second (an idle one stops at once even if the other was stuck).
    QElapsedTimer waited;
    waited.start();
    const auto join = [&](QThread *thread, const char *what) {
        if (thread->wait(QDeadlineTimer(std::max<qint64>(500, 5000 - waited.elapsed())))) {
            delete thread;
        } else {
            qCWarning(lcNetMd) << what << "didn't stop in time: leaving its thread behind";
        }
    };
    join(m_encodeThread, "preparing a track");
    join(m_thread, "the recorder");
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
            m_edit = Edit::None;
            m_hasDisc = false;
            m_disc = {};
            setError(NetMdError::Disconnected); // before uploadFinished: its listeners see why
            if (m_uploading) {
                *m_cancelEncode = true; // a track being prepared stops; the handle is useless now
                // The track being recorded (or prepared for it) is lost (not one already done,
                // nor one canceled before it reached the recorder); the rest can't be recorded.
                const int at = uploadIndex();
                const bool canceledBefore = m_cancelRequested && m_sending < 0;
                if (at >= 0 && !canceledBefore && m_items[at].state != MdItemState::Done) {
                    m_items[at].state = MdItemState::Failed;
                    m_items[at].error = describe(NetMdError::Disconnected);
                }
                skipRemaining(std::max(0, at)); // from it on (a failed one stays failed)
                dropPrepared();
                m_uploadFailed = true;
                finishUpload(false);
            }
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
    m_edit = Edit::None; // one on its way is for the old selection
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

template<typename Command>
void NetMdManager::startEdit(Edit edit, Command command)
{
    const quint64 generation = nextGeneration();
    m_edit = edit;
    setError(NetMdError::None);
    setState(State::Writing);
    QMetaObject::invokeMethod(m_worker, [w = m_worker, generation, command] { command(w, generation); }, Qt::QueuedConnection);
}

bool NetMdManager::deleteTracks(const QList<int> &indices)
{
    const auto onDisc = [&](int i) { return i >= 0 && i < m_disc.tracks.size(); };
    if (!canEdit() || indices.isEmpty() || !std::all_of(indices.cbegin(), indices.cend(), onDisc))
        return false;
    startEdit(Edit::Delete, [indices](NetMdWorker *w, quint64 generation) { w->deleteTracks(generation, indices); });
    return true;
}

bool NetMdManager::moveTrack(int from, int to)
{
    const int n = static_cast<int>(m_disc.tracks.size());
    if (!canEdit() || from == to || from < 0 || to < 0 || from >= n || to >= n)
        return false;
    startEdit(Edit::Move, [from, to, disc = m_disc](NetMdWorker *w, quint64 generation) { w->moveTrack(generation, from, to, disc); });
    return true;
}

bool NetMdManager::eraseDisc()
{
    if (!canEdit())
        return false;
    startEdit(Edit::Erase, [](NetMdWorker *w, quint64 generation) { w->eraseDisc(generation); });
    return true;
}

void NetMdManager::onEdited(quint64 generation, NetMdError error, const MdDisc &disc, bool haveDisc)
{
    if (generation != m_generation)
        return;
    m_edit = Edit::None;
    const bool discChanges = haveDisc || error == NetMdError::Disconnected;
    if (haveDisc) {
        m_disc = disc;
        const MdTitle::Header h = MdTitle::parseHeader(disc.rawHeader);
        m_disc.title = h.title;
        m_disc.groups = h.groups;
    } else if (error == NetMdError::Disconnected) {
        m_hasDisc = false; // what's shown is gone
        m_disc = {};
    }
    setError(error);
    setState(State::Ready);
    if (discChanges)
        Q_EMIT discChanged();
    Q_EMIT editFinished(error == NetMdError::None);
    // Stopped answering (still plugged in): open it again (resets its USB port first).
    if (error == NetMdError::Disconnected && !m_selected.isEmpty())
        refresh();
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
    auto dir = std::make_shared<QTemporaryDir>(QDir(base).filePath(QStringLiteral("record-XXXXXX")));
    if (!dir->isValid())
        return false;
    m_tempDir = std::move(dir);
    m_items = items;
    for (MdUploadItem &item : m_items) {
        item.state = MdItemState::Pending;
        item.error.clear();
    }
    m_uploading = true;
    m_run = ++m_runs;
    m_uploadMode = mode;
    m_cancelEncode = std::make_shared<std::atomic_bool>(false); // this recording's alone
    m_nextUpload = 0;
    m_sending = -1;
    m_nextEncode = 0;
    m_encoding = -1;
    m_prepared.clear();
    m_encodeBlocked = false;
    m_cancelRequested = false;
    m_uploadFailed = false;
    m_titlingDisc = false;
    m_discTitleAfter = MdTitle::clean(discTitle, MdTitle::kMaxHeaderBytes).text;
    m_progress = {};
    setError(NetMdError::None);
    setState(State::Writing);
    pumpUpload();
    return true;
}

void NetMdManager::cancelUpload()
{
    if (!m_uploading || m_cancelRequested)
        return;
    m_cancelRequested = true;
    m_encodeBlocked = true;
    *m_cancelEncode = true; // a track being prepared stops now; one uploading finishes
    dropPrepared();          // one prepared ahead won't go on
    Q_EMIT uploadChanged();
    pumpUpload();            // nothing running: done now
}

int NetMdManager::uploadIndex() const
{
    if (!m_uploading)
        return -1;
    if (m_sending >= 0)
        return m_sending;
    return m_nextUpload < m_items.size() ? m_nextUpload : -1; // past the end: titling the disc
}

void NetMdManager::pumpUpload()
{
    if (!m_uploading)
        return;
    const int count = static_cast<int>(m_items.size());
    if (m_cancelRequested) {
        // The track on its way to the disc can't be stopped, nor the disc's title; a track
        // being prepared stops at once. Then it's over.
        if (m_sending >= 0 || m_encoding >= 0 || m_titlingDisc)
            return;
        dropPrepared();
        skipRemaining(m_nextUpload);
        finishUpload(false);
        return;
    }

    // The disc: the next track, if it's ready.
    if (m_sending < 0 && !m_titlingDisc) {
        if (m_nextUpload >= count) {
            if (!m_discTitleAfter.isEmpty()) {
                // Everything's on the disc: title it, then finish (onHeaderWritten).
                m_titlingDisc = true;
                const quint64 generation = nextGeneration();
                QMetaObject::invokeMethod(m_worker, [w = m_worker, generation, t = m_discTitleAfter] { w->writeDiscTitle(generation, t); },
                                          Qt::QueuedConnection);
                Q_EMIT uploadChanged();
                return;
            }
            finishUpload(true);
            return;
        }
        if (const auto it = m_prepared.constFind(m_nextUpload); it != m_prepared.cend()) {
            const Prepared prepared = *it;
            m_prepared.erase(it);
            if (!prepared.error.isEmpty()) {
                // It couldn't be prepared: a failed track stops the rest (as an upload's would).
                m_items[m_nextUpload].state = MdItemState::Failed;
                m_items[m_nextUpload].error = prepared.error;
                m_uploadFailed = true;
                *m_cancelEncode = true;
                dropPrepared();
                skipRemaining(m_nextUpload + 1);
                finishUpload(false);
                return;
            }
            m_sending = m_nextUpload;
            m_items[m_sending].state = MdItemState::Uploading;
            m_progress = {};
            Q_EMIT uploadProgressChanged();
            const quint64 generation = nextGeneration();
            QMetaObject::invokeMethod(m_worker, [w = m_worker, generation, index = m_sending, audio = prepared.audio,
                                                 title = m_items[m_sending].title, dir = m_tempDir] {
                w->uploadOne(generation, index, audio, title, dir);
            }, Qt::QueuedConnection);
        }
    }

    // Preparing: the next track, one ahead of the disc at most (so at most two on disk).
    if (!m_encodeBlocked && !m_titlingDisc && m_encoding < 0 && m_nextEncode < count) {
        const int limit = m_sending >= 0 ? m_sending + 1 : m_nextUpload;
        if (m_nextEncode <= limit) {
            const int index = m_nextEncode++;
            m_encoding = index;
            m_items[index].state = MdItemState::Decoding;
            const QString destination = m_tempDir->filePath(
                QStringLiteral("track-%1.%2").arg(index + 1).arg(m_uploadMode == MdRecordMode::Sp ? QStringLiteral("pcm") : QStringLiteral("atrac")));
            QMetaObject::invokeMethod(m_encodeWorker, [w = m_encodeWorker, run = m_run, index, source = m_items[index].track.path,
                                                       destination, mode = m_uploadMode, cancel = m_cancelEncode, dir = m_tempDir] {
                w->encodeOne(run, index, source, destination, mode, cancel, dir);
            }, Qt::QueuedConnection);
        }
    }
    Q_EMIT uploadChanged();
}

void NetMdManager::onEncoded(quint64 run, int index, int outcome, const MdEncodedAudio &audio, const QString &error)
{
    if (!m_uploading || run != m_run || index != m_encoding) {
        // A recording that's over (failed, unplugged): its track won't be used.
        if (outcome == MdEncodeWorker::Ok)
            QFile::remove(audio.path);
        return;
    }
    m_encoding = -1;
    if (outcome == MdEncodeWorker::Canceled || m_cancelRequested) {
        if (outcome == MdEncodeWorker::Ok)
            QFile::remove(audio.path);
        m_items[index].state = MdItemState::Skipped;
    } else if (outcome == MdEncodeWorker::Failed) {
        // Applied in its turn: if the track before it fails on the disc, that's the error.
        m_encodeBlocked = true;
        m_prepared.insert(index, Prepared{{}, error.isEmpty() ? QStringLiteral("Couldn't prepare the track.") : error});
        m_items[index].state = MdItemState::Pending;
    } else {
        m_prepared.insert(index, Prepared{audio, {}});
        m_items[index].state = MdItemState::Ready; // uploading at once if it's its turn (pumpUpload)
    }
    pumpUpload();
}

void NetMdManager::onUploadProgress(quint64 generation, qint64 sent, qint64 total)
{
    if (generation != m_generation || !m_uploading || m_sending < 0)
        return;
    m_progress = MdTransferProgress{sent, total};
    Q_EMIT uploadProgressChanged();
}

void NetMdManager::onUploadStep(quint64 generation, int index, int outcome, NetMdError error, const QString &message,
                                const MdDisc &disc, bool haveDisc)
{
    Q_UNUSED(message)
    if (generation != m_generation || !m_uploading || index != m_sending)
        return;
    m_sending = -1;
    if (haveDisc) {
        m_disc = disc;
        const MdTitle::Header h = MdTitle::parseHeader(disc.rawHeader);
        m_disc.title = h.title;
        m_disc.groups = h.groups;
        Q_EMIT discChanged();
    }
    MdUploadItem &item = m_items[index];
    if (outcome == 0) {
        item.state = MdItemState::Done;
        ++m_nextUpload;
        m_progress = {};
        Q_EMIT uploadProgressChanged();
        pumpUpload();
        return;
    }
    // A failed track stops the rest: an album with a hole is worse than half an album.
    item.state = MdItemState::Failed;
    item.error = describe(error);
    m_uploadFailed = true;
    setError(error);
    if (error == NetMdError::Disconnected) {
        m_hasDisc = false; // what's shown is gone
        m_disc = {};
        Q_EMIT discChanged();
    }
    *m_cancelEncode = true; // the next track, being prepared: no use now
    m_encodeBlocked = true;
    dropPrepared();
    skipRemaining(index + 1);
    finishUpload(false);
    // The recorder stopped answering (still plugged in): reopen it, which resets its USB
    // port first (LibNetMdBackend::close), instead of leaving it to a replug.
    if (m_error == NetMdError::Disconnected && !m_selected.isEmpty())
        refresh();
}

void NetMdManager::skipRemaining(int from)
{
    for (int i = std::max(0, from); i < m_items.size(); ++i) {
        const MdItemState s = m_items[i].state;
        if (s == MdItemState::Pending || s == MdItemState::Decoding || s == MdItemState::Ready)
            m_items[i].state = MdItemState::Skipped;
    }
}

void NetMdManager::dropPrepared()
{
    for (const Prepared &p : std::as_const(m_prepared)) {
        if (!p.audio.path.isEmpty())
            QFile::remove(p.audio.path);
    }
    m_prepared.clear();
}

void NetMdManager::finishUpload(bool ok)
{
    m_uploading = false;
    m_titlingDisc = false;
    m_sending = -1;
    m_encoding = -1; // one winding down reports to a recording that's over (dropped)
    dropPrepared();
    m_tempDir.reset(); // removed once the threads still using it are done
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
