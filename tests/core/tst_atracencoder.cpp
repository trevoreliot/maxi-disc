// SPDX-License-Identifier: GPL-3.0-or-later
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

#include <thread>

#include "atracencoder.h"
#include "testmedia.h"

using namespace mmm;

// M12: LP2 / LP4 through the real, vendored atracdenc (built with the project, so always here).
class TestAtracEncoder : public QObject
{
    Q_OBJECT

    QTemporaryDir m_dir;
    std::atomic_bool m_noCancel{false};
    QString path(const QString &name) const { return m_dir.filePath(name); }

    MdEncodedAudio encode(const QString &source, MdRecordMode mode, QString *error = nullptr)
    {
        AtracEncoder encoder;
        MdEncodedAudio out;
        if (!encoder.encode(source, mode, path(QFileInfo(source).completeBaseName() + mdModeName(mode)), m_noCancel, &out, error))
            return {};
        return out;
    }

    void writeFile(const QString &name, const QByteArray &bytes)
    {
        QFile f(path(name));
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(bytes);
    }

    static QByteArray le32(quint32 v)
    {
        QByteArray b(4, '\0');
        for (int i = 0; i < 4; ++i)
            b[i] = char((v >> (8 * i)) & 0xff);
        return b;
    }
    static QByteArray le16(quint16 v) { return le32(v).left(2); }
    // A RIFF file like atracdenc's: fmt (tag, channels, rate, block align) + `dataBytes` of data.
    // `actualBytes` (default: dataBytes) follow the header, which says `dataBytes`.
    static QByteArray at3(quint16 tag, quint16 channels, quint16 align, qint64 dataBytes, qint64 actualBytes = -1)
    {
        QByteArray fmt = le16(tag) + le16(channels) + le32(44100) + le32(16537) + le16(align) + le16(0);
        QByteArray body = "WAVE" + QByteArray("fmt ") + le32(quint32(fmt.size())) + fmt + "data"
            + le32(quint32(dataBytes)) + QByteArray(actualBytes < 0 ? dataBytes : actualBytes, '\x5a');
        return "RIFF" + le32(quint32(body.size())) + body;
    }

private Q_SLOTS:
    void initTestCase()
    {
        QVERIFY(m_dir.isValid());
        QString why;
        QVERIFY2(AtracEncoder::isAvailable(&why), qPrintable(why));
    }

    // LP2: 384-byte frames; LP4: 192-byte ones, half the size; ~1024 samples per frame.
    void lp2AndLp4()
    {
        const QString flac = path(QStringLiteral("tone.flac"));
        QVERIFY(test::generateTone(flac, QStringLiteral("flacenc"), 2000).isEmpty());
        QString error;
        const MdEncodedAudio lp2 = encode(flac, MdRecordMode::Lp2, &error);
        QVERIFY2(!lp2.path.isEmpty(), qPrintable(error));
        QCOMPARE(lp2.mode, MdRecordMode::Lp2);
        QCOMPARE(lp2.bytes % 384, 0);
        // 88200 samples = 86 whole frames in atracdenc's header; the frames after them (the
        // encoder's delay: the end of the music) are kept too.
        QVERIFY2(lp2.bytes / 384 > 88200 / 1024, qPrintable(QString::number(lp2.bytes / 384)));
        QCOMPARE(QFileInfo(lp2.path).size(), lp2.bytes);
        QVERIFY2(std::abs(lp2.durationMs - 2000) <= 100, qPrintable(QString::number(lp2.durationMs)));
        const MdEncodedAudio lp4 = encode(flac, MdRecordMode::Lp4, &error);
        QVERIFY2(!lp4.path.isEmpty(), qPrintable(error));
        QCOMPARE(lp4.bytes % 192, 0);
        QCOMPARE(lp4.bytes * 2, lp2.bytes); // same frames, half the size
        QCOMPARE(lp4.durationMs, lp2.durationMs);
        // Only the frames: no RIFF header left in front.
        QFile f(lp2.path);
        QVERIFY(f.open(QIODevice::ReadOnly));
        QVERIFY(!f.read(4).startsWith("RIFF"));
        // Nothing else left behind in the work folder.
        const QStringList left = QDir(m_dir.path()).entryList(QDir::Dirs | QDir::NoDotAndDotDot);
        QVERIFY2(left.isEmpty(), qPrintable(left.join(QLatin1Char(','))));
    }

    void otherRatesAndMono()
    {
        const QString wav = path(QStringLiteral("mono48.wav"));
        QVERIFY(test::generateTone(wav, QStringLiteral("audio/x-raw,rate=48000,channels=1 ! wavenc"), 1000).isEmpty());
        QString error;
        const MdEncodedAudio lp2 = encode(wav, MdRecordMode::Lp2, &error);
        QVERIFY2(!lp2.path.isEmpty(), qPrintable(error));
        QVERIFY(std::abs(lp2.durationMs - 1000) <= 100); // + the encoder's delay frames
    }

    void failures()
    {
        const QString flac = path(QStringLiteral("tone.flac"));
        const QString dest = path(QStringLiteral("fail.atrac"));
        QString error;
        // SP isn't ATRAC3.
        QVERIFY(!AtracEncoder().encode(flac, MdRecordMode::Sp, dest, m_noCancel, nullptr, &error));
        QVERIFY(!QFile::exists(dest));
        // No encoder.
        const QString missing = path(QStringLiteral("no-such-atracdenc"));
        QVERIFY(!AtracEncoder::isAvailable(&error, missing));
        QVERIFY(error.contains(QStringLiteral("atracdenc")));
        QVERIFY(!AtracEncoder(missing).encode(flac, MdRecordMode::Lp2, dest, m_noCancel, nullptr, &error));
        QVERIFY(!QFile::exists(dest));
        // Not audio.
        writeFile(QStringLiteral("junk.flac"), "not audio at all");
        QVERIFY(!AtracEncoder().encode(path(QStringLiteral("junk.flac")), MdRecordMode::Lp2, dest, m_noCancel, nullptr, &error));
        QVERIFY(!QFile::exists(dest));
        // Canceled before it starts, and while atracdenc runs.
        std::atomic_bool cancel{true};
        QVERIFY(!AtracEncoder().encode(flac, MdRecordMode::Lp4, dest, cancel, nullptr, &error));
        QCOMPARE(error, kEncodeCanceled);
        QVERIFY(!QFile::exists(dest));
        const QString longTone = path(QStringLiteral("long.flac"));
        QVERIFY(test::generateTone(longTone, QStringLiteral("flacenc"), 60000).isEmpty());
        cancel = false;
        std::thread stopper([&] {
            std::this_thread::sleep_for(std::chrono::milliseconds(300));
            cancel = true;
        });
        const bool ok = AtracEncoder().encode(longTone, MdRecordMode::Lp2, dest, cancel, nullptr, &error);
        stopper.join();
        QVERIFY(!ok);
        QCOMPARE(error, kEncodeCanceled);
        QVERIFY(!QFile::exists(dest));
    }

    // What's accepted as atracdenc's output.
    void readAt3()
    {
        qint64 offset = 0;
        qint64 bytes = 0;
        QString error;
        writeFile(QStringLiteral("good.at3"), at3(0x270, 2, 384, 384 * 3));
        QVERIFY2(AtracEncoder::readAt3(path(QStringLiteral("good.at3")), 384, &offset, &bytes, &error), qPrintable(error));
        QCOMPARE(bytes, qint64(384 * 3));
        QCOMPARE(offset, qint64(12 + 8 + 16 + 8));
        // LP4 frames where LP2 ones are expected, PCM, mono, a partial frame, not RIFF.
        QVERIFY(!AtracEncoder::readAt3(path(QStringLiteral("good.at3")), 192, &offset, &bytes, &error));
        writeFile(QStringLiteral("pcm.wav"), at3(1, 2, 4, 400));
        QVERIFY(!AtracEncoder::readAt3(path(QStringLiteral("pcm.wav")), 384, &offset, &bytes, &error));
        writeFile(QStringLiteral("mono.at3"), at3(0x270, 1, 384, 384));
        QVERIFY(!AtracEncoder::readAt3(path(QStringLiteral("mono.at3")), 384, &offset, &bytes, &error));
        writeFile(QStringLiteral("partial.at3"), at3(0x270, 2, 384, 500));
        QVERIFY(!AtracEncoder::readAt3(path(QStringLiteral("partial.at3")), 384, &offset, &bytes, &error));
        QVERIFY(error.contains(QStringLiteral("whole number")));
        // atracdenc counts whole input frames in the header, then writes one or two more: kept.
        writeFile(QStringLiteral("more.at3"), at3(0x270, 2, 384, 384 * 3, 384 * 5));
        QVERIFY2(AtracEncoder::readAt3(path(QStringLiteral("more.at3")), 384, &offset, &bytes, &error), qPrintable(error));
        QCOMPARE(bytes, qint64(384 * 5));
        // Shorter than the header says: cut short.
        writeFile(QStringLiteral("short.at3"), at3(0x270, 2, 384, 384 * 5, 384 * 3));
        QVERIFY(!AtracEncoder::readAt3(path(QStringLiteral("short.at3")), 384, &offset, &bytes, &error));
        QVERIFY(error.contains(QStringLiteral("cut short")));
        writeFile(QStringLiteral("text.at3"), "hello");
        QVERIFY(!AtracEncoder::readAt3(path(QStringLiteral("text.at3")), 384, &offset, &bytes, &error));
    }

    void defaultEncoderPicksByMode()
    {
        const QString flac = path(QStringLiteral("tone.flac"));
        const auto encoder = makeDefaultMdEncoder();
        MdEncodedAudio sp;
        MdEncodedAudio lp4;
        QString error;
        QVERIFY2(encoder->encode(flac, MdRecordMode::Sp, path(QStringLiteral("d.pcm")), m_noCancel, &sp, &error), qPrintable(error));
        QVERIFY2(encoder->encode(flac, MdRecordMode::Lp4, path(QStringLiteral("d.atrac")), m_noCancel, &lp4, &error), qPrintable(error));
        QCOMPARE(sp.bytes % 4, 0);
        QCOMPARE(lp4.bytes % 192, 0);
        QVERIFY(sp.bytes > lp4.bytes * 10); // PCM is ~11× the size of LP4
    }
};

QTEST_GUILESS_MAIN(TestAtracEncoder)
#include "tst_atracencoder.moc"
