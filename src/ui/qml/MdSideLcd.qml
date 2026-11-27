// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound

import QtQuick

// The MiniDisc player's side LCD (V3): a narrow window on the left edge, drawn upright here (the
// player's side panel shears it into the oblique view). From the top: a two-bar level meter (the
// analyser is mono: one bar for the bass, bands 0–9, one for mid and treble, 10–23; blank while
// the analyser is off), the track number, and the readout, turned 90° to run along the edge:
// elapsed, −remaining, or the title (which scrolls only while playing, shown and too long to fit).
Rectangle {
    id: root

    property bool active: false // a disc in and a track to show
    property bool playing: false
    property bool animationsEnabled: true
    property bool analyzerEnabled: false
    property var levels: []
    property int trackNumber: 0
    property real positionMs: 0
    property real durationMs: 0
    property string title
    property int displayMode: 0 // 0 elapsed, 1 remaining, 2 title

    readonly property int segments: 10
    readonly property real low: bandLevel(0, 10)
    readonly property real high: bandLevel(10, 24)
    readonly property int lowLit: analyzerEnabled ? Math.round(low * segments) : 0
    readonly property int highLit: analyzerEnabled ? Math.round(high * segments) : 0
    readonly property string readout: !active ? "" : displayMode === 0 ? clock(positionMs)
                                       : displayMode === 1 ? "-" + clock(Math.max(0, durationMs - positionMs))
                                       : title

    function bandLevel(from: int, to: int): real {
        if (!levels || levels.length < to)
            return 0;
        let peak = 0;
        let sum = 0;
        for (let i = from; i < to; ++i) {
            peak = Math.max(peak, levels[i]);
            sum += levels[i];
        }
        return Math.min(1, 0.65 * peak + 0.35 * sum / (to - from));
    }

    function clock(ms: real): string {
        const total = Math.max(0, Math.floor(ms / 1000));
        const m = Math.floor(total / 60);
        const s = total % 60;
        return (m < 10 ? "0" : "") + m + ":" + (s < 10 ? "0" : "") + s;
    }

    color: Theme.mdLcdBg
    border.color: Theme.mdLcdBorder
    border.width: 1
    radius: 2
    clip: true

    // ---- the meter: two columns of segments, the bottom one first ----
    Row {
        id: meter
        objectName: "lcdMeter"
        visible: root.analyzerEnabled
        anchors.horizontalCenter: parent.horizontalCenter
        y: 4
        spacing: 3

        Repeater {
            model: 2
            Column {
                id: bar
                required property int index
                readonly property int lit: index === 0 ? root.lowLit : root.highLit
                spacing: 1

                Repeater {
                    model: root.segments
                    Rectangle {
                        required property int index
                        readonly property int fromBottom: root.segments - 1 - index
                        width: Math.max(4, Math.round((root.width - 14) / 2))
                        height: Math.max(2, Math.round(root.height * 0.32 / root.segments) - 1)
                        color: fromBottom >= bar.lit ? Theme.mdLcdDim : fromBottom >= root.segments - 2 ? Theme.mdLcdPeak : Theme.mdLcdMeter
                    }
                }
            }
        }
    }

    // ---- the track number and the readout, along the edge ----
    Item {
        id: numberSlot
        anchors.left: parent.left
        anchors.right: parent.right
        y: Math.round(root.height * 0.36)
        height: Math.round(root.height * 0.16)

        Text {
            objectName: "lcdTrack"
            anchors.centerIn: parent
            width: parent.height
            height: parent.width
            rotation: 90
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            text: root.active && root.trackNumber > 0 ? (root.trackNumber < 10 ? "0" : "") + root.trackNumber : "--"
            color: Theme.mdLcdText
            font.family: Theme.fontMono
            font.bold: true
            font.pixelSize: Math.max(10, Math.round(root.width * 0.42))
        }
    }

    Item {
        id: readoutSlot
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: numberSlot.bottom
        anchors.bottom: parent.bottom
        anchors.margins: 3
        clip: true

        Item { // the readout's own space, turned to run down the edge
            id: turned
            anchors.centerIn: parent
            width: parent.height
            height: parent.width
            rotation: 90
            clip: true

            Text {
                id: readoutText
                objectName: "lcdReadout"
                y: Math.round((parent.height - height) / 2)
                x: overflows && root.displayMode === 2 ? scrollX : Math.round((parent.width - implicitWidth) / 2)
                text: root.readout
                color: Theme.mdLcdText
                font.family: Theme.fontMono
                font.bold: true
                font.pixelSize: Math.max(10, Math.round(root.width * 0.36))

                readonly property bool overflows: implicitWidth > parent.width
                property real scrollX: 0

                // A new title: start again from the right, with the new length (a running
                // animation keeps the bounds it started with).
                onTextChanged: if (marquee.running) marquee.restart()

                NumberAnimation on scrollX { // the title marquee: only while playing and shown
                    id: marquee
                    running: root.displayMode === 2 && readoutText.overflows && root.playing && root.animationsEnabled
                    from: turned.width
                    to: -readoutText.implicitWidth
                    duration: Math.max(3000, (turned.width + readoutText.implicitWidth) * 25)
                    loops: Animation.Infinite
                }
            }
        }
    }
}
