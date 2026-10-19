import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Dialogs
import QtQuick.Layouts

// Material (EdMaterial.jsx): el del proyecto, la biblioteca global y los SFX, con vídeos, imágenes y
// audios. Cada tarjeta se arrastra a la timeline (DropArea con la clave «material»).
Rectangle {
    id: panel
    required property var model
    required property Item dragLayer
    property var sfxModal: null
    property string filter: "all"
    property string source: "project"
    property string query: ""
    property string sfxCategory: ""
    property string collection: ""
    property bool manage: false
    property string fxCategory: ""
    function words(text) { return text.toLowerCase().split(/\s+/).filter((w) => w) }

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
                model: [["project", "Proyecto"], ["library", "Biblio."], ["collection", "Colecc."], ["sfx", "SFX"], ["fx", "Efectos"]]
                TabButton {
                    required property var modelData
                    objectName: "material:source:" + modelData[0]
                    text: modelData[1]
                    font.pixelSize: 11
                    checked: panel.source === modelData[0]
                    onClicked: panel.source = modelData[0]
                    contentItem: Label { text: parent.text; font: parent.font; color: parent.checked ? Theme.text : Theme.muted; horizontalAlignment: Text.AlignHCenter }
                    background: Rectangle { color: "transparent"; Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 2; color: parent.parent.checked ? Theme.accent : "transparent" } }
                }
            }
        }
        Row {
            visible: panel.source === "project" || panel.source === "library"
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
        TextField {
            objectName: "material:search"
            Layout.fillWidth: true
            placeholderText: panel.source === "sfx" ? "Buscar sonidos…" : panel.source === "fx" ? "Buscar: glitch, papel, temblor…" : "Buscar…"
            color: Theme.text
            background: Rectangle { radius: Theme.radiusSm; color: Theme.card2; border.color: Theme.lineBright }
            onTextChanged: {
                panel.query = text
                if (panel.source === "sfx")
                    panel.model.searchSfx(text, panel.sfxCategory)
                else if (panel.source === "collection")
                    panel.model.searchCollections(text, panel.collection)
            }
        }
        SmallButton {
            objectName: "sfx:new"
            visible: panel.source === "sfx" && !!panel.sfxModal
            text: "＋ Nuevo SFX"
            onClicked: panel.sfxModal.openNew(panel.sfxCategory)
        }
        Flow {
            visible: panel.source === "sfx" && panel.model.sfxCategories.length > 0
            Layout.fillWidth: true
            spacing: 4
            Repeater {
                model: [{ id: "", label: "Todo" }].concat(panel.model.sfxCategories)
                Rectangle {
                    required property var modelData
                    readonly property bool on: panel.sfxCategory === modelData.id
                    width: catLabel.implicitWidth + 14
                    height: 22
                    radius: 11
                    color: on ? Qt.rgba(1, 0.23, 0.36, 0.25) : Theme.card2
                    border.color: on ? Theme.accent : Theme.lineBright
                    Label { id: catLabel; anchors.centerIn: parent; text: parent.modelData.label + (parent.modelData.count ? " " + parent.modelData.count : ""); color: Theme.text; font.pixelSize: 11 }
                    TapHandler {
                        onTapped: {
                            panel.sfxCategory = parent.modelData.id
                            panel.model.searchSfx(panel.query, panel.sfxCategory)
                        }
                    }
                }
            }
        }
        RowLayout {
            visible: panel.source === "collection"
            SmallButton {
                objectName: "col:manage"
                text: "Gestionar carpetas"
                checkable: true
                checked: panel.manage
                onToggled: panel.manage = checked
            }
            Item { Layout.fillWidth: true }
            SmallButton {
                visible: !!panel.collection
                text: "Subir"
                onClicked: uploadDialog.open()
            }
        }
        CollectionsManager {
            visible: panel.source === "collection" && panel.manage
            Layout.fillWidth: true
            model: panel.model
            onCreated: (id) => {
                panel.collection = id
                panel.model.searchCollections(panel.query, id)
            }
        }
        FileDialog {
            id: uploadDialog
            title: "Subir a la carpeta"
            fileMode: FileDialog.OpenFiles
            nameFilters: ["Medios (*.mp4 *.mov *.webm *.mkv *.png *.jpg *.jpeg *.webp *.gif *.mp3 *.wav *.ogg *.m4a)", "Todos (*)"]
            onAccepted: panel.model.addCollectionFiles(panel.collection, selectedFiles)
        }
        Flow {
            visible: panel.source === "collection" && panel.model.collections.length > 0
            Layout.fillWidth: true
            spacing: 4
            Repeater {
                model: [{ id: "", name: "Todas", count: 0 }].concat(panel.model.collections.filter((c) => c.enabled))
                Rectangle {
                    required property var modelData
                    readonly property bool on: panel.collection === modelData.id
                    width: colLabel.implicitWidth + 14
                    height: 22
                    radius: 11
                    color: on ? Qt.rgba(1, 0.23, 0.36, 0.25) : Theme.card2
                    border.color: on ? Theme.accent : Theme.lineBright
                    Label { id: colLabel; anchors.centerIn: parent; text: parent.modelData.name + (parent.modelData.count ? " " + parent.modelData.count : ""); color: Theme.text; font.pixelSize: 11 }
                    TapHandler {
                        onTapped: {
                            panel.collection = parent.modelData.id
                            panel.model.searchCollections(panel.query, panel.collection)
                        }
                    }
                }
            }
        }
        Label {
            visible: list.count === 0 && panel.source !== "fx"
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            text: panel.source === "library" ? "La biblioteca está vacía: guarda material de un proyecto para usarlo en todos."
                : panel.source === "sfx" ? "No hay sonidos: elige la carpeta de SFX en los ajustes."
                : panel.source === "collection" ? "No hay colecciones: crea carpetas en la de material (assets/material o la de los ajustes)."
                : "Este proyecto no tiene material todavía."
            color: Theme.muted
        }
        Flow {
            visible: panel.source === "fx"
            Layout.fillWidth: true
            spacing: 4
            Repeater {
                model: [{ id: "", label: "Todos" }].concat(panel.model.vfxCategories)
                Rectangle {
                    required property var modelData
                    readonly property bool on: panel.fxCategory === modelData.id
                    objectName: "fx:cat:" + modelData.id
                    width: fxCatLabel.implicitWidth + 14
                    height: 22
                    radius: 11
                    color: on ? Qt.rgba(1, 0.23, 0.36, 0.25) : Theme.card2
                    border.color: on ? Theme.accent : Theme.lineBright
                    Label { id: fxCatLabel; anchors.centerIn: parent; text: parent.modelData.label; color: Theme.text; font.pixelSize: 11 }
                    TapHandler { onTapped: panel.fxCategory = parent.modelData.id }
                }
            }
        }
        GridView {
            id: fxGrid
            visible: panel.source === "fx"
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            cellWidth: width / 2
            cellHeight: 52
            readonly property var active: (panel.model.selectedClip.vfx || []).map((e) => e.effect)
            model: panel.source === "fx" ? panel.model.searchVfx(panel.query, panel.fxCategory) : []
            delegate: Rectangle {
                required property var modelData
                objectName: "fx:" + modelData.id
                width: fxGrid.cellWidth - 4
                height: fxGrid.cellHeight - 4
                radius: Theme.radiusSm
                color: Theme.card2
                border.color: fxGrid.active.indexOf(modelData.id) >= 0 ? Theme.accent : fxHover.hovered ? Theme.lineBright : Theme.line
                ToolTip.visible: fxHover.hovered
                ToolTip.text: modelData.description + "\nClic: añadir al clip seleccionado"
                ToolTip.delay: 600
                HoverHandler { id: fxHover }
                TapHandler { onTapped: panel.model.addVfx(parent.modelData.id) }
                Column {
                    anchors.fill: parent
                    anchors.margins: 6
                    Label { width: parent.width; text: modelData.name; color: Theme.text; font.pixelSize: 12; elide: Text.ElideRight }
                    Label { width: parent.width; text: modelData.category; color: Theme.muted; font.pixelSize: 10; elide: Text.ElideRight }
                }
            }
            Label {
                visible: fxGrid.count === 0
                text: "Ningún efecto coincide."
                color: Theme.muted
            }
        }
        ListView {
            id: list
            visible: panel.source !== "fx"
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            spacing: 6
            model: panel.model.materials.filter((m) => m.source === panel.source
                && (panel.source === "sfx" || panel.filter === "all" || m.kind === panel.filter)
                && (panel.source === "sfx" || panel.source === "collection" || panel.words(panel.query).every((w) => m.name.toLowerCase().includes(w))))
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
                        ToolButton {
                            objectName: "sfx:edit:" + slot.modelData.key
                            visible: slot.modelData.source === "sfx" && !!panel.sfxModal
                            implicitWidth: 22
                            implicitHeight: 22
                            text: "✎"
                            palette.buttonText: Theme.muted
                            background: null
                            onClicked: panel.sfxModal.openEdit(slot.modelData.key)
                        }
                    }
                }
            }
        }
    }
}
