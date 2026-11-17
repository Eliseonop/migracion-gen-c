import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// .ghost.small de video-yt: fondo de tarjeta con borde, icono opcional y texto. `danger`: en rojo.
AbstractButton {
    id: btn
    property string iconName
    property int iconSize: 15
    property string tip
    property bool danger: false
    property bool accent: false
    property bool on: false
    readonly property color ink: danger ? (hovered ? "#ffffff" : "#f87171")
                                        : accent || on ? Theme.accent : (hovered ? "#ffffff" : Theme.text)
    hoverEnabled: true
    opacity: enabled ? 1 : 0.4
    leftPadding: text ? 10 : 6
    rightPadding: text ? 10 : 6
    topPadding: 5
    bottomPadding: 5
    font.pixelSize: 12
    ToolTip.visible: hovered && !!tip
    ToolTip.delay: 500
    ToolTip.text: tip
    contentItem: RowLayout {
        spacing: 6
        MIcon { visible: !!btn.iconName && btn.iconName !== "@magnet"; name: btn.iconName; size: btn.iconSize; color: btn.ink }
        MagnetIcon { visible: btn.iconName === "@magnet"; size: btn.iconSize; color: btn.ink } // el imán no está en Material Icons
        Label { visible: !!btn.text; text: btn.text; font: btn.font; color: btn.ink }
    }
    background: Rectangle {
        radius: Theme.radiusSm
        color: btn.danger && btn.hovered ? Qt.rgba(0.94, 0.27, 0.27, 0.15) : btn.hovered ? Theme.card3 : Theme.card2
        border.color: btn.danger ? (btn.hovered ? Theme.danger : Qt.rgba(0.94, 0.27, 0.27, 0.3))
                                 : btn.on || btn.accent ? Theme.accent : btn.hovered ? Qt.rgba(1, 1, 1, 0.25) : Theme.lineBright
    }
}
