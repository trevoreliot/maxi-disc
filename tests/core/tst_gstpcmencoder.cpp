// SPDX-License-Identifier: GPL-3.0-or-later
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

#include "gstpcmencoder.h"
#include "testmedia.h"

using namespace mmm;

namespace {

QByteArray readAll(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

// The samples of a PCM WAV file (its "data" chunk).
QByteArray wavData(const QString &path)
{
    const QByteArray wav = readAll(path);
    qsizetype i = 12;
    while (i + 8 <= wav.size()) {
        const QByteArray id = wav.mid(i, 4);
        const quint32 size = quint32(quint8(wav[i + 4])) | quint32(quint8(wav[i + 5])) << 8
            | quint32(quint8(wav[i + 6])) << 16 | quint32(quint8(wav[i + 7])) << 24;
        if (id == "data")
            return wav.mid(i + 8, size);
        i += 8 + size + (size & 1);
    }
    return {};
}

QByteArray swapped(QByteArray samples)
{
    for (qsizetype i = 0; i + 1 < samples.size(); i += 2)
        std::swap(samples[i], samples[i + 1]);
    return samples;
}

} // namespace

class TestGstPcmEncoder : public QObject
{
    Q_OBJECT

    QTemporaryDir m_dir;
    QString path(const QString &name) const { return m_dir.filePath(name); }
    std::atomic_bool m_noCancel{false};

    MdEncodedAudio encode(const QString &source, QString *error = nullptr)
    {
        GstPcmEncoder encoder;
        MdEncodedAudio out;
        const QString dest = path(QFileInfo(source).completeBaseName() + QStringLiteral(".pcm"));
        if (!encoder.encode(source, MdRecordMode::Sp, dest, m_noCancel, &out, error))
            return {};
        return out;
    }

private Q_SLOTS:
    void initTestCase() { QVERIFY(m_dir.isValid()); }

    // 44.1 kHz 16-bit stereo already: the samples come out unchanged, big-endian.
    void bigEndianAndExact()
    {
        const QString wav = path(QStringLiteral("stereo.wav"));
        QVERIFY(test::generateTone(wav, QStringLiteral("wavenc"), 500).isEmpty());
        const MdEncodedAudio out = encode(wav);
        QVERIFY(!out.path.isEmpty());
        const QByteArray samples = wavData(wav);
        QVERIFY(samples.size() > 0);
        QCOMPARE(out.bytes, samples.size());
        QCOMPARE(readAll(out.path), swapped(samples));
        QCOMPARE(out.durationMs, samples.size() * 1000 / GstPcmEncoder::kBytesPerSecond);
        QCOMPARE(out.bytes % 4, 0);
    }

    // FLAC is lossless: the same samples as the WAV of the same tone.
    void flacDecodesToTheSameSamples()
    {
        const QString flac = path(QStringLiteral("tone.flac"));
        const QString wav = path(QStringLiteral("tone.wav"));
        QVERIFY(test::generateTone(flac, QStringLiteral("flacenc"), 500).isEmpty());
        QVERIFY(test::generateTone(wav, QStringLiteral("wavenc"), 500).isEmpty());
        QCOMPARE(readAll(encode(flac).path), swapped(wavData(wav)));
    }

    void monoBecomesStereo()
    {
        const QString wav = path(QStringLiteral("mono.wav"));
        QVERIFY(test::generateTone(wav, QStringLiteral("audio/x-raw,channels=1 ! wavenc"), 400).isEmpty());
        const QByteArray mono = wavData(wav);
        const MdEncodedAudio out = encode(wav);
        QCOMPARE(out.bytes, mono.size() * 2);
        const QByteArray pcm = readAll(out.path);
        // Left = right = the mono sample, big-endian, give or take audioconvert's dither (it goes
        // through a wider format when mixing channels; kept: it's right for 24-bit sources).
        auto be16 = [](const QByteArray &b, qsizetype at) { return qint16((quint8(b[at]) << 8) | quint8(b[at + 1])); };
        auto le16 = [](const QByteArray &b, qsizetype at) { return qint16((quint8(b[at + 1]) << 8) | quint8(b[at])); };
        for (qsizetype frame = 0; frame < 200; ++frame) {
            QVERIFY(std::abs(be16(pcm, frame * 4) - be16(pcm, frame * 4 + 2)) <= 2);
            QVERIFY(std::abs(be16(pcm, frame * 4) - le16(mono, frame * 2)) <= 2);
        }
    }

    void otherRatesAreResampled()
    {
        const QString wav = path(QStringLiteral("48k.wav"));
        QVERIFY(test::generateTone(wav, QStringLiteral("audio/x-raw,rate=48000 ! wavenc"), 1000).isEmpty());
        const MdEncodedAudio out = encode(wav);
        QVERIFY(std::abs(out.bytes - GstPcmEncoder::kBytesPerSecond) < GstPcmEncoder::kBytesPerSecond / 100);
        QVERIFY(std::abs(out.durationMs - 1000) <= 10);
    }

    void failuresLeaveNoFile()
    {
        GstPcmEncoder encoder;
        QString error;
        const QString dest = path(QStringLiteral("fail.pcm"));
        QVERIFY(!encoder.encode(path(QStringLiteral("missing.flac")), MdRecordMode::Sp, dest, m_noCancel, nullptr, &error));
        QVERIFY(error.contains(QStringLiteral("not found")));
        QFile junk(path(QStringLiteral("junk.flac")));
        QVERIFY(junk.open(QIODevice::WriteOnly));
        junk.write("not audio at all");
        junk.close();
        QVERIFY(!encoder.encode(junk.fileName(), MdRecordMode::Sp, dest, m_noCancel, nullptr, &error));
        QVERIFY(!QFile::exists(dest));
        // LP2 / LP4 come with M12.
        const QString wav = path(QStringLiteral("stereo.wav"));
        QVERIFY(!encoder.encode(wav, MdRecordMode::Lp2, dest, m_noCancel, nullptr, &error));
        // Canceled.
        std::atomic_bool cancel{true};
        QVERIFY(!encoder.encode(wav, MdRecordMode::Sp, dest, cancel, nullptr, &error));
        QVERIFY(!QFile::exists(dest));
    }
};

QTEST_GUILESS_MAIN(TestGstPcmEncoder)
#include "tst_gstpcmencoder.moc"
