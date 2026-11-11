import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Dialogs
import QtQuick.Layouts

// Material (EdMaterial.jsx): el del proyecto, la biblioteca global y los SFX, con vídeos, imágenes y
// audios. Cada tarjeta se arrastra a la timeline (DropArea con la clave «material»); los archivos del
// sistema se sueltan en el panel para importarlos.
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
    property string fileDrag: "" // lo que se arrastra desde el sistema (dropFileKind), o ""
    function words(text) { return text.toLowerCase().split(/\s+/).filter((w) => w) }

    radius: Theme.radiusLg
    color: Theme.card
    border.color: Theme.line

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 12
        spacing: 8

        Label { text: "Material"; color: Theme.text; font.pixelSize: 15; font.bold: true }
        Flow {
            Layout.fillWidth: true
            spacing: 2
            Repeater {
                model: [["project", "Proyecto"], ["library", "Biblioteca"], ["collection", "Colecciones"], ["sfx", "SFX"],
                        ["text", "Texto"], ["fx", "Efectos"], ["shapes", "Figuras"], ["narrator", "Cargar audio"], ["youtube", "YouTube"]]
                TabButton {
                    required property var modelData
                    objectName: "material:source:" + modelData[0]
                    text: modelData[1]
                    font.pixelSize: 12
                    implicitWidth: contentItem.implicitWidth + 14
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
            visible: ["shapes", "text", "narrator", "youtube"].indexOf(panel.source) < 0
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
        // La zona «Arrastra vídeo, imagen o audio, o elige un archivo» de EdMaterial.
        SmallButton {
            objectName: "material:import"
            visible: panel.source === "project"
            Layout.fillWidth: true
            text: "Importar vídeo, imagen o audio…"
            onClicked: importDialog.open()
        }
        FileDialog {
            id: importDialog
            title: "Importar al proyecto"
            fileMode: FileDialog.OpenFiles
            nameFilters: ["Medios (*.mp4 *.mov *.mkv *.webm *.avi *.m4v *.mp4v *.mpg *.mpeg *.wmv *.flv *.png *.jpg *.jpeg *.webp *.gif *.bmp *.tif *.tiff *.mp3 *.wav *.m4a *.aac *.ogg *.flac *.wma)",
                          "Todos (*)"]
            onAccepted: panel.model.importFiles(selectedFiles)
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
            visible: list.count === 0 && ["fx", "shapes", "text", "narrator", "youtube"].indexOf(panel.source) < 0
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
        Flickable {
            visible: panel.source === "text"
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            contentHeight: textColumn.implicitHeight
            Column {
                id: textColumn
                width: parent.width
                spacing: 6
                readonly property var textMaterials: panel.model.materials.filter((m) => m.source === "text")
                // EdFxLibrary (texto): sobre un texto (o su pista) el tema se aplica; si no, añade un texto con él.
                function pick(themeId) {
                    const c = panel.model.selectedClip
                    if (themeId && (c.kind === "text" || panel.model.selectedTrack.kind === "text"))
                        panel.model.applyTextTheme(themeId)
                    else
                        panel.model.addText(themeId)
                }
                Label { text: "Añadir texto"; color: Theme.muted; font.pixelSize: 11; font.bold: true }
                MaterialTile {
                    width: parent.width
                    height: 40
                    materialKey: "text:default"
                    dragLayer: panel.dragLayer
                    tip: "Arrástralo a la timeline, o clic para añadirlo en el cursor"
                    onTapped: textColumn.pick("")
                    Label { anchors.left: parent.left; anchors.leftMargin: 10; anchors.verticalCenter: parent.verticalCenter; text: "Texto predeterminado"; color: Theme.text }
                    Label { anchors.right: parent.right; anchors.rightMargin: 10; anchors.verticalCenter: parent.verticalCenter; text: "＋"; color: Theme.muted }
                }
                Label { text: "Temas"; color: Theme.muted; font.pixelSize: 11; font.bold: true }
                Flow {
                    width: parent.width
                    spacing: 4
                    Repeater {
                        model: panel.model.textThemes
                        MaterialTile {
                            id: themeTile
                            required property var modelData
                            width: 74
                            height: 54
                            materialKey: "text:" + modelData.id
                            dragLayer: panel.dragLayer
                            tip: modelData.name
                            onTapped: textColumn.pick(modelData.id)
                            Rectangle {
                                x: 6; y: 6
                                width: parent.width - 12
                                height: 26
                                radius: 6
                                color: themeTile.modelData.bg && themeTile.modelData.bg !== "none" ? themeTile.modelData.bg : "#0b0d14"
                                Row {
                                    anchors.centerIn: parent
                                    spacing: 4
                                    Repeater {
                                        model: [themeTile.modelData.color, themeTile.modelData.hot || themeTile.modelData.color]
                                        Text {
                                            required property string modelData
                                            text: "Aa"
                                            font.pixelSize: 13
                                            font.bold: true
                                            color: modelData
                                            style: themeTile.modelData.stroke ? Text.Outline : Text.Normal
                                            styleColor: themeTile.modelData.stroke || "transparent"
                                        }
                                    }
                                }
                            }
                            Label {
                                anchors.bottom: parent.bottom
                                anchors.bottomMargin: 3
                                width: parent.width
                                horizontalAlignment: Text.AlignHCenter
                                text: themeTile.modelData.name
                                elide: Text.ElideRight
                                color: Theme.textSecondary
                                font.pixelSize: 10
                            }
                        }
                    }
                }
            }
        }
        Flickable {
            visible: panel.source === "shapes"
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            contentHeight: shapeColumn.implicitHeight
            Column {
                id: shapeColumn
                width: parent.width
                spacing: 8
                Repeater {
                    model: ["Básicas", "Flechas", "Geométricas", "UI"]
                    Column {
                        id: shapeGroup
                        required property string modelData
                        width: shapeColumn.width
                        spacing: 4
                        Label { text: shapeGroup.modelData; color: Theme.muted; font.pixelSize: 11; font.bold: true }
                        Flow {
                            width: parent.width
                            spacing: 4
                            Repeater {
                                model: panel.model.materials.filter((m) => m.source === "shapes" && m.category === shapeGroup.modelData)
                                ShapeCard { dragLayer: panel.dragLayer; geometry: panel.model.shapeThumb(modelData.type); onPicked: (type, label) => panel.model.addShape(type, label) }
                            }
                        }
                    }
                }
            }
        }
        YoutubePanel {
            visible: panel.source === "youtube"
            Layout.fillWidth: true
            Layout.fillHeight: true
            model: panel.model
        }
        NarratorPanel {
            visible: panel.source === "narrator"
            Layout.fillWidth: true
            Layout.fillHeight: true
            model: panel.model
        }
        ListView {
            id: list
            visible: ["fx", "shapes", "text", "narrator", "youtube"].indexOf(panel.source) < 0
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
                    // El menú del material (EdMaterial): el seguimiento de caras de un vídeo del proyecto.
                    TapHandler {
                        acceptedButtons: Qt.RightButton
                        enabled: slot.modelData.source === "project" && slot.modelData.kind === "video"
                        onTapped: matMenu.popup()
                    }
                    Menu {
                        id: matMenu
                        objectName: "material:menu:" + slot.modelData.key
                        MenuItem {
                            objectName: "material:faceTrack:" + slot.modelData.key
                            enabled: !panel.model.faceTrackBusy
                            text: slot.modelData.faceTrack ? "Aplicar seguimiento de caras" : "Seguimiento de caras"
                            onTriggered: panel.model.faceTrackMaterial(slot.modelData.key, false)
                        }
                        MenuItem {
                            objectName: "material:faceRetrack:" + slot.modelData.key
                            visible: !!slot.modelData.faceTrack
                            height: visible ? implicitHeight : 0
                            enabled: !panel.model.faceTrackBusy
                            text: "Volver a analizar caras"
                            onTriggered: panel.model.faceTrackMaterial(slot.modelData.key, true)
                        }
                    }
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

    // Soltar archivos del sistema en el panel (onFileDrop de EdMaterial): el aviso dice qué se suelta
    // y, si no es vídeo, imagen ni audio, no deja soltarlo. Lo importado se ve en Proyecto, con el
    // filtro del tipo. Las tarjetas de material (clave «material») no entran aquí.
    DropArea {
        id: fileDrop
        objectName: "material:fileDrop"
        anchors.fill: parent
        keys: ["text/uri-list"]
        // Lo que no es material se acepta con la acción «ignorar» (el cursor de no se puede, el
        // dropEffect «none» de React) y no rechazado: así sigue llegando la salida y el aviso se quita.
        onEntered: (drag) => {
            panel.fileDrag = panel.model.dropFileKind(drag.urls)
            if (panel.fileDrag === "other")
                drag.accept(Qt.IgnoreAction)
            else
                drag.accept(Qt.CopyAction)
        }
        onExited: panel.fileDrag = ""
        onDropped: (drop) => {
            const kind = panel.model.dropFileKind(drop.urls)
            panel.fileDrag = ""
            if (kind === "other")
                return
            drop.accept(Qt.CopyAction)
            if (panel.model.importFiles(drop.urls, true) > 0) {
                panel.source = "project"
                panel.filter = kind
            }
        }
    }
    Rectangle {
        objectName: "material:dropHint"
        visible: panel.fileDrag !== ""
        anchors.fill: parent
        radius: panel.radius
        color: Qt.rgba(0, 0, 0, 0.55)
        border.width: 2
        border.color: panel.fileDrag === "other" ? "#f87171" : Theme.accent
        Label {
            anchors.centerIn: parent
            text: ({ video: "Suelta el vídeo aquí", image: "Suelta la imagen aquí", audio: "Suelta el audio aquí",
                     other: "Formato no compatible" })[panel.fileDrag] || "Suelta el archivo aquí"
            color: panel.fileDrag === "other" ? "#f87171" : Theme.text
            font.pixelSize: 14
            font.bold: true
        }
    }
}
