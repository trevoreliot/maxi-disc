// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls

// Q3: the transport bar's spectrum analyser, an MD-deck style LED meter (SpectrumMeter, C++) fed by
// Playback.levels: GStreamer `spectrum` results held until their audio is heard. It moves only
// while something moves (FrameAnimation runs while the meter is animating and the window shows).
// Right-click turns it off. docs/plans/q3-q4-analyser-and-disc.md.
Rectangle {
    id: root

    readonly property alias meter: meter
    readonly property alias ticking: ticker.running

    function openMenu(x: real, y: real) {
        ContextMenus.open(root, x, y, [{
            text: Playback.analyzerEnabled ? qsTr("Turn analyser off") : qsTr("Turn analyser on"),
            onTriggered: () => {
                Playback.analyzerEnabled = !Playback.analyzerEnabled;
            }
        }], null);
    }

    color: Theme.surfaceInset
    border.color: Theme.borderSubtle
    radius: Theme.radiusSmall

    SpectrumMeter {
        id: meter
        objectName: "spectrumMeter"
        anchors.fill: parent
        anchors.margins: Theme.spaceSM
        levels: Playback.levels
        playing: Playback.playing && Playback.analyzerEnabled
        unlitColor: Theme.analyzerUnlit
        lowColor: Theme.analyzerLow
        midColor: Theme.analyzerMid
        highColor: Theme.analyzerHigh
        peakColor: Theme.analyzerPeak
    }

    FrameAnimation {
        id: ticker
        running: meter.animating && Playback.windowVisible
        onTriggered: meter.step(frameTime)
    }
    // Hidden mid-song: start from rest when shown again, not from bars frozen back then.
    Connections {
        target: Playback
        function onWindowVisibleChanged() {
            if (!Playback.windowVisible)
                meter.reset();
        }
    }

    Label {
        objectName: "analyzerOffLabel"
        anchors.centerIn: parent
        visible: !Playback.analyzerEnabled
        text: qsTr("ANALYSER OFF")
        font.pixelSize: Theme.fontMicro
        font.letterSpacing: 1.5
        color: Theme.textMuted
    }

    TapHandler {
        acceptedButtons: Qt.RightButton
        onTapped: point => root.openMenu(point.position.x, point.position.y)
    }
}
