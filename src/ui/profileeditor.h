// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QObject>
#include <QStringList>
#include <QtQmlIntegration/qqmlintegration.h>

#include "deviceprofile.h"

// Backs the device profile dialog (QML type `ProfileEditor`): load() takes a device's saved
// profile, or a built-in one for a device that isn't set up; the fields are edited here;
// `example` shows where a sample track would go (with the extension it would get), and
// `layoutError` what's wrong with the layout. save() stores it through Devices' manager.
//
// transcodeTarget: "mp3_v0" / "mp3_cbr_320"; artworkPolicy: "keep" / "shrink_500" / "strip".
class ProfileEditor : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QString deviceId READ deviceId NOTIFY loaded FINAL)
    Q_PROPERTY(QString deviceName READ deviceName NOTIFY loaded FINAL)
    Q_PROPERTY(bool isNew READ isNew NOTIFY loaded FINAL)
    Q_PROPERTY(QStringList allCodecs READ allCodecs CONSTANT FINAL)
    Q_PROPERTY(QString name READ name WRITE setName NOTIFY changed FINAL)
    Q_PROPERTY(QString presetId READ presetId NOTIFY changed FINAL)
    Q_PROPERTY(QString musicDir READ musicDir WRITE setMusicDir NOTIFY changed FINAL)
    Q_PROPERTY(QString pathLayout READ pathLayout WRITE setPathLayout NOTIFY changed FINAL)
    Q_PROPERTY(QStringList playableCodecs READ playableCodecs WRITE setPlayableCodecs NOTIFY changed FINAL)
    Q_PROPERTY(QString transcodeTarget READ transcodeTarget WRITE setTranscodeTarget NOTIFY changed FINAL)
    Q_PROPERTY(QString artworkPolicy READ artworkPolicy WRITE setArtworkPolicy NOTIFY changed FINAL)
    Q_PROPERTY(QString layoutError READ layoutError NOTIFY changed FINAL)
    Q_PROPERTY(QString example READ example NOTIFY changed FINAL)
    Q_PROPERTY(QString exampleNote READ exampleNote NOTIFY changed FINAL)
    Q_PROPERTY(bool canSave READ canSave NOTIFY changed FINAL)
    Q_PROPERTY(QString errorString READ errorString NOTIFY errorStringChanged FINAL)

public:
    explicit ProfileEditor(QObject *parent = nullptr);

    QString deviceId() const { return m_profile.deviceId; }
    QString deviceName() const { return m_deviceName; }
    bool isNew() const { return m_isNew; }
    QStringList allCodecs() const { return mmm::DeviceProfiles::allCodecs(); }
    QString name() const { return m_profile.name; }
    void setName(const QString &name);
    QString presetId() const { return m_profile.presetId; }
    QString musicDir() const { return m_profile.musicDir; }
    void setMusicDir(const QString &dir);
    QString pathLayout() const { return m_profile.pathLayout; }
    void setPathLayout(const QString &layout);
    QStringList playableCodecs() const { return m_profile.playableCodecs; }
    void setPlayableCodecs(const QStringList &codecs);
    QString transcodeTarget() const { return mmm::DeviceProfiles::toString(m_profile.transcodeTarget); }
    void setTranscodeTarget(const QString &target);
    QString artworkPolicy() const { return mmm::DeviceProfiles::toString(m_profile.artworkPolicy); }
    void setArtworkPolicy(const QString &policy);
    QString layoutError() const;
    QString example() const;
    QString exampleNote() const;
    bool canSave() const;
    QString errorString() const { return m_error; }

    // Loads the device's profile, or (not set up yet) the built-in `presetId` named after the device.
    Q_INVOKABLE bool load(const QString &deviceId, const QString &presetId = QStringLiteral("usb-flash"));
    // Replaces the formats, transcoding, artwork and layout with a built-in profile's (keeps the name).
    Q_INVOKABLE void applyPreset(const QString &presetId);
    Q_INVOKABLE void setCodecEnabled(const QString &codec, bool enabled);
    Q_INVOKABLE bool save();
    Q_INVOKABLE bool remove();

    mmm::DeviceProfile profile() const { return m_profile; }

Q_SIGNALS:
    void loaded();
    void changed();
    void errorStringChanged();
    void saved();
    void removed();

private:
    void setError(const QString &message);

    mmm::DeviceProfile m_profile;
    QString m_deviceName;
    bool m_isNew = true;
    QString m_error;
};
