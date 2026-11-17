// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// First-run screen: no library folders yet.
Item {
    id: root

    signal addFolderRequested()

    ColumnLayout {
        anchors.centerIn: parent
        spacing: 16

        Label {
            Layout.alignment: Qt.AlignHCenter
            text: qsTr("Your library is empty")
            font.pixelSize: 24
        }
        Label {
            Layout.alignment: Qt.AlignHCenter
            text: qsTr("Add the folder where your music lives. It will be scanned and kept up to date.")
            opacity: 0.7
        }
        Button {
            Layout.alignment: Qt.AlignHCenter
            text: qsTr("Add music folder…")
            highlighted: true
            onClicked: root.addFolderRequested()
        }
    }
}
