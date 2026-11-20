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
    property string filter: "video"
    property string source: "project"
    property string query: ""
    property string sfxCategory: ""
    property string collection: ""
    property bool manage: false
    property string fxCategory: ""
    property string fileDrag: "" // lo que se arrastra desde el sistema (dropFileKind), o ""
    Gradient {
        id: accentGradient
        orientation: Gradient.Horizontal
        GradientStop { position: 0; color: Theme.accent }
        GradientStop { position: 1; color: Theme.accent2 }
    }
    function words(text) { return text.toLowerCase().split(/\s+/).filter((w) => w) }

    // La pestaña de la barra de iconos (MAT_NAV de materialNav.js) y su ámbito (ScopeFilter: Todos,
    // Guardados, Cargar). Las dos dicen qué se ve: `source` y `filter`.
    property string tab: "video"
    // Vídeos o imágenes del proyecto o de la biblioteca: en rejilla con miniatura.
    readonly property bool gridMode: (source === "project" || source === "library") && (filter === "video" || filter === "image")
    // Lo que se ve (filtrado por tipo y por la búsqueda).
    readonly property var shown: model.materials.filter((m) => (source === "project" ? (m.source === "project" || m.source === "library")
                                                                                      : m.source === source)
        && (source === "sfx" || filter === "all" || m.kind === filter)
        && (source === "sfx" || source === "collection" || words(query).every((w) => m.name.toLowerCase().includes(w))))
    property string scope: "all"
    signal openSettings
    function go(nextTab, nextScope) {
        tab = nextTab
        scope = nextScope || "all"
        const media = { video: "video", image: "image", audio: "audio" }[tab]
        if (media) {
            filter = media
            source = scope === "cargar" ? (tab === "video" ? "youtube" : "narrator") : scope === "saved" ? "library" : "project"
        } else {
            filter = "all"
            source = { sfx: "sfx", library: scope === "collections" ? "collection" : scope === "sticks" ? "stick" : "library", effects: "fx",
                       shapes: "shapes", text: "text" }[tab] || source
        }
        if (source === "collection")
            model.searchCollections(query, collection)
        else if (source === "sfx")
            model.searchSfx(query, sfxCategory)
        else if (source === "stick")
            model.closeStick()
    }
    readonly property var navItems: [
        { id: "video", icon: "movie", label: "Video", short: "Video" },
        { id: "image", icon: "image", label: "Imagen", short: "Imagen" },
        { id: "audio", icon: "mic", label: "Audio", short: "Audio" },
        { id: "sfx", icon: "graphic_eq", label: "SFX", short: "SFX" },
        { id: "library", icon: "collections_bookmark", label: "Biblioteca", short: "Biblio", sep: true },
        { id: "effects", icon: "auto_awesome", label: "Efectos", short: "Efectos", sep: true },
        { id: "shapes", icon: "category", label: "Figuras", short: "Figuras" },
        { id: "text", icon: "title", label: "Texto", short: "Texto" },
        { id: "transitions", icon: "animation", label: "Animación", short: "Anim.", off: "Las animaciones de clip están en la pestaña Animar del inspector" },
        // Sin Motion (se rehará de cero) ni Paper (es el efecto paper_cutout).
        { id: "ai", icon: "smart_toy", label: "IA", short: "IA", sep: true, off: "La pestaña IA aún no está en GenClipPro" },
        { id: "settings", icon: "settings", label: "Configuración", short: "Config", sep: true },
        { id: "chat", icon: "forum", label: "Chat IA", short: "Chat", off: "El chat con IA aún no está en GenClipPro" }
    ]

    // .ed-material
    radius: 6
    color: Theme.topbar
    border.color: Theme.line

    ColumnLayout {
        anchors.fill: parent
        anchors.leftMargin: 8
        anchors.rightMargin: 8
        anchors.topMargin: 5
        anchors.bottomMargin: 6
        spacing: 6

        // .ed-mat-nav: los iconos con su nombre corto; si no caben, se desplaza.
        Flickable {
            id: navFlick
            Layout.fillWidth: true
            implicitHeight: 34
            contentWidth: navRow.implicitWidth
            contentHeight: height
            flickableDirection: Flickable.HorizontalFlick
            boundsBehavior: Flickable.StopAtBounds
            clip: true
            WheelHandler {
                onWheel: (e) => navFlick.contentX = Math.max(0, Math.min(navFlick.contentWidth - navFlick.width,
                                                                         navFlick.contentX - e.angleDelta.y / 2))
            }
            Row {
                id: navRow
                spacing: 1
                Repeater {
                    model: panel.navItems
                    Row {
                        id: navSlot
                        required property var modelData
                        spacing: 1
                        Rectangle { visible: !!navSlot.modelData.sep; width: 1; height: 24; anchors.verticalCenter: parent.verticalCenter; color: Theme.line }
                        Item { visible: !!navSlot.modelData.sep; width: 2; height: 1 }
                        AbstractButton {
                            id: navBtn
                            readonly property bool on: panel.tab === navSlot.modelData.id
                            objectName: "material:nav:" + navSlot.modelData.id
                            width: 36
                            height: 34
                            hoverEnabled: true
                            opacity: navSlot.modelData.off ? 0.45 : 1
                            ToolTip.visible: hovered
                            ToolTip.delay: 400
                            ToolTip.text: navSlot.modelData.off || navSlot.modelData.label
                            onClicked: {
                                if (navSlot.modelData.off)
                                    return
                                if (navSlot.modelData.id === "settings")
                                    panel.openSettings()
                                else
                                    panel.go(navSlot.modelData.id, "all")
                            }
                            contentItem: Column {
                                spacing: 1
                                MIcon {
                                    anchors.horizontalCenter: parent.horizontalCenter
                                    name: navSlot.modelData.icon
                                    size: 18
                                    color: navBtn.on || navBtn.hovered ? "white" : Theme.textSecondary
                                }
                                Label {
                                    anchors.horizontalCenter: parent.horizontalCenter
                                    width: Math.min(implicitWidth, 34)
                                    text: navSlot.modelData.short
                                    font.pixelSize: 9
                                    font.weight: Font.DemiBold
                                    elide: Text.ElideRight
                                    color: navBtn.on || navBtn.hovered ? "white" : Theme.textSecondary
                                }
                            }
                            background: Rectangle {
                                radius: 7
                                color: !navBtn.on && navBtn.hovered ? Qt.rgba(1, 1, 1, 0.06) : "transparent"
                                gradient: navBtn.on ? accentGradient : null
                            }
                        }
                    }
                }
            }
        }
        Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: Theme.line }

        // ScopeFilter: Todos / Guardados (la biblioteca) / Cargar (YouTube o el narrador). En la
        // biblioteca: lo guardado o las colecciones de la carpeta de material.
        RowLayout {
            id: scopeRow
            readonly property var scopes: panel.tab === "video" ? [["all", "Todos"], ["saved", "Guardados"], ["cargar", "Cargar clips", "add"]]
                                        : panel.tab === "audio" ? [["all", "Todos"], ["saved", "Guardados"], ["cargar", "Cargar audio", "add"]]
                                        : panel.tab === "image" ? [["all", "Todas"], ["saved", "Guardadas"]]
                                        : panel.tab === "library" ? [["all", "Guardado"], ["collections", "Colecciones"], ["sticks", "Sticks"]]
                                        : []
            visible: scopes.length > 0
            Layout.fillWidth: true
            spacing: 5
            Repeater {
                model: scopeRow.scopes
                AbstractButton {
                    id: scopeBtn
                    required property var modelData
                    readonly property bool on: panel.scope === modelData[0]
                    objectName: "material:scope:" + modelData[0]
                    Layout.fillWidth: true
                    implicitHeight: 26
                    hoverEnabled: true
                    onClicked: panel.go(panel.tab, modelData[0])
                    contentItem: Item {
                        Row {
                            anchors.centerIn: parent
                            spacing: 4
                            MIcon {
                                visible: !!scopeBtn.modelData[2]
                                anchors.verticalCenter: parent.verticalCenter
                                name: scopeBtn.modelData[2] || ""
                                size: 13
                                color: scopeBtn.on ? "white" : Theme.textSecondary
                            }
                            Label {
                                anchors.verticalCenter: parent.verticalCenter
                                text: scopeBtn.modelData[1]
                                font.pixelSize: 11
                                font.weight: scopeBtn.on ? Font.Bold : Font.Normal
                                color: scopeBtn.on || scopeBtn.hovered ? "white" : Theme.textSecondary
                            }
                        }
                    }
                    background: Rectangle {
                        radius: Theme.radiusSm
                        color: !scopeBtn.on && scopeBtn.hovered ? Theme.card3 : scopeBtn.on ? "transparent" : Theme.card2
                        border.color: scopeBtn.on ? Theme.accent : Theme.line
                        gradient: scopeBtn.on ? accentGradient : null
                    }
                }
            }
        }
        TextField {
            visible: ["shapes", "text", "narrator", "youtube", "stick"].indexOf(panel.source) < 0
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
            visible: panel.shown.length === 0 && ["fx", "shapes", "text", "narrator", "youtube", "stick"].indexOf(panel.source) < 0
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            text: panel.source === "library" ? "La biblioteca está vacía: guarda material de un proyecto para usarlo en todos."
                : panel.source === "sfx" ? "No hay sonidos: elige la carpeta de SFX en los ajustes."
                : panel.source === "collection" ? "No hay colecciones: crea carpetas en la de material (assets/material o la de los ajustes)."
                : "Este proyecto no tiene material todavía."
            color: Theme.muted
        }
        // EdLibraryStick: la biblioteca de sticks (personajes con croma). Cuando se abre un stick,
        // se ven sus expresiones (grupos) con las tarjetas de cada recurso.
        Flickable {
            id: stickScroll
            objectName: "stick:scroll"
            visible: panel.source === "stick"
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            contentHeight: stickColumn.implicitHeight
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
            Column {
                id: stickColumn
                width: stickScroll.width
                spacing: 6
                // Si hay un stick abierto: la cabecera con «volver» y los grupos por expresión.
                Row {
                    visible: !!panel.model.stickDetail.id
                    spacing: 6
                    padding: 2
                    AbstractButton {
                        objectName: "stick:back"
                        width: 24; height: 24
                        onClicked: panel.model.closeStick()
                        contentItem: MIcon { name: "arrow_back"; size: 16; color: Theme.text }
                        background: Rectangle { radius: 4; color: parent.hovered ? Theme.card3 : "transparent" }
                        hoverEnabled: true
                    }
                    Label {
                        anchors.verticalCenter: parent.verticalCenter
                        text: (panel.model.stickDetail.emoji || "🎭") + " " + (panel.model.stickDetail.name || "")
                        color: Theme.text; font.pixelSize: 13; font.weight: Font.Bold
                    }
                    Rectangle {
                        visible: !!panel.model.stickDetail.chromaColor
                        anchors.verticalCenter: parent.verticalCenter
                        width: 14; height: 14; radius: 3
                        color: panel.model.stickDetail.chromaColor || "transparent"
                        border.color: Theme.line
                        ToolTip.visible: chromaHover.hovered
                        ToolTip.text: "Croma " + (panel.model.stickDetail.chromaColor || "") + ": se quita al agregarlo"
                        HoverHandler { id: chromaHover }
                    }
                }
                Repeater {
                    model: panel.model.stickDetail.groups || []
                    Column {
                        required property var modelData
                        width: stickColumn.width
                        spacing: 3
                        Row {
                            leftPadding: 4; spacing: 4
                            Label { text: modelData.emoji || ""; font.pixelSize: 13 }
                            Label { text: modelData.label || ""; color: Theme.text; font.pixelSize: 12; font.weight: Font.DemiBold }
                            Label { text: String((modelData.items || []).length); color: Theme.muted; font.pixelSize: 11 }
                        }
                        Flow {
                            width: parent.width
                            spacing: 4
                            padding: 2
                            Repeater {
                                model: modelData.items || []
                                Rectangle {
                                    id: stile
                                    required property var modelData
                                    objectName: "stick:tile:" + modelData.key
                                    width: 100; height: 90
                                    radius: Theme.radiusSm
                                    color: stileHover.hovered ? Theme.card3 : Theme.card2
                                    border.color: stileHover.hovered ? Theme.lineBright : Theme.line
                                    property bool dragging: false
                                    Drag.active: dragging
                                    Drag.keys: ["material"]
                                    HoverHandler { id: stileHover; cursorShape: stile.dragging ? Qt.ClosedHandCursor : Qt.OpenHandCursor }
                                    DragHandler {
                                        id: stileDrag
                                        onActiveChanged: {
                                            if (active) {
                                                stile.Drag.hotSpot = stileDrag.centroid.pressPosition
                                                stile.dragging = true
                                                return
                                            }
                                            stile.Drag.drop()
                                            stile.dragging = false
                                            stile.x = 0; stile.y = 0
                                        }
                                    }
                                    states: State {
                                        when: stile.dragging
                                        ParentChange { target: stile; parent: panel.dragLayer }
                                    }
                                    ColumnLayout {
                                        anchors.fill: parent; anchors.margins: 3; spacing: 2
                                        Rectangle {
                                            Layout.fillWidth: true; Layout.fillHeight: true
                                            radius: 4; color: "#111111"; clip: true
                                            MIcon {
                                                anchors.centerIn: parent
                                                name: stile.modelData.kind === "video" ? "movie" : "image"
                                                size: 22; color: Theme.muted
                                            }
                                            // «+» para agregar al proyecto (con croma).
                                            AbstractButton {
                                                objectName: "stick:add:" + stile.modelData.key
                                                anchors.top: parent.top; anchors.right: parent.right; anchors.margins: 3
                                                width: 20; height: 20; hoverEnabled: true
                                                ToolTip.visible: hovered; ToolTip.delay: 500; ToolTip.text: "Agregar al proyecto (con croma)"
                                                onClicked: panel.model.addStickItem(stile.modelData.key)
                                                contentItem: MIcon { name: "add"; size: 14; color: "white" }
                                                background: Rectangle {
                                                    radius: 5
                                                    gradient: Gradient {
                                                        orientation: Gradient.Horizontal
                                                        GradientStop { position: 0; color: Theme.accent }
                                                        GradientStop { position: 1; color: Theme.accent2 }
                                                    }
                                                }
                                            }
                                            Label {
                                                visible: stile.modelData.kind === "video" && stile.modelData.duration > 0
                                                anchors.right: parent.right; anchors.bottom: parent.bottom; anchors.margins: 3
                                                leftPadding: 3; rightPadding: 3
                                                text: panel.model.formatTime(stile.modelData.duration)
                                                color: "white"; font.pixelSize: 9
                                                background: Rectangle { radius: 3; color: Qt.rgba(0, 0, 0, 0.6) }
                                            }
                                        }
                                        Label {
                                            Layout.fillWidth: true
                                            text: stile.modelData.title || ""; color: Theme.text; font.pixelSize: 10
                                            elide: Text.ElideRight
                                        }
                                    }
                                }
                            }
                        }
                        Label {
                            visible: (modelData.items || []).length === 0
                            leftPadding: 8
                            text: "Arrastra aquí vídeos o imágenes «" + (modelData.label || "").toLowerCase() + "»"
                            color: Theme.muted; font.pixelSize: 11; font.italic: true
                        }
                    }
                }
                // Si NO hay stick abierto: la lista de personajes disponibles.
                Label {
                    visible: !panel.model.stickDetail.id && panel.model.sticks.length === 0
                    width: parent.width
                    wrapMode: Text.Wrap
                    text: "No hay sticks. Crea uno en la biblioteca de colecciones (una carpeta con stick.json)."
                    color: Theme.muted
                }
                Repeater {
                    model: !panel.model.stickDetail.id ? panel.model.sticks : []
                    AbstractButton {
                        id: stickRow
                        required property var modelData
                        objectName: "stick:open:" + modelData.id
                        width: stickColumn.width; height: 40
                        hoverEnabled: true
                        onClicked: panel.model.openStick(modelData.id)
                        background: Rectangle {
                            radius: Theme.radiusSm
                            color: stickRow.hovered ? Theme.card3 : "transparent"
                        }
                        contentItem: RowLayout {
                            spacing: 8
                            Label { text: stickRow.modelData.emoji || "🎭"; font.pixelSize: 18; leftPadding: 6 }
                            ColumnLayout {
                                spacing: 0
                                Label { text: stickRow.modelData.name; color: Theme.text; font.pixelSize: 12; font.weight: Font.DemiBold }
                                Label {
                                    text: stickRow.modelData.count + " recursos"
                                    color: Theme.muted; font.pixelSize: 10
                                }
                            }
                            Item { Layout.fillWidth: true }
                            Rectangle {
                                visible: !!stickRow.modelData.chromaColor
                                width: 12; height: 12; radius: 3
                                Layout.rightMargin: 8
                                color: stickRow.modelData.chromaColor || "transparent"
                                border.color: Theme.line
                            }
                            MIcon { name: "chevron_right"; size: 16; color: Theme.muted; Layout.rightMargin: 4 }
                        }
                    }
                }
            }
        }
        // EdFxLibrary (mode="effects"): todo en una lista que se desplaza (.ed-mat-list), en su orden: los efectos
        // animados (EdVfxLibrary), las recetas en un clic (EdRecipes) y la capa de ajuste (adjustBtn).
        Flickable {
            id: fxScroll
            objectName: "material:fxScroll"
            visible: panel.source === "fx"
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            contentHeight: fxColumn.implicitHeight
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
            Column {
                id: fxColumn
                width: fxScroll.width
                spacing: 6
                Flow {
                    width: parent.width
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
                Grid {
                    id: fxGrid
                    width: parent.width
                    columns: 2
                    spacing: 4
                    readonly property real cellWidth: (width - spacing) / 2
                    readonly property var active: (panel.model.selectedClip.vfx || []).map((e) => e.effect)
                    Repeater {
                        id: fxRepeater
                        model: panel.source === "fx" ? panel.model.searchVfx(panel.query, panel.fxCategory) : []
                        Rectangle {
                            required property var modelData
                            objectName: "fx:" + modelData.id
                            width: fxGrid.cellWidth
                            height: 48
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
                    }
                }
                Label {
                    visible: fxRepeater.count === 0
                    text: "Ningún efecto coincide."
                    color: Theme.muted
                }
                // EdRecipes (.ed-recipes): los trucos del vídeo montados de una vez, con lo seleccionado.
                Column {
                    objectName: "recipes"
                    width: parent.width
                    spacing: 6
                    topPadding: 4
                    bottomPadding: 4
                    Label { text: "Recetas en un clic"; color: Theme.muted; font.pixelSize: 11; font.bold: true }
                    Repeater {
                        // Por número: las tarjetas se quedan al cambiar la selección y solo cambia si se puede.
                        model: panel.source === "fx" ? panel.model.recipes.length : 0
                        // .ed-recipe: nombre y truco, el botón y la descripción debajo; atenuada si no se puede.
                        Rectangle {
                            id: recipeCard
                            required property int index
                            readonly property var modelData: panel.model.recipes[index]
                            objectName: "recipe:" + modelData.id
                            width: parent.width
                            height: recipeBody.implicitHeight + 16
                            radius: 8
                            color: Theme.card2
                            border.color: Theme.line
                            opacity: modelData.ok ? 1 : 0.6
                            ColumnLayout {
                                id: recipeBody
                                anchors.left: parent.left
                                anchors.right: parent.right
                                anchors.top: parent.top
                                anchors.topMargin: 8
                                anchors.leftMargin: 10
                                anchors.rightMargin: 10
                                spacing: 4
                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: 8
                                    Label {
                                        Layout.fillWidth: true
                                        textFormat: Text.StyledText
                                        text: "<b>" + recipeCard.modelData.label + "</b>&nbsp;&nbsp;<font color=\"" + Theme.muted
                                              + "\">truco " + recipeCard.modelData.trick + "</font>"
                                        color: Theme.text
                                        font.pixelSize: 13
                                        elide: Text.ElideRight
                                    }
                                    GhostButton {
                                        objectName: "recipe:apply:" + recipeCard.modelData.id
                                        iconName: "auto_awesome"
                                        text: "Aplicar"
                                        enabled: recipeCard.modelData.ok
                                        tip: recipeCard.modelData.ok ? "Aplicar (se deshace con Ctrl+Z)" : recipeCard.modelData.why
                                        onClicked: panel.model.applyRecipe(recipeCard.modelData.id)
                                    }
                                }
                                Label {
                                    objectName: "recipe:desc:" + recipeCard.modelData.id
                                    Layout.fillWidth: true
                                    wrapMode: Text.Wrap
                                    text: recipeCard.modelData.desc + (recipeCard.modelData.ok ? "" : " — " + recipeCard.modelData.why + ".")
                                    color: Theme.muted
                                    font.pixelSize: 11
                                }
                            }
                        }
                    }
                }
                // Barras de cine (#20): un clic por proporción (CINEMA_RATIOS de shapes.js).
                Row {
                    width: parent.width
                    spacing: 6
                    leftPadding: 4
                    MIcon { anchors.verticalCenter: parent.verticalCenter; name: "crop_7_5"; size: 16; color: Theme.muted }
                    Label { anchors.verticalCenter: parent.verticalCenter; text: "Barras de cine"; color: Theme.muted; font.pixelSize: 11; font.bold: true }
                    Repeater {
                        model: [
                            { id: "2.39", label: "2,39:1 (cine)" },
                            { id: "2",    label: "2:1" },
                            { id: "1.85", label: "1,85:1" },
                            { id: "16:9", label: "16:9" }
                        ]
                        Rectangle {
                            required property var modelData
                            anchors.verticalCenter: parent.verticalCenter
                            width: cinemaLabel.implicitWidth + 12
                            height: 22
                            radius: 11
                            color: cinemaHover.hovered ? Theme.card3 : Theme.card2
                            border.color: cinemaHover.hovered ? Theme.accent : Theme.lineBright
                            HoverHandler { id: cinemaHover }
                            TapHandler { onTapped: panel.model.addCinemaBars(parent.modelData.id) }
                            ToolTip.visible: cinemaHover.hovered
                            ToolTip.delay: 500
                            ToolTip.text: "Barras negras arriba y abajo: lo visible queda en " + parent.modelData.label
                            Label { id: cinemaLabel; anchors.centerIn: parent; text: parent.modelData.label; color: Theme.text; font.pixelSize: 11 }
                        }
                    }
                }
                // El adjustBtn de EdFxLibrary: un clip que filtra todo lo que tiene debajo durante su tramo.
                AbstractButton {
                    id: adjustButton
                    objectName: "material:adjustment"
                    width: parent.width
                    implicitHeight: 42
                    hoverEnabled: true
                    ToolTip.visible: hovered
                    ToolTip.delay: 500
                    ToolTip.text: "Un clip que filtra todo lo que tiene debajo durante su tramo"
                    onClicked: panel.model.addAdjustment()
                    contentItem: Row {
                        spacing: 8
                        leftPadding: 10
                        MIcon { anchors.verticalCenter: parent.verticalCenter; name: "tune"; size: 18; color: Theme.text }
                        Column {
                            anchors.verticalCenter: parent.verticalCenter
                            Label { text: "Capa de ajuste"; color: Theme.text; font.pixelSize: 12; font.bold: true }
                            Label { text: "filtros y color para todo lo de debajo"; color: Theme.muted; font.pixelSize: 10 }
                        }
                    }
                    background: Rectangle {
                        radius: Theme.radiusSm
                        color: adjustButton.hovered ? Theme.card3 : Theme.card2
                        border.color: adjustButton.hovered ? Theme.accent : Theme.lineBright
                    }
                }
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
        // .ed-mat-grid: los vídeos y las imágenes en tarjetas con miniatura (MaterialClipGrid). «Todos» junta
        // los del proyecto y los guardados en la biblioteca, como shownClips de EdMaterial.
        GridView {
            id: grid
            readonly property int columns: Math.max(1, Math.floor(width / 118))
            visible: panel.gridMode
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            cellWidth: width / columns
            cellHeight: Math.min(168, Math.max(88, cellWidth * (panel.filter === "video" ? 1.25 : 1))) + 34
            model: panel.gridMode ? panel.shown : []
            delegate: MaterialGridCard {
                width: grid.cellWidth - 5
                height: grid.cellHeight - 5
                host: panel
                thumb: panel.model.materialThumbs[modelData.key] || ""
            }
        }
        ListView {
            id: list
            visible: ["fx", "shapes", "text", "narrator", "youtube"].indexOf(panel.source) < 0 && !panel.gridMode
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            spacing: 6
            model: panel.gridMode ? [] : panel.shown
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
                    TapHandler {
                        acceptedButtons: Qt.RightButton
                        onTapped: matMenu.popup()
                    }
                    Menu {
                        id: matMenu
                        objectName: "material:menu:" + slot.modelData.key
                        MenuItem {
                            objectName: "material:info:" + slot.modelData.key
                            text: "Editar información"
                            onTriggered: panel.model.openMaterialInfo(slot.modelData.key)
                        }
                        MenuItem {
                            objectName: "material:faceTrack:" + slot.modelData.key
                            visible: slot.modelData.source === "project" && slot.modelData.kind === "video"
                            height: visible ? implicitHeight : 0
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
                        MenuItem {
                            objectName: "material:upgrade:" + slot.modelData.key
                            visible: !!slot.modelData.canUpgrade
                            height: visible ? implicitHeight : 0
                            text: "Usar esta calidad en la timeline"
                            onTriggered: panel.model.checkUpgrade(slot.modelData.key)
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
                        // .ed-add-btn: el + que lo pone en el cabezal (addAsset).
                        AbstractButton {
                            id: addButton
                            objectName: "material:add:" + slot.modelData.key
                            implicitWidth: 26
                            implicitHeight: 24
                            hoverEnabled: true
                            ToolTip.visible: hovered
                            ToolTip.delay: 500
                            ToolTip.text: "Añadir en el cursor"
                            onClicked: panel.model.addMaterial(slot.modelData.key)
                            contentItem: MIcon { name: "add"; size: 16; color: addButton.hovered ? "white" : Theme.text }
                            background: Rectangle {
                                radius: 6
                                color: addButton.hovered ? Theme.accent : Theme.card3
                                border.color: addButton.hovered ? Theme.accent : Theme.lineBright
                            }
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
            if (panel.model.importFiles(drop.urls, true) > 0)
                panel.go(kind, "all")
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
