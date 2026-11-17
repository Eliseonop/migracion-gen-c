pragma Singleton
import QtQuick

QtObject {
    readonly property color bg: "#090b11"
    readonly property color card: "#141724"
    readonly property color card2: "#1b2032"
    readonly property color card3: "#242b42"
    readonly property color line: Qt.rgba(1, 1, 1, 0.08)
    readonly property color lineBright: Qt.rgba(1, 1, 1, 0.16)
    readonly property color text: "#f1f5f9"
    readonly property color textSecondary: "#cbd5e1"
    readonly property color muted: "#94a3b8"
    readonly property color accent: "#ff3b5c"
    readonly property color accent2: "#ff6b3d"
    readonly property color folder: "#fbbf24"
    readonly property color danger: "#ef4444"
    readonly property color success: "#22c55e"
    readonly property color warning: "#fbbf24"
    // Los paneles del editor (editor.css): barra superior e inspector, columna central y sus barras.
    readonly property color topbar: "#10131c"
    readonly property color stage: "#07080c"
    readonly property color panelBar: "#0e1118"
    // Material Icons (la fuente de iconos de video-yt, Apache-2.0): el nombre del icono es el texto.
    readonly property FontLoader iconLoader: FontLoader { source: "qrc:/fonts/MaterialIcons-Regular.ttf" }
    readonly property string iconFamily: iconLoader.status === FontLoader.Ready ? iconLoader.font.family : "Material Icons"
    readonly property int radiusLg: 16
    readonly property int radiusMd: 10
    readonly property int radiusSm: 8
}
