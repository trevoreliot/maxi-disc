// SPDX-License-Identifier: GPL-3.0-or-later
#include <QElapsedTimer>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

#include "passsecretstore.h"

using namespace mmm;

namespace {

// A stand-in `pass` (never the real one: no GPG, no ~/.password-store). Entries are plain files
// under $PASSWORD_STORE_DIR; it logs its arguments and stdin; FAKE_MODE picks a failure.
const QByteArray kFakePass =
    "#!/bin/sh\n"
    "printf '%s\\n' \"$*\" >> \"$FAKE_LOG_DIR/argv.log\"\n"
    "cmd=\"$1\"; shift\n"
    "case \"${FAKE_MODE:-ok}\" in\n"
    "  slow) exec sleep 5 ;;\n"
    "  stdout_fail) cat > /dev/null; echo 'SECRET-ON-STDOUT'; exit 1 ;;\n"
    "  gpg_cancel)\n"
    "    cat > /dev/null\n"
    "    echo 'gpg: encrypted with 4096-bit RSA key, ID 0123456789ABCDEF, created 2020-01-01' >&2\n"
    "    echo '      \"Someone <someone@example.org>\"' >&2\n"
    "    echo 'gpg: public key decryption failed: Operation cancelled' >&2\n"
    "    echo 'gpg: decryption failed: Operation cancelled' >&2\n"
    "    exit 2 ;;\n"
    "  encrypt_fail)\n"
    "    cat > /dev/null\n"
    "    echo 'gpg: ABCDEF: skipped: No public key' >&2\n"
    "    echo 'gpg: [stdin]: encryption failed: No public key' >&2\n"
    "    exit 2 ;;\n"
    "esac\n"
    "entry=''\n"
    "for a in \"$@\"; do case \"$a\" in -*) ;; *) entry=\"$a\" ;; esac; done\n"
    "f=\"$PASSWORD_STORE_DIR/$entry.txt\"\n"
    "case \"$cmd\" in\n"
    "  show) if [ -f \"$f\" ]; then cat \"$f\"; else echo \"Error: $entry is not in the password store.\" >&2; exit 1; fi ;;\n"
    "  insert) mkdir -p \"$(dirname \"$f\")\"; cat > \"$f\"; cat \"$f\" >> \"$FAKE_LOG_DIR/stdin.log\" ;;\n"
    "  rm) if [ -f \"$f\" ]; then rm \"$f\"; else echo \"Error: $entry is not in the password store.\" >&2; exit 1; fi ;;\n"
    "esac\n";

const QString kToken = QStringLiteral("0123abcd-ef45-6789-abcd-ef0123456789");
const QString kKey = QStringLiteral("listenbrainz/token");

struct Got
{
    bool called = false;
    QString value;
    QString error;
};

} // namespace

class TestPassSecretStore : public QObject
{
    Q_OBJECT

    std::unique_ptr<QTemporaryDir> m_dir;
    QString m_program;

    PassSecretStore::Options options(const QString &mode = QStringLiteral("ok"), int timeoutMs = 10'000)
    {
        PassSecretStore::Options o;
        o.program = m_program;
        o.extraEnv.insert(QStringLiteral("PASSWORD_STORE_DIR"), m_dir->filePath(QStringLiteral("store")));
        o.extraEnv.insert(QStringLiteral("FAKE_LOG_DIR"), m_dir->path());
        o.extraEnv.insert(QStringLiteral("FAKE_MODE"), mode);
        o.timeoutMs = timeoutMs;
        return o;
    }
    QByteArray log(const char *name) const
    {
        QFile f(m_dir->filePath(QString::fromLatin1(name)));
        return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
    }
    Got readNow(PassSecretStore &s, const QString &key = kKey)
    {
        Got g;
        s.read(key, this, [&](const QString &v, const QString &e) { g = Got{true, v, e}; });
        [&] { QTRY_VERIFY_WITH_TIMEOUT(g.called, 5000); }();
        return g;
    }
    Got writeNow(PassSecretStore &s, const QString &value, const QString &key = kKey)
    {
        Got g;
        s.write(key, value, this, [&](const QString &e) { g = Got{true, {}, e}; });
        [&] { QTRY_VERIFY_WITH_TIMEOUT(g.called, 5000); }();
        return g;
    }
    Got removeNow(PassSecretStore &s, const QString &key = kKey)
    {
        Got g;
        s.remove(key, this, [&](const QString &e) { g = Got{true, {}, e}; });
        [&] { QTRY_VERIFY_WITH_TIMEOUT(g.called, 5000); }();
        return g;
    }
    void putEntry(const QByteArray &content)
    {
        QVERIFY(QDir(m_dir->path()).mkpath(QStringLiteral("store/music-md-manager/listenbrainz")));
        QFile f(m_dir->filePath(QStringLiteral("store/music-md-manager/listenbrainz/token.txt")));
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(content);
    }

private Q_SLOTS:
    void init()
    {
        m_dir = std::make_unique<QTemporaryDir>();
        QVERIFY(m_dir->isValid());
        m_program = m_dir->filePath(QStringLiteral("fake-pass"));
        QFile f(m_program);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(kFakePass);
        f.close();
        QVERIFY(f.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
    }
    void cleanup() { m_dir.reset(); }

    void writeReadRemove()
    {
        PassSecretStore s(options());
        QCOMPARE(s.backend(), SecretBackend::Pass);
        QCOMPARE(s.entryLocation(kKey), QStringLiteral("music-md-manager/listenbrainz/token"));
        QCOMPARE(writeNow(s, kToken).error, QString());
        const Got g = readNow(s);
        QCOMPARE(g.error, QString());
        QCOMPARE(g.value, kToken);
        QCOMPARE(removeNow(s).error, QString());
        const Got gone = readNow(s);
        QCOMPARE(gone.value, QString()); // not there: ("", "")
        QCOMPARE(gone.error, QString());

        // The token went in on stdin (one line), never on a command line.
        QCOMPARE(log("stdin.log"), (kToken + QLatin1Char('\n')).toUtf8());
        const QByteArray argv = log("argv.log");
        QVERIFY(!argv.contains(kToken.toUtf8()));
        QVERIFY(argv.contains("insert --multiline --force -- music-md-manager/listenbrainz/token"));
        QVERIFY(argv.contains("show -- music-md-manager/listenbrainz/token"));
        QVERIFY(argv.contains("rm --force -- music-md-manager/listenbrainz/token"));
    }

    void removingWhatIsntThereIsFine()
    {
        PassSecretStore s(options());
        QCOMPARE(removeNow(s).error, QString()); // pass says "not in the password store", exit 1
    }

    void theFirstLineIsTheSecret()
    {
        // Edited in QtPass, say: more lines under the password.
        putEntry("tok-123\r\nuser: alice\n");
        PassSecretStore s(options());
        QCOMPARE(readNow(s).value, QStringLiteral("tok-123"));
        putEntry("no-newline");
        QCOMPARE(readNow(s).value, QStringLiteral("no-newline"));
        putEntry("");
        const Got empty = readNow(s);
        QCOMPARE(empty.value, QString());
        QCOMPARE(empty.error, QString());
    }

    void gpgsOwnWordsAreTheError()
    {
        PassSecretStore cancel(options(QStringLiteral("gpg_cancel")));
        const Got g = readNow(cancel);
        QCOMPARE(g.value, QString());
        QCOMPARE(g.error, QStringLiteral("gpg: decryption failed: Operation cancelled")); // not the key line
        PassSecretStore noKey(options(QStringLiteral("encrypt_fail")));
        QCOMPARE(writeNow(noKey, kToken).error, QStringLiteral("gpg: [stdin]: encryption failed: No public key"));
        QCOMPARE(PassSecretStore::errorFrom({}, 3), QStringLiteral("pass exited with code 3"));
        QCOMPARE(PassSecretStore::errorFrom({}, 0, true), QStringLiteral("pass crashed"));
        QCOMPARE(PassSecretStore::errorFrom("\nError: no store\n", 1), QStringLiteral("Error: no store"));
    }

    void aSecretOnStdoutNeverBecomesTheError()
    {
        // pass printed something (the secret, say) and then failed without a word on stderr.
        PassSecretStore s(options(QStringLiteral("stdout_fail")));
        const Got g = readNow(s);
        QCOMPARE(g.value, QString());
        QCOMPARE(g.error, QStringLiteral("pass exited with code 1"));
        QVERIFY(!g.error.contains(QStringLiteral("SECRET")));
    }

    void badNamesAndValuesNeverRunPass()
    {
        for (const char *bad : {"", "/abs", "a/", "a//b", "../x", "a/../b", "a/./b", "bad space", "bad$char", "tök"}) {
            const QString entry = QString::fromUtf8(bad);
            QVERIFY2(!PassSecretStore::isValidEntry(entry), bad);
        }
        QVERIFY(!PassSecretStore::isValidEntry(QStringLiteral("-flag")));
        QVERIFY(PassSecretStore::isValidEntry(QStringLiteral("music-md-manager/listenbrainz/token")));
        PassSecretStore s(options());
        QVERIFY(!readNow(s, QStringLiteral("../escape")).error.isEmpty());
        QVERIFY(!writeNow(s, QStringLiteral("line1\nline2")).error.isEmpty());
        QVERIFY(!writeNow(s, QString()).error.isEmpty());
        QVERIFY(log("argv.log").isEmpty()); // pass never ran
    }

    void noPassProgram()
    {
        PassSecretStore::Options o = options();
        o.program = m_dir->filePath(QStringLiteral("no-such-pass"));
        PassSecretStore s(o);
        const Got g = readNow(s);
        QVERIFY(g.error.startsWith(QStringLiteral("can't run pass")));
        QCOMPARE(writeNow(s, kToken).error.startsWith(QStringLiteral("can't run pass")), true); // and the next one too
    }

    void aPromptLeftOpenTimesOut()
    {
        PassSecretStore s(options(QStringLiteral("slow"), 150));
        QElapsedTimer t;
        t.start();
        const Got g = readNow(s);
        QVERIFY(g.error.contains(QStringLiteral("didn't answer")));
        QVERIFY(t.elapsed() < 3000);
    }

    void aGoneCallerHearsNothing()
    {
        PassSecretStore s(options(QStringLiteral("slow"), 300));
        auto first = std::make_unique<QObject>();
        auto second = std::make_unique<QObject>();
        bool firstCalled = false;
        bool secondCalled = false;
        s.read(kKey, first.get(), [&](const QString &, const QString &) { firstCalled = true; });
        s.read(kKey, second.get(), [&](const QString &, const QString &) { secondCalled = true; }); // queued
        QTRY_VERIFY(!log("argv.log").isEmpty());
        first.reset();  // gone while its pass runs: not called back
        second.reset(); // gone while queued: its pass never runs (no passphrase prompt for nobody)
        QTest::qWait(700);
        QVERIFY(!firstCalled);
        QVERIFY(!secondCalled);
        QCOMPARE(log("argv.log").count('\n'), 1);
    }

    void oneCallAtATime()
    {
        PassSecretStore s(options());
        QStringList order;
        QString readBack;
        s.write(kKey, kToken, this, [&](const QString &e) { order << QStringLiteral("write:") + e; });
        s.read(kKey, this, [&](const QString &v, const QString &e) {
            order << QStringLiteral("read:") + e;
            readBack = v;
        });
        s.remove(kKey, this, [&](const QString &e) { order << QStringLiteral("remove:") + e; });
        QTRY_COMPARE(order.size(), 3);
        QCOMPARE(order, QStringList({QStringLiteral("write:"), QStringLiteral("read:"), QStringLiteral("remove:")}));
        QCOMPARE(readBack, kToken); // the read ran after the write finished
    }

    void availableOnlyWhenSetUp()
    {
        PassSecretStore::Options o = options();
        QVERIFY(!PassSecretStore::available(o)); // no .gpg-id in the store yet
        QVERIFY(QDir(m_dir->path()).mkpath(QStringLiteral("store")));
        QFile id(m_dir->filePath(QStringLiteral("store/.gpg-id")));
        QVERIFY(id.open(QIODevice::WriteOnly));
        id.write("ABCDEF\n");
        id.close();
        QVERIFY(PassSecretStore::available(o));
        o.program = m_dir->filePath(QStringLiteral("no-such-pass"));
        QVERIFY(!PassSecretStore::available(o));
    }

    void aKeyringThatFailsHandsOverToPass()
    {
        // QtKeychain says there's a keyring; every call to it fails (the user's switched-off wallet).
        auto keyring = std::make_shared<MemorySecretStore>();
        keyring->pretendBackend = SecretBackend::Keyring;
        keyring->failWith = QStringLiteral("The name is not activatable");
        auto pass = std::make_shared<MemorySecretStore>();
        pass->pretendBackend = SecretBackend::Pass;
        pass->values.insert(kKey, kToken);
        const auto store = FallbackSecretStore::create(keyring, pass);
        QCOMPARE(store->backend(), SecretBackend::Keyring);
        Got g;
        store->read(kKey, this, [&](const QString &v, const QString &e) { g = Got{true, v, e}; });
        QTRY_VERIFY(g.called);
        QCOMPARE(g.error, QString()); // the read went on to pass
        QCOMPARE(g.value, kToken);
        QVERIFY(store->switched());
        QCOMPARE(store->backend(), SecretBackend::Pass);
        QCOMPARE(store->entryLocation(kKey), QStringLiteral("music-md-manager/listenbrainz/token"));
        // From now on: pass only.
        keyring->failWith.clear();
        bool written = false;
        store->write(kKey, QStringLiteral("new"), this, [&](const QString &e) { written = e.isEmpty(); });
        QTRY_VERIFY(written);
        QCOMPARE(pass->values.value(kKey), QStringLiteral("new"));
        QVERIFY(!keyring->values.contains(kKey));
    }

    void aKeyringThatWorkedStays()
    {
        auto keyring = std::make_shared<MemorySecretStore>();
        keyring->pretendBackend = SecretBackend::Keyring;
        auto pass = std::make_shared<MemorySecretStore>();
        pass->pretendBackend = SecretBackend::Pass;
        const auto store = FallbackSecretStore::create(keyring, pass);
        bool written = false;
        store->write(kKey, kToken, this, [&](const QString &e) { written = e.isEmpty(); });
        QTRY_VERIFY(written);
        QCOMPARE(keyring->values.value(kKey), kToken);
        // A later failure (a cancelled unlock prompt, say) is just an error: nothing moves to pass.
        keyring->failWith = QStringLiteral("cancelled");
        Got g;
        store->read(kKey, this, [&](const QString &v, const QString &e) { g = Got{true, v, e}; });
        QTRY_VERIFY(g.called);
        QCOMPARE(g.error, QStringLiteral("cancelled"));
        QVERIFY(!store->switched());
        QVERIFY(pass->values.isEmpty());
        // Not-found on the keyring is an answer, not a failure.
        auto fresh = FallbackSecretStore::create(std::make_shared<MemorySecretStore>(), pass);
        Got none;
        fresh->read(kKey, this, [&](const QString &v, const QString &e) { none = Got{true, v, e}; });
        QTRY_VERIFY(none.called);
        QVERIFY(!fresh->switched());
    }

    void cancellingTheKeyringsPromptIsntASwitch()
    {
        // The first call of the session: the user says no to the wallet's unlock prompt.
        auto keyring = std::make_shared<MemorySecretStore>();
        keyring->failWith = QStringLiteral("Access to keychain denied");
        keyring->failureIsUnusable = false;
        auto pass = std::make_shared<MemorySecretStore>();
        pass->pretendBackend = SecretBackend::Pass;
        const auto store = FallbackSecretStore::create(keyring, pass);
        Got g;
        store->read(kKey, this, [&](const QString &v, const QString &e) { g = Got{true, v, e}; });
        QTRY_VERIFY(g.called);
        QCOMPARE(g.error, QStringLiteral("Access to keychain denied"));
        QVERIFY(!store->switched());
    }

    void callsInFlightFollowTheSwitch()
    {
        // A read and a write both sent to the broken keyring before either answered: both go on to pass.
        auto keyring = std::make_shared<MemorySecretStore>();
        keyring->failWith = QStringLiteral("The name is not activatable");
        auto pass = std::make_shared<MemorySecretStore>();
        pass->pretendBackend = SecretBackend::Pass;
        const auto store = FallbackSecretStore::create(keyring, pass);
        Got r;
        QString writeError = QStringLiteral("(not called)");
        store->read(kKey, this, [&](const QString &v, const QString &e) { r = Got{true, v, e}; });
        store->write(kKey, kToken, this, [&](const QString &e) { writeError = e; });
        QTRY_VERIFY(r.called);
        QTRY_COMPARE(writeError, QString());
        QCOMPARE(pass->values.value(kKey), kToken);
    }

    void choosingTheStore()
    {
        int keyringProbes = 0;
        int passProbes = 0;
        bool keyringWorks = true;
        bool passSetUp = true;
        SecretStoreProbe probe;
        probe.keyringAvailable = [&] { ++keyringProbes; return keyringWorks; };
        probe.passAvailable = [&] { ++passProbes; return passSetUp; };
        probe.makeKeyring = [] {
            auto m = std::make_shared<MemorySecretStore>();
            m->pretendBackend = SecretBackend::Keyring;
            return std::shared_ptr<SecretStore>(m);
        };
        probe.makePass = [] {
            auto m = std::make_shared<MemorySecretStore>();
            m->pretendBackend = SecretBackend::Pass;
            return std::shared_ptr<SecretStore>(m);
        };
        using P = SecretBackendPreference;
        // A keyring and pass: the keyring, with pass behind it (QtKeychain's word isn't enough).
        const auto both = chooseSecretStore(P::Auto, probe);
        QCOMPARE(both->backend(), SecretBackend::Keyring);
        QVERIFY(std::dynamic_pointer_cast<FallbackSecretStore>(both));
        passSetUp = false;
        QVERIFY(!std::dynamic_pointer_cast<FallbackSecretStore>(chooseSecretStore(P::Auto, probe))); // no pass: just the keyring
        passSetUp = true;
        keyringWorks = false;
        QCOMPARE(chooseSecretStore(P::Auto, probe)->backend(), SecretBackend::Pass);
        passSetUp = false;
        QCOMPARE(chooseSecretStore(P::Auto, probe)->backend(), SecretBackend::Keyring); // its errors say why
        keyringProbes = passProbes = 0;
        QCOMPARE(chooseSecretStore(P::Pass, probe)->backend(), SecretBackend::Pass);
        QCOMPARE(chooseSecretStore(P::Keyring, probe)->backend(), SecretBackend::Keyring);
        QCOMPARE(keyringProbes + passProbes, 0); // asked for one: nothing probed

        QCOMPARE(parseSecretBackendPreference(QStringLiteral(" Pass ")), P::Pass);
        QCOMPARE(parseSecretBackendPreference(QStringLiteral("keyring")), P::Keyring);
        QCOMPARE(parseSecretBackendPreference(QString()), P::Auto);
        QCOMPARE(parseSecretBackendPreference(QStringLiteral("auto")), P::Auto);
        QCOMPARE(parseSecretBackendPreference(QStringLiteral("plaintext")), P::Auto); // no such thing
    }
};

QTEST_GUILESS_MAIN(TestPassSecretStore)
#include "tst_passsecretstore.moc"
