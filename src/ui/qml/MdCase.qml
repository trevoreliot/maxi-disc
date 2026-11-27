// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick

// One MiniDisc case in the MD Visualizer's pile (V1, docs/plans/v1-visualizer-pile.md), lying
// flat with its spine towards you: the cover's colour (SpineColors) between clear-plastic ends,
// the title (and the artist, in piles that mix artists) printed along it. A mix disc (a playlist)
// is a blank-MD coloured case with a paper label. The cursor's case slides part-way out to the
// right, towards where V2 takes it out, with a shadow under it. Only Rectangles and Text, so the
// offscreen software renderer draws it too; nothing animates once it has settled.
Item {
    id: root

    property string caseKind: "album" // "album" | "playlist" (a mix disc)
    property int caseId: 0
    property string label
    property string detail // the artist (album) or the track count (mix disc); "" for none
    property string cover  // an "image://cover/…" URL, "" without art
    property bool current: false
    property bool paneActive: false
    property bool menuTarget: false
    property bool taken: false // V2: this case is out of the pile (a gap stays)
    property real slideDistance: 24

    readonly property bool mix: caseKind === "playlist"
    property color caseColor: mix ? Theme.mdBlankColor(caseId) : Theme.spineDefault
    readonly property color ink: SpineColors.prefersDarkText(caseColor) ? Theme.spineInkDark : Theme.spineInkLight
    // A real pile isn't straight: each case sits a fixed -2..2 px off.
    readonly property int jitter: ((caseId * 7919) % 5 + 5) % 5 - 2
    property real slide: current ? 1 : 0
    readonly property alias body: body

    property string colorKey: ""
    property string requested: ""
    property bool ready: false

    signal clicked()
    signal contextMenuRequested(real x, real y)

    // Asks SpineColors for the cover's colour (and lets go of the one asked for before).
    function follow() {
        if (requested !== "")
            SpineColors.release(requested);
        requested = "";
        colorKey = mix ? "" : SpineColors.key(cover);
        if (colorKey === "") {
            caseColor = mix ? Theme.mdBlankColor(caseId) : Theme.spineDefault;
            return;
        }
        const known = SpineColors.color(cover);
        caseColor = known.valid ? known : Theme.spineDefault;
        requested = cover;
        SpineColors.request(cover);
    }

    Component.onCompleted: {
        follow();
        ready = true;
    }
    Component.onDestruction: {
        ready = false;
        if (requested !== "")
            SpineColors.release(requested);
        requested = "";
    }
    onCoverChanged: if (ready) follow()
    onCaseKindChanged: if (ready) follow()
    onCaseIdChanged: if (ready) follow()

    Connections {
        target: SpineColors
        enabled: root.colorKey !== ""
        function onColorReady(key: string) {
            if (key !== root.colorKey)
                return;
            const c = SpineColors.color(root.cover);
            if (c.valid)
                root.caseColor = c;
        }
    }

    // Animated only where someone can see it: never in a hidden pile or a hidden window.
    Behavior on caseColor {
        enabled: root.ready && root.visible && Playback.windowVisible
        ColorAnimation { duration: 150 }
    }
    Behavior on slide {
        enabled: root.ready && root.visible && Playback.windowVisible
        NumberAnimation { duration: 140; easing.type: Easing.OutCubic }
    }

    Item {
        id: body
        objectName: "caseBody"
        x: 4 + root.jitter + root.slide * root.slideDistance
        width: root.width - root.slideDistance - 8
        height: root.height
        visible: !root.taken

        Rectangle { // the shadow under the case slid out
            x: 3
            y: 3
            width: parent.width
            height: parent.height
            radius: 2
            color: Theme.spineShadow
            opacity: root.slide
            visible: root.slide > 0
        }

        Rectangle {
            id: spine
            anchors.fill: parent
            radius: 2
            color: root.caseColor

            readonly property int plastic: 6

            Rectangle { // the spine is rounded: lit from above, darker underneath
                anchors.fill: parent
                radius: 2
                gradient: Gradient {
                    GradientStop { position: 0.0; color: Theme.spineEdgeLight }
                    GradientStop { position: 0.35; color: "transparent" }
                    GradientStop { position: 0.7; color: "transparent" }
                    GradientStop { position: 1.0; color: Theme.spineEdgeDark }
                }
            }
            Rectangle { // where it lies on the next case
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                height: 1
                color: Theme.spineEdgeDark
            }
            Rectangle { // clear plastic past the insert, left ...
                id: leftEnd
                anchors.left: parent.left
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                width: spine.plastic
                radius: 2
                color: Theme.spinePlastic
            }
            Rectangle { // ... and its hinge
                anchors.left: leftEnd.right
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                width: 1
                color: Theme.spineEdgeDark
            }
            Rectangle { // right end
                id: rightEnd
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                width: spine.plastic
                radius: 2
                color: Theme.spinePlastic
            }
            Rectangle {
                anchors.right: rightEnd.left
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                width: 1
                color: Theme.spineEdgeDark
            }

            Rectangle { // a mix disc's paper label
                id: paper
                visible: root.mix
                anchors.left: leftEnd.right
                anchors.leftMargin: 6
                anchors.verticalCenter: parent.verticalCenter
                width: Math.round(parent.width * 0.66)
                height: parent.height - 10
                radius: 1
                color: Theme.mdLabelPaper
            }

            Text {
                id: titleText
                objectName: "caseTitle"
                anchors.left: root.mix ? paper.left : leftEnd.right
                anchors.leftMargin: root.mix ? 6 : 8
                anchors.right: root.mix ? paper.right : (detailText.visible ? detailText.left : rightEnd.left)
                anchors.rightMargin: root.mix ? 6 : 10
                anchors.verticalCenter: parent.verticalCenter
                text: root.label
                elide: Text.ElideRight
                color: root.mix ? Theme.mdLabelInk : root.ink
                font.pixelSize: Theme.fontBody
                font.bold: !root.mix
                font.italic: root.mix
            }
            Text {
                id: detailText
                objectName: "caseDetail"
                visible: root.detail !== ""
                anchors.right: rightEnd.left
                anchors.rightMargin: 8
                anchors.verticalCenter: parent.verticalCenter
                width: Math.min(implicitWidth, Math.round(parent.width * 0.4))
                text: root.detail
                elide: Text.ElideRight
                color: root.ink
                opacity: 0.8
                font.pixelSize: Theme.fontSmall
            }
        }

        Rectangle { // hover
            anchors.fill: parent
            radius: 2
            color: Theme.rowHover
            visible: hover.hovered && !root.current
        }
        Rectangle { // the keyboard's case (or the one a menu is open for)
            anchors.fill: parent
            radius: 2
            color: "transparent"
            border.color: Theme.focusRing
            border.width: root.current && root.paneActive ? 2 : 1
            visible: (root.current && root.paneActive) || root.menuTarget
        }
    }

    HoverHandler {
        id: hover
    }
    TapHandler {
        acceptedButtons: Qt.LeftButton
        onTapped: root.clicked()
    }
    TapHandler {
        acceptedButtons: Qt.RightButton
        onTapped: point => root.contextMenuRequested(point.position.x, point.position.y)
    }
}
