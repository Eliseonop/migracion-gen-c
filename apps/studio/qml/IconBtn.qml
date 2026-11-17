import QtQuick
import QtQuick.Controls.Basic

// .icon-btn de video-yt: solo el icono, gris; al pasar por encima, blanco sobre un fondo tenue. `big`
// (el ▶ del reproductor): blanco, y al pasar por encima, del color de acento.
AbstractButton {
    id: btn
    property string iconName
    property int iconSize: 18
    property string tip
    property bool big: false
    property bool on: false
    property color tint: Theme.muted
    implicitWidth: iconSize + (big ? 8 : 12)
    implicitHeight: iconSize + (big ? 8 : 12)
    opacity: enabled ? 1 : 0.28
    hoverEnabled: true
    ToolTip.visible: hovered && !!tip
    ToolTip.delay: 500
    ToolTip.text: tip
    contentItem: MIcon {
        name: btn.iconName
        size: btn.iconSize
        color: btn.big ? (btn.hovered ? Theme.accent : Theme.text)
                       : btn.on ? Theme.accent : (btn.hovered ? Theme.text : btn.tint)
        scale: btn.big && btn.hovered ? 1.1 : 1
    }
    background: Rectangle {
        radius: 8
        color: !btn.big && (btn.hovered || btn.on) ? Qt.rgba(1, 1, 1, btn.on ? 0.06 : 0.08) : "transparent"
    }
}
