// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Shapes

// Main-menu tile illustrations after the pencil drawings (docs/ui-draft/main-menu.jpeg),
// drawn on a 120×100 grid in theme colours and scaled to fit.
//   "library":    vinyl record, MiniDisc, stacked cases
//   "export":     folder → arrow → MiniDisc, plus USB and CD
//   "visualizer": placeholder outline (not designed yet)
Item {
    id: root

    property string kind: "library"
    property bool muted: false

    readonly property color ink: muted ? Theme.textMuted : Theme.textSecondary
    readonly property color accent: muted ? Theme.textMuted : Theme.accent

    implicitWidth: 132
    implicitHeight: 110

    function circle(cx: real, cy: real, r: real): string {
        return "M " + (cx - r) + " " + cy + " A " + r + " " + r + " 0 1 0 " + (cx + r) + " " + cy
             + " A " + r + " " + r + " 0 1 0 " + (cx - r) + " " + cy + " Z";
    }

    Shape {
        width: 120
        height: 100
        anchors.centerIn: parent
        scale: Math.min(root.width / 120, root.height / 100)
        preferredRendererType: Shape.CurveRenderer

        // ---- library ----
        ShapePath { // record
            fillColor: root.kind === "library" ? Theme.surfaceInset : "transparent"
            strokeColor: root.kind === "library" ? root.ink : "transparent"
            strokeWidth: 2
            PathSvg { path: root.circle(40, 44, 34) }
        }
        ShapePath { // grooves
            fillColor: "transparent"
            strokeColor: root.kind === "library" ? Theme.borderStrong : "transparent"
            strokeWidth: 1
            PathSvg { path: root.circle(40, 44, 27) + " " + root.circle(40, 44, 20) }
        }
        ShapePath { // label
            fillColor: root.kind === "library" ? root.accent : "transparent"
            strokeColor: "transparent"
            PathSvg { path: root.circle(40, 44, 10) }
        }
        ShapePath { // spindle hole
            fillColor: root.kind === "library" ? Theme.bg : "transparent"
            strokeColor: "transparent"
            PathSvg { path: root.circle(40, 44, 2.5) }
        }
        ShapePath { // MiniDisc body
            fillColor: root.kind === "library" ? Theme.surfaceElevated : "transparent"
            strokeColor: root.kind === "library" ? root.ink : "transparent"
            strokeWidth: 1.5
            joinStyle: ShapePath.RoundJoin
            PathSvg { path: "M82 6 H114 V38 H87 L82 33 Z" }
        }
        ShapePath { // MiniDisc shutter
            fillColor: root.kind === "library" ? Theme.borderStrong : "transparent"
            strokeColor: "transparent"
            PathSvg { path: "M90 6 H106 V20 H90 Z" }
        }
        ShapePath { // stacked cases
            fillColor: root.kind === "library" ? Theme.surfaceElevated : "transparent"
            strokeColor: root.kind === "library" ? root.ink : "transparent"
            strokeWidth: 1.5
            joinStyle: ShapePath.RoundJoin
            PathSvg { path: "M58 58 H112 V70 H58 Z M62 71 H116 V83 H62 Z M56 84 H112 V96 H56 Z" }
        }
        ShapePath { // case spines
            fillColor: "transparent"
            strokeColor: root.kind === "library" ? root.accent : "transparent"
            strokeWidth: 2
            capStyle: ShapePath.RoundCap
            PathSvg { path: "M66 64 H88 M70 77 H96 M64 90 H84" }
        }

        // ---- export ----
        ShapePath { // folder
            fillColor: root.kind === "export" ? Theme.surfaceElevated : "transparent"
            strokeColor: root.kind === "export" ? root.ink : "transparent"
            strokeWidth: 1.5
            joinStyle: ShapePath.RoundJoin
            PathSvg { path: "M30 6 H50 L56 12 H82 V34 H30 Z" }
        }
        ShapePath { // arrow
            fillColor: "transparent"
            strokeColor: root.kind === "export" ? root.accent : "transparent"
            strokeWidth: 4
            capStyle: ShapePath.RoundCap
            joinStyle: ShapePath.RoundJoin
            PathSvg { path: "M56 40 V58 M47 50 L56 59 L65 50" }
        }
        ShapePath { // MiniDisc body
            fillColor: root.kind === "export" ? Theme.surfaceElevated : "transparent"
            strokeColor: root.kind === "export" ? root.ink : "transparent"
            strokeWidth: 1.5
            joinStyle: ShapePath.RoundJoin
            PathSvg { path: "M39 64 H73 V98 H45 L39 92 Z" }
        }
        ShapePath { // MiniDisc shutter + hub
            fillColor: root.kind === "export" ? Theme.borderStrong : "transparent"
            strokeColor: "transparent"
            PathSvg { path: "M47 64 H65 V79 H47 Z " + root.circle(56, 89, 3) }
        }
        ShapePath { // USB symbol
            fillColor: "transparent"
            strokeColor: root.kind === "export" ? root.ink : "transparent"
            strokeWidth: 2
            capStyle: ShapePath.RoundCap
            joinStyle: ShapePath.RoundJoin
            PathSvg {
                path: "M16 97 V64 M16 86 L8 79 V73 M16 79 L24 72 V68 " + root.circle(8, 70, 3)
                    + " M21 62 H27 V68 H21 Z"
            }
        }
        ShapePath { // USB arrow head
            fillColor: root.kind === "export" ? root.ink : "transparent"
            strokeColor: "transparent"
            PathSvg { path: "M11 66 L16 58 L21 66 Z" }
        }
        ShapePath { // CD
            fillColor: root.kind === "export" ? Theme.surfaceInset : "transparent"
            strokeColor: root.kind === "export" ? root.ink : "transparent"
            strokeWidth: 1.5
            PathSvg { path: root.circle(100, 22, 15) + " " + root.circle(100, 22, 4) }
        }

        // ---- visualizer (placeholder) ----
        ShapePath {
            fillColor: "transparent"
            strokeColor: root.kind === "visualizer" ? root.ink : "transparent"
            strokeWidth: 1.5
            strokeStyle: ShapePath.DashLine
            dashPattern: [4, 3]
            PathSvg { path: "M22 10 H98 V90 H22 Z" }
        }
        ShapePath {
            fillColor: root.kind === "visualizer" ? root.ink : "transparent"
            strokeColor: "transparent"
            PathSvg { path: "M38 70 H46 V78 H38 Z M50 54 H58 V78 H50 Z M62 40 H70 V78 H62 Z M74 60 H82 V78 H74 Z" }
        }
    }
}
