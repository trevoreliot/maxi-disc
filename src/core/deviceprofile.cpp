// SPDX-License-Identifier: GPL-3.0-or-later
#include "deviceprofile.h"

namespace mmm::DeviceProfiles {

QStringList allCodecs()
{
    return {QStringLiteral("FLAC"), QStringLiteral("MP3"),    QStringLiteral("AAC"),  QStringLiteral("ALAC"),
            QStringLiteral("Vorbis"), QStringLiteral("Opus"), QStringLiteral("WAV"),  QStringLiteral("AIFF")};
}

QString extensionForCodec(const QString &codec)
{
    const QString c = codec.toUpper();
    if (c == QLatin1String("FLAC"))
        return QStringLiteral("flac");
    if (c == QLatin1String("MP3"))
        return QStringLiteral("mp3");
    if (c == QLatin1String("AAC") || c == QLatin1String("ALAC"))
        return QStringLiteral("m4a");
    if (c == QLatin1String("VORBIS"))
        return QStringLiteral("ogg");
    if (c == QLatin1String("OPUS"))
        return QStringLiteral("opus");
    if (c == QLatin1String("WAV"))
        return QStringLiteral("wav");
    if (c == QLatin1String("AIFF"))
        return QStringLiteral("aiff");
    return {};
}

Decision decide(const QString &codec, const DeviceProfile &profile)
{
    const QString ext = extensionForCodec(codec);
    if (!ext.isEmpty() && profile.canPlay(codec))
        return {false, ext};
    return {true, QStringLiteral("mp3")}; // both targets are MP3
}

QString defaultLayout()
{
    return QStringLiteral("{albumartist}/{album}/[{disc}-]{track:02} {title}");
}

QList<DeviceProfile> builtIns()
{
    DeviceProfile flash;
    flash.presetId = QStringLiteral("usb-flash");
    flash.name = QStringLiteral("USB flash drive");
    flash.pathLayout = defaultLayout();
    flash.playableCodecs = allCodecs();
    flash.transcodeTarget = TranscodeTarget::Mp3V0;
    flash.artworkPolicy = ArtworkPolicy::Keep;

    DeviceProfile dap;
    dap.presetId = QStringLiteral("generic-dap");
    dap.name = QStringLiteral("Generic DAP");
    dap.pathLayout = defaultLayout();
    dap.playableCodecs = {QStringLiteral("MP3")};
    dap.transcodeTarget = TranscodeTarget::Mp3V0;
    dap.artworkPolicy = ArtworkPolicy::Shrink500;
    return {flash, dap};
}

std::optional<DeviceProfile> builtIn(const QString &presetId)
{
    for (const DeviceProfile &p : builtIns()) {
        if (p.presetId == presetId)
            return p;
    }
    return std::nullopt;
}

QString toString(TranscodeTarget t)
{
    return t == TranscodeTarget::Mp3Cbr320 ? QStringLiteral("mp3_cbr_320") : QStringLiteral("mp3_v0");
}

QString toString(ArtworkPolicy a)
{
    switch (a) {
    case ArtworkPolicy::Keep: return QStringLiteral("keep");
    case ArtworkPolicy::Shrink500: return QStringLiteral("shrink_500");
    case ArtworkPolicy::Strip: return QStringLiteral("strip");
    }
    return QStringLiteral("keep");
}

std::optional<TranscodeTarget> transcodeTargetFrom(const QString &s)
{
    if (s == QLatin1String("mp3_v0"))
        return TranscodeTarget::Mp3V0;
    if (s == QLatin1String("mp3_cbr_320"))
        return TranscodeTarget::Mp3Cbr320;
    return std::nullopt;
}

std::optional<ArtworkPolicy> artworkPolicyFrom(const QString &s)
{
    if (s == QLatin1String("keep"))
        return ArtworkPolicy::Keep;
    if (s == QLatin1String("shrink_500"))
        return ArtworkPolicy::Shrink500;
    if (s == QLatin1String("strip"))
        return ArtworkPolicy::Strip;
    return std::nullopt;
}

QString describe(TranscodeTarget t)
{
    return t == TranscodeTarget::Mp3Cbr320 ? QStringLiteral("MP3 320 kbps (CBR)") : QStringLiteral("MP3 V0 (VBR)");
}

QString describe(ArtworkPolicy a)
{
    switch (a) {
    case ArtworkPolicy::Keep: return QStringLiteral("Keep as is");
    case ArtworkPolicy::Shrink500: return QStringLiteral("Shrink to 500 px JPEG");
    case ArtworkPolicy::Strip: return QStringLiteral("Remove");
    }
    return {};
}

} // namespace mmm::DeviceProfiles
