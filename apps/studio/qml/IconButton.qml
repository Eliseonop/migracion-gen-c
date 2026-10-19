import QtQuick
import QtQuick.Controls.Basic

ToolButton {
    id: button
    property string tip
    implicitWidth: 28
    implicitHeight: 28
    font.pixelSize: 14
    palette.buttonText: Theme.text
    ToolTip.visible: hovered && !!tip
    ToolTip.text: tip
    background: Rectangle { radius: 14; color: button.hovered ? Qt.rgba(1, 1, 1, 0.12) : Qt.rgba(0, 0, 0, 0.45) }
}
