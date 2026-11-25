// SPDX-License-Identifier: GPL-3.0-or-later
#include "secretstore.h"

#include <QPointer>
#include <QTimer>

#include <qt6keychain/keychain.h>

namespace mmm {

namespace {

const QString kService = QStringLiteral("music-md-manager");

class KeychainSecretStore final : public SecretStore
{
public:
    void read(const QString &key, QObject *context, ReadDone done) override
    {
        auto *job = new QKeychain::ReadPasswordJob(kService);
        job->setAutoDelete(true);
        job->setKey(key);
        QPointer<QObject> guard(context);
        QObject::connect(job, &QKeychain::Job::finished, job, [job, guard, done = std::move(done)] {
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
    static void finish(QKeychain::Job *job, QObject *context, WriteDone done, bool missingIsFine = false)
    {
        QPointer<QObject> guard(context);
        QObject::connect(job, &QKeychain::Job::finished, job, [job, guard, done = std::move(done), missingIsFine] {
            if (!guard || !done)
                return;
            const bool ok = job->error() == QKeychain::NoError || (missingIsFine && job->error() == QKeychain::EntryNotFound);
            done(ok ? QString() : job->errorString());
        });
        job->start();
    }
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
