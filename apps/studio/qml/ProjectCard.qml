import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// Tarjeta de proyecto: se arrastra a una carpeta (DropArea con la clave «project»).
Item {
    id: slot
    required property var project
    required property Item dragLayer
    property bool editing
    signal open
    signal renamed(string name)
    signal duplicated
    signal removed

    width: 220
    height: 190

    Rectangle {
        id: card
        readonly property string projectId: slot.project.id
        width: slot.width
        height: slot.height
        radius: Theme.radiusLg
        color: Theme.card
        border.color: hover.hovered && !card.dragging ? Theme.accent : Theme.line
        property bool dragging
        opacity: dragging ? 0.6 : 1
        Drag.active: dragging
        Drag.keys: ["project"]
        Drag.hotSpot: Qt.point(width / 2, height / 2)

        HoverHandler { id: hover }
        TapHandler { onTapped: if (!slot.editing) slot.open() }
        DragHandler {
            id: drag
            enabled: !slot.editing
            onActiveChanged: {
                if (active) {
                    card.dragging = true
                    return
                }
                card.Drag.drop()
                card.dragging = false
                card.x = 0
                card.y = 0
            }
        }
        states: State {
            when: card.dragging
            ParentChange { target: card; parent: slot.dragLayer }
        }

        ColumnLayout {
            anchors.fill: parent
            spacing: 0
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 124
                radius: Theme.radiusLg
                color: Theme.card2
                Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: Theme.radiusLg; color: Theme.card2 }
                Label { anchors.centerIn: parent; text: "🎬"; font.pixelSize: 34; color: Theme.muted }
            }
            ColumnLayout {
                Layout.fillWidth: true
                Layout.margins: 12
                spacing: 4
                Label {
                    Layout.fillWidth: true
                    visible: !slot.editing
                    text: slot.project.name
                    elide: Text.ElideRight
                    color: Theme.text
                    font.pixelSize: 15
                    font.bold: true
                    TapHandler { onDoubleTapped: slot.editing = true }
                }
                InlineEdit {
                    Layout.fillWidth: true
                    visible: slot.editing
                    active: slot.editing
                    initial: slot.project.name
                    onDone: (value) => { slot.editing = false; if (value !== null) slot.renamed(value) }
                }
                Label {
                    text: slot.project.clips + " clip(s) · " + slot.project.date
                    color: Theme.muted
                    font.pixelSize: 12
                }
            }
        }

        Row {
            anchors.top: parent.top
            anchors.right: parent.right
            anchors.margins: 8
            spacing: 6
            IconButton { text: "✎"; tip: "Renombrar proyecto"; onClicked: slot.editing = true }
            IconButton { text: "⧉"; tip: "Duplicar proyecto"; onClicked: slot.duplicated() }
            IconButton { text: "✕"; tip: "Eliminar proyecto"; onClicked: slot.removed() }
        }
    }
}
