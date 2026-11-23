// SPDX-License-Identifier: GPL-3.0-or-later
#include <QQmlContext>
#include <QQmlEngine>
#include <QTemporaryDir>
#include <QtQuickTest/quicktest.h>

#include "coverimageprovider.h"
#include "devices.h"
#include "fakenetmd.h"
#include "minidisc.h"
#include "ondevice.h"
#include "sync.h"
#include "fixturelibrary.h"
#include "library.h"
#include "playback.h"

// Gives the QML tests a real (fixture) Library and the cover image provider.
// QML can save screenshots of views to $MMM_SCREENSHOT_DIR (context property `screenshotDir`).
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
        // No device detection: tests set devices with Devices.setTestDevices().
        m_devices = std::make_unique<Devices>(Devices::Options{.databasePath = db, .monitor = false});
        m_sync = std::make_unique<Sync>(Sync::Options{.databasePath = db, .cacheDir = m_dir.filePath(QStringLiteral("transcode"))});
        m_onDevice = std::make_unique<OnDevice>(OnDevice::Options{.databasePath = db});
        // NetMD: an in-memory recorder; tests add it with MiniDisc.setTestDevices().
        m_miniDisc = std::make_unique<MiniDisc>(MiniDisc::Options{mmm::test::fakeNetMd(m_recorder), false});
    }

    void qmlEngineAvailable(QQmlEngine *engine)
    {
        engine->addImageProvider(QStringLiteral("cover"), new CoverImageProvider(m_dir.filePath(QStringLiteral("covers"))));
        engine->rootContext()->setContextProperty(QStringLiteral("screenshotDir"),
                                                  qEnvironmentVariable("MMM_SCREENSHOT_DIR"));
        engine->rootContext()->setContextProperty(QStringLiteral("fixtureDir"), m_dir.path());
    }

    void cleanupTestCase()
    {
        m_miniDisc.reset();
        m_onDevice.reset();
        m_sync.reset();
        m_devices.reset();
        m_playback.reset();
        m_library.reset();
    }

private:
    QTemporaryDir m_dir;
    std::unique_ptr<Library> m_library;
    std::unique_ptr<Playback> m_playback;
    std::unique_ptr<Devices> m_devices;
    std::unique_ptr<Sync> m_sync;
    std::unique_ptr<OnDevice> m_onDevice;
    std::shared_ptr<mmm::test::FakeRecorder> m_recorder = std::make_shared<mmm::test::FakeRecorder>();
    std::unique_ptr<MiniDisc> m_miniDisc;
};

QUICK_TEST_MAIN_WITH_SETUP(mmm_qml, Setup)

#include "tst_qml.moc"
