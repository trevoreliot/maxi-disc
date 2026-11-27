// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick

// The spine of a MiniDisc case (V1 / V2): the case colour under a gradient that makes it look
// rounded, clear plastic at both ends with their hinges, the title (and the artist or track count)
// printed along it; a mix disc has a paper label. Drawn the same in the pile (MdCase) and by the
// case being taken out (MdCaseStage), so the hand-over between the two is exact. Rectangles and
// Text only: the offscreen software renderer draws it too.
Rectangle {
    id: root

    property bool mix: false
    property string label
    property string detail
    property color caseColor: Theme.spineDefault
    readonly property color ink: SpineColors.prefersDarkText(caseColor) ? Theme.spineInkDark : Theme.spineInkLight

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
        width: root.plastic
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
        width: root.plastic
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
