import QtQuick
import QtQuick.Controls.Basic

Button {
    id: small
    font.pixelSize: 11
    padding: 4
    contentItem: Label { text: small.text; font: small.font; color: small.enabled ? Theme.text : Theme.muted; horizontalAlignment: Text.AlignHCenter }
    background: Rectangle { radius: Theme.radiusSm; color: small.down || small.checked ? Theme.card3 : Theme.card2; border.color: Theme.lineBright }
}
