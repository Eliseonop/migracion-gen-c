import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// Material (EdMaterial.jsx): el del proyecto, la biblioteca global y los SFX, con vídeos, imágenes y
// audios. Cada tarjeta se arrastra a la timeline (DropArea con la clave «material»).
Rectangle {
    id: panel
    required property var model
    required property Item dragLayer
    property string filter: "all"
    property string source: "project"

    radius: Theme.radiusLg
    color: Theme.card
    border.color: Theme.line

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 12
        spacing: 8

        Label { text: "Material"; color: Theme.text; font.pixelSize: 15; font.bold: true }
        TabBar {
            Layout.fillWidth: true
            background: Rectangle { color: "transparent" }
            Repeater {
                model: [["project", "Proyecto"], ["library", "Biblioteca"], ["sfx", "SFX"]]
                TabButton {
                    required property var modelData
                    objectName: "material:source:" + modelData[0]
                    text: modelData[1]
                    font.pixelSize: 12
                    checked: panel.source === modelData[0]
                    onClicked: panel.source = modelData[0]
                    contentItem: Label { text: parent.text; font: parent.font; color: parent.checked ? Theme.text : Theme.muted; horizontalAlignment: Text.AlignHCenter }
                    background: Rectangle { color: "transparent"; Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 2; color: parent.parent.checked ? Theme.accent : "transparent" } }
                }
            }
        }
        Row {
            visible: panel.source !== "sfx"
            spacing: 4
            Repeater {
                model: [["all", "Todo"], ["video", "Vídeos"], ["image", "Imágenes"], ["audio", "Audios"]]
                delegate: Button {
                    id: tab
                    required property var modelData
                    text: modelData[1]
                    flat: true
                    font.pixelSize: 12
                    contentItem: Label {
                        text: tab.text
                        font: tab.font
                        color: panel.filter === tab.modelData[0] ? "white" : Theme.textSecondary
                    }
                    background: Rectangle {
                        radius: Theme.radiusSm
                        color: panel.filter === parent.modelData[0] ? Qt.rgba(1, 0.23, 0.36, 0.25) : "transparent"
                    }
                    onClicked: panel.filter = modelData[0]
                }
            }
        }
        Label {
            visible: list.count === 0
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            text: panel.source === "library" ? "La biblioteca está vacía: guarda material de un proyecto para usarlo en todos."
                : panel.source === "sfx" ? "No hay sonidos: elige la carpeta de SFX en los ajustes."
                : "Este proyecto no tiene material todavía."
            color: Theme.muted
        }
        ListView {
            id: list
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            spacing: 6
            model: panel.model.materials.filter((m) => m.source === panel.source && (panel.source === "sfx" || panel.filter === "all" || m.kind === panel.filter))
            delegate: Item {
                id: slot
                required property var modelData
                width: ListView.view.width
                height: 44

                Rectangle {
                    id: card
                    readonly property string materialKey: slot.modelData.key
                    property bool dragging
                    objectName: "material:" + slot.modelData.key
                    width: slot.width
                    height: slot.height
                    radius: Theme.radiusSm
                    color: Theme.card2
                    border.color: hover.hovered ? Theme.accent : Theme.line
                    opacity: dragging ? 0.75 : 1
                    Drag.active: dragging
                    Drag.keys: ["material"]

                    HoverHandler { id: hover }
                    DragHandler {
                        id: drag
                        onActiveChanged: {
                            if (active) {
                                card.Drag.hotSpot = drag.centroid.pressPosition // el clip empieza bajo el puntero
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
                        ParentChange { target: card; parent: panel.dragLayer }
                    }

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 10
                        anchors.rightMargin: 10
                        spacing: 8
                        Label {
                            text: slot.modelData.kind === "audio" ? "♪" : slot.modelData.kind === "image" ? "▨" : "▶"
                            color: slot.modelData.kind === "audio" ? "#818cf8" : slot.modelData.kind === "image" ? "#5eead4" : "#ff7a93"
                        }
                        Label {
                            Layout.fillWidth: true
                            text: slot.modelData.name
                            elide: Text.ElideRight
                            color: Theme.text
                            font.pixelSize: 12
                        }
                        Label {
                            text: panel.model.formatTime(slot.modelData.duration)
                            color: Theme.muted
                            font.pixelSize: 11
                        }
                    }
                }
            }
        }
    }
}
