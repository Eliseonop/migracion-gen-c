import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Rectangle {
    id: item
    property string groupId
    property string icon
    property string name
    property int count
    property bool on
    property bool editing
    property bool editable
    signal clicked
    signal renamed(string name)
    signal removed
    signal dropped(string projectId)

    implicitHeight: 38
    radius: Theme.radiusSm
    color: drop.containsDrag ? Qt.rgba(1, 0.23, 0.36, 0.22) : on ? Qt.rgba(1, 0.23, 0.36, 0.12)
         : hover.hovered ? Qt.rgba(1, 1, 1, 0.05) : "transparent"
    border.color: drop.containsDrag ? Theme.accent : "transparent"

    HoverHandler { id: hover }
    TapHandler { onTapped: if (!item.editing) item.clicked(); onDoubleTapped: if (item.editable) item.editing = true }
    DropArea {
        id: drop
        anchors.fill: parent
        keys: ["project"]
        onDropped: (e) => item.dropped(e.source.projectId)
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 10
        anchors.rightMargin: 10
        spacing: 10
        Label { text: item.icon; color: item.editable ? Theme.folder : item.on ? Theme.accent : Theme.muted }
        Label {
            Layout.fillWidth: true
            visible: !item.editing
            text: item.name
            elide: Text.ElideRight
            color: item.on ? "white" : Theme.textSecondary
            font.pixelSize: 14
        }
        InlineEdit {
            Layout.fillWidth: true
            visible: item.editing
            active: item.editing
            initial: item.name
            onDone: (value) => { item.editing = false; if (value !== null) item.renamed(value) }
        }
        Label { visible: !hover.hovered || !item.editable; text: item.count; color: Theme.muted; font.pixelSize: 12 }
        IconButton { visible: hover.hovered && item.editable && !item.editing; text: "✎"; tip: "Renombrar carpeta"; onClicked: item.editing = true }
        IconButton { visible: hover.hovered && item.editable && !item.editing; text: "🗑"; tip: "Eliminar carpeta"; onClicked: item.removed() }
    }
}
