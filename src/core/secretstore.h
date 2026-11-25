// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QHash>
#include <QObject>
#include <QString>

#include <functional>
#include <memory>

namespace mmm {

// Secrets the app keeps for the user (M14: the ListenBrainz user token), in the
// desktop's keyring (KWallet / Secret Service via QtKeychain), never in plain files. The keyring
// may ask the user to unlock it, so every call answers later, on the caller's thread; `done`
// isn't called if `context` is gone by then.
class SecretStore
{
public:
    virtual ~SecretStore() = default;
    // `value` "" and `error` "": nothing stored under `key`.
    using ReadDone = std::function<void(const QString &value, const QString &error)>;
    using WriteDone = std::function<void(const QString &error)>;
    virtual void read(const QString &key, QObject *context, ReadDone done) = 0;
    virtual void write(const QString &key, const QString &value, QObject *context, WriteDone done = {}) = 0;
    virtual void remove(const QString &key, QObject *context, WriteDone done = {}) = 0;
};

// The desktop keyring (service "music-md-manager").
std::shared_ptr<SecretStore> makeKeychainSecretStore();

// In memory (tests): what's stored is public to look at.
class MemorySecretStore final : public SecretStore
{
public:
    QHash<QString, QString> values;
    QString failWith; // every call fails with this ("": they work)

    void read(const QString &key, QObject *context, ReadDone done) override;
    void write(const QString &key, const QString &value, QObject *context, WriteDone done = {}) override;
    void remove(const QString &key, QObject *context, WriteDone done = {}) override;
};

} // namespace mmm
