// SPDX-License-Identifier: GPL-3.0-or-later
#include <QQmlContext>
#include <QQmlEngine>
#include <QTemporaryDir>
#include <QtQuickTest/quicktest.h>

#include "coverimageprovider.h"
#include "fixturelibrary.h"
#include "library.h"

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
    }

    void qmlEngineAvailable(QQmlEngine *engine)
    {
        engine->addImageProvider(QStringLiteral("cover"), new CoverImageProvider(m_dir.filePath(QStringLiteral("covers"))));
        engine->rootContext()->setContextProperty(QStringLiteral("screenshotDir"),
                                                  qEnvironmentVariable("MMM_SCREENSHOT_DIR"));
    }

    void cleanupTestCase() { m_library.reset(); }

private:
    QTemporaryDir m_dir;
    std::unique_ptr<Library> m_library;
};

QUICK_TEST_MAIN_WITH_SETUP(mmm_qml, Setup)

#include "tst_qml.moc"
