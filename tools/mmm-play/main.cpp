// SPDX-License-Identifier: GPL-3.0-or-later
//
// mmm-play: minimal terminal player built on mmm::Player.
//   mmm-play [--volume 0.8] [--seek SECONDS] FILE...
// Keys: space pause/resume · ←/→ seek ±10 s · +/- volume · n/p next/previous · q quit

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QFileInfo>
#include <QSocketNotifier>
#include <QStringList>
#include <QTextStream>

#include <csignal>
#include <cstdio>
#include <termios.h>
#include <unistd.h>

#include "appinfo.h"
#include "player.h"

namespace {

// Puts the terminal in non-canonical, no-echo mode for single-key input; restores it on
// destruction and on SIGINT/SIGTERM so the shell isn't left broken.
class RawTerminal
{
public:
    RawTerminal()
    {
        if (!isatty(STDIN_FILENO) || tcgetattr(STDIN_FILENO, &s_saved) != 0)
            return;
        s_active = true;
        termios raw = s_saved;
        raw.c_lflag &= ~static_cast<tcflag_t>(ICANON | ECHO);
        raw.c_cc[VMIN] = 1;
        raw.c_cc[VTIME] = 0;
        tcsetattr(STDIN_FILENO, TCSANOW, &raw);

        struct sigaction sa {};
        sa.sa_handler = &RawTerminal::onSignal;
        sigemptyset(&sa.sa_mask);
        sigaction(SIGINT, &sa, nullptr);
        sigaction(SIGTERM, &sa, nullptr);
    }
    ~RawTerminal() { restore(); }
    RawTerminal(const RawTerminal &) = delete;
    RawTerminal &operator=(const RawTerminal &) = delete;

    bool active() const { return s_active; }

private:
    static void restore()
    {
        if (s_active)
            tcsetattr(STDIN_FILENO, TCSANOW, &s_saved);
    }
    static void onSignal(int sig)
    {
        restore(); // async-signal-safe
        const char nl = '\n';
        [[maybe_unused]] auto n = write(STDOUT_FILENO, &nl, 1);
        _exit(128 + sig);
    }

    static inline termios s_saved{};
    static inline volatile sig_atomic_t s_active = 0;
};

QString formatTime(qint64 ms)
{
    if (ms < 0)
        return QStringLiteral("--:--");
    const qint64 s = ms / 1000;
    return QStringLiteral("%1:%2").arg(s / 60, 2, 10, QLatin1Char('0')).arg(s % 60, 2, 10, QLatin1Char('0'));
}

class Cli : public QObject
{
public:
    Cli(QStringList files, double volume, qint64 startSeekMs, const QByteArray &sink)
        : m_files(std::move(files))
        , m_player(mmm::PlayerOptions{.audioSink = sink})
        , m_startSeekMs(startSeekMs)
    {
        m_player.setVolume(volume);

        connect(&m_player, &mmm::Player::positionChanged, this, &Cli::printStatus);
        connect(&m_player, &mmm::Player::durationChanged, this, &Cli::printStatus);
        connect(&m_player, &mmm::Player::volumeChanged, this, &Cli::printStatus);
        connect(&m_player, &mmm::Player::playbackStateChanged, this, &Cli::printStatus);
        connect(&m_player, &mmm::Player::endOfStream, this, [this] { next(); });
        connect(&m_player, &mmm::Player::errorOccurred, this, [this](const QString &msg) {
            m_out << "\n  error: " << msg << Qt::endl;
            m_failed = true;
            next();
        });

        if (m_term.active()) {
            m_stdin = new QSocketNotifier(STDIN_FILENO, QSocketNotifier::Read, this);
            connect(m_stdin, &QSocketNotifier::activated, this, &Cli::readKeys);
            m_out << "keys: space pause · ←/→ seek 10s · +/- volume · n/p next/prev · q quit\n";
        }
        start(0);
    }

    bool failed() const { return m_failed; }

private:
    void start(qsizetype index)
    {
        m_index = index;
        const QString &file = m_files.at(m_index);
        m_out << "\n[" << (m_index + 1) << "/" << m_files.size() << "] " << file << Qt::endl;
        m_player.setSource(QUrl::fromLocalFile(QFileInfo(file).absoluteFilePath()));
        if (m_startSeekMs > 0)
            m_player.seek(m_startSeekMs);
        m_player.play();
    }

    void next()
    {
        if (m_index + 1 < m_files.size())
            start(m_index + 1);
        else
            quit();
    }

    void previous()
    {
        // Like most players: restart the track unless we're near its start.
        if (m_player.position() > 3000 || m_index == 0)
            m_player.seek(0);
        else
            start(m_index - 1);
    }

    void quit()
    {
        m_player.stop();
        m_out << Qt::endl;
        QCoreApplication::exit(m_failed ? 1 : 0);
    }

    void printStatus()
    {
        const char *icon = "■";
        switch (m_player.playbackState()) {
        case mmm::Player::PlaybackState::Playing: icon = "▶"; break;
        case mmm::Player::PlaybackState::Paused: icon = "⏸"; break;
        case mmm::Player::PlaybackState::Stopped: break;
        }
        m_out << "\r  " << icon << "  " << formatTime(m_player.position()) << " / "
              << formatTime(m_player.duration()) << "   vol "
              << qRound(m_player.volume() * 100) << "%   \x1b[K" << Qt::flush;
    }

    void readKeys()
    {
        char buf[16];
        const ssize_t n = ::read(STDIN_FILENO, buf, sizeof buf);
        if (n <= 0) {
            m_stdin->setEnabled(false);
            return;
        }
        const QByteArray keys(buf, static_cast<qsizetype>(n));
        if (keys == "\x1b[C") {
            m_player.seek(m_player.position() + 10000);
        } else if (keys == "\x1b[D") {
            m_player.seek(m_player.position() - 10000);
        } else {
            for (const char c : keys) {
                switch (c) {
                case ' ':
                    if (m_player.playbackState() == mmm::Player::PlaybackState::Playing)
                        m_player.pause();
                    else
                        m_player.play();
                    break;
                case '+': case '=': m_player.setVolume(m_player.volume() + 0.05); break;
                case '-': case '_': m_player.setVolume(m_player.volume() - 0.05); break;
                case 'n': next(); return;
                case 'p': previous(); return;
                case 'q': quit(); return;
                default: break;
                }
            }
        }
    }

    QStringList m_files;
    mmm::Player m_player;
    qint64 m_startSeekMs;
    qsizetype m_index = 0;
    bool m_failed = false;
    RawTerminal m_term;
    QSocketNotifier *m_stdin = nullptr;
    QTextStream m_out{stdout};
};

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("mmm-play"));
    QCoreApplication::setApplicationVersion(mmm::AppInfo::version());

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Play audio files with the music-md-manager engine."));
    parser.addHelpOption();
    parser.addVersionOption();
    const QCommandLineOption volumeOpt(QStringLiteral("volume"),
                                       QStringLiteral("Initial volume 0.0–1.0 (default 0.8)."),
                                       QStringLiteral("level"), QStringLiteral("0.8"));
    const QCommandLineOption seekOpt(QStringLiteral("seek"),
                                     QStringLiteral("Start each file at this many seconds."),
                                     QStringLiteral("seconds"), QStringLiteral("0"));
    const QCommandLineOption sinkOpt(QStringLiteral("audio-sink"),
                                     QStringLiteral("GStreamer sink description (default: system audio)."),
                                     QStringLiteral("description"));
    parser.addOptions({volumeOpt, seekOpt, sinkOpt});
    parser.addPositionalArgument(QStringLiteral("files"), QStringLiteral("Audio files to play in order."),
                                 QStringLiteral("FILE..."));
    parser.process(app);

    const QStringList files = parser.positionalArguments();
    if (files.isEmpty())
        parser.showHelp(2);

    bool ok = false;
    const double volume = parser.value(volumeOpt).toDouble(&ok);
    if (!ok) {
        std::fputs("--volume must be a number\n", stderr);
        return 2;
    }
    const double seekSec = parser.value(seekOpt).toDouble(&ok);
    if (!ok || seekSec < 0) {
        std::fputs("--seek must be a non-negative number\n", stderr);
        return 2;
    }

    Cli cli(files, volume, static_cast<qint64>(seekSec * 1000), parser.value(sinkOpt).toUtf8());
    return QCoreApplication::exec();
}
