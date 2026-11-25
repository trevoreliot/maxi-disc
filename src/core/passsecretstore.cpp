// SPDX-License-Identifier: GPL-3.0-or-later
#include "passsecretstore.h"

#include <QDir>
#include <QFileInfo>
#include <QLoggingCategory>
#include <QProcess>
#include <QStandardPaths>

#include <signal.h>

Q_LOGGING_CATEGORY(lcPass, "mmm.pass")

namespace mmm {

namespace {

const QByteArray kMissing = QByteArrayLiteral("is not in the password store");

// pass is a shell script running gpg: stop all of it (its own session / process group).
void killAll(QProcess *p)
{
    if (const qint64 pid = p->processId(); pid > 0)
        ::kill(-static_cast<pid_t>(pid), SIGKILL);
    p->kill();
}

} // namespace

PassSecretStore::PassSecretStore(Options options)
    : m_options(std::move(options))
{
    if (!m_options.entryPrefix.isEmpty() && !m_options.entryPrefix.endsWith(QLatin1Char('/')))
        m_options.entryPrefix += QLatin1Char('/');
    m_timeout.setSingleShot(true);
    QObject::connect(&m_timeout, &QTimer::timeout, &m_self, [this] {
        finish({}, QStringLiteral("pass didn't answer in time (a passphrase prompt left open?)"));
    });
}

PassSecretStore::~PassSecretStore()
{
    m_queue.clear();
    m_busy = false;
    if (QProcess *p = m_process) {
        m_process = nullptr;
        p->disconnect(&m_self);
        killAll(p);
        p->waitForFinished(1000); // (quitting: else ~QProcess waits up to 30 s)
        delete p;
    }
}

bool PassSecretStore::available(const Options &options)
{
    const QFileInfo program(options.program);
    const bool runnable = program.isAbsolute() ? program.isFile() && program.isExecutable()
                                               : !QStandardPaths::findExecutable(options.program).isEmpty();
    if (!runnable)
        return false;
    // (What the pass we run will see: the extra environment wins, even "".)
    const QString var = QStringLiteral("PASSWORD_STORE_DIR");
    QString store = options.extraEnv.contains(var) ? options.extraEnv.value(var) : qEnvironmentVariable("PASSWORD_STORE_DIR");
    if (store.isEmpty())
        store = QDir::home().filePath(QStringLiteral(".password-store"));
    return QFileInfo(QDir(store).filePath(QStringLiteral(".gpg-id"))).isFile();
}

bool PassSecretStore::isValidEntry(const QString &entry)
{
    if (entry.isEmpty() || entry.startsWith(QLatin1Char('-')) || entry.startsWith(QLatin1Char('/')))
        return false;
    for (const QChar c : entry) {
        const char16_t u = c.unicode();
        const bool ok = (u >= u'a' && u <= u'z') || (u >= u'A' && u <= u'Z') || (u >= u'0' && u <= u'9') || u == u'.'
            || u == u'_' || u == u'-' || u == u'/';
        if (!ok)
            return false;
    }
    const QStringList segments = entry.split(QLatin1Char('/'));
    return std::none_of(segments.cbegin(), segments.cend(), [](const QString &s) {
        return s.isEmpty() || s == QLatin1String(".") || s == QLatin1String("..");
    });
}

QString PassSecretStore::errorFrom(const QByteArray &stderrBytes, int exitCode, bool crashed)
{
    // Only stderr: stdout is where a secret goes.
    const auto lines = [](const QByteArray &bytes) {
        QStringList out;
        for (const QString &l : QString::fromUtf8(bytes).split(QLatin1Char('\n'))) {
            if (!l.trimmed().isEmpty())
                out << l.trimmed();
        }
        return out;
    };
    const QStringList err = lines(stderrBytes);
    QString found;
    for (auto it = err.crbegin(); it != err.crend() && found.isEmpty(); ++it) {
        if ((it->startsWith(QLatin1String("Error:")) || it->startsWith(QLatin1String("gpg:")))
            && !it->startsWith(QLatin1String("gpg: encrypted with")))
            found = *it;
    }
    if (found.isEmpty() && !err.isEmpty())
        found = err.last();
    if (found.isEmpty())
        found = crashed ? QStringLiteral("pass crashed") : QStringLiteral("pass exited with code %1").arg(exitCode);
    if (found.size() > 200)
        found = found.left(199) + QChar(0x2026);
    return found;
}

void PassSecretStore::read(const QString &key, QObject *context, ReadDone done)
{
    Pending p;
    p.op = Op::Read;
    p.entry = m_options.entryPrefix + key;
    p.context = context;
    p.hadContext = context != nullptr;
    p.readDone = std::move(done);
    enqueue(std::move(p));
}

void PassSecretStore::write(const QString &key, const QString &value, QObject *context, WriteDone done)
{
    Pending p;
    p.op = Op::Write;
    p.entry = m_options.entryPrefix + key;
    p.value = value;
    p.context = context;
    p.hadContext = context != nullptr;
    p.writeDone = std::move(done);
    // One line (the first line is what's read back), and something to keep.
    if (value.isEmpty()) {
        failLater(std::move(p), QStringLiteral("nothing to keep"));
        return;
    }
    if (value.contains(QLatin1Char('\n')) || value.contains(QLatin1Char('\r'))) {
        failLater(std::move(p), QStringLiteral("only a single line can be kept in pass"));
        return;
    }
    enqueue(std::move(p));
}

void PassSecretStore::remove(const QString &key, QObject *context, WriteDone done)
{
    Pending p;
    p.op = Op::Remove;
    p.entry = m_options.entryPrefix + key;
    p.context = context;
    p.hadContext = context != nullptr;
    p.writeDone = std::move(done);
    enqueue(std::move(p));
}

void PassSecretStore::failLater(Pending p, const QString &error)
{
    QObject *target = p.hadContext ? p.context.data() : &m_self;
    if (!target)
        return;
    QTimer::singleShot(0, target, [p = std::move(p), error] {
        if (p.readDone)
            p.readDone({}, error);
        else if (p.writeDone)
            p.writeDone(error);
    });
}

void PassSecretStore::enqueue(Pending p)
{
    if (!isValidEntry(p.entry)) {
        failLater(std::move(p), QStringLiteral("not a valid pass entry name"));
        return;
    }
    m_queue.push_back(std::move(p));
    startNext();
}

void PassSecretStore::startNext()
{
    while (!m_busy && !m_queue.empty()) {
        Pending p = std::move(m_queue.front());
        m_queue.pop_front();
        if (p.op == Op::Read && p.hadContext && !p.context)
            continue; // nobody left to tell: don't ask for a passphrase for nothing
        m_active = std::move(p);
        m_busy = true;

        auto *proc = new QProcess(&m_self); // (gone with the store at the latest)
        m_process = proc;
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment(); // pinentry needs the display
        env.insert(QStringLiteral("LC_MESSAGES"), QStringLiteral("C")); // errors we can pick out
        for (auto it = m_options.extraEnv.cbegin(); it != m_options.extraEnv.cend(); ++it)
            env.insert(it.key(), it.value());
        proc->setProcessEnvironment(env);
        proc->setUnixProcessParameters(QProcess::UnixProcessFlag::CreateNewSession); // killAll()
        proc->setProgram(m_options.program);
        switch (m_active.op) {
        case Op::Read:
            proc->setArguments({QStringLiteral("show"), QStringLiteral("--"), m_active.entry});
            break;
        case Op::Write:
            proc->setArguments({QStringLiteral("insert"), QStringLiteral("--multiline"), QStringLiteral("--force"),
                                QStringLiteral("--"), m_active.entry});
            break;
        case Op::Remove:
            proc->setArguments({QStringLiteral("rm"), QStringLiteral("--force"), QStringLiteral("--"), m_active.entry});
            break;
        }
        QObject::connect(proc, &QProcess::started, &m_self, [this, proc] {
            if (proc != m_process)
                return;
            if (m_active.op == Op::Write)
                proc->write(m_active.value.toUtf8() + '\n'); // stdin, never argv
            proc->closeWriteChannel();
        });
        QObject::connect(proc, &QProcess::finished, &m_self, [this, proc](int code, QProcess::ExitStatus status) {
            if (proc == m_process)
                finished(code, status == QProcess::CrashExit);
        });
        QObject::connect(proc, &QProcess::errorOccurred, &m_self, [this, proc](QProcess::ProcessError e) {
            // (No finished() follows a failed start, which may report from inside start():
            // answered later, like everything else.)
            if (proc != m_process || e != QProcess::FailedToStart)
                return;
            const QString error = QStringLiteral("can't run pass: %1").arg(proc->errorString());
            QTimer::singleShot(0, &m_self, [this, proc, error] {
                if (proc == m_process)
                    finish({}, error);
            });
        });
        if (m_options.timeoutMs > 0)
            m_timeout.start(m_options.timeoutMs);
        proc->start();
        return; // one at a time: the next starts when this one's done
    }
}

void PassSecretStore::finished(int exitCode, bool crashed)
{
    const QByteArray out = m_process->readAllStandardOutput();
    const QByteArray err = m_process->readAllStandardError();
    const bool ok = !crashed && exitCode == 0;
    const bool missing = !crashed && err.contains(kMissing);
    const QString error = ok || missing ? QString() : errorFrom(err, exitCode, crashed);
    if (!error.isEmpty())
        qCInfo(lcPass) << "pass failed:" << error;
    switch (m_active.op) {
    case Op::Read:
        if (ok) {
            QString value = QString::fromUtf8(out);
            value = value.left(value.indexOf(QLatin1Char('\n'))); // the first line (-1: all of it)
            if (value.endsWith(QLatin1Char('\r')))
                value.chop(1);
            finish(value, {});
        } else {
            finish({}, error); // not there: ("", "")
        }
        return;
    case Op::Write:
        finish({}, !ok && missing ? errorFrom(err, exitCode, crashed) : error);
        return;
    case Op::Remove:
        finish({}, error); // not there: gone is gone
        return;
    }
}

void PassSecretStore::finish(const QString &value, const QString &error)
{
    if (!m_busy)
        return;
    m_timeout.stop();
    if (QProcess *p = m_process) {
        m_process = nullptr;
        p->disconnect(&m_self);
        if (p->state() != QProcess::NotRunning) {
            QObject::connect(p, &QProcess::finished, p, &QObject::deleteLater);
            killAll(p);
        } else {
            p->deleteLater();
        }
    }
    const Pending p = std::move(m_active);
    m_active = Pending{};
    m_busy = false;
    QPointer<QObject> alive(&m_self); // a callback may end the store
    if (!p.hadContext || p.context) {
        if (p.op == Op::Read) {
            if (p.readDone)
                p.readDone(value, error);
        } else if (p.writeDone) {
            p.writeDone(error);
        }
    }
    if (alive)
        startNext();
}

} // namespace mmm
