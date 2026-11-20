import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// «Información del material» (MaterialInfoModal.jsx): título y descripción de un vídeo, una imagen o
// un audio, del proyecto o guardado. Es lo que lee la IA para saber qué representa cada material.
Popup {
    id: modal
    objectName: "materialInfo"
    required property var model
    property string key: ""
    property var info: ({})
    property string error: ""
    readonly property var kindLabel: ({ clips: "Vídeo", images: "Imagen", audios: "Audio" })

    function openFor(materialKey) {
        const found = model.materialInfo(materialKey)
        if (!found.kind)
            return
        key = materialKey
        info = found
        error = ""
        title.text = found.label
        description.text = found.description
        open()
        title.forceActiveFocus()
        title.selectAll()
    }
    function save() {
        error = model.saveMaterialInfo(key, title.text, description.text)
        if (!error)
            close()
    }
    function duration(s) { // fmtDur
        if (!(s > 0))
            return ""
        return Math.floor(s / 60) + ":" + String(Math.round(s % 60)).padStart(2, "0")
    }

    anchors.centerIn: parent
    width: Math.min(parent.width - 40, 520)
    modal: true
    focus: true
    padding: 18
    background: Rectangle { radius: Theme.radiusLg; color: Theme.card; border.color: Theme.lineBright }

    Shortcut { sequences: ["Ctrl+Return", "Ctrl+Enter"]; enabled: modal.opened; onActivated: modal.save() }

    component Chip: Rectangle {
        property alias text: chipLabel.text
        visible: !!text
        implicitWidth: chipLabel.implicitWidth + 14
        implicitHeight: 22
        radius: 11
        color: Theme.card2
        border.color: Theme.lineBright
        Label { id: chipLabel; anchors.centerIn: parent; color: Theme.textSecondary; font.pixelSize: 11 }
    }

    contentItem: ColumnLayout {
        spacing: 10
        RowLayout {
            Label { text: "Información del material"; color: Theme.text; font.pixelSize: 16; font.bold: true }
            Item { Layout.fillWidth: true }
            IconBtn { iconName: "close"; iconSize: 20; tip: "Cerrar (Esc)"; onClicked: modal.close() }
        }
        RowLayout {
            spacing: 6
            Chip { text: modal.kindLabel[modal.info.kind] || "Material" }
            Chip { text: modal.info.library ? "Guardado" : "Proyecto" }
            Chip { text: modal.duration(modal.info.duration) }
            Chip { text: modal.info.width && modal.info.height ? modal.info.width + "×" + modal.info.height : "" }
            Label {
                Layout.fillWidth: true
                text: modal.info.filename || ""
                elide: Text.ElideMiddle
                color: Theme.muted
                font.pixelSize: 11
            }
        }
        Label { text: "Título"; color: Theme.textSecondary; font.pixelSize: 12 }
        TextField {
            id: title
            objectName: "materialInfo:title"
            Layout.fillWidth: true
            placeholderText: "Nombre corto y reconocible: “Watney en el invernadero”"
            color: Theme.text
            background: Rectangle { radius: Theme.radiusSm; color: Theme.card2; border.color: Theme.lineBright }
        }
        Label { text: "Descripción"; color: Theme.textSecondary; font.pixelSize: 12 }
        ScrollView {
            Layout.fillWidth: true
            Layout.preferredHeight: 110
            background: Rectangle { radius: Theme.radiusSm; color: Theme.card2; border.color: Theme.lineBright }
            TextArea {
                id: description
                objectName: "materialInfo:description"
                wrapMode: TextEdit.Wrap
                color: Theme.text
                placeholderText: "Qué se ve y qué representa: personajes, acción, plano, ambiente… Es lo que lee la IA para elegir este material."
                background: null
            }
        }
        RowLayout {
            visible: !!modal.info.descriptionAi && modal.info.descriptionAi !== description.text
            Layout.fillWidth: true
            Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                text: "✨ Descripción de la IA: " + (modal.info.descriptionAi || "")
                color: Theme.muted
                font.pixelSize: 12
            }
            GhostButton { objectName: "materialInfo:useAi"; text: "Usar"; onClicked: description.text = modal.info.descriptionAi }
        }
        Label { visible: !!modal.error; Layout.fillWidth: true; wrapMode: Text.WordWrap; text: modal.error; color: Theme.danger }
        RowLayout {
            Layout.alignment: Qt.AlignRight
            GhostButton { text: "Cancelar"; onClicked: modal.close() }
            PrimaryButton { objectName: "materialInfo:save"; text: "Guardar"; tip: "Ctrl+Enter"; onClicked: modal.save() }
        }
    }
}
