import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// «Agregar Stick» desde la timeline (EdStickMenu.jsx): personaje → vídeo o imagen → expresiones. Al elegir
// una, el clip entra en el hueco (o en la pista de vídeo) con el croma del personaje ya activado.
Popup {
    id: menu
    objectName: "stickMenu"
    required property var model
    property real time
    property string trackId
    property string stickId: ""
    property string kind: ""
    property var detail: null
    readonly property var kindLabel: ({ video: "Vídeo", image: "Imagen" })

    function openAt(t, track) {
        time = t
        trackId = track
        const list = model.sticks
        pick(list.length ? list[0].id : "")
        open()
    }
    function pick(id) { // el tipo con más recursos: vídeo si los hay
        stickId = id
        detail = id ? model.stickMenu(id) : null
        kind = detail && detail.kinds ? (detail.kinds.video ? "video" : "image") : ""
    }

    anchors.centerIn: parent
    width: Math.min(parent.width - 40, 600)
    height: Math.min(parent.height - 40, 440)
    modal: true
    focus: true
    padding: 10
    background: Rectangle { radius: Theme.radiusLg; color: Theme.card; border.color: Theme.lineBright }

    component Row_: AbstractButton {
        id: rowBtn
        property bool on: false
        property string emoji
        property string label
        property string count
        Layout.fillWidth: true
        implicitHeight: 30
        hoverEnabled: true
        contentItem: RowLayout {
            spacing: 6
            Label { text: rowBtn.emoji; visible: !!rowBtn.emoji }
            Label { Layout.fillWidth: true; text: rowBtn.label; color: Theme.text; font.pixelSize: 12; elide: Text.ElideRight }
            Label { text: rowBtn.count; color: Theme.muted; font.pixelSize: 11 }
        }
        background: Rectangle { radius: Theme.radiusSm; color: rowBtn.on ? Qt.rgba(1, 0.23, 0.36, 0.2) : rowBtn.hovered ? Theme.card3 : "transparent" }
    }

    contentItem: RowLayout {
        spacing: 10
        ColumnLayout {
            Layout.preferredWidth: 150
            Layout.fillHeight: true
            Label { text: "STICK"; color: Theme.muted; font.pixelSize: 11; font.bold: true }
            Repeater {
                model: menu.model.sticks
                Row_ {
                    required property var modelData
                    objectName: "stickMenu:stick:" + modelData.id
                    on: menu.stickId === modelData.id
                    emoji: modelData.emoji || "🎭"
                    label: modelData.name
                    count: modelData.count
                    onClicked: menu.pick(modelData.id)
                }
            }
            Label {
                visible: !menu.model.sticks.length
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                text: "No hay sticks. Crea uno en Biblioteca → «Nuevo stick»."
                color: Theme.muted
                font.pixelSize: 11
            }
            Item { Layout.fillHeight: true }
        }
        ColumnLayout {
            visible: !!menu.detail
            Layout.preferredWidth: 110
            Layout.fillHeight: true
            Label { text: "TIPO"; color: Theme.muted; font.pixelSize: 11; font.bold: true }
            Repeater {
                model: menu.detail && menu.detail.kinds ? ["video", "image"].filter((k) => menu.detail.kinds[k] > 0) : []
                Row_ {
                    required property string modelData
                    objectName: "stickMenu:kind:" + modelData
                    on: menu.kind === modelData
                    label: menu.kindLabel[modelData]
                    count: menu.detail.kinds[modelData]
                    onClicked: menu.kind = modelData
                }
            }
            Item { Layout.fillHeight: true }
        }
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Label {
                text: menu.detail ? menu.detail.name + " · " + (menu.kindLabel[menu.kind] || "") : "Expresiones"
                color: Theme.muted
                font.pixelSize: 11
                font.bold: true
            }
            Flickable {
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                contentHeight: gallery.implicitHeight
                Column {
                    id: gallery
                    width: parent.width
                    spacing: 8
                    Label {
                        visible: !!menu.detail && !(menu.detail[menu.kind] || []).length
                        text: "Sin " + (menu.kindLabel[menu.kind] || "recursos").toLowerCase() + " en este stick."
                        color: Theme.muted
                    }
                    Repeater {
                        model: menu.detail ? menu.detail[menu.kind] || [] : []
                        Column {
                            id: group
                            required property var modelData
                            width: gallery.width
                            spacing: 4
                            Label { text: group.modelData.emoji + " " + group.modelData.label + "  " + group.modelData.items.length; color: Theme.text; font.pixelSize: 12 }
                            Flow {
                                width: parent.width
                                spacing: 4
                                Repeater {
                                    model: group.modelData.items
                                    AbstractButton {
                                        id: card
                                        required property var modelData
                                        objectName: "stickMenu:card:" + modelData.rel
                                        width: 92
                                        height: 84
                                        hoverEnabled: true
                                        ToolTip.visible: hovered
                                        ToolTip.text: modelData.title
                                        onClicked: {
                                            menu.model.addStickClip(card.modelData.key, menu.time, menu.trackId)
                                            menu.close()
                                        }
                                        contentItem: Column {
                                            spacing: 2
                                            Item {
                                                width: card.width
                                                height: 62
                                                Image {
                                                    anchors.fill: parent
                                                    visible: !!card.modelData.thumb
                                                    source: card.modelData.thumb
                                                    fillMode: Image.PreserveAspectFit
                                                    sourceSize.width: 184
                                                    asynchronous: true
                                                }
                                                MIcon { anchors.centerIn: parent; visible: !card.modelData.thumb; name: "movie"; size: 24; color: Theme.textSecondary }
                                            }
                                            Label { width: card.width; text: card.modelData.title; elide: Text.ElideRight; color: Theme.textSecondary; font.pixelSize: 10; horizontalAlignment: Text.AlignHCenter }
                                        }
                                        background: Rectangle { radius: Theme.radiusSm; color: card.hovered ? Theme.card3 : Theme.card2; border.color: card.hovered ? Theme.accent : Theme.line }
                                    }
                                }
                            }
                        }
                    }
                }
            }
            Label { text: "✨ Se agrega con fondo por croma ya activado"; color: Theme.muted; font.pixelSize: 10 }
        }
    }
}
