// SPDX-License-Identifier: GPL-3.0-or-later
#include <QQmlContext>
#include <QQmlEngine>
#include <QTemporaryDir>
#include <QtQuickTest/quicktest.h>

#include "coverimageprovider.h"
#include "devices.h"
#include "fakenetmd.h"
#include "minidisc.h"
#include "sendtodevice.h"
#include "ondevice.h"
#include "sync.h"
#include "fixturelibrary.h"
#include "library.h"
#include "listenbrainz.h"
#include "playback.h"
#include "secretstore.h"
#include "testhttpserver.h"

// Gives the QML tests a real (fixture) Library and the cover image provider.
// QML can save screenshots of views to $MMM_SCREENSHOT_DIR (context property `screenshotDir`).
// `fakeRecorder` in the tests: puts the shared in-memory recorder back (after wiping it, say).
class FakeRecorderControl : public QObject
{
    Q_OBJECT

public:
    explicit FakeRecorderControl(std::shared_ptr<mmm::test::FakeRecorder> recorder) : m_recorder(std::move(recorder)) {}
    Q_INVOKABLE void reset() { m_recorder->reset(); }
    Q_INVOKABLE void setHeader(const QString &raw)
    {
        std::lock_guard lock(m_recorder->mutex);
        m_recorder->header = raw;
    }

private:
    std::shared_ptr<mmm::test::FakeRecorder> m_recorder;
};

// The web pages the app asked the browser to open (none really opens): `openedUrls.all()`.
class OpenedUrls : public QObject
{
    Q_OBJECT

public:
    Q_INVOKABLE QStringList all() const { return urls; }
    QStringList urls;
};

// A stand-in ListenBrainz for the dialog tests (M14): the token "GOOD" is alice's, any other is
// unknown; listens are always taken.
std::unique_ptr<mmm::test::TestHttpServer> makeFakeListenBrainz()
{
    using Server = mmm::test::TestHttpServer;
    auto server = std::make_unique<Server>();
    if (!server->listen())
        qFatal("could not start the fake ListenBrainz");
    server->route(QStringLiteral("/1/validate-token"), [](const Server::Request &r) {
        if (r.headers.value("authorization") == "Token GOOD")
            return Server::Response{200, "{\"code\":200,\"message\":\"Token valid.\",\"valid\":true,\"user_name\":\"alice\"}", {}, 0, -1};
        return Server::Response{200, "{\"code\":200,\"message\":\"Token invalid.\",\"valid\":false}", {}, 0, -1};
    });
    server->route(QStringLiteral("/1/submit-listens"), 200, "{\"status\":\"ok\"}");
    return server;
}

class Setup : public QObject
{
    Q_OBJECT

public Q_SLOTS:
    void applicationAvailable()
    {
        const QString db = mmm::test::buildFixtureLibrary(m_dir.path());
        if (db.isEmpty())
            qFatal("could not build the fixture library");
        m_library = std::make_unique<Library>(Library::Options{.databasePath = db, .scanOnStart = false, .watch = false});
        m_playback = std::make_unique<Playback>(Playback::Options{
            .player = {.audioSink = "fakesink sync=true"}, .mpris = false, .persistSettings = false});
        // ListenBrainz: a stand-in server, an in-memory keyring, no browser (the pages it would
        // open are counted in `openedUrls`).
        m_listenBrainzServer = makeFakeListenBrainz();
        ListenBrainz::Options lb;
        lb.databasePath = m_dir.filePath(QStringLiteral("scrobbles.db"));
        lb.persistSettings = false;
        lb.secrets = std::make_shared<mmm::MemorySecretStore>();
        lb.baseUrl = m_listenBrainzServer->url(QStringLiteral("/"));
        lb.openUrl = [this](const QUrl &url) { m_openedUrls.urls << url.toString(); };
        lb.watchNetwork = false;
        m_listenBrainz = std::make_unique<ListenBrainz>(lb);
        // No device detection: tests set devices with Devices.setTestDevices().
        m_devices = std::make_unique<Devices>(Devices::Options{.databasePath = db, .monitor = false});
        m_sync = std::make_unique<Sync>(Sync::Options{.databasePath = db, .cacheDir = m_dir.filePath(QStringLiteral("transcode"))});
        m_onDevice = std::make_unique<OnDevice>(OnDevice::Options{.databasePath = db});
        // NetMD: an in-memory recorder; tests add it with MiniDisc.setTestDevices().
        m_miniDisc = std::make_unique<MiniDisc>(MiniDisc::Options{mmm::test::fakeNetMd(m_recorder), false, mmm::test::fakeEncoder(),
                                                                  m_dir.filePath(QStringLiteral("md-record")), false});
        m_send = std::make_unique<SendToDevice>(nullptr);
    }

    void qmlEngineAvailable(QQmlEngine *engine)
    {
        engine->addImageProvider(QStringLiteral("cover"), new CoverImageProvider(m_dir.filePath(QStringLiteral("covers"))));
        engine->rootContext()->setContextProperty(QStringLiteral("screenshotDir"),
                                                  qEnvironmentVariable("MMM_SCREENSHOT_DIR"));
        engine->rootContext()->setContextProperty(QStringLiteral("fixtureDir"), m_dir.path());
        engine->rootContext()->setContextProperty(QStringLiteral("fakeRecorder"), &m_recorderControl);
        engine->rootContext()->setContextProperty(QStringLiteral("openedUrls"), &m_openedUrls);
    }

    void cleanupTestCase()
    {
        m_send.reset();
        m_miniDisc.reset();
        m_onDevice.reset();
        m_sync.reset();
        m_devices.reset();
        m_listenBrainz.reset();
        m_listenBrainzServer.reset();
        m_playback.reset();
        m_library.reset();
    }

private:
    QTemporaryDir m_dir;
    std::unique_ptr<Library> m_library;
    std::unique_ptr<Playback> m_playback;
    std::unique_ptr<mmm::test::TestHttpServer> m_listenBrainzServer;
    std::unique_ptr<ListenBrainz> m_listenBrainz;
    OpenedUrls m_openedUrls;
    std::unique_ptr<Devices> m_devices;
    std::unique_ptr<Sync> m_sync;
    std::unique_ptr<OnDevice> m_onDevice;
    std::shared_ptr<mmm::test::FakeRecorder> m_recorder = std::make_shared<mmm::test::FakeRecorder>();
    FakeRecorderControl m_recorderControl{m_recorder};
    std::unique_ptr<MiniDisc> m_miniDisc;
    std::unique_ptr<SendToDevice> m_send;
};

QUICK_TEST_MAIN_WITH_SETUP(mmm_qml, Setup)

#include "tst_qml.moc"
