// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QList>
#include <QObject>
#include <QString>

#include <atomic>
#include <memory>

#include "mdtypes.h"
#include "netmdbackend.h"

class QThread;

namespace mmm {

class NetMdMonitor;
class NetMdWorker;

// NetMD recorders for the app (M10): which are plugged in (NetMdMonitor), and the one the user
// picked: its disc, renaming the disc and its tracks. libnetmd isn't thread-safe and its calls
// block for seconds, so every recorder command runs, one at a time, on one worker thread that
// owns the backend; results come back as queued signals. A newer selection makes older results
// stale (generation). The recorder is only held while it's selected, so other programs (Web
// MiniDisc Pro) can use it otherwise. Uploads (M11) will run on the same worker.
class NetMdManager : public QObject
{
    Q_OBJECT

public:
    enum class State {
        Idle,     // nothing selected
        Opening,  // opening the recorder and reading the disc
        Ready,    // disc() is the disc (or error() says why not: no disc, no permission)
        Writing,  // a rename is on its way
    };
    Q_ENUM(State)

    struct Options
    {
        NetMdBackendFactory backend;   // default: libnetmd
        bool monitor = true;           // false: no USB detection (tests, --no-devices)
        int pollIntervalMs = 2000;
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

private:
    // From the worker (queued).
    void onOpened(quint64 generation, mmm::NetMdError error, const mmm::MdDisc &disc);
    void onHeaderWritten(quint64 generation, mmm::NetMdError error, const QString &rawHeader);
    void onTrackWritten(quint64 generation, mmm::NetMdError error, int index, const QString &title);
    void setState(State state);
    void setError(NetMdError error);
    void deviceListChanged();
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
};

} // namespace mmm
