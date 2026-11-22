// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QObject>
#include <QStringList>
#include <QVariantMap>
#include <QtQmlIntegration/qqmlintegration.h>

#include <optional>

#include "tagwriter.h"
#include "tracktags.h"

// Backs the tag editor dialog for one or more tracks (QML type `TagEditor`).
//
// Setting `paths` (even to the same list) reads the files' current tags (off the UI thread). `values` then holds
// each field's value shared by all tracks, and `mixedFields` the fields whose value
// differs between them (their value is ""). apply() writes only the fields it is given
// through Library::editTags().
//
// Fields: title, artist, album, albumArtist, genre, composer, comment, year, trackNumber,
// trackTotal, discNumber, discTotal (strings; "" = no tag) and compilation (bool).
class TagEditor : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QStringList paths READ paths WRITE setPaths NOTIFY pathsChanged FINAL)
    Q_PROPERTY(int trackCount READ trackCount NOTIFY pathsChanged FINAL)
    Q_PROPERTY(bool loading READ isLoading NOTIFY loadingChanged FINAL)
    Q_PROPERTY(bool saving READ isSaving NOTIFY savingChanged FINAL)
    Q_PROPERTY(QVariantMap values READ values NOTIFY valuesChanged FINAL)
    Q_PROPERTY(QStringList mixedFields READ mixedFields NOTIFY valuesChanged FINAL)
    Q_PROPERTY(QString errorString READ errorString NOTIFY errorStringChanged FINAL)

public:
    explicit TagEditor(QObject *parent = nullptr);

    QStringList paths() const { return m_paths; }
    void setPaths(const QStringList &paths);
    int trackCount() const { return static_cast<int>(m_paths.size()); }
    bool isLoading() const { return m_loading; }
    bool isSaving() const { return m_saving; }
    QVariantMap values() const { return m_values; }
    QStringList mixedFields() const { return m_mixed; }
    QString errorString() const { return m_error; }

    // Writes `changes` ({field: new value}, only the fields the user changed) to all tracks.
    // Returns false (and sets errorString) if a value is invalid or a save is running;
    // otherwise finished() follows. Nothing to change → finished(true, ...) right away.
    Q_INVOKABLE bool apply(const QVariantMap &changes);

    // {field: value} → TagChanges; nullopt + error on an unknown field or a bad number.
    static std::optional<mmm::TagChanges> toChanges(const QVariantMap &changes, QString *error);
    // Field values of one track, in the form `values` uses.
    static QVariantMap toValues(const mmm::TrackTags &tags);

Q_SIGNALS:
    void pathsChanged();
    void loadingChanged();
    void savingChanged();
    void valuesChanged();
    void errorStringChanged();
    // A save ended. `albumId` = the album all edited tracks are in now, or 0 if they're in
    // several / none (the album page follows a renamed album with it).
    void finished(bool ok, const QString &message, int albumId);

private:
    void setError(const QString &message);
    void setLoading(bool loading);

    QStringList m_paths;
    QVariantMap m_values;
    QStringList m_mixed;
    QString m_error;
    bool m_loading = false;
    bool m_saving = false;
    quint64 m_generation = 0;
};
