// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QString>

namespace mmm::AppPaths {

// ~/.local/share/music-md-manager/library.db (XDG_DATA_HOME respected).
// Shared by the app and the command-line tools so they see the same library.
QString defaultDatabasePath();

// ~/.cache/music-md-manager/covers (XDG_CACHE_HOME respected): resized album-art thumbnails.
QString coverCacheDir();

// ~/.cache/music-md-manager/art: embedded cover art extracted as-is (for MPRIS artUrl).
QString artCacheDir();

// ~/.cache/music-md-manager/transcode: files prepared for devices (MP3s, resized art), reused
// across syncs (TranscodeCache).
QString transcodeCacheDir();

// ~/.cache/music-md-manager/md-record: decoded audio on its way to a MiniDisc (M11), deleted
// as soon as each track is recorded.
QString mdRecordDir();

} // namespace mmm::AppPaths
