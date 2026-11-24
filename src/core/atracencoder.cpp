// SPDX-License-Identifier: GPL-3.0-or-later
#include "atracencoder.h"

#include "gstpcmencoder.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLoggingCategory>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>

namespace mmm {

Q_LOGGING_CATEGORY(lcAtrac, "mmm.atrac")

namespace {

constexpr int kAtracSamplesPerFrame = 1024;
constexpr quint16 kAtrac3FormatTag = 0x270;

quint32 le32(const QByteArray &b, qsizetype at)
{
    return quint32(quint8(b[at])) | quint32(quint8(b[at + 1])) << 8 | quint32(quint8(b[at + 2])) << 16
        | quint32(quint8(b[at + 3])) << 24;
}
quint16 le16(const QByteArray &b, qsizetype at)
{
    return quint16(quint8(b[at]) | quint8(b[at + 1]) << 8);
}

bool executable(const QString &path)
{
    const QFileInfo fi(path);
    return !path.isEmpty() && fi.isFile() && fi.isExecutable();
}

// Copies `bytes` from `from` at `offset` into a new file `to`.
bool copyRange(const QString &from, qint64 offset, qint64 bytes, const QString &to)
{
    QFile in(from);
    QFile out(to);
    if (!in.open(QIODevice::ReadOnly) || !in.seek(offset) || !out.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    QByteArray buf;
    while (bytes > 0) {
        buf = in.read(std::min<qint64>(bytes, 1 << 20));
        if (buf.isEmpty() || out.write(buf) != buf.size())
            return false;
        bytes -= buf.size();
    }
    return out.flush();
}

class DefaultMdEncoder final : public MdAudioEncoder
{
public:
    explicit DefaultMdEncoder(QString atracdenc)
        : m_atracdenc(std::move(atracdenc))
    {
    }
    bool encode(const QString &source, MdRecordMode mode, const QString &destination, const std::atomic_bool &cancel,
                MdEncodedAudio *out, QString *error) override
    {
        if (mode == MdRecordMode::Sp)
            return GstPcmEncoder().encode(source, mode, destination, cancel, out, error);
        return AtracEncoder(m_atracdenc).encode(source, mode, destination, cancel, out, error);
    }

private:
    QString m_atracdenc;
};

} // namespace

std::unique_ptr<MdAudioEncoder> makeDefaultMdEncoder(const QString &atracdenc)
{
    return std::make_unique<DefaultMdEncoder>(atracdenc.isEmpty() ? AtracEncoder::findBinary() : atracdenc);
}

AtracEncoder::AtracEncoder(QString binary)
    : m_binary(binary.isEmpty() ? findBinary() : std::move(binary))
{
}

QString AtracEncoder::findBinary()
{
    if (const QString env = qEnvironmentVariable("MMM_ATRACDENC"); !env.isEmpty())
        return executable(env) ? env : QString(); // set but wrong: don't silently use another
    QStringList candidates;
    if (QCoreApplication::instance())
        candidates << QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("atracdenc"));
#ifdef MMM_ATRACDENC_BUILD_PATH
    candidates << QStringLiteral(MMM_ATRACDENC_BUILD_PATH);
#endif
    for (const QString &c : std::as_const(candidates)) {
        if (executable(c))
            return c;
    }
    return QStandardPaths::findExecutable(QStringLiteral("atracdenc"));
}

bool AtracEncoder::isAvailable(QString *reason, const QString &binary)
{
    const QString path = binary.isEmpty() ? findBinary() : binary;
    if (executable(path))
        return true;
    if (reason)
        *reason = QStringLiteral("LP2 / LP4 need atracdenc, which wasn't found (it's built with the app; "
                                 "MMM_ATRACDENC can point to it).");
    return false;
}

bool AtracEncoder::readAt3(const QString &path, int frameBytes, qint64 *dataOffset, qint64 *dataBytes, QString *error)
{
    auto fail = [&](const QString &message) {
        if (error)
            *error = message;
        return false;
    };
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return fail(QStringLiteral("can't read the encoded file"));
    const QByteArray head = f.read(12);
    if (head.size() < 12 || !head.startsWith("RIFF") || head.mid(8, 4) != "WAVE")
        return fail(QStringLiteral("the encoder didn't write a RIFF file"));
    bool haveFormat = false;
    qint64 pos = 12;
    while (pos + 8 <= f.size()) {
        if (!f.seek(pos))
            break;
        const QByteArray chunk = f.read(8);
        if (chunk.size() < 8)
            break;
        const qint64 size = le32(chunk, 4);
        if (chunk.startsWith("fmt ")) {
            const QByteArray fmt = f.read(std::min<qint64>(size, 64));
            if (fmt.size() < 14)
                return fail(QStringLiteral("a broken format chunk"));
            const quint16 tag = le16(fmt, 0);
            const quint16 channels = le16(fmt, 2);
            const quint32 rate = le32(fmt, 4);
            const quint16 align = le16(fmt, 12);
            if (tag != kAtrac3FormatTag || channels != 2 || rate != 44100 || align != frameBytes)
                return fail(QStringLiteral("unexpected ATRAC3 format (tag %1, %2 channels, %3 Hz, %4-byte frames; "
                                           "%5-byte frames needed)")
                                .arg(tag, 0, 16)
                                .arg(channels)
                                .arg(rate)
                                .arg(align)
                                .arg(frameBytes));
            haveFormat = true;
        } else if (chunk.startsWith("data")) {
            if (!haveFormat)
                return fail(QStringLiteral("no format before the audio"));
            // atracdenc 0.2.3 writes the header first, counting whole input frames, then encodes
            // a frame or two more (the encoder's delay: the end of the music). The data runs to
            // the end of the file; a file shorter than its header says is truncated.
            const qint64 present = f.size() - (pos + 8);
            if (size > present)
                return fail(QStringLiteral("the encoded file is cut short (%1 of %2 bytes)").arg(present).arg(size));
            if (present <= 0 || present % frameBytes != 0)
                return fail(QStringLiteral("%1 bytes of ATRAC3 isn't a whole number of %2-byte frames")
                                .arg(present)
                                .arg(frameBytes));
            *dataOffset = pos + 8;
            *dataBytes = present;
            return true;
        }
        pos += 8 + size + (size & 1);
    }
    return fail(QStringLiteral("no audio in the encoded file"));
}

bool AtracEncoder::encode(const QString &source, MdRecordMode mode, const QString &destination,
                          const std::atomic_bool &cancel, MdEncodedAudio *out, QString *error)
{
    auto fail = [&](const QString &message) {
        if (error)
            *error = message;
        QFile::remove(destination);
        return false;
    };
    if (mode == MdRecordMode::Sp)
        return fail(QStringLiteral("SP isn't ATRAC3: use GstPcmEncoder"));
    QString reason;
    if (!isAvailable(&reason, m_binary))
        return fail(reason);
    const int frameBytes = mdWireSpec(mode).frameBytes;

    // Next to the destination (the recording's temp dir): removed with everything in it.
    QTemporaryDir work(QFileInfo(destination).absolutePath() + QStringLiteral("/atrac-XXXXXX"));
    if (!work.isValid())
        return fail(QStringLiteral("can't make a temporary folder: %1").arg(work.errorString()));
    const QString wav = work.filePath(QStringLiteral("in.wav"));
    const QString at3 = work.filePath(QStringLiteral("out.at3"));
    if (!GstPcmEncoder::decode(source, wav, GstPcmEncoder::Output::Wav, cancel, error)) {
        QFile::remove(destination);
        return false;
    }

    if (cancel.load())
        return fail(kEncodeCanceled);
    QProcess p;
    p.setProgram(m_binary);
    p.setArguments({QStringLiteral("-e"), mode == MdRecordMode::Lp4 ? QStringLiteral("atrac3_lp4") : QStringLiteral("atrac3"),
                    QStringLiteral("--nostdout"), QStringLiteral("-i"), wav, QStringLiteral("-o"), at3});
    p.setProcessChannelMode(QProcess::MergedChannels);
    p.start();
    if (!p.waitForStarted(5000))
        return fail(QStringLiteral("couldn't start atracdenc: %1").arg(p.errorString()));
    while (!p.waitForFinished(100)) {
        if (cancel.load()) {
            p.kill();
            p.waitForFinished(2000);
            return fail(kEncodeCanceled);
        }
        if (p.state() == QProcess::NotRunning)
            break;
    }
    QFile::remove(wav); // the biggest file: gone before the copy
    if (cancel.load())
        return fail(kEncodeCanceled); // canceled as it finished: still canceled
    const QByteArray log = p.readAll().trimmed().right(400);
    if (p.exitStatus() != QProcess::NormalExit || p.exitCode() != 0) {
        qCWarning(lcAtrac) << "atracdenc failed:" << p.exitCode() << log;
        return fail(QStringLiteral("atracdenc failed (%1): %2").arg(p.exitCode()).arg(QString::fromLocal8Bit(log)));
    }

    qint64 offset = 0;
    qint64 bytes = 0;
    QString why;
    if (!readAt3(at3, frameBytes, &offset, &bytes, &why))
        return fail(why);
    if (!copyRange(at3, offset, bytes, destination))
        return fail(QStringLiteral("can't write %1").arg(destination));
    if (out) {
        out->mode = mode;
        out->path = destination;
        out->bytes = bytes;
        out->durationMs = bytes / frameBytes * kAtracSamplesPerFrame * 1000 / 44100;
    }
    return true;
}

} // namespace mmm
