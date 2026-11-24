// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QList>
#include <QObject>
#include <QString>

#include <atomic>
#include <memory>

#include "mdtypes.h"
#include "mdupload.h"
#include "netmdbackend.h"

class QTemporaryDir;

class QThread;

namespace mmm {

class NetMdMonitor;
class NetMdWorker;

// NetMD recorders for the app (M10): which are plugged in (NetMdMonitor), and the one the user
// picked: its disc, renaming the disc and its tracks. libnetmd isn't thread-safe and its calls
// block for seconds, so every recorder command runs, one at a time, on one worker thread that
// owns the backend; results come back as queued signals. A newer selection makes older results
// stale (generation). The recorder is only held while it's selected, so other programs (Web
// MiniDisc Pro) can use it otherwise. Recording (M11) runs on the same worker, one track per
// command: decode (MdAudioEncoder), upload, re-read the disc.
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
    // Stops after the track being recorded (a track can't be stopped part-way); a track still
    // being decoded stops at once.
    void cancelUpload();
    bool isUploading() const { return m_uploading; }
    bool isCancelling() const { return m_uploading && m_cancelRequested; }
    QList<MdUploadItem> uploadItems() const { return m_items; }
    int uploadIndex() const { return m_uploadIndex; }   // the item being recorded (-1: none)
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
    void onItemState(quint64 generation, int index, int state);
    void uploadNext();
    void finishUpload(bool ok);
    void skipRemaining(int from);
    quint64 nextGeneration();   // also tells the worker (stale commands are skipped)
    bool needsReopen() const;   // the last error means "open the recorder again"

    Options m_options;
    std::unique_ptr<NetMdMonitor> m_monitor;
    QThread *m_thread = nullptr;     // owned unless abandoned stuck at shutdown
    NetMdWorker *m_worker = nullptr; // lives on m_thread
    QList<NetMdDevice> m_testDevices;
    bool m_testMode = false;
    QString m_selected;
    quint64 m_generation = 0;
    std::shared_ptr<std::atomic<quint64>> m_current = std::make_shared<std::atomic<quint64>>(0);
    State m_state = State::Idle;
    bool m_hasDisc = false;
    MdDisc m_disc;
    NetMdError m_error = NetMdError::None;

    // Recording
    std::shared_ptr<std::atomic_bool> m_cancelDecode = std::make_shared<std::atomic_bool>(false);
    QList<MdUploadItem> m_items;
    int m_uploadIndex = -1;
    bool m_uploading = false;
    MdRecordMode m_uploadMode = MdRecordMode::Sp;
    bool m_cancelRequested = false;
    bool m_titlingDisc = false;
    Edit m_edit = Edit::None;
    bool m_uploadFailed = false;
    QString m_discTitleAfter;
    std::unique_ptr<QTemporaryDir> m_tempDir;
    MdTransferProgress m_progress;
};

} // namespace mmm
