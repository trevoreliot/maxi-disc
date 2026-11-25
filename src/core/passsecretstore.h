// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QHash>
#include <QObject>
#include <QPointer>
#include <QTimer>

#include <deque>

#include "secretstore.h"

class QProcess;

namespace mmm {

// Secrets kept in pass, the standard Unix password manager (M14b; GPG-encrypted files in
// ~/.password-store, also what QtPass shows): for desktops without a working keyring. Entry =
// prefix + key ("music-md-manager/listenbrainz/token"). Runs the `pass` program, one call at a
// time (no overlapping passphrase prompts, no git lock races in a git-backed store); never
// waits on the caller's thread. A value goes in on stdin, never on the command line. Reading
// may ask for the GPG passphrase (the user's pinentry); writing only encrypts, so it doesn't.
class PassSecretStore final : public SecretStore
{
public:
    struct Options
    {
        QString program = QStringLiteral("pass");
        QString entryPrefix = QStringLiteral("music-md-manager/");
        QHash<QString, QString> extraEnv; // on top of the app's environment (tests: the store dir)
        int timeoutMs = 120'000;          // a passphrase prompt left open
    };

    explicit PassSecretStore(Options options);
    ~PassSecretStore() override;

    SecretBackend backend() const override { return SecretBackend::Pass; }
    QString entryLocation(const QString &key) const override { return m_options.entryPrefix + key; }
    void read(const QString &key, QObject *context, ReadDone done) override;
    void write(const QString &key, const QString &value, QObject *context, WriteDone done = {}) override;
    void remove(const QString &key, QObject *context, WriteDone done = {}) override;

    // `pass` can be run and its store is set up (has a .gpg-id).
    static bool available(const Options &options);
    // An entry name that's safe to hand to pass: ASCII letters, digits, ".", "_", "-", "/"
    // between non-empty segments, no "." / ".." segments, not starting with "-" or "/".
    static bool isValidEntry(const QString &entry);
    // The line of pass / gpg's errors that says what went wrong (gpg prints the key first). Never
    // looks at stdout (that's where a secret goes).
    static QString errorFrom(const QByteArray &stderrBytes, int exitCode, bool crashed = false);

private:
    enum class Op { Read, Write, Remove };
    struct Pending
    {
        Op op = Op::Read;
        QString entry;
        QString value;
        QPointer<QObject> context;
        bool hadContext = false;
        ReadDone readDone;
        WriteDone writeDone;
    };

    void enqueue(Pending p);
    void failLater(Pending p, const QString &error);
    void startNext();
    void finished(int exitCode, bool crashed);
    void finish(const QString &value, const QString &error);

    Options m_options;
    QObject m_self; // context of every connection: gone with the store
    std::deque<Pending> m_queue;
    Pending m_active;
    bool m_busy = false;
    QProcess *m_process = nullptr;
    QTimer m_timeout;
};

} // namespace mmm
