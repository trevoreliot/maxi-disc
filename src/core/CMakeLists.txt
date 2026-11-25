# SPDX-License-Identifier: GPL-3.0-or-later
# Core: C++ and Qt Core only (plus non-GUI libs like GStreamer). No QML / GUI — testable headless.
find_package(PkgConfig REQUIRED)
pkg_check_modules(GST REQUIRED IMPORTED_TARGET gstreamer-1.0>=1.22)
find_package(TagLib 2.0 REQUIRED)
# M14: the ListenBrainz token goes in the desktop keyring (Qt Core + DBus only, no GUI).
find_package(Qt6Keychain REQUIRED)

# MusicBrainz asks clients to identify themselves: "app/version ( contact )". The project's
# public page identifies the app (not a person). Forks that ship their own builds: set your own.
set(MMM_CONTACT_URL "https://github.com/trevoreliot/maxi-disc" CACHE STRING
    "Project URL or contact address sent in the MusicBrainz User-Agent")
configure_file(version.h.in "${CMAKE_CURRENT_BINARY_DIR}/mmm/version.h" @ONLY)

add_library(mmm_core STATIC
    appinfo.h appinfo.cpp
    gstinit.h gstinit.cpp
    player.h player.cpp
    playerengine.h playerengine.cpp
    tracktags.h
    tagreader.h tagreader.cpp
    tagwriter.h tagwriter.cpp
    database.h database.cpp
    apppaths.h apppaths.cpp
    libraryscanner.h libraryscanner.cpp
    libraryquery.h libraryquery.cpp
    playqueue.h playqueue.cpp
    playbackcontroller.h playbackcontroller.cpp
    m3u.h m3u.cpp
    artcache.h artcache.cpp
    artworksaver.h artworksaver.cpp
    httpfetcher.h httpfetcher.cpp
    musicbrainzclient.h musicbrainzclient.cpp
    coverartarchiveclient.h coverartarchiveclient.cpp
    artworkmatch.h artworkmatch.cpp
    artworkfinder.h artworkfinder.cpp
    device.h
    deviceprofile.h deviceprofile.cpp
    pathlayout.h pathlayout.cpp
    udisksmonitor.h udisksmonitor.cpp
    devicemanager.h devicemanager.cpp
    synctypes.h synctypes.cpp
    devicescanner.h devicescanner.cpp
    syncplanner.h syncplanner.cpp
    artshrinker.h artshrinker.cpp
    transcoder.h transcoder.cpp
    transcodecache.h transcodecache.cpp
    usbsync.h usbsync.cpp
    devicebrowser.h devicebrowser.cpp
    mdtypes.h
    mdtitle.h mdtitle.cpp
    netmdbackend.h libnetmdbackend.cpp
    netmdmonitor.h netmdmonitor.cpp
    netmdmanager.h netmdmanager.cpp
    mdencodeworker.h mdencodeworker.cpp
    scrobbletypes.h
    scrobbletracker.h scrobbletracker.cpp
    listenbrainzclient.h listenbrainzclient.cpp
    secretstore.h secretstore.cpp
    scrobbler.h scrobbler.cpp
    mdupload.h
    mduploadplanner.h mduploadplanner.cpp
    gstpcmencoder.h gstpcmencoder.cpp
    atracencoder.h atracencoder.cpp
    syncjob.h syncjob.cpp
    mprisservice.h mprisservice.cpp)
target_include_directories(mmm_core PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_BINARY_DIR}")
target_link_libraries(mmm_core
    PUBLIC Qt6::Core Qt6::Sql Qt6::DBus Qt6::Network
    PRIVATE Qt6::Concurrent
    PRIVATE PkgConfig::GST TagLib::tag netmd Qt6Keychain::Qt6Keychain mmm_build_flags)
# M12: the vendored atracdenc (third_party/atracdenc.cmake) is built with core, and its build-tree
# path is AtracEncoder's fallback when it isn't next to the running program (tests, tools).
add_dependencies(mmm_core atracdenc_build)
target_compile_definitions(mmm_core PRIVATE MMM_ATRACDENC_BUILD_PATH="${MMM_ATRACDENC_PATH}")

# Publish moc metatypes so QML modules can use core types via QML_FOREIGN (e.g. PlayQueue).
qt_extract_metatypes(mmm_core)
