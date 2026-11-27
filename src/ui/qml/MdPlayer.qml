// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls

// The MD Visualizer's portable MiniDisc player (V3, docs/plans/v3-visualizer-player.md): an original
// drawing after the shape of the MZ-M200 (reference photos in docs/ui-draft, local only; no maker's
// marks), in flat 2D. The face is seen straight on: a glossy key bezel down the left, the lid (smoked
// and translucent, so the cartridge and its turning disc show through; hinged on the right, it folds
// back to open), the bay under it. The left edge is seen obliquely: an upright side panel with an
// affine shear (both renderers draw it), carrying the side LCD. The keys are Playback commands, like
// the transport bar's: DISPLAY, VOL − / +, ■ stop, ❚❚ pause, the jog lever (ends: previous / next,
// the wheel too; middle: play / pause), and the OPEN slide on the top edge, which ejects.
// Everything is in proportion to the body height `bodyH`.
Item {
    id: root

    property real bodyH: 240
    property var disc: null          // MdDeck's loaded disc: { kind, id, title, coverUrl, ... }
    property bool discVisible: false // the cartridge seated in the bay
    property real lidOpen: 0         // 0 shut .. 1 folded back
    property int displayMode: 0      // the LCD: 0 elapsed, 1 remaining, 2 title
    property bool animationsEnabled: true
    property bool discTurning: false // this disc is playing
    property bool discPaused: false

    readonly property real h: Math.round(bodyH)
    readonly property real k: 0.35 // the side panel's shear (its back edge rises k · width)
    readonly property real sideW: Math.round(h * 0.18)
    readonly property real faceW: Math.round(h * 1.01)
    readonly property real rise: Math.round(k * sideW)
    readonly property real tabH: Math.max(4, Math.round(h * 0.048))
    readonly property real topSpace: Math.max(rise, tabH)
    readonly property real jogDrop: Math.round(h * 0.04)
    readonly property real totalW: sideW + faceW
    readonly property real totalH: topSpace + h + jogDrop
    readonly property real bezelW: Math.round(h * 0.12)
    readonly property real lidW: faceW - bezelW
    readonly property real bayW: Math.round(h * 0.78)
    readonly property real bayH: Math.round(h * 0.83)
    readonly property real bayX: bezelW + Math.round((lidW - bayW) / 2)
    readonly property real bayY: Math.round((h - bayH) / 2)
    readonly property real cartW: Math.round(bayW * 0.98)
    readonly property real cartH: Math.round(cartW * 72 / 68) // a cartridge is 68 × 72 mm
    readonly property alias lcd: lcdDisplay
    readonly property alias loadedCartridge: loaded
    readonly property alias lid: lidPanel

    signal ejectRequested()
    signal playPauseRequested() // the jog's middle (MdDeck plays the disc again if the queue is empty)
    signal interacted()

    // Where a cartridge sits in the bay, and just above it, in `target`'s coordinates.
    function cartridgeRect(target: Item): rect {
        return seat.mapToItem(target, 0, 0, seat.width, seat.height);
    }
    function aboveBayRect(target: Item): rect {
        const r = cartridgeRect(target);
        return Qt.rect(r.x, r.y - Math.round(h * 0.42), r.width, r.height);
    }

    function stepVolume(delta: real) {
        Playback.volume = Math.max(0, Math.min(1, Playback.volume + delta));
    }

    implicitWidth: totalW
    implicitHeight: totalH

    // A round key on the bezel, a hit target of at least 22 px around it.
    component RoundKey: Item {
        id: key
        property string tip
        property string glyph
        signal activated()
        x: Math.round((root.bezelW - width) / 2)
        width: Math.max(22, Math.round(root.h * 0.068))
        height: width

        Rectangle {
            anchors.centerIn: parent
            width: Math.round(root.h * 0.068)
            height: width
            radius: width / 2
            color: keyHover.hovered ? Theme.mdPlayerKeyRim : Theme.mdPlayerKey
            border.color: Theme.mdPlayerKeyRim
            border.width: 1

            Text {
                anchors.centerIn: parent
                text: key.glyph
                color: Theme.mdPlayerLabel
                font.pixelSize: Math.max(8, Math.round(parent.width * 0.7))
                font.bold: true
            }
        }
        HoverHandler {
            id: keyHover
            cursorShape: Qt.PointingHandCursor
        }
        TapHandler {
            onTapped: {
                root.interacted();
                key.activated();
            }
        }
        ToolTip.visible: keyHover.hovered
        ToolTip.delay: 600
        ToolTip.text: key.tip
    }

    // A silver slide key (■, ❚❚).
    component SilverKey: Item {
        id: skey
        property string tip
        property string glyph
        signal activated()
        x: Math.round((root.bezelW - width) / 2)
        width: Math.max(22, Math.round(root.h * 0.085))
        height: Math.max(22, Math.round(root.h * 0.12))

        Rectangle {
            anchors.centerIn: parent
            width: Math.round(root.h * 0.085)
            height: Math.round(root.h * 0.12)
            radius: 2
            border.color: Theme.mdPlayerMetalDark
            border.width: 1
            gradient: Gradient {
                GradientStop { position: 0.0; color: Theme.mdPlayerMetalLight }
                GradientStop { position: 1.0; color: skeyHover.hovered ? Theme.mdPlayerMetalLight : Theme.mdPlayerMetalDark }
            }

            Text {
                anchors.centerIn: parent
                text: skey.glyph
                color: Theme.mdPlayerBezel
                font.pixelSize: Math.max(8, Math.round(parent.width * 0.55))
                font.bold: true
            }
        }
        HoverHandler {
            id: skeyHover
            cursorShape: Qt.PointingHandCursor
        }
        TapHandler {
            onTapped: {
                root.interacted();
                skey.activated();
            }
        }
        ToolTip.visible: skeyHover.hovered
        ToolTip.delay: 600
        ToolTip.text: skey.tip
    }

    // A name printed on the lid beside its key, running down the edge.
    component Printed: Text {
        property real centerY: 0
        x: root.bezelW + Math.round(root.h * 0.015) - Math.round((width - height) / 2)
        y: Math.round(centerY - height / 2)
        rotation: 90
        color: Theme.mdPlayerLabel
        font.pixelSize: Math.max(7, Math.round(root.h * 0.03))
        font.bold: true
    }

    Item {
        id: body
        x: Math.round((root.width - root.totalW) / 2)
        y: Math.round((root.height - root.totalH) / 2)
        width: root.totalW
        height: root.totalH

        // ---- the left edge, seen obliquely: an upright panel, sheared ----
        Item {
            id: side
            objectName: "playerSide"
            y: root.topSpace
            width: root.sideW
            height: root.h
            transform: Matrix4x4 {
                matrix: Qt.matrix4x4(1, 0, 0, 0,
                                     root.k, 1, 0, -root.k * root.sideW,
                                     0, 0, 1, 0,
                                     0, 0, 0, 1)
            }

            Rectangle {
                anchors.fill: parent
                radius: 2
                color: Theme.mdPlayerSide
                border.color: Theme.mdPlayerBezelLight
                border.width: 1
            }
            MdSideLcd {
                id: lcdDisplay
                objectName: "sideLcd"
                x: Math.round(root.sideW * 0.16)
                y: Math.round(root.h * 0.06)
                width: root.sideW - 2 * x
                height: Math.round(root.h * 0.72)
                active: root.discVisible && Playback.hasTrack
                playing: Playback.playing
                animationsEnabled: root.animationsEnabled
                analyzerEnabled: Playback.analyzerEnabled
                levels: Playback.levels
                trackNumber: Playback.queue ? Playback.queue.currentIndex + 1 : 0
                positionMs: Playback.position
                durationMs: Playback.duration
                title: Playback.title
                displayMode: root.displayMode
            }
            Rectangle { // the jog lever's end, seen from the side
                x: Math.round(root.sideW * 0.2)
                y: Math.round(root.h * 0.84)
                width: root.sideW - 2 * x
                height: Math.round(root.h * 0.14)
                radius: 3
                color: Theme.mdPlayerBezel
                border.color: Theme.mdPlayerMetalDark
                border.width: 1
            }
        }

        // ---- the face, straight on ----
        Rectangle {
            id: face
            objectName: "playerFace"
            x: root.sideW
            y: root.topSpace
            width: root.faceW
            height: root.h
            radius: Math.round(root.h * 0.03)
            color: Theme.mdPlayerBody

            Rectangle { // the glossy key bezel
                width: root.bezelW
                height: parent.height
                radius: face.radius
                gradient: Gradient {
                    GradientStop { position: 0.0; color: Theme.mdPlayerBezelLight }
                    GradientStop { position: 0.5; color: Theme.mdPlayerBezel }
                    GradientStop { position: 1.0; color: Theme.mdPlayerBezelLight }
                }
            }
            Rectangle { // the chrome seam
                x: root.bezelW - 1
                width: 2
                height: parent.height
                color: Theme.mdPlayerSeam
            }

            Rectangle { // the bay under the lid
                objectName: "playerBay"
                x: root.bayX
                y: root.bayY
                width: root.bayW
                height: root.bayH
                radius: 3
                color: Theme.mdPlayerBay

                Rectangle { // the spindle's ring
                    anchors.centerIn: parent
                    width: Math.round(root.bayW * 0.36)
                    height: width
                    radius: width / 2
                    color: "transparent"
                    border.color: Theme.mdPlayerBayDetail
                    border.width: 2
                }
                Rectangle { // the laser sled's slot
                    x: Math.round(parent.width * 0.14)
                    y: Math.round(parent.height * 0.6)
                    width: Math.round(parent.width * 0.12)
                    height: Math.round(parent.height * 0.3)
                    radius: 2
                    color: Theme.mdPlayerBayDetail
                }
            }
            Item { // where a cartridge sits
                id: seat
                x: root.bayX + Math.round((root.bayW - root.cartW) / 2)
                y: root.bayY + Math.round((root.bayH - root.cartH) / 2)
                width: root.cartW
                height: root.cartH
            }
            MdCartridge {
                id: loaded
                objectName: "playerCartridge"
                x: seat.x
                y: seat.y
                width: seat.width
                height: seat.height
                visible: root.discVisible && root.disc !== null
                mix: root.disc !== null && root.disc.kind === "playlist"
                caseId: root.disc ? root.disc.id : 0
                title: root.disc ? root.disc.title : ""
                coverUrl: root.disc ? root.disc.coverUrl : ""
                discPlaying: root.discTurning
                discPaused: root.discPaused
                discShown: root.animationsEnabled
            }

            Item { // the smoked lid, hinged on the right: it folds back to open
                id: lidPanel
                objectName: "playerLid"
                x: root.bezelW
                width: root.lidW
                height: parent.height
                transform: Scale {
                    origin.x: lidPanel.width
                    xScale: Math.max(0.001, 1 - 0.86 * root.lidOpen)
                }

                Rectangle {
                    anchors.fill: parent
                    radius: face.radius
                    color: Theme.mdPlayerLidTint
                    border.color: Theme.mdPlayerLidBorder
                    border.width: 1
                }
                Rectangle { // the light on it
                    anchors.fill: parent
                    radius: face.radius
                    gradient: Gradient {
                        GradientStop { position: 0.0; color: Theme.mdPlayerLidSheen }
                        GradientStop { position: 0.4; color: "transparent" }
                    }
                }
                Rectangle { // its inner rim, seen as it opens
                    width: Math.max(2, Math.round(root.h * 0.03))
                    height: parent.height
                    color: Theme.mdPlayerMetalDark
                    opacity: root.lidOpen
                    visible: root.lidOpen > 0.01
                }
            }

            Printed { centerY: root.h * 0.10 + displayKey.height / 2; text: "DISPLAY" }
            Printed { centerY: root.h * 0.28 + volDown.height / 2; text: "−  VOL  +" }
            Printed { centerY: root.h * 0.56 + stopKey.height / 2; text: "STOP" }
            Printed { centerY: root.h * 0.69 + pauseKey.height / 2; text: "PAUSE" }

            RoundKey {
                id: displayKey
                objectName: "keyDisplay"
                y: Math.round(root.h * 0.10)
                glyph: "•"
                tip: qsTr("Display: elapsed, remaining, title")
                onActivated: root.displayMode = (root.displayMode + 1) % 3
            }
            RoundKey {
                id: volDown
                objectName: "keyVolDown"
                y: Math.round(root.h * 0.22)
                glyph: "−"
                tip: qsTr("Volume down (-)")
                onActivated: root.stepVolume(-0.05)
            }
            RoundKey {
                objectName: "keyVolUp"
                y: Math.round(root.h * 0.34)
                glyph: "+"
                tip: qsTr("Volume up (+)")
                onActivated: root.stepVolume(0.05)
            }
            SilverKey {
                id: stopKey
                objectName: "keyStop"
                y: Math.round(root.h * 0.56)
                glyph: "■"
                tip: qsTr("Stop")
                onActivated: Playback.stop()
            }
            SilverKey {
                id: pauseKey
                objectName: "keyPause"
                y: Math.round(root.h * 0.69)
                glyph: "❚❚"
                tip: qsTr("Pause (Space)")
                onActivated: root.playPauseRequested() // as the jog's middle
            }

            Item { // the jog lever at the bottom corner: previous (top end), play / pause, next (bottom end)
                id: jog
                objectName: "jogLever"
                x: -Math.round(root.h * 0.02)
                y: Math.round(root.h * 0.84)
                width: Math.max(22, Math.round(root.h * 0.16))
                height: Math.round(root.h * 0.16) + root.jogDrop
                property real wheelAccum: 0

                Rectangle {
                    anchors.fill: parent
                    radius: Math.round(width * 0.3)
                    color: jogHover.hovered ? Theme.mdPlayerBezelLight : Theme.mdPlayerBezel
                    border.color: Theme.mdPlayerMetalDark
                    border.width: 1
                }
                Repeater { // its knurling
                    model: 5
                    Rectangle {
                        required property int index
                        x: Math.round(jog.width * 0.2)
                        width: Math.round(jog.width * 0.6)
                        y: Math.round(jog.height * (0.18 + index * 0.16))
                        height: 1
                        color: Theme.mdPlayerMetalDark
                    }
                }
                HoverHandler {
                    id: jogHover
                    cursorShape: Qt.PointingHandCursor
                }
                TapHandler {
                    onTapped: point => {
                        root.interacted();
                        const third = point.position.y / jog.height;
                        if (third < 1 / 3)
                            Playback.previous();
                        else if (third > 2 / 3)
                            Playback.next();
                        else
                            root.playPauseRequested();
                    }
                }
                WheelHandler {
                    acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
                    onWheel: event => {
                        jog.wheelAccum += event.angleDelta.y;
                        if (jog.wheelAccum >= 120) {
                            jog.wheelAccum = 0;
                            Playback.previous();
                        } else if (jog.wheelAccum <= -120) {
                            jog.wheelAccum = 0;
                            Playback.next();
                        }
                    }
                }
                ToolTip.visible: jogHover.hovered
                ToolTip.delay: 600
                ToolTip.text: qsTr("Jog: top previous, middle play / pause, bottom next (or the wheel)")
            }
        }

        Rectangle { // the OPEN slide on the top edge: eject
            id: openSlide
            objectName: "openSlide"
            x: root.sideW + Math.round(root.h * 0.05) + Math.round(root.h * 0.04 * Math.min(1, root.lidOpen * 2))
            y: root.topSpace - root.tabH
            width: Math.round(root.h * 0.14)
            height: root.tabH + 2
            radius: 1
            border.color: Theme.mdPlayerMetalDark
            border.width: 1
            gradient: Gradient {
                GradientStop { position: 0.0; color: Theme.mdPlayerMetalLight }
                GradientStop { position: 1.0; color: Theme.mdPlayerMetalDark }
            }

            Row { // grip ridges
                anchors.centerIn: parent
                spacing: Math.max(2, Math.round(root.h * 0.012))
                Repeater {
                    model: 3
                    Rectangle {
                        width: 1
                        height: Math.max(2, Math.round(openSlide.height * 0.6))
                        color: Theme.mdPlayerMetalDark
                    }
                }
            }
            Item { // a hit target of at least 22 px
                anchors.centerIn: parent
                width: Math.max(22, parent.width)
                height: Math.max(22, parent.height)

                HoverHandler {
                    id: slideHover
                    cursorShape: Qt.PointingHandCursor
                }
                TapHandler {
                    onTapped: {
                        root.interacted();
                        root.ejectRequested();
                    }
                }
            }
            ToolTip.visible: slideHover.hovered
            ToolTip.delay: 600
            ToolTip.text: qsTr("Open: eject (O)")
        }
    }
}
