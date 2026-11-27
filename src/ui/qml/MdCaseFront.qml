// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// The front of a MiniDisc case taken out of the pile (V2): the insert behind the clear sleeve.
// An album's is its cover, full bleed, with the title, artist and year on a scrim (no art: the
// case colour and a note); a mix disc's is a ruled paper card in its blank colour with the
// playlist's name. A fixed-size item: the case is turned and flipped by scaling it.
Item {
    id: root

    property bool mix: false
    property int caseId: 0
    property string title
    property string artist
    property int year: 0
    property int trackCount: 0
    property string coverUrl
    property color caseColor: Theme.spineDefault

    Rectangle { // the clear sleeve
        anchors.fill: parent
        radius: 3
        color: Theme.mdCasePlastic
        border.color: Theme.mdCaseBorder
        border.width: 1
    }

    Item {
        id: insert
        anchors.fill: parent
        anchors.margins: 5
        clip: true

        // ---- album ----
        Rectangle {
            anchors.fill: parent
            visible: !root.mix
            color: root.caseColor

            Label {
                anchors.centerIn: parent
                anchors.verticalCenterOffset: -parent.height * 0.12
                visible: !cover.ready
                text: "♪"
                font.pixelSize: Math.round(parent.height * 0.3)
                color: SpineColors.prefersDarkText(root.caseColor) ? Theme.spineInkDark : Theme.spineInkLight
                opacity: 0.5
            }
        }
        CoverArt {
            id: cover
            objectName: "frontCover"
            anchors.fill: parent
            visible: !root.mix && root.coverUrl !== ""
            source: root.mix ? "" : root.coverUrl
            showPlaceholder: false
        }
        Rectangle { // the caption's scrim
            visible: !root.mix
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: caption.implicitHeight + 2 * Theme.spaceMD
            gradient: Gradient {
                GradientStop { position: 0.0; color: "transparent" }
                GradientStop { position: 0.45; color: Theme.mdInsertScrim }
                GradientStop { position: 1.0; color: Theme.mdInsertScrim }
            }
        }
        ColumnLayout {
            id: caption
            visible: !root.mix
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.margins: Theme.spaceSM
            spacing: 0

            Label {
                objectName: "frontTitle"
                Layout.fillWidth: true
                text: root.title
                elide: Text.ElideRight
                font.bold: true
                font.pixelSize: Theme.fontHeading
                color: Theme.textPrimary
            }
            Label {
                Layout.fillWidth: true
                visible: text !== ""
                text: root.year > 0 ? (root.artist !== "" ? root.artist + " · " + root.year : String(root.year)) : root.artist
                elide: Text.ElideRight
                font.pixelSize: Theme.fontSmall
                color: Theme.textSecondary
            }
        }

        // ---- mix disc: a ruled paper card ----
        Rectangle {
            id: paper
            visible: root.mix
            anchors.fill: parent
            color: Theme.mdLabelPaper

            Rectangle { // header bar in the blank's colour
                id: header
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                height: Math.round(parent.height * 0.16)
                color: Theme.mdBlankColor(root.caseId)
            }
            Repeater {
                model: Math.max(0, Math.floor((paper.height - header.height) / 18))
                Rectangle {
                    required property int index
                    x: Theme.spaceSM
                    width: paper.width - 2 * Theme.spaceSM
                    y: header.height + 18 * (index + 1)
                    height: 1
                    color: Theme.mdLabelRule
                }
            }
            ColumnLayout {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: header.bottom
                anchors.margins: Theme.spaceMD
                spacing: Theme.spaceXS

                Label {
                    objectName: "frontMixTitle"
                    Layout.fillWidth: true
                    text: root.title
                    wrapMode: Text.Wrap
                    maximumLineCount: 3
                    elide: Text.ElideRight
                    font.italic: true
                    font.pixelSize: Theme.fontTitle
                    color: Theme.mdLabelInk
                }
                Label {
                    Layout.fillWidth: true
                    text: root.trackCount === 1 ? qsTr("1 track") : qsTr("%1 tracks").arg(root.trackCount)
                    font.italic: true
                    font.pixelSize: Theme.fontSmall
                    color: Theme.mdLabelInk
                }
            }
        }
    }

    Rectangle { // the sleeve's sheen
        anchors.fill: parent
        radius: 3
        gradient: Gradient {
            GradientStop { position: 0.0; color: Theme.mdCaseSheen }
            GradientStop { position: 0.3; color: "transparent" }
        }
    }
    Rectangle { // the thumb notch at the open top edge
        anchors.horizontalCenter: parent.horizontalCenter
        y: -height / 2
        width: Math.round(parent.width * 0.2)
        height: 10
        radius: 5
        color: Theme.mdFaceShade
        opacity: 0.55
    }
}
