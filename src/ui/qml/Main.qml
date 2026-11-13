// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ApplicationWindow {
    id: root

    width: 1100
    height: 720
    visible: true
    title: AppBridge.displayName

    ColumnLayout {
        anchors.centerIn: parent
        spacing: 8

        Label {
            Layout.alignment: Qt.AlignHCenter
            text: AppBridge.displayName
            font.pixelSize: 28
        }

        Label {
            Layout.alignment: Qt.AlignHCenter
            text: qsTr("Version %1").arg(AppBridge.version)
            opacity: 0.6
        }
    }
}
