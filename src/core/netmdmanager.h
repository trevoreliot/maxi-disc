// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QList>
#include <QObject>
#include <QString>

#include <QHash>

#include <atomic>
#include <memory>

#include "mdtypes.h"
#include "mdupload.h"
#include "netmdbackend.h"

class QTemporaryDir;

class QThread;

namespace mmm {

class MdEncodeWorker;
class NetMdMonitor;
class NetMdWorker;

// NetMD recorders for the app (M10): which are plugged in (NetMdMonitor), and the one the user
// picked: its disc, renaming the disc and its tracks. libnetmd isn't thread-safe and its calls
// block for seconds, so every recorder command runs, one at a time, on one worker thread that
// owns the backend; results come back as queued signals. A newer selection makes older results
// stale (generation). The recorder is only held while it's selected, so other programs (Web
// MiniDisc Pro) can use it otherwise. Recording (M11) uploads on the same worker, one track per
// command (upload, re-read the disc); the tracks are prepared (MdAudioEncoder) on a second
// thread (MdEncodeWorker, M12c), one track ahead of the one uploading.
class NetMdManager : public QObject
{
    Q_OBJECT

public:
    enum class State {
        Idle,     // nothing selected
        Opening,  // opening the recorder and reading the disc
        Ready,    // disc() is the disc (or error() says why not: no disc, no permission)
        Writing,  // a rename or a recording is on its way
    };
    Q_ENUM(State)

    struct Options
    {
        NetMdBackendFactory backend;   // default: libnetmd
        bool monitor = true;           // false: no USB detection (tests, --no-devices)
        int pollIntervalMs = 2000;
        MdAudioEncoderFactory encoder; // default: GstPcmEncoder (SP)
        QString tempDir;               // for decoded audio ("" = the app's cache)
    };

    explicit NetMdManager(Options options, QObject *parent = nullptr);
    ~NetMdManager() override; // closes the recorder; never hangs on a stuck USB command

    void start();
    QList<NetMdDevice> devices() const;
    // Test hook: these devices instead of detected ones.
    void setDevicesForTesting(const QList<NetMdDevice> &devices);

    // Selects a recorder (opens it and reads its disc), or none ("").
    void select(const QString &deviceId);
    QString selectedId() const { return m_selected; }
    void refresh();
    // Titles are cleaned for the disc first (MdTitle::clean); "" clears a title.
    void renameDisc(const QString &title);
    void renameTrack(int index, const QString &title);

    // --- Recording (M11) ---
    // Records `items` (titles already set) at the end of the disc, then titles the disc
    // `discTitle` if given. False if busy or there's no disc. Progress: uploadChanged /
    // uploadProgressChanged; uploadFinished at the end.
    bool startUpload(const QList<MdUploadItem> &items, const QString &discTitle = {}, MdRecordMode mode = MdRecordMode::Sp);
    // Stops after the track being recorded (a track can't be stopped part-way); one being
    // prepared stops at once, one prepared ahead is dropped.
    void cancelUpload();
    bool isUploading() const { return m_uploading; }
    bool isCancelling() const { return m_uploading && m_cancelRequested; }
    QList<MdUploadItem> uploadItems() const { return m_items; }
    // The item being recorded, else the next one (being prepared); -1: none (or titling the disc).
    int uploadIndex() const;
    MdRecordMode uploadMode() const { return m_uploadMode; } // the recording's mode (fixed per run)
    MdTransferProgress uploadProgress() const { return m_progress; }

    // --- Editing the disc (M12b) ---
    // One at a time, not while busy or recording; editFinished(ok) after each. The groups (in
    // the disc header) follow the tracks: the header is read fresh and rewritten if they change.
    enum class Edit { None, Delete, Move, Erase };
    bool canEdit() const { return m_hasDisc && !isBusy() && !m_uploading; }
    // Deletes these tracks (0-based, any order), the last first so the others keep their
    // numbers. If the recorder refuses one, the ones done stay done (the disc is read again).
    bool deleteTracks(const QList<int> &indices);
    // The track at `from` ends up at `to`. The disc isn't read again (that takes seconds):
    // the list is moved here, like the recorder did.
    bool moveTrack(int from, int to);
    // Every track and title.
    bool eraseDisc();
    Edit edit() const { return m_edit; } // the edit on its way (None: none)

    State state() const { return m_state; }
    bool isBusy() const { return m_state == State::Opening || m_state == State::Writing; }
    bool hasDisc() const { return m_hasDisc; }
    MdDisc disc() const { return m_disc; }
    NetMdError error() const { return m_error; }
    QString errorString() const;

Q_SIGNALS:
    void devicesChanged();
    void stateChanged();
    void discChanged();
    void errorChanged();
    void uploadChanged();          // items' states, the current index, uploading / cancelling
    void uploadProgressChanged();  // bytes of the current track
    void uploadFinished(bool ok);
    void editFinished(bool ok);    // after discChanged (if the disc changed)

private:
    // From the worker (queued).
    void onOpened(quint64 generation, mmm::NetMdError error, const mmm::MdDisc &disc);
    void onHeaderWritten(quint64 generation, mmm::NetMdError error, const QString &rawHeader);
    void onTrackWritten(quint64 generation, mmm::NetMdError error, int index, const QString &title);
    void onEdited(quint64 generation, mmm::NetMdError error, const mmm::MdDisc &disc, bool haveDisc);
    template<typename Command>
    void startEdit(Edit edit, Command command);
    void setState(State state);
    void setError(NetMdError error);
    void deviceListChanged();
    void onUploadStep(quint64 generation, int index, int outcome, mmm::NetMdError error, const QString &message,
                      const mmm::MdDisc &disc, bool haveDisc);
    void onUploadProgress(quint64 generation, qint64 sent, qint64 total);
    void onEncoded(quint64 run, int index, int outcome, const mmm::MdEncodedAudio &audio, const QString &error);
    // Starts what can start: the next upload (if its track is ready), the next track to prepare
    // (one ahead of the upload at most); finishes the recording when all's done or canceled.
    void pumpUpload();
    void finishUpload(bool ok);
    void skipRemaining(int from); // pending / preparing / ready items from `from` on: skipped
    void dropPrepared();          // prepared tracks that won't be uploaded: their files go
    quint64 nextGeneration();   // also tells the worker (stale commands are skipped)
    bool needsReopen() const;   // the last error means "open the recorder again"

    Options m_options;
    std::unique_ptr<NetMdMonitor> m_monitor;
    QThread *m_thread = nullptr;     // owned unless abandoned stuck at shutdown
    NetMdWorker *m_worker = nullptr; // lives on m_thread
    QThread *m_encodeThread = nullptr;         // the same
    MdEncodeWorker *m_encodeWorker = nullptr;  // lives on m_encodeThread
    QList<NetMdDevice> m_testDevices;
    bool m_testMode = false;
    QString m_selected;
    quint64 m_generation = 0;
    std::shared_ptr<std::atomic<quint64>> m_current = std::make_shared<std::atomic<quint64>>(0);
    State m_state = State::Idle;
    bool m_hasDisc = false;
    MdDisc m_disc;
    NetMdError m_error = NetMdError::None;

    Edit m_edit = Edit::None;

    // Recording. Per recording: its own cancel flag and temporary folder, shared with the
    // threads working on it (an old recording's encode winding down can't touch a new one).
    struct Prepared
    {
        MdEncodedAudio audio;
        QString error; // it couldn't be prepared (applied when its turn comes)
    };
    QList<MdUploadItem> m_items;
    bool m_uploading = false;
    quint64 m_run = 0;           // this recording: the encoder's results carry it
    quint64 m_runs = 0;
    MdRecordMode m_uploadMode = MdRecordMode::Sp;
    std::shared_ptr<std::atomic_bool> m_cancelEncode;
    std::shared_ptr<QTemporaryDir> m_tempDir;
    int m_nextUpload = 0;        // the next track for the disc (every one before it is done)
    int m_sending = -1;          // the track uploading (-1: none)
    int m_nextEncode = 0;        // the next track to prepare
    int m_encoding = -1;         // the track being prepared (-1: none)
    QHash<int, Prepared> m_prepared; // prepared, waiting for their turn (one at most)
    bool m_encodeBlocked = false;    // prepare no more (canceled, or one couldn't be)
    bool m_cancelRequested = false;
    bool m_titlingDisc = false;
    bool m_uploadFailed = false;
    QString m_discTitleAfter;
    MdTransferProgress m_progress;
};

} // namespace mmm
