import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Dialogs
import QtQuick.Layouts

// Ajustes (EdSettings.jsx): la carpeta de los proyectos (del studio), exportación, transcripción y API
// keys. Las claves no se ven nunca: solo cuántas tiene cada proveedor; se añaden, se cambian o se quitan.
Rectangle {
    id: page
    required property var model
    required property var folder // projects_folder.hpp
    required property var updater // updater.hpp
    signal closed
    readonly property bool updating: updater.status === "checking" || updater.status === "downloading"
                                     || updater.status === "installing"

    color: Theme.bg

    component Chip: Rectangle {
        id: chip
        property alias text: label.text
        property bool on
        signal clicked
        implicitWidth: label.implicitWidth + 24
        implicitHeight: 32
        radius: Theme.radiusSm
        color: on ? Qt.rgba(1, 0.23, 0.36, 0.25) : Theme.card2
        border.color: on ? Theme.accent : Theme.lineBright
        Label { id: label; anchors.centerIn: parent; color: chip.on ? "white" : Theme.textSecondary }
        TapHandler { onTapped: chip.clicked() }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 20
        spacing: 16

        RowLayout {
            Layout.fillWidth: true
            ToolButton {
                text: "‹ Volver"
                palette.buttonText: Theme.textSecondary
                background: Rectangle { radius: Theme.radiusSm; color: parent.hovered ? Qt.rgba(1, 1, 1, 0.06) : "transparent" }
                onClicked: page.closed()
            }
            Label { Layout.fillWidth: true; text: "Ajustes"; color: Theme.text; font.pixelSize: 20; font.bold: true }
            // La versión (la de CMakeLists.txt).
            Label {
                objectName: "set:version"
                visible: !!Qt.application.version
                text: "GenClipPro " + Qt.application.version
                color: Theme.muted
                font.pixelSize: 12
            }
        }
        Label { visible: !!page.model.error; text: page.model.error; color: Theme.danger }

        ScrollView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            contentWidth: availableWidth
            clip: true

            ColumnLayout {
                width: parent.width
                spacing: 16

                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: folderBox.implicitHeight + 40
                    radius: Theme.radiusLg
                    color: Theme.card
                    border.color: Theme.line
                    ColumnLayout {
                        id: folderBox
                        anchors.fill: parent
                        anchors.margins: 20
                        spacing: 10
                        Label { text: "Carpeta de los proyectos"; color: Theme.text; font.pixelSize: 16; font.bold: true }
                        Label {
                            Layout.fillWidth: true
                            wrapMode: Text.Wrap
                            text: "Aquí se guardan los proyectos, sus vídeos (clips) y estos ajustes (data). Cambiarla no mueve nada: GenClipPro pasa a abrir y guardar los de la carpeta nueva. Si eliges la de video-yt, ves los mismos proyectos que allí."
                            color: Theme.muted
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 10
                            MIcon { name: "folder"; size: 20; color: Theme.folder }
                            Label {
                                objectName: "set:folder:path"
                                Layout.fillWidth: true
                                text: page.folder.path
                                elide: Text.ElideMiddle
                                color: Theme.text
                                ToolTip.visible: pathHover.hovered && truncated
                                ToolTip.text: text
                                HoverHandler { id: pathHover }
                            }
                            Label {
                                objectName: "set:folder:count"
                                text: page.folder.projectCount === 1 ? "1 proyecto"
                                      : page.folder.projectCount > 1 ? page.folder.projectCount + " proyectos" : "Sin proyectos"
                                color: Theme.textSecondary
                                font.pixelSize: 12
                            }
                        }
                        Flow {
                            Layout.fillWidth: true
                            spacing: 8
                            Button {
                                objectName: "set:folder:change"
                                text: "Cambiar…"
                                enabled: !page.folder.blocker
                                onClicked: folderDialog.open()
                            }
                            Button {
                                objectName: "set:folder:default"
                                visible: !page.folder.isDefault
                                enabled: !page.folder.blocker
                                text: "Usar la de siempre"
                                ToolTip.visible: hovered
                                ToolTip.text: page.folder.defaultPath
                                onClicked: page.folder.useDefault()
                            }
                            Button {
                                objectName: "set:folder:open"
                                text: "Abrir la carpeta"
                                onClicked: Qt.openUrlExternally(page.folder.url)
                            }
                        }
                        Label {
                            objectName: "set:folder:blocker"
                            Layout.fillWidth: true
                            visible: !!page.folder.blocker
                            wrapMode: Text.Wrap
                            text: page.folder.blocker
                            color: Theme.warning
                        }
                        Label {
                            objectName: "set:folder:cli"
                            Layout.fillWidth: true
                            visible: page.folder.fromCommandLine
                            wrapMode: Text.Wrap
                            text: "La abriste con --root, que manda al arrancar. Sin --root, GenClipPro abre la que elijas aquí."
                            color: Theme.muted
                            font.pixelSize: 12
                        }
                        Label {
                            objectName: "set:folder:message"
                            Layout.fillWidth: true
                            visible: !!page.folder.message
                            wrapMode: Text.Wrap
                            text: page.folder.message
                            color: Theme.success
                        }
                        Label {
                            objectName: "set:folder:error"
                            Layout.fillWidth: true
                            visible: !!page.folder.error
                            wrapMode: Text.Wrap
                            text: page.folder.error
                            color: Theme.danger
                        }
                        Label {
                            Layout.fillWidth: true
                            wrapMode: Text.Wrap
                            text: "Lo que aún hace el backend de video-yt (cargar de YouTube, voces, subtítulos y «Eliminar fondo») usa siempre su propia carpeta."
                            color: Theme.muted
                            font.pixelSize: 12
                        }
                    }
                }

                // Versión: la de producción se actualiza aquí (updater.hpp); la de desarrollo es la última compilada.
                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: versionBox.implicitHeight + 40
                    radius: Theme.radiusLg
                    color: Theme.card
                    border.color: Theme.line
                    ColumnLayout {
                        id: versionBox
                        anchors.fill: parent
                        anchors.margins: 20
                        spacing: 10
                        Label { text: "Versión"; color: Theme.text; font.pixelSize: 16; font.bold: true }
                        Label {
                            objectName: "set:channel"
                            Layout.fillWidth: true
                            wrapMode: Text.Wrap
                            text: page.updater.installed
                                  ? "GenClipPro " + page.updater.version + " · producción. Se actualiza aquí: primero con las releases de este PC (las que deja tools\\release\\empaquetar.ps1) y, si no hay una más nueva, con la de GitHub."
                                  : "GenClipPro " + page.updater.version + " · desarrollo. Es la build: siempre la última que se compila, no hace falta actualizarla. La de producción («GenClipPro» del menú Inicio) se actualiza desde sus Ajustes."
                            color: Theme.textSecondary
                        }
                        Flow {
                            Layout.fillWidth: true
                            spacing: 8
                            visible: page.updater.installed
                            Button {
                                objectName: "set:checkUpdate"
                                text: "Buscar actualizaciones"
                                enabled: !page.updating
                                onClicked: page.updater.check()
                            }
                            Button {
                                objectName: "set:update"
                                visible: page.updater.status === "available"
                                enabled: !page.folder.blocker
                                text: "Actualizar a la " + page.updater.latest
                                onClicked: page.updater.update()
                            }
                        }
                        ProgressBar {
                            Layout.fillWidth: true
                            visible: page.updater.status === "downloading"
                            value: page.updater.progress
                        }
                        Label {
                            objectName: "set:updateMessage"
                            Layout.fillWidth: true
                            visible: !!page.updater.message
                            wrapMode: Text.Wrap
                            text: page.updater.message
                            color: page.updater.status === "available" ? Theme.accent : Theme.muted
                        }
                        Label {
                            Layout.fillWidth: true
                            visible: page.updater.status === "available" && !!page.folder.blocker
                            wrapMode: Text.Wrap
                            text: "Para actualizar, cierra el proyecto y espera a que acabe la grabación o la exportación: GenClipPro se cierra y se vuelve a abrir."
                            color: Theme.warning
                        }
                        Label {
                            objectName: "set:updateError"
                            Layout.fillWidth: true
                            visible: !!page.updater.error
                            wrapMode: Text.Wrap
                            text: page.updater.error
                            color: Theme.danger
                        }
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: exportBox.implicitHeight + 40
                    radius: Theme.radiusLg
                    color: Theme.card
                    border.color: Theme.line
                    ColumnLayout {
                        id: exportBox
                        anchors.fill: parent
                        anchors.margins: 20
                        spacing: 10
                        Label { text: "Exportación"; color: Theme.text; font.pixelSize: 16; font.bold: true }
                        Label { text: "Fotogramas por segundo"; color: Theme.muted }
                        Row {
                            spacing: 6
                            Repeater {
                                model: [24, 25, 30, 50, 60]
                                Chip {
                                    required property int modelData
                                    objectName: "set:fps:" + modelData
                                    text: modelData + " fps"
                                    on: page.model.fps === modelData
                                    onClicked: page.model.setExport(modelData, page.model.quality)
                                }
                            }
                        }
                        Label { text: "Calidad"; color: Theme.muted }
                        Row {
                            spacing: 6
                            Repeater {
                                model: [["draft", "Borrador", "Más rápido, más compresión"],
                                        ["standard", "Estándar", "Equilibrio calidad / tamaño"],
                                        ["high", "Alta", "Mejor calidad, export más lento"]]
                                Chip {
                                    required property var modelData
                                    objectName: "set:quality:" + modelData[0]
                                    text: modelData[1]
                                    on: page.model.quality === modelData[0]
                                    ToolTip.visible: hover.hovered
                                    ToolTip.text: modelData[2]
                                    HoverHandler { id: hover }
                                    onClicked: page.model.setExport(page.model.fps, modelData[0])
                                }
                            }
                        }
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: txBox.implicitHeight + 40
                    radius: Theme.radiusLg
                    color: Theme.card
                    border.color: Theme.line
                    ColumnLayout {
                        id: txBox
                        anchors.fill: parent
                        anchors.margins: 20
                        spacing: 10
                        Label { text: "Transcripción (Whisper local)"; color: Theme.text; font.pixelSize: 16; font.bold: true }
                        Flow {
                            Layout.fillWidth: true
                            spacing: 6
                            Repeater {
                                model: [["tiny", "Tiny", "Más rápido, menos preciso (~75 MB)"],
                                        ["base", "Base", "Equilibrio velocidad / calidad (~140 MB)"],
                                        ["small", "Small", "Mejor precisión, un poco más lento (~460 MB)"],
                                        ["medium", "Medium", "Alta precisión, más lento (~1.5 GB)"],
                                        ["large-v3", "Large v3", "Máxima precisión (~3 GB)"]]
                                Chip {
                                    required property var modelData
                                    text: modelData[1]
                                    on: page.model.transcribeModel === modelData[0]
                                    ToolTip.visible: txHover.hovered
                                    ToolTip.text: modelData[2]
                                    HoverHandler { id: txHover }
                                    onClicked: page.model.setTranscribeModel(modelData[0])
                                }
                            }
                        }
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: keysBox.implicitHeight + 40
                    radius: Theme.radiusLg
                    color: Theme.card
                    border.color: Theme.line
                    ColumnLayout {
                        id: keysBox
                        anchors.fill: parent
                        anchors.margins: 20
                        spacing: 6
                        RowLayout {
                            Layout.fillWidth: true
                            Label { Layout.fillWidth: true; text: "API keys"; color: Theme.text; font.pixelSize: 16; font.bold: true }
                            // Las de video-yt (su settings.json y sus .env), al JSON de aquí: solo las que faltan.
                            Button {
                                objectName: "set:importKeys"
                                text: "Traer las de video-yt…"
                                onClicked: keysDialog.open()
                            }
                        }
                        Label {
                            Layout.fillWidth: true
                            wrapMode: Text.Wrap
                            text: "Se guardan en " + page.folder.path + "\\data\\settings.json, solo en este PC (no van al repositorio ni a las releases), y no se muestran nunca. Varias claves por proveedor se turnan. «Traer las de video-yt…» copia las que aquí faltan de su settings.json y de sus .env (GEMINI_API_KEY=…)."
                            color: Theme.muted
                        }
                        Label {
                            objectName: "set:keysMessage"
                            Layout.fillWidth: true
                            visible: !!page.model.message
                            wrapMode: Text.Wrap
                            text: page.model.message
                            color: Theme.success
                        }
                        Repeater {
                            model: page.model.providers
                            delegate: RowLayout {
                                id: row
                                required property var modelData
                                property bool adding: false
                                Layout.fillWidth: true
                                spacing: 10
                                Label { Layout.preferredWidth: 130; text: row.modelData.label; color: Theme.text; font.bold: true }
                                Label {
                                    Layout.preferredWidth: 110
                                    text: row.modelData.use + (row.modelData.wired ? "" : " · sin uso")
                                    color: Theme.muted
                                    font.pixelSize: 12
                                }
                                Label {
                                    Layout.fillWidth: true
                                    visible: !row.adding
                                    text: row.modelData.count ? row.modelData.count + (row.modelData.count > 1 ? " claves" : " clave") : row.modelData.hint
                                    color: row.modelData.count ? Theme.textSecondary : Theme.muted
                                    elide: Text.ElideRight
                                    font.pixelSize: 12
                                }
                                TextField {
                                    id: keyField
                                    objectName: "set:key:" + row.modelData.id
                                    Layout.fillWidth: true
                                    visible: row.adding
                                    echoMode: TextInput.Password
                                    placeholderText: "Pega la clave…"
                                    color: Theme.text
                                    background: Rectangle { radius: Theme.radiusSm; color: Theme.card2; border.color: Theme.accent }
                                    onAccepted: { page.model.addKey(row.modelData.id, text); text = ""; row.adding = false }
                                    Keys.onEscapePressed: { text = ""; row.adding = false }
                                }
                                Button {
                                    objectName: "set:add:" + row.modelData.id
                                    text: row.adding ? "Guardar" : "Añadir"
                                    onClicked: {
                                        if (row.adding) {
                                            page.model.addKey(row.modelData.id, keyField.text)
                                            keyField.text = ""
                                        }
                                        row.adding = !row.adding
                                        if (row.adding)
                                            keyField.forceActiveFocus()
                                    }
                                }
                                Button {
                                    objectName: "set:remove:" + row.modelData.id
                                    visible: row.modelData.count > 0
                                    text: "Quitar"
                                    onClicked: page.model.removeKey(row.modelData.id, row.modelData.count - 1)
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    FolderDialog {
        id: folderDialog
        title: "¿Dónde se guardan los proyectos?"
        currentFolder: page.folder.url
        onAccepted: page.folder.choose(selectedFolder.toString())
    }
    // La carpeta de video-yt (o su backend): sus claves, al settings.json de aquí.
    FolderDialog {
        id: keysDialog
        title: "¿Dónde está video-yt?"
        onAccepted: page.model.importKeys(selectedFolder.toString())
    }
}
