// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtTest
import MusicMdManager

TestCase {
    name: "AppBridge"

    function test_identity() {
        compare(AppBridge.displayName, "Music MD Manager");
        verify(AppBridge.version.length > 0);
    }
}
