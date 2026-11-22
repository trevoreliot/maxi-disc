// SPDX-License-Identifier: GPL-3.0-or-later
#include "profileeditor.h"

#include "devices.h"
#include "libraryquery.h"
#include "pathlayout.h"

namespace DP = mmm::DeviceProfiles;

ProfileEditor::ProfileEditor(QObject *parent)
    : QObject(parent)
{
    m_profile = *DP::builtIn(QStringLiteral("usb-flash"));
}

void ProfileEditor::setError(const QString &message)
{
    if (m_error == message)
        return;
    m_error = message;
    Q_EMIT errorStringChanged();
}

bool ProfileEditor::load(const QString &deviceId, const QString &presetId)
{
    Devices *devices = Devices::instance();
    const auto entry = devices ? devices->manager()->device(deviceId) : std::nullopt;
    setError({});
    m_deviceName = entry ? entry->device.name : deviceId;
    if (entry && entry->profile) {
        m_profile = *entry->profile;
        m_isNew = false;
    } else {
        m_profile = DP::builtIn(presetId).value_or(*DP::builtIn(QStringLiteral("usb-flash")));
        m_profile.name = m_deviceName.isEmpty() ? m_profile.name : m_deviceName;
        m_isNew = true;
    }
    m_profile.deviceId = deviceId;
    Q_EMIT loaded();
    Q_EMIT changed();
    return entry.has_value();
}

void ProfileEditor::applyPreset(const QString &presetId)
{
    const auto preset = DP::builtIn(presetId);
    if (!preset)
        return;
    mmm::DeviceProfile p = *preset;
    p.deviceId = m_profile.deviceId;
    p.name = m_profile.name;
    p.lastSeenName = m_profile.lastSeenName;
    m_profile = p;
    Q_EMIT changed();
}

void ProfileEditor::setName(const QString &name)
{
    if (m_profile.name == name)
        return;
    m_profile.name = name;
    Q_EMIT changed();
}

void ProfileEditor::setMusicDir(const QString &dir)
{
    if (m_profile.musicDir == dir)
        return;
    m_profile.musicDir = dir;
    Q_EMIT changed();
}

void ProfileEditor::setPathLayout(const QString &layout)
{
    if (m_profile.pathLayout == layout)
        return;
    m_profile.pathLayout = layout;
    Q_EMIT changed();
}

void ProfileEditor::setPlayableCodecs(const QStringList &codecs)
{
    QStringList ordered;
    for (const QString &c : DP::allCodecs()) {
        if (codecs.contains(c, Qt::CaseInsensitive))
            ordered << c;
    }
    if (ordered == m_profile.playableCodecs)
        return;
    m_profile.playableCodecs = ordered;
    Q_EMIT changed();
}

void ProfileEditor::setCodecEnabled(const QString &codec, bool enabled)
{
    QStringList codecs = m_profile.playableCodecs;
    codecs.removeAll(codec);
    if (enabled)
        codecs << codec;
    setPlayableCodecs(codecs);
}

void ProfileEditor::setTranscodeTarget(const QString &target)
{
    const auto t = DP::transcodeTargetFrom(target);
    if (!t || *t == m_profile.transcodeTarget)
        return;
    m_profile.transcodeTarget = *t;
    Q_EMIT changed();
}

void ProfileEditor::setArtworkPolicy(const QString &policy)
{
    const auto a = DP::artworkPolicyFrom(policy);
    if (!a || *a == m_profile.artworkPolicy)
        return;
    m_profile.artworkPolicy = *a;
    Q_EMIT changed();
}

QString ProfileEditor::layoutError() const
{
    return mmm::PathLayout::validate(m_profile.pathLayout);
}

QString ProfileEditor::example() const
{
    return mmm::PathLayout::render(mmm::PathLayout::sampleTrack(), m_profile);
}

QString ProfileEditor::exampleNote() const
{
    const auto d = DP::decide(mmm::PathLayout::sampleTrack().codec, m_profile);
    return d.transcode ? QStringLiteral("A FLAC track is converted to %1.").arg(DP::describe(m_profile.transcodeTarget))
                       : QStringLiteral("A FLAC track is copied as it is.");
}

bool ProfileEditor::canSave() const
{
    return !m_profile.deviceId.isEmpty() && !m_profile.name.trimmed().isEmpty() && layoutError().isEmpty();
}

bool ProfileEditor::save()
{
    Devices *devices = Devices::instance();
    if (!devices) {
        setError(QStringLiteral("No device list."));
        return false;
    }
    QString error;
    if (!devices->manager()->saveProfile(m_profile, &error)) {
        setError(error);
        return false;
    }
    setError({});
    m_isNew = false;
    Q_EMIT loaded();
    Q_EMIT saved();
    return true;
}

bool ProfileEditor::remove()
{
    Devices *devices = Devices::instance();
    if (!devices || !devices->manager()->deleteProfile(m_profile.deviceId))
        return false;
    m_isNew = true;
    Q_EMIT loaded();
    Q_EMIT removed();
    return true;
}
