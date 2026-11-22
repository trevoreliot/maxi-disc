// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls

// Album art from the "cover" image provider, with a placeholder while loading or when
// the album has none. Decoding/resizing happens off the UI thread.
Item {
    id: root

    property string source
    // Square pixel size to request; the provider rounds it up to a cached bucket.
    property int requestSize: Math.ceil(Math.max(width, height) * Screen.devicePixelRatio)
    property bool showPlaceholder: true
    readonly property bool ready: image.status === Image.Ready

    Rectangle {
        anchors.fill: parent
        visible: root.showPlaceholder && image.status !== Image.Ready
        color: Theme.surfaceElevated
        radius: Theme.radiusSmall

        Label {
            anchors.centerIn: parent
            text: "♪"
            font.pixelSize: Math.max(12, root.height * 0.35)
            color: Theme.textMuted
        }
    }

    Image {
        id: image
        anchors.fill: parent
        source: root.source
        sourceSize: Qt.size(root.requestSize, root.requestSize)
        asynchronous: true
        fillMode: Image.PreserveAspectCrop
        smooth: true
        visible: status === Image.Ready
    }
}
