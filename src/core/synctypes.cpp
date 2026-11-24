// SPDX-License-Identifier: GPL-3.0-or-later
#include "synctypes.h"

namespace mmm {

QString SyncItem::toString(Kind k)
{
    switch (k) {
    case Kind::Playlist:
        return QStringLiteral("playlist");
    case Kind::Album:
        return QStringLiteral("album");
    case Kind::Artist:
        return QStringLiteral("artist");
    case Kind::Genre:
        return QStringLiteral("genre");
    case Kind::Track:
        return QStringLiteral("track");
    }
    return {};
}

std::optional<SyncItem::Kind> SyncItem::kindFrom(const QString &s)
{
    for (Kind k : {Kind::Playlist, Kind::Album, Kind::Artist, Kind::Genre, Kind::Track}) {
        if (s == toString(k))
            return k;
    }
    return std::nullopt;
}

} // namespace mmm
