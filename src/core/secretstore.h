// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QHash>
#include <QObject>
#include <QString>

#include <functional>
#include <memory>

namespace mmm {

enum class SecretBackend { Keyring, Pass, Memory };
enum class SecretBackendPreference { Auto, Keyring, Pass };

// Secrets the app keeps for the user (M14: the ListenBrainz user token): in the desktop's
// keyring (KWallet / Secret Service via QtKeychain) or, without one, in pass (M14b,
// PassSecretStore). Never in plain files. Either may ask the user to unlock it, so every call
// answers later, on the caller's thread; `done` isn't called if `context` is gone by then.
class SecretStore
{
public:
    virtual ~SecretStore() = default;
    virtual SecretBackend backend() const = 0;
    // Whether `error` (one of this store's) means it can't be used at all, as opposed to the
    // user saying no (a cancelled unlock prompt).
    virtual bool isUnusable(const QString &error) const
    {
        Q_UNUSED(error)
        return true;
    }
    // Where `key` is kept, for the user ("" if there's nothing useful to say).
    virtual QString entryLocation(const QString &key) const
    {
        Q_UNUSED(key)
        return {};
    }
    // `value` "" and `error` "": nothing stored under `key`.
    using ReadDone = std::function<void(const QString &value, const QString &error)>;
    using WriteDone = std::function<void(const QString &error)>;
    virtual void read(const QString &key, QObject *context, ReadDone done) = 0;
    virtual void write(const QString &key, const QString &value, QObject *context, WriteDone done = {}) = 0;
    virtual void remove(const QString &key, QObject *context, WriteDone done = {}) = 0;
};

// The desktop keyring (service "music-md-manager").
std::shared_ptr<SecretStore> makeKeychainSecretStore();

// The first store (the keyring) until it turns out unusable (isUnusable), then the second (pass)
// for the rest of the session: the call that failed is made again there. Once the first has
// worked it stays, and the user cancelling its unlock prompt is just an error, never a switch.
class FallbackSecretStore final : public SecretStore, public std::enable_shared_from_this<FallbackSecretStore>
{
public:
    static std::shared_ptr<FallbackSecretStore> create(std::shared_ptr<SecretStore> first, std::shared_ptr<SecretStore> second);
    SecretBackend backend() const override;
    QString entryLocation(const QString &key) const override;
    void read(const QString &key, QObject *context, ReadDone done) override;
    void write(const QString &key, const QString &value, QObject *context, WriteDone done = {}) override;
    void remove(const QString &key, QObject *context, WriteDone done = {}) override;
    bool switched() const { return m_switched; }

private:
    FallbackSecretStore(std::shared_ptr<SecretStore> first, std::shared_ptr<SecretStore> second); // create()
    const std::shared_ptr<SecretStore> &current() const { return m_switched ? m_second : m_first; }
    template<typename Done, typename Call>
    void attempt(QObject *context, Done done, Call call);

    std::shared_ptr<SecretStore> m_first;
    std::shared_ptr<SecretStore> m_second;
    bool m_switched = false;
    bool m_firstWorked = false;
};

// "auto" / "keyring" / "pass" (case and spaces ignored; anything else: Auto).
SecretBackendPreference parseSecretBackendPreference(const QString &value);

// What chooseSecretStore() asks (injected in tests).
struct SecretStoreProbe
{
    std::function<bool()> keyringAvailable;
    std::function<bool()> passAvailable;
    std::function<std::shared_ptr<SecretStore>()> makeKeyring;
    std::function<std::shared_ptr<SecretStore>()> makePass;
};
// Keyring / Pass: that one (the other isn't even probed). Auto: the keyring if QtKeychain says
// there is one (falling back to pass, if it's set up, when it fails: FallbackSecretStore), else
// pass if it's set up, else the keyring (whose errors then say what's wrong).
std::shared_ptr<SecretStore> chooseSecretStore(SecretBackendPreference preference, const SecretStoreProbe &probe);
// chooseSecretStore() with the real probes. MMM_SECRET_BACKEND, if set, wins over `preference`
// (ctest: "pass" with an empty store dir, so the real keyring isn't even probed).
std::shared_ptr<SecretStore> makeDefaultSecretStore(SecretBackendPreference preference = SecretBackendPreference::Auto);

// In memory (tests): what's stored is public to look at.
class MemorySecretStore final : public SecretStore
{
public:
    QHash<QString, QString> values;
    QString failWith; // every call fails with this ("": they work)
    bool failureIsUnusable = true; // false: failWith is the user saying no
    SecretBackend pretendBackend = SecretBackend::Memory; // what backend() says (UI tests)

    SecretBackend backend() const override { return pretendBackend; }
    bool isUnusable(const QString &) const override { return failureIsUnusable; }
    QString entryLocation(const QString &key) const override
    {
        return pretendBackend == SecretBackend::Pass ? QStringLiteral("music-md-manager/") + key : QString();
    }

    void read(const QString &key, QObject *context, ReadDone done) override;
    void write(const QString &key, const QString &value, QObject *context, WriteDone done = {}) override;
    void remove(const QString &key, QObject *context, WriteDone done = {}) override;
};

} // namespace mmm
