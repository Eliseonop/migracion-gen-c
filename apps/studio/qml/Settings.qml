import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// Ajustes (EdSettings.jsx): exportación, transcripción y API keys. Las claves no se ven nunca: solo
// cuántas tiene cada proveedor; se añaden, se cambian o se quitan.
Rectangle {
    id: page
    required property var model
    signal closed

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
            ToolButton {
                text: "‹ Volver"
                palette.buttonText: Theme.textSecondary
                background: Rectangle { radius: Theme.radiusSm; color: parent.hovered ? Qt.rgba(1, 1, 1, 0.06) : "transparent" }
                onClicked: page.closed()
            }
            Label { text: "Ajustes"; color: Theme.text; font.pixelSize: 20; font.bold: true }
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
                        Label { text: "API keys"; color: Theme.text; font.pixelSize: 16; font.bold: true }
                        Label {
                            Layout.fillWidth: true
                            wrapMode: Text.Wrap
                            text: "Se guardan en data/settings.json y no se muestran nunca. Varias claves por proveedor se turnan."
                            color: Theme.muted
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
}
