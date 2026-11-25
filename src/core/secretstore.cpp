// SPDX-License-Identifier: GPL-3.0-or-later
#include "secretstore.h"

#include "passsecretstore.h"

#include <QLoggingCategory>
#include <QPointer>
#include <QSet>
#include <QTimer>

#include <tuple>

#include <qt6keychain/keychain.h>

namespace mmm {

namespace {

const QString kService = QStringLiteral("music-md-manager");

class KeychainSecretStore final : public SecretStore
{
public:
    SecretBackend backend() const override { return SecretBackend::Keyring; }
    // The user saying no (a cancelled unlock prompt) isn't the keyring being unusable.
    bool isUnusable(const QString &error) const override { return !m_refusals->contains(error); }

    void read(const QString &key, QObject *context, ReadDone done) override
    {
        auto *job = new QKeychain::ReadPasswordJob(kService);
        job->setAutoDelete(true);
        job->setKey(key);
        QPointer<QObject> guard(context);
        QObject::connect(job, &QKeychain::Job::finished, job, [job, guard, done = std::move(done), refusals = m_refusals] {
            note(job, *refusals);
            if (!guard || !done)
                return;
            if (job->error() == QKeychain::EntryNotFound)
                done({}, {});
            else if (job->error() != QKeychain::NoError)
                done({}, job->errorString());
            else
                done(job->textData(), {});
        });
        job->start();
    }

    void write(const QString &key, const QString &value, QObject *context, WriteDone done) override
    {
        auto *job = new QKeychain::WritePasswordJob(kService);
        job->setAutoDelete(true);
        job->setKey(key);
        job->setTextData(value);
        finish(job, context, std::move(done));
    }

    void remove(const QString &key, QObject *context, WriteDone done) override
    {
        auto *job = new QKeychain::DeletePasswordJob(kService);
        job->setAutoDelete(true);
        job->setKey(key);
        finish(job, context, std::move(done), true);
    }

private:
    static void note(const QKeychain::Job *job, QSet<QString> &refusals)
    {
        if (job->error() == QKeychain::AccessDeniedByUser)
            refusals.insert(job->errorString());
    }

    void finish(QKeychain::Job *job, QObject *context, WriteDone done, bool missingIsFine = false)
    {
        QPointer<QObject> guard(context);
        QObject::connect(job, &QKeychain::Job::finished, job, [job, guard, done = std::move(done), missingIsFine, refusals = m_refusals] {
            note(job, *refusals);
            if (!guard || !done)
                return;
            const bool ok = job->error() == QKeychain::NoError || (missingIsFine && job->error() == QKeychain::EntryNotFound);
            done(ok ? QString() : job->errorString());
        });
        job->start();
    }

    std::shared_ptr<QSet<QString>> m_refusals = std::make_shared<QSet<QString>>(); // errors that were the user's "no"
};

// Answers later, like a keyring.
void later(QObject *context, std::function<void()> f)
{
    QTimer::singleShot(0, context, std::move(f));
}

} // namespace

std::shared_ptr<SecretStore> makeKeychainSecretStore()
{
    return std::make_shared<KeychainSecretStore>();
}

SecretBackendPreference parseSecretBackendPreference(const QString &value)
{
    const QString v = value.trimmed().toLower();
    if (v == QLatin1String("keyring"))
        return SecretBackendPreference::Keyring;
    if (v == QLatin1String("pass"))
        return SecretBackendPreference::Pass;
    if (!v.isEmpty() && v != QLatin1String("auto"))
        qWarning() << "unknown secret store" << value << "(auto, keyring or pass): auto";
    return SecretBackendPreference::Auto;
}

FallbackSecretStore::FallbackSecretStore(std::shared_ptr<SecretStore> first, std::shared_ptr<SecretStore> second)
    : m_first(std::move(first))
    , m_second(std::move(second))
{
}

std::shared_ptr<FallbackSecretStore> FallbackSecretStore::create(std::shared_ptr<SecretStore> first,
                                                                 std::shared_ptr<SecretStore> second)
{
    // (Shared-owned only: the answers find the store through weak_from_this().)
    return std::shared_ptr<FallbackSecretStore>(new FallbackSecretStore(std::move(first), std::move(second)));
}

SecretBackend FallbackSecretStore::backend() const
{
    return current()->backend();
}

QString FallbackSecretStore::entryLocation(const QString &key) const
{
    return current()->entryLocation(key);
}

template<typename Done, typename Call>
void FallbackSecretStore::attempt(QObject *context, Done done, Call call)
{
    if (m_switched || m_firstWorked) {
        call(current(), context, std::move(done));
        return;
    }
    // Not known yet: the first store, and on an error the second (for the rest of the session).
    std::weak_ptr<FallbackSecretStore> self = weak_from_this();
    Q_ASSERT(!self.expired());
    call(m_first, context, [self, context, done, call](auto... result) {
        const auto store = self.lock();
        const QString &error = std::get<sizeof...(result) - 1>(std::tie(result...));
        // (Also when another call switched meanwhile: this one goes on to the second too.)
        if (!store || error.isEmpty() || store->m_firstWorked || !store->m_first->isUnusable(error)) {
            if (store && error.isEmpty() && !store->m_switched)
                store->m_firstWorked = true;
            if (done)
                done(result...);
            return;
        }
        if (!store->m_switched) {
            qInfo() << "the keyring can't be used (" << error << "): using pass";
            store->m_switched = true;
        }
        call(store->m_second, context, done);
    });
}

void FallbackSecretStore::read(const QString &key, QObject *context, ReadDone done)
{
    attempt(context, std::move(done), [key](const std::shared_ptr<SecretStore> &s, QObject *ctx, ReadDone d) {
        s->read(key, ctx, std::move(d));
    });
}

void FallbackSecretStore::write(const QString &key, const QString &value, QObject *context, WriteDone done)
{
    attempt(context, std::move(done), [key, value](const std::shared_ptr<SecretStore> &s, QObject *ctx, WriteDone d) {
        s->write(key, value, ctx, std::move(d));
    });
}

void FallbackSecretStore::remove(const QString &key, QObject *context, WriteDone done)
{
    attempt(context, std::move(done), [key](const std::shared_ptr<SecretStore> &s, QObject *ctx, WriteDone d) {
        s->remove(key, ctx, std::move(d));
    });
}

std::shared_ptr<SecretStore> chooseSecretStore(SecretBackendPreference preference, const SecretStoreProbe &probe)
{
    switch (preference) {
    case SecretBackendPreference::Keyring:
        return probe.makeKeyring();
    case SecretBackendPreference::Pass:
        return probe.makePass();
    case SecretBackendPreference::Auto:
        break;
    }
    // QtKeychain can say a keyring is there when every call to it fails ("The name is not
    // activatable": a wallet that's switched off). With pass set up, the keyring is tried and pass
    // takes over if it fails.
    const bool pass = probe.passAvailable();
    if (probe.keyringAvailable())
        return pass ? FallbackSecretStore::create(probe.makeKeyring(), probe.makePass()) : probe.makeKeyring();
    return pass ? probe.makePass() : probe.makeKeyring();
}

std::shared_ptr<SecretStore> makeDefaultSecretStore(SecretBackendPreference preference)
{
    if (const QString env = qEnvironmentVariable("MMM_SECRET_BACKEND"); !env.trimmed().isEmpty())
        preference = parseSecretBackendPreference(env);
    SecretStoreProbe probe;
    probe.keyringAvailable = [] { return QKeychain::isAvailable(); };
    probe.passAvailable = [] { return PassSecretStore::available(PassSecretStore::Options{}); };
    probe.makeKeyring = [] { return makeKeychainSecretStore(); };
    probe.makePass = [] { return std::make_shared<PassSecretStore>(PassSecretStore::Options{}); };
    return chooseSecretStore(preference, probe);
}

void MemorySecretStore::read(const QString &key, QObject *context, ReadDone done)
{
    const QString error = failWith;
    const QString value = error.isEmpty() ? values.value(key) : QString();
    later(context, [done = std::move(done), value, error] { done(value, error); });
}

void MemorySecretStore::write(const QString &key, const QString &value, QObject *context, WriteDone done)
{
    const QString error = failWith;
    if (error.isEmpty())
        values.insert(key, value);
    if (done)
        later(context, [done = std::move(done), error] { done(error); });
}

void MemorySecretStore::remove(const QString &key, QObject *context, WriteDone done)
{
    const QString error = failWith;
    if (error.isEmpty())
        values.remove(key);
    if (done)
        later(context, [done = std::move(done), error] { done(error); });
}

} // namespace mmm
