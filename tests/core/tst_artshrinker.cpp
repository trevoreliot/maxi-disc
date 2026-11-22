// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>

#include "artshrinker.h"
#include "testmedia.h"

using namespace mmm;

class TestArtShrinker : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void imageSize()
    {
        const QByteArray png = test::generateImage(64, 48);
        const QByteArray jpeg = test::generateImage(80, 40, QStringLiteral("jpegenc"));
        QVERIFY(!png.isEmpty());
        QVERIFY(!jpeg.isEmpty());
        QCOMPARE(ArtShrinker::imageSize(png), QSize(64, 48));
        QCOMPARE(ArtShrinker::imageSize(jpeg), QSize(80, 40));
        QVERIFY(!ArtShrinker::imageSize(QByteArray("not an image")));
        QVERIFY(!ArtShrinker::imageSize(jpeg.left(6)));
        QVERIFY(!ArtShrinker::isProgressiveJpeg(jpeg));

        // The same header with a progressive frame marker (SOF2).
        QByteArray progressive = jpeg;
        const qsizetype sof = progressive.indexOf(QByteArray("\xFF\xC0", 2));
        QVERIFY(sof > 0);
        progressive[sof + 1] = char(0xC2);
        QVERIFY(ArtShrinker::isProgressiveJpeg(progressive));
        QCOMPARE(ArtShrinker::imageSize(progressive), QSize(80, 40));
    }

    void shrinksLargeArt_data()
    {
        QTest::addColumn<int>("width");
        QTest::addColumn<int>("height");
        QTest::addColumn<QString>("encoder");
        QTest::addColumn<QSize>("expected");
        QTest::newRow("png landscape") << 1000 << 600 << QStringLiteral("pngenc") << QSize(500, 300);
        QTest::newRow("jpeg portrait") << 600 << 1200 << QStringLiteral("jpegenc") << QSize(250, 500);
        QTest::newRow("small png: re-encoded, not upscaled") << 300 << 200 << QStringLiteral("pngenc") << QSize(300, 200);
    }
    void shrinksLargeArt()
    {
        QFETCH(int, width);
        QFETCH(int, height);
        QFETCH(QString, encoder);
        QFETCH(QSize, expected);
        const QByteArray image = test::generateImage(width, height, encoder);
        QVERIFY(!image.isEmpty());
        QString error;
        const auto out = ArtShrinker::shrink(image, ArtShrinker::kMaxSide, &error);
        QVERIFY2(out, qPrintable(error));
        QVERIFY(out->startsWith(QByteArray("\xFF\xD8", 2)));
        QCOMPARE(ArtShrinker::imageSize(*out), expected);
        QVERIFY(!ArtShrinker::isProgressiveJpeg(*out));
    }

    void smallBaselineJpegIsKept()
    {
        const QByteArray jpeg = test::generateImage(320, 320, QStringLiteral("jpegenc"));
        const auto out = ArtShrinker::shrink(jpeg);
        QVERIFY(out);
        QCOMPARE(*out, jpeg);
    }

    void brokenImagesFail()
    {
        QString error;
        QVERIFY(!ArtShrinker::shrink(QByteArray("garbage garbage garbage"), 500, &error));
        QVERIFY(!error.isEmpty());
        // A real header with the image data cut off: an error (or a partial picture), never a hang.
        const QByteArray jpeg = test::generateImage(900, 900, QStringLiteral("jpegenc"));
        const auto out = ArtShrinker::shrink(jpeg.left(jpeg.size() / 8), 500, &error);
        if (out)
            QVERIFY(out->startsWith(QByteArray("\xFF\xD8", 2)));
    }
};

QTEST_GUILESS_MAIN(TestArtShrinker)
#include "tst_artshrinker.moc"
