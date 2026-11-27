// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls
import QtQuick.Shapes

// The MiniDisc cartridge (V2; V3 puts it in the player): a clear shell, like clear MD blanks, with
// the chamfered corner, through which the disc shows: Q4's SpinningDisc, still, with the cover
// printed on it (still, unless the player turns it: discPlaying). On the front, a metal shutter and
// the label (the cover; a mix disc's is paper with its name); on the back, the shutter on the other
// side and no label. An original drawing in MD
// proportions (68 × 72 mm): no maker's marks. Shapes, Rectangles, Images and Text only.
Item {
    id: root

    property bool mix: false
    property int caseId: 0
    property string title
    property string coverUrl // "image://cover/…" or ""
    property bool showingBack: false
    // V3, in the player: the disc turns with Playback (off by default: in a case it stays still).
    property bool discPlaying: false
    property bool discPaused: false
    property bool discShown: false
    readonly property alias disc: disc

    readonly property real chamfer: Math.round(width * 0.08)
    readonly property color shellColor: mix ? Qt.rgba(Theme.mdBlankColor(caseId).r, Theme.mdBlankColor(caseId).g,
                                                       Theme.mdBlankColor(caseId).b, 0.35)
                                            : Theme.mdShellFill

    Shape { // the shell, its bottom-left corner cut off
        anchors.fill: parent
        preferredRendererType: Shape.CurveRenderer
        ShapePath {
            fillColor: root.shellColor
            strokeColor: Theme.mdShellBorder
            strokeWidth: 1.2
            joinStyle: ShapePath.MiterJoin
            startX: 0
            startY: 0
            PathLine { x: root.width; y: 0 }
            PathLine { x: root.width; y: root.height }
            PathLine { x: root.chamfer; y: root.height }
            PathLine { x: 0; y: root.height - root.chamfer }
            PathLine { x: 0; y: 0 }
        }
    }

    SpinningDisc { // seen through the shell; V3 turns it
        id: disc
        objectName: "cartridgeDisc"
        width: Math.round(root.width * 0.84)
        height: width
        x: Math.round((root.width - width) / 2)
        y: Math.round(root.height * 0.56 - height / 2)
        opacity: 0.92
        interactive: false
        playing: root.discPlaying
        paused: root.discPaused
        shown: root.discShown
        coverUrl: root.coverUrl.startsWith("image://cover/") ? "image://coverround/" + root.coverUrl.substring(14) : ""
        coverKey: SpineColors.key(root.coverUrl)
    }

    Rectangle { // the label (front)
        id: label
        objectName: "cartridgeLabel"
        visible: !root.showingBack
        x: Math.round(root.width * 0.05)
        y: Math.round(root.height * 0.05)
        width: Math.round(root.width * 0.52)
        height: Math.round(root.height * 0.36)
        radius: 2
        color: root.mix ? Theme.mdLabelPaper : Theme.surfaceElevated
        border.color: Theme.mdShellBorder
        border.width: 1
        clip: true

        CoverArt {
            anchors.fill: parent
            anchors.margins: 1
            visible: !root.mix && root.coverUrl !== ""
            source: root.mix ? "" : root.coverUrl
            showPlaceholder: false
        }
        Label {
            anchors.fill: parent
            anchors.margins: 4
            visible: root.mix || root.coverUrl === ""
            text: root.title
            wrapMode: Text.Wrap
            elide: Text.ElideRight
            maximumLineCount: 3
            font.pixelSize: Theme.fontSmall
            font.italic: root.mix
            color: root.mix ? Theme.mdLabelInk : Theme.textSecondary
        }
    }

    Rectangle { // the shutter: on the right in front, on the left from the back
        objectName: "cartridgeShutter"
        x: root.showingBack ? Math.round(root.width * 0.06) : Math.round(root.width * 0.62)
        y: Math.round(root.height * 0.03)
        width: Math.round(root.width * 0.32)
        height: Math.round(root.height * 0.5)
        radius: 1
        gradient: Gradient {
            GradientStop { position: 0.0; color: Theme.mdShutterLight }
            GradientStop { position: 0.55; color: Theme.mdShutterMid }
            GradientStop { position: 1.0; color: Theme.mdShutterDark }
        }

        Rectangle { // its slot
            anchors.horizontalCenter: parent.horizontalCenter
            y: Math.round(parent.height * 0.18)
            width: Math.max(2, Math.round(parent.width * 0.12))
            height: Math.round(parent.height * 0.64)
            radius: width / 2
            color: Theme.mdShutterSlot
        }
    }
}
