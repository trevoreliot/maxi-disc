# SPDX-License-Identifier: GPL-3.0-or-later
find_package(Qt6 REQUIRED COMPONENTS Test QuickTest)
find_package(PkgConfig REQUIRED)
pkg_check_modules(GST REQUIRED IMPORTED_TARGET gstreamer-1.0>=1.22)
find_package(TagLib 2.0 REQUIRED)

# --- Core unit tests (headless, Qt Test) ---
qt_add_executable(tst_appinfo core/tst_appinfo.cpp)
target_link_libraries(tst_appinfo PRIVATE mmm_core Qt6::Test mmm_build_flags)
add_test(NAME tst_appinfo COMMAND tst_appinfo)

qt_add_executable(tst_player core/tst_player.cpp core/testmedia.h core/testmedia.cpp)
target_link_libraries(tst_player PRIVATE mmm_core PkgConfig::GST TagLib::tag Qt6::Test mmm_build_flags)
add_test(NAME tst_player COMMAND tst_player)

qt_add_executable(tst_tagreader core/tst_tagreader.cpp core/testmedia.h core/testmedia.cpp)
target_link_libraries(tst_tagreader PRIVATE mmm_core PkgConfig::GST TagLib::tag Qt6::Test mmm_build_flags)
add_test(NAME tst_tagreader COMMAND tst_tagreader)

qt_add_executable(tst_tagwriter core/tst_tagwriter.cpp core/testmedia.h core/testmedia.cpp)
target_link_libraries(tst_tagwriter PRIVATE mmm_core PkgConfig::GST TagLib::tag Qt6::Test mmm_build_flags)
add_test(NAME tst_tagwriter COMMAND tst_tagwriter)

qt_add_executable(tst_database core/tst_database.cpp)
target_link_libraries(tst_database PRIVATE mmm_core Qt6::Test mmm_build_flags)
add_test(NAME tst_database COMMAND tst_database)

qt_add_executable(tst_libraryscanner core/tst_libraryscanner.cpp core/testmedia.h core/testmedia.cpp)
target_link_libraries(tst_libraryscanner PRIVATE mmm_core PkgConfig::GST TagLib::tag Qt6::Test mmm_build_flags)
add_test(NAME tst_libraryscanner COMMAND tst_libraryscanner)

qt_add_executable(tst_libraryquery core/tst_libraryquery.cpp)
target_link_libraries(tst_libraryquery PRIVATE mmm_core Qt6::Test mmm_build_flags)
add_test(NAME tst_libraryquery COMMAND tst_libraryquery)

qt_add_executable(tst_playqueue core/tst_playqueue.cpp)
target_link_libraries(tst_playqueue PRIVATE mmm_core Qt6::Test mmm_build_flags)
add_test(NAME tst_playqueue COMMAND tst_playqueue)

qt_add_executable(tst_m3u core/tst_m3u.cpp)
target_link_libraries(tst_m3u PRIVATE mmm_core Qt6::Test mmm_build_flags)
add_test(NAME tst_m3u COMMAND tst_m3u)

qt_add_executable(tst_playbackcontroller core/tst_playbackcontroller.cpp core/testmedia.h core/testmedia.cpp)
target_link_libraries(tst_playbackcontroller PRIVATE mmm_core PkgConfig::GST Qt6::Test mmm_build_flags)
add_test(NAME tst_playbackcontroller COMMAND tst_playbackcontroller)

# --- Artwork finder (M7). Network tests use a loopback server (testhttpserver.h), never the internet. ---
qt_add_executable(tst_artworksaver core/tst_artworksaver.cpp core/testmedia.h core/testmedia.cpp)
target_link_libraries(tst_artworksaver PRIVATE mmm_core PkgConfig::GST TagLib::tag Qt6::Test mmm_build_flags)
add_test(NAME tst_artworksaver COMMAND tst_artworksaver)

qt_add_executable(tst_artworkmatch core/tst_artworkmatch.cpp)
target_link_libraries(tst_artworkmatch PRIVATE mmm_core Qt6::Test mmm_build_flags)
add_test(NAME tst_artworkmatch COMMAND tst_artworkmatch)

foreach(t tst_httpfetcher tst_musicbrainzclient tst_coverartarchiveclient tst_artworkfinder)
    qt_add_executable(${t} core/${t}.cpp core/testhttpserver.h)
    target_link_libraries(${t} PRIVATE mmm_core Qt6::Network Qt6::Test mmm_build_flags)
    add_test(NAME ${t} COMMAND ${t})
endforeach()

# --- Devices + profiles (M8). UDisks2 tests run against a fake service on a private bus. ---
foreach(t tst_deviceprofile tst_pathlayout)
    qt_add_executable(${t} core/${t}.cpp)
    target_link_libraries(${t} PRIVATE mmm_core Qt6::Test mmm_build_flags)
    add_test(NAME ${t} COMMAND ${t})
endforeach()
foreach(t tst_udisksmonitor tst_devicemanager)
    qt_add_executable(${t} core/${t}.cpp core/fakeudisks.h)
    target_link_libraries(${t} PRIVATE mmm_core Qt6::DBus Qt6::Test mmm_build_flags)
    if(DBUS_RUN_SESSION)
        add_test(NAME ${t} COMMAND "${DBUS_RUN_SESSION}" -- $<TARGET_FILE:${t}>)
    else()
        message(WARNING "dbus-run-session not found: ${t} will not run")
    endif()
endforeach()
set(MMM_DEVICE_TESTS tst_deviceprofile tst_pathlayout tst_udisksmonitor tst_devicemanager)

# --- USB sync (M9): a temp dir stands in for the mounted device. ---
qt_add_executable(tst_syncplanner core/tst_syncplanner.cpp)
target_link_libraries(tst_syncplanner PRIVATE mmm_core Qt6::Test mmm_build_flags)
add_test(NAME tst_syncplanner COMMAND tst_syncplanner)
foreach(t tst_artshrinker tst_transcoder tst_usbsync tst_devicebrowser)
    qt_add_executable(${t} core/${t}.cpp core/testmedia.h core/testmedia.cpp)
    target_link_libraries(${t} PRIVATE mmm_core PkgConfig::GST TagLib::tag Qt6::Test mmm_build_flags)
    add_test(NAME ${t} COMMAND ${t})
endforeach()
set(MMM_SYNC_TESTS tst_syncplanner tst_artshrinker tst_transcoder tst_usbsync tst_devicebrowser)

# --- NetMD MiniDisc (M10): an in-memory recorder (fakenetmd.h), never USB. ---
foreach(t tst_mdtitle tst_netmdmanager tst_mduploadplanner)
    qt_add_executable(${t} core/${t}.cpp core/fakenetmd.h)
    target_link_libraries(${t} PRIVATE mmm_core Qt6::Test mmm_build_flags)
    add_test(NAME ${t} COMMAND ${t})
endforeach()
qt_add_executable(tst_gstpcmencoder core/tst_gstpcmencoder.cpp core/testmedia.h core/testmedia.cpp)
target_link_libraries(tst_gstpcmencoder PRIVATE mmm_core PkgConfig::GST Qt6::Test mmm_build_flags)
add_test(NAME tst_gstpcmencoder COMMAND tst_gstpcmencoder)
qt_add_executable(tst_atracencoder core/tst_atracencoder.cpp core/testmedia.h core/testmedia.cpp)
target_link_libraries(tst_atracencoder PRIVATE mmm_core PkgConfig::GST Qt6::Test mmm_build_flags)
add_test(NAME tst_atracencoder COMMAND tst_atracencoder)
set(MMM_NETMD_TESTS tst_mdtitle tst_netmdmanager tst_mduploadplanner tst_gstpcmencoder tst_atracencoder)

set(MMM_ARTWORK_TESTS tst_artworksaver tst_artworkmatch tst_httpfetcher tst_musicbrainzclient tst_coverartarchiveclient tst_artworkfinder)

# MPRIS needs a session bus: run it on a private one so tests never touch the desktop's.
find_program(DBUS_RUN_SESSION dbus-run-session)
qt_add_executable(tst_mpris core/tst_mpris.cpp core/testmedia.h core/testmedia.cpp)
target_link_libraries(tst_mpris PRIVATE mmm_core PkgConfig::GST Qt6::Test mmm_build_flags)
mmm_add_sanitizer_defaults(tst_mpris)
if(DBUS_RUN_SESSION)
    add_test(NAME tst_mpris COMMAND "${DBUS_RUN_SESSION}" -- $<TARGET_FILE:tst_mpris>)
    set_tests_properties(tst_mpris PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
else()
    message(WARNING "dbus-run-session not found: tst_mpris will not run")
endif()

# --- UI: C++ models, cover provider (needs a GUI app; runs offscreen) ---
add_library(mmm_test_fixture STATIC ui/fixturelibrary.h ui/fixturelibrary.cpp core/testmedia.h core/testmedia.cpp)
target_include_directories(mmm_test_fixture PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/ui" "${CMAKE_CURRENT_SOURCE_DIR}/core")
target_link_libraries(mmm_test_fixture PUBLIC mmm_core Qt6::Gui PkgConfig::GST TagLib::tag PRIVATE mmm_build_flags)

qt_add_executable(tst_uimodels ui/tst_uimodels.cpp)
target_link_libraries(tst_uimodels PRIVATE mmm_ui mmm_test_fixture Qt6::Test mmm_build_flags)
add_test(NAME tst_uimodels COMMAND tst_uimodels)

# --- QML tests (Qt Quick Test) ---
qt_add_executable(tst_qml qml/tst_qml.cpp)
target_link_libraries(tst_qml PRIVATE mmm_ui mmm_uiplugin mmm_test_fixture Qt6::QuickTest mmm_build_flags)
target_compile_definitions(tst_qml PRIVATE
    QUICK_TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/qml")
add_test(NAME tst_qml COMMAND tst_qml)

foreach(t tst_appinfo tst_player tst_tagreader tst_tagwriter tst_database tst_libraryscanner tst_libraryquery tst_playqueue tst_m3u tst_playbackcontroller tst_uimodels tst_qml ${MMM_ARTWORK_TESTS} ${MMM_DEVICE_TESTS} ${MMM_SYNC_TESTS} ${MMM_NETMD_TESTS})
    mmm_add_sanitizer_defaults(${t})
endforeach()

# --- App smoke test: window loads and exits cleanly ---
# Isolated database and cache: tests must never touch the user's real library.
set(MMM_TEST_HOME "${CMAKE_BINARY_DIR}/test-home")
add_test(NAME app_smoke COMMAND music-md-manager --quit-after-load --no-scan --no-mpris --no-devices
    --database "${MMM_TEST_HOME}/smoke.db")

set_tests_properties(tst_appinfo tst_player tst_tagreader tst_tagwriter tst_database tst_libraryscanner tst_libraryquery tst_playqueue tst_m3u tst_playbackcontroller tst_uimodels tst_qml app_smoke ${MMM_ARTWORK_TESTS} ${MMM_DEVICE_TESTS} ${MMM_SYNC_TESTS} ${MMM_NETMD_TESTS} PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;XDG_DATA_HOME=${MMM_TEST_HOME}/data;XDG_CACHE_HOME=${MMM_TEST_HOME}/cache;XDG_CONFIG_HOME=${MMM_TEST_HOME}/config;MMM_MUSICBRAINZ_URL=http://127.0.0.1:9;MMM_COVERARTARCHIVE_URL=http://127.0.0.1:9")

# --- qmllint: any warning fails the test ---
add_test(NAME qmllint
    COMMAND "${CMAKE_COMMAND}" --build "${CMAKE_BINARY_DIR}" --target all_qmllint)
set_tests_properties(qmllint PROPERTIES FAIL_REGULAR_EXPRESSION "Warning:|Error:")
