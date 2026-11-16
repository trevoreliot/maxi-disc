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
target_link_libraries(tst_player PRIVATE mmm_core PkgConfig::GST Qt6::Test mmm_build_flags)
add_test(NAME tst_player COMMAND tst_player)

qt_add_executable(tst_tagreader core/tst_tagreader.cpp core/testmedia.h core/testmedia.cpp)
target_link_libraries(tst_tagreader PRIVATE mmm_core PkgConfig::GST TagLib::tag Qt6::Test mmm_build_flags)
add_test(NAME tst_tagreader COMMAND tst_tagreader)

qt_add_executable(tst_database core/tst_database.cpp)
target_link_libraries(tst_database PRIVATE mmm_core Qt6::Test mmm_build_flags)
add_test(NAME tst_database COMMAND tst_database)

qt_add_executable(tst_libraryscanner core/tst_libraryscanner.cpp core/testmedia.h core/testmedia.cpp)
target_link_libraries(tst_libraryscanner PRIVATE mmm_core PkgConfig::GST TagLib::tag Qt6::Test mmm_build_flags)
add_test(NAME tst_libraryscanner COMMAND tst_libraryscanner)

# --- QML tests (Qt Quick Test) ---
qt_add_executable(tst_qml qml/tst_qml.cpp)
target_link_libraries(tst_qml PRIVATE mmm_ui mmm_uiplugin Qt6::QuickTest mmm_build_flags)
target_compile_definitions(tst_qml PRIVATE
    QUICK_TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/qml")
add_test(NAME tst_qml COMMAND tst_qml)

foreach(t tst_appinfo tst_player tst_tagreader tst_database tst_libraryscanner tst_qml)
    mmm_add_sanitizer_defaults(${t})
endforeach()

# --- App smoke test: window loads and exits cleanly ---
add_test(NAME app_smoke COMMAND music-md-manager --quit-after-load)

set_tests_properties(tst_appinfo tst_player tst_tagreader tst_database tst_libraryscanner tst_qml app_smoke PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# --- qmllint: any warning fails the test ---
add_test(NAME qmllint
    COMMAND "${CMAKE_COMMAND}" --build "${CMAKE_BINARY_DIR}" --target all_qmllint)
set_tests_properties(qmllint PROPERTIES FAIL_REGULAR_EXPRESSION "Warning:|Error:")
