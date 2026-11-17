import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// .primary.small de video-yt: el degradado de acento, texto blanco en negrita (el botón Exportar).
AbstractButton {
    id: btn
    property string iconName
    property string tip
    hoverEnabled: true
    opacity: enabled ? 1 : 0.5
    leftPadding: 12
    rightPadding: 12
    topPadding: 6
    bottomPadding: 6
    font.pixelSize: 13 // 12,5 px en React (pixelSize solo admite enteros)
    font.weight: Font.ExtraBold
    ToolTip.visible: hovered && !!tip
    ToolTip.delay: 500
    ToolTip.text: tip
    contentItem: RowLayout {
        spacing: 8
        MIcon { visible: !!btn.iconName; name: btn.iconName; size: 15; color: "white" }
        Label { text: btn.text; font: btn.font; color: "white" }
    }
    background: Rectangle {
        radius: Theme.radiusSm
        border.color: Qt.rgba(1, 1, 1, 0.15)
        gradient: Gradient {
            orientation: Gradient.Horizontal
            GradientStop { position: 0; color: Theme.accent }
            GradientStop { position: 1; color: Theme.accent2 }
        }
        Rectangle {
            anchors.fill: parent
            radius: parent.radius
            color: "white"
            opacity: btn.hovered && btn.enabled ? 0.06 : 0
        }
    }
}
