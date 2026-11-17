// SPDX-License-Identifier: GPL-3.0-or-later
#include "playback.h"

#include "coverimageprovider.h"
#include "library.h"
#include "librarymodels.h"
#include "mprisservice.h"

#include <QDBusConnection>
#include <QJSEngine>
#include <QLoggingCategory>
#include <QSettings>

#include <cmath>

Q_LOGGING_CATEGORY(lcPlaybackUi, "mmm.playback.ui")

namespace {

Playback *s_instance = nullptr;

// Perceived loudness is roughly logarithmic; a cubic curve makes the slider feel even.
double sliderToLinear(double v) { return std::pow(std::clamp(v, 0.0, 1.0), 3.0); }
double linearToSlider(double v) { return std::cbrt(std::clamp(v, 0.0, 1.0)); }

const QString kVolumeKey = QStringLiteral("playback/volume");
const QString kRepeatKey = QStringLiteral("playback/repeat");
const QString kReplayGainKey = QStringLiteral("playback/replayGain");

} // namespace

Playback::Playback(const Options &options, QObject *parent)
    : QObject(parent)
    , m_controller(options.player)
    , m_persist(options.persistSettings)
{
    Q_ASSERT_X(!s_instance, "Playback", "only one Playback may exist");
    s_instance = this;

    mmm::Player *p = m_controller.player();
    mmm::PlayQueue *q = m_controller.queue();

    if (m_persist) {
        const QSettings s;
        p->setVolume(sliderToLinear(s.value(kVolumeKey, 0.8).toDouble()));
        q->setRepeatMode(static_cast<mmm::PlayQueue::RepeatMode>(std::clamp(s.value(kRepeatKey, 0).toInt(), 0, 2)));
        p->setReplayGainMode(static_cast<mmm::Player::ReplayGainMode>(std::clamp(s.value(kReplayGainKey, 2).toInt(), 0, 2)));
    }

    connect(p, &mmm::Player::playbackStateChanged, this, &Playback::stateChanged);
    connect(p, &mmm::Player::positionChanged, this, &Playback::positionChanged);
    connect(p, &mmm::Player::durationChanged, this, &Playback::durationChanged);
    connect(p, &mmm::Player::seekableChanged, this, &Playback::seekableChanged);
    connect(p, &mmm::Player::volumeChanged, this, &Playback::volumeChanged);
    connect(p, &mmm::Player::replayGainModeChanged, this, &Playback::replayGainModeChanged);
    connect(q, &mmm::PlayQueue::repeatModeChanged, this, &Playback::repeatModeChanged);
    connect(q, &mmm::PlayQueue::upcomingChanged, this, &Playback::navigationChanged);
    connect(&m_controller, &mmm::PlaybackController::currentTrackChanged, this, [this] {
        Q_EMIT currentChanged();
        Q_EMIT navigationChanged();
    });
    if (m_persist) {
        connect(this, &Playback::volumeChanged, this, &Playback::saveSettings);
        connect(this, &Playback::repeatModeChanged, this, &Playback::saveSettings);
        connect(this, &Playback::replayGainModeChanged, this, &Playback::saveSettings);
    }

    if (options.mpris) {
        m_mpris = std::make_unique<mmm::MprisService>(&m_controller);
        QString error;
        if (!m_mpris->registerOn(QDBusConnection::sessionBus(), &error)) {
            qCWarning(lcPlaybackUi) << "MPRIS unavailable (no media keys / desktop controls):" << error;
        } else {
            connect(m_mpris.get(), &mmm::MprisService::raiseRequested, this, &Playback::raiseRequested);
            connect(m_mpris.get(), &mmm::MprisService::quitRequested, this, &Playback::quitRequested);
        }
    }
}

Playback::~Playback()
{
    m_mpris.reset();
    s_instance = nullptr;
}

Playback *Playback::instance() { return s_instance; }

Playback *Playback::create(QQmlEngine *, QJSEngine *)
{
    Q_ASSERT_X(s_instance, "Playback::create", "construct Playback before loading QML");
    QJSEngine::setObjectOwnership(s_instance, QJSEngine::CppOwnership);
    return s_instance;
}

void Playback::saveSettings() const
{
    QSettings s;
    s.setValue(kVolumeKey, volume());
    s.setValue(kRepeatKey, repeatMode());
    s.setValue(kReplayGainKey, replayGainMode());
}

bool Playback::isPlaying() const { return m_controller.player()->playbackState() == mmm::Player::PlaybackState::Playing; }
bool Playback::isPaused() const { return m_controller.player()->playbackState() == mmm::Player::PlaybackState::Paused; }
bool Playback::hasTrack() const { return m_controller.currentTrack() != nullptr; }
int Playback::currentTrackId() const { return hasTrack() ? static_cast<int>(m_controller.currentTrack()->id) : 0; }
QString Playback::title() const { return hasTrack() ? m_controller.currentTrack()->title : QString(); }
QString Playback::artist() const { return hasTrack() ? m_controller.currentTrack()->artist : QString(); }
QString Playback::album() const { return hasTrack() ? m_controller.currentTrack()->album : QString(); }

QString Playback::coverUrl() const
{
    const mmm::TrackRow *t = m_controller.currentTrack();
    if (!t)
        return {};
    return CoverLoader::url(t->albumId, t->coverPath, t->hasEmbeddedArt ? t->path : QString());
}

qint64 Playback::position() const { return m_controller.player()->position(); }

qint64 Playback::duration() const
{
    const qint64 d = m_controller.player()->duration();
    if (d > 0)
        return d;
    return hasTrack() ? m_controller.currentTrack()->durationMs : 0; // until GStreamer knows
}

bool Playback::isSeekable() const { return m_controller.player()->isSeekable(); }
bool Playback::canGoNext() const { return m_controller.canGoNext(); }
bool Playback::canGoPrevious() const { return m_controller.canGoPrevious(); }
double Playback::volume() const { return linearToSlider(m_controller.player()->volume()); }
void Playback::setVolume(double sliderValue) { m_controller.player()->setVolume(sliderToLinear(sliderValue)); }
int Playback::repeatMode() const { return static_cast<int>(m_controller.queue()->repeatMode()); }

void Playback::setRepeatMode(int mode)
{
    m_controller.queue()->setRepeatMode(static_cast<mmm::PlayQueue::RepeatMode>(std::clamp(mode, 0, 2)));
}

int Playback::replayGainMode() const { return static_cast<int>(m_controller.player()->replayGainMode()); }

void Playback::setReplayGainMode(int mode)
{
    m_controller.player()->setReplayGainMode(static_cast<mmm::Player::ReplayGainMode>(std::clamp(mode, 0, 2)));
}

void Playback::playFromModel(TrackListModel *model, int index)
{
    if (!model || model->rows().isEmpty())
        return;
    m_controller.playTracks(model->rows(), std::clamp(index, 0, static_cast<int>(model->rows().size()) - 1));
}

void Playback::enqueueFromModel(TrackListModel *model, bool playNext, int index)
{
    if (!model)
        return;
    QList<mmm::TrackRow> rows;
    if (index >= 0 && index < model->rows().size())
        rows << model->rows().at(index);
    else
        rows = model->rows();
    if (playNext)
        m_controller.queue()->insertNext(rows);
    else
        m_controller.queue()->append(rows);
}

void Playback::playPause() { m_controller.togglePlayPause(); }
void Playback::next() { m_controller.next(); }
void Playback::previous() { m_controller.previous(); }
void Playback::stop() { m_controller.stop(); }
void Playback::seek(qint64 positionMs) { m_controller.seek(positionMs); }
void Playback::playQueueIndex(int index) { m_controller.playIndex(index); }
void Playback::removeFromQueue(int index) { m_controller.queue()->removeAt(index); }
void Playback::moveInQueue(int from, int to) { m_controller.queue()->move(from, to); }
void Playback::clearQueue() { m_controller.clearQueue(); }
void Playback::cycleRepeatMode() { setRepeatMode((repeatMode() + 1) % 3); }

int Playback::saveQueueAsPlaylist(const QString &name)
{
    Library *lib = Library::instance();
    if (!lib)
        return 0;
    QStringList paths;
    for (const mmm::TrackRow &t : m_controller.queue()->items())
        paths << t.path;
    return lib->createPlaylist(name, paths);
}
