// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// First-run screen: no library folders yet. Enter (via the Library screen) adds a folder too.
Item {
    id: root

    signal addFolderRequested()

    ColumnLayout {
        anchors.centerIn: parent
        spacing: Theme.spaceLG

        Label {
            Layout.alignment: Qt.AlignHCenter
            text: qsTr("Your library is empty")
            font.pixelSize: Theme.fontDisplay
        }
        Label {
            Layout.alignment: Qt.AlignHCenter
            text: qsTr("Add the folder where your music lives. It will be scanned and kept up to date.")
            color: Theme.textSecondary
        }
        Button {
            Layout.alignment: Qt.AlignHCenter
            text: qsTr("Add music folder…  (Enter)")
            highlighted: true
            focusPolicy: Qt.NoFocus
            onClicked: root.addFolderRequested()
        }
    }
}
