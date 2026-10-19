import QtQuick
import QtQuick.Controls.Basic

// Enter o perder el foco confirma; Escape cancela (done(null)).
TextField {
    id: field
    property bool active
    property string initial
    property bool cancelled
    signal done(var value)

    color: Theme.text
    font.pixelSize: 14
    padding: 4
    background: Rectangle { color: Theme.card2; border.color: Theme.accent; radius: 6 }
    onActiveChanged: if (active) { cancelled = false; text = initial; forceActiveFocus(); selectAll() }
    Keys.onEscapePressed: { cancelled = true; focus = false }
    onAccepted: focus = false
    onActiveFocusChanged: if (!activeFocus && active) done(cancelled ? null : text)
}
