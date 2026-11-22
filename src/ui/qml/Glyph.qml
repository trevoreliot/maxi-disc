// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Shapes

// Small vector icons for the transport controls, drawn on a 24×24 grid and scaled to `size`.
// Vector rather than Unicode so they're centred and crisp whatever fonts are installed.
Item {
    id: root

    // "play", "pause", "stop", "prev", "next" (filled) or "repeat", "shuffle" (stroked)
    property string name: "play"
    property color color: Theme.textPrimary
    property real size: 24

    readonly property bool stroked: name === "repeat" || name === "shuffle"

    implicitWidth: size
    implicitHeight: size

    Shape {
        width: 24
        height: 24
        anchors.centerIn: parent
        scale: root.size / 24
        preferredRendererType: Shape.CurveRenderer

        ShapePath {
            fillColor: root.stroked ? "transparent" : root.color
            strokeColor: root.stroked ? root.color : "transparent"
            strokeWidth: root.stroked ? 2 : 0
            capStyle: ShapePath.RoundCap
            joinStyle: ShapePath.RoundJoin

            PathSvg {
                path: {
                    switch (root.name) {
                    case "play":
                        return "M8 5 L19 12 L8 19 Z";
                    case "pause":
                        return "M7 5 H10.5 V19 H7 Z M13.5 5 H17 V19 H13.5 Z";
                    case "stop":
                        return "M7 7 H17 V17 H7 Z";
                    case "prev":
                        return "M6 6 H8 V18 H6 Z M18 6 L9 12 L18 18 Z";
                    case "next":
                        return "M16 6 H18 V18 H16 Z M6 6 L15 12 L6 18 Z";
                    case "repeat":
                        return "M4 11 V9 A3 3 0 0 1 7 6 H18 M15 3 L18 6 L15 9 M20 13 V15 A3 3 0 0 1 17 18 H6 M9 21 L6 18 L9 15";
                    case "shuffle":
                        return "M3 7 H7 L16 17 H20 M17 14 L20 17 L17 20 M3 17 H7 L10 13.5 M13 10.5 L16 7 H20 M17 4 L20 7 L17 10";
                    }
                    return "";
                }
            }
        }
    }
}
