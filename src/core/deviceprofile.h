// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QList>
#include <QString>
#include <QStringList>

#include <optional>

namespace mmm {

// What a sync target can play and how the music is laid out on it (M8). Stored in the
// library database, keyed by Device::id (docs/decisions/0004). M9 syncs by these rules.
enum class TranscodeTarget { Mp3V0, Mp3Cbr320 };
enum class ArtworkPolicy { Keep, Shrink500, Strip };

struct DeviceProfile
{
    QString deviceId;             // Device::id ("uuid:6478-1AB0", ...)
    QString name;                 // shown in the device list
    QString presetId;             // built-in profile it started from ("usb-flash", "generic-dap")
    QString musicDir = QStringLiteral("Music"); // folder on the device ("" = its root)
    QString pathLayout;           // PathLayout pattern, e.g. "{albumartist}/{album}/{track:02} {title}"
    QStringList playableCodecs;   // TrackRow::codec values copied as they are; others are transcoded
    TranscodeTarget transcodeTarget = TranscodeTarget::Mp3V0;
    ArtworkPolicy artworkPolicy = ArtworkPolicy::Keep;
    QString lastSeenName;         // the device's name when last seen (for unplugged devices)

    bool canPlay(const QString &codec) const { return playableCodecs.contains(codec, Qt::CaseInsensitive); }
    friend bool operator==(const DeviceProfile &, const DeviceProfile &) = default;
};

namespace DeviceProfiles {

// Every codec the library knows (TrackRow::codec), in display order.
QStringList allCodecs();

// "Copy" or the transcode target, and the file extension the device gets.
struct Decision
{
    bool transcode = false;
    QString extension; // without the dot: "flac", "mp3", "m4a", ...
};
Decision decide(const QString &codec, const DeviceProfile &profile);
// File extension for a codec as stored ("FLAC" → "flac", "ALAC"/"AAC" → "m4a"); "" if unknown.
QString extensionForCodec(const QString &codec);

// Built-in profiles (PLAN decisions): "usb-flash" copies everything as it is (files the
// target can't play get MP3 V0 once the user unticks them); "generic-dap" plays MP3 only
// (the rest becomes MP3 V0) and gets 500 px embedded art.
QList<DeviceProfile> builtIns();
std::optional<DeviceProfile> builtIn(const QString &presetId);
QString defaultLayout();

QString toString(TranscodeTarget t);   // "mp3_v0", "mp3_cbr_320"
QString toString(ArtworkPolicy a);     // "keep", "shrink_500", "strip"
std::optional<TranscodeTarget> transcodeTargetFrom(const QString &s);
std::optional<ArtworkPolicy> artworkPolicyFrom(const QString &s);
QString describe(TranscodeTarget t);   // "MP3 V0 (VBR)", "MP3 320 kbps (CBR)"
QString describe(ArtworkPolicy a);     // "Keep as is", ...

} // namespace DeviceProfiles
} // namespace mmm
