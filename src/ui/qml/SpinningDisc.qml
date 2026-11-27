// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Shapes

// Q4: a bare MiniDisc in the transport bar, after the analyser. It shows the playing track's cover
// (cut round by the "coverround" provider, so no shader effects: the offscreen software renderer
// draws it too) and spins while playing. The angle is integrated from a speed: only the speed is
// animated (spin up on play, coast down on pause, a quick stop on Stop), so it never jumps and it
// resumes from where it stopped. A new picture cross-fades in over the old one once it's loaded;
// tracks of one album never reload it (compared by Playback.coverKey, not the URL).
// Left click: go to the album. Right click: the now-playing menu + "Stop / Start spinning".
// docs/plans/q3-q4-analyser-and-disc.md.
Item {
    id: root

    // Inputs: Playback by default (tests set them directly).
    property string coverKey: Playback.coverKey
    property string coverUrl: Playback.discCoverUrl
    property bool playing: Playback.playing
    property bool paused: Playback.paused
    property bool spinEnabled: Playback.discSpinEnabled
    property bool shown: Playback.windowVisible
    // False: just the picture (the MD Visualizer's cartridge): no tooltip, no clicks.
    property bool interactive: true

    // State (tests read it).
    property real angle: 0 // degrees, the turning layer's rotation
    property real speed: 0 // 0..1 of full speed
    readonly property bool animating: speed > 0
    property bool showingCover: false
    property Image front: slotA // the picture showing
    property Image back: slotB  // the next one loads here, then fades in on top

    readonly property real degreesPerSecond: 360 / 2.6
    readonly property bool wantSpin: playing && spinEnabled && shown
    readonly property int requestSize: Math.ceil(disc.width * Screen.devicePixelRatio)

    property string shownKey: ""   // the picture showing or on its way
    property string pendingUrl: "" // the one loading into `back`

    function updateSpin() {
        speedAnim.stop();
        if (wantSpin) {
            speedAnim.to = 1;
            speedAnim.duration = 400;
            speedAnim.start();
        } else if (!spinEnabled || !shown) {
            speed = 0; // switched off / hidden: halt, keep the angle
        } else {
            speedAnim.to = 0;
            speedAnim.duration = paused ? 800 : 250; // pause coasts, stop stops
            speedAnim.start();
        }
    }

    function loadCover() {
        if (coverKey === shownKey)
            return; // same picture (another track of the album): nothing reloads
        shownKey = coverKey;
        fadeIn.stop();
        if (coverKey === "" || coverUrl === "") {
            pendingUrl = "";
            fadeOut.start();
            return;
        }
        fadeOut.stop();
        pendingUrl = coverUrl;
        back.opacity = 0;
        back.z = 2;
        front.z = 1;
        back.source = coverUrl;
        slotChanged(back); // already cached: Ready at once
    }

    function slotChanged(slot: Image) {
        if (slot !== back || pendingUrl === "" || slot.source.toString() !== pendingUrl)
            return; // an old load finishing late
        if (slot.status === Image.Ready) {
            pendingUrl = "";
            fadeIn.target = slot;
            fadeIn.start();
        } else if (slot.status === Image.Error) {
            pendingUrl = "";
            fadeOut.start();
        }
    }

    function menuEntries(): var {
        const spin = {
            text: Playback.discSpinEnabled ? qsTr("Stop spinning") : qsTr("Start spinning"),
            onTriggered: () => {
                Playback.discSpinEnabled = !Playback.discSpinEnabled;
            }
        };
        return Playback.hasTrack ? ContextMenus.nowPlayingEntries().concat([ContextMenus.separator(), spin]) : [spin];
    }

    function openMenu(x: real, y: real) {
        ContextMenus.open(root, x, y, menuEntries(), null);
    }

    implicitWidth: 56
    implicitHeight: 56

    // Bindings settle first (playing / paused change together): then decide.
    onWantSpinChanged: Qt.callLater(root.updateSpin)
    onCoverKeyChanged: Qt.callLater(root.loadCover)
    Component.onCompleted: {
        root.updateSpin();
        root.loadCover();
    }

    NumberAnimation {
        id: speedAnim
        target: root
        property: "speed"
        easing.type: Easing.OutQuad // starts from the current speed: no jump
    }

    FrameAnimation {
        running: root.speed > 0 && root.shown
        onTriggered: root.angle = (root.angle + root.speed * root.degreesPerSecond * Math.min(frameTime, 0.05)) % 360
    }

    NumberAnimation {
        id: fadeIn
        property: "opacity"
        from: 0
        to: 1
        duration: 240
        easing.type: Easing.OutCubic
        // The old picture stays fully opaque underneath until now: the plain disc never shows through.
        onFinished: {
            const old = root.front;
            old.opacity = 0;
            old.source = "";
            root.front = root.back;
            root.back = old;
            root.showingCover = true;
        }
    }

    ParallelAnimation {
        id: fadeOut
        NumberAnimation { target: slotA; property: "opacity"; to: 0; duration: 220; easing.type: Easing.OutCubic }
        NumberAnimation { target: slotB; property: "opacity"; to: 0; duration: 220; easing.type: Easing.OutCubic }
        onFinished: {
            slotA.source = "";
            slotB.source = "";
            root.showingCover = false;
        }
    }

    Item {
        id: disc
        anchors.centerIn: parent
        width: Math.min(root.width, root.height)
        height: width

        // What turns: the disc, a printed wedge (so a disc without a cover visibly turns), the cover.
        Item {
            id: turntable
            anchors.fill: parent
            rotation: root.angle

            Rectangle {
                anchors.fill: parent
                radius: width / 2
                color: Theme.discBase
            }
            Shape {
                anchors.fill: parent
                ShapePath {
                    strokeWidth: -1
                    fillColor: Theme.discSheen
                    startX: disc.width / 2
                    startY: disc.height / 2
                    PathAngleArc {
                        centerX: disc.width / 2
                        centerY: disc.height / 2
                        radiusX: disc.width / 2 - 2
                        radiusY: disc.height / 2 - 2
                        startAngle: -120
                        sweepAngle: 45
                        moveToStart: false
                    }
                    PathLine {
                        x: disc.width / 2
                        y: disc.height / 2
                    }
                }
            }
            Image {
                id: slotA
                objectName: "discSlotA"
                anchors.fill: parent
                asynchronous: true
                smooth: true
                fillMode: Image.PreserveAspectCrop
                sourceSize: Qt.size(root.requestSize, root.requestSize)
                opacity: 0
                onStatusChanged: root.slotChanged(slotA)
            }
            Image {
                id: slotB
                objectName: "discSlotB"
                anchors.fill: parent
                asynchronous: true
                smooth: true
                fillMode: Image.PreserveAspectCrop
                sourceSize: Qt.size(root.requestSize, root.requestSize)
                opacity: 0
                onStatusChanged: root.slotChanged(slotB)
            }
        }

        // What stays still: the rim and the metal hub with its spindle hole (real proportions:
        // a 64 mm disc, a ~20 mm hub).
        Rectangle {
            anchors.fill: parent
            radius: width / 2
            color: "transparent"
            border.color: Theme.discRim
            border.width: 1
        }
        Rectangle {
            anchors.centerIn: parent
            width: Math.round(disc.width * 0.32)
            height: width
            radius: width / 2
            border.color: Theme.discHubBorder
            border.width: 1
            gradient: Gradient {
                GradientStop { position: 0.0; color: Theme.discHubHighlight }
                GradientStop { position: 1.0; color: Theme.discHub }
            }
        }
        Rectangle {
            anchors.centerIn: parent
            width: Math.max(4, Math.round(disc.width * 0.1))
            height: width
            radius: width / 2
            color: Theme.surfaceChrome
        }
    }

    HoverHandler {
        id: hover
        enabled: root.interactive
    }
    ToolTip.visible: root.interactive && hover.hovered && Playback.hasTrack
    ToolTip.delay: 600
    ToolTip.text: Playback.album === "" ? Playback.title
                : Playback.artist === "" ? Playback.album : Playback.album + " — " + Playback.artist

    TapHandler {
        acceptedButtons: Qt.LeftButton
        enabled: root.interactive
        onTapped: {
            if (Playback.currentAlbumId > 0)
                ContextMenus.goToAlbumRequested(Playback.currentAlbumId);
        }
    }
    TapHandler {
        acceptedButtons: Qt.RightButton
        enabled: root.interactive
        onTapped: point => root.openMenu(point.position.x, point.position.y)
    }
}
