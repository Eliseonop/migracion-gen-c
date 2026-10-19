import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Rectangle {
    id: card
    required property var group
    signal open
    signal dropped(string projectId)

    width: 220
    height: 190
    radius: Theme.radiusLg
    color: Theme.card
    border.color: drop.containsDrag || hover.hovered ? Theme.accent : Theme.line

    HoverHandler { id: hover }
    TapHandler { onTapped: card.open() }
    DropArea {
        id: drop
        anchors.fill: parent
        keys: ["project"]
        onDropped: (e) => card.dropped(e.source.projectId)
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0
        Label {
            Layout.fillWidth: true
            Layout.preferredHeight: 124
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            text: drop.containsDrag ? "📂" : "📁"
            font.pixelSize: 52
            color: Theme.folder
        }
        ColumnLayout {
            Layout.margins: 12
            spacing: 4
            Label { text: card.group.name; color: Theme.text; font.pixelSize: 15; font.bold: true }
            Label { text: card.group.count + " proyecto(s)"; color: Theme.muted; font.pixelSize: 12 }
        }
    }
}
