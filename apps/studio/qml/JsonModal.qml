import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// «JSON del proyecto» (JsonEditor.jsx): el manifest del proyecto para verlo o editarlo a mano; al
// guardar se aplica al proyecto y se escribe manifest.json en su carpeta. Copiar lo lleva al
// portapapeles.
Popup {
    id: modal
    objectName: "jsonModal"
    required property var model
    property string error: ""
    property string info: ""
    property bool busy: false

    function openNow() {
        error = ""
        info = ""
        jsonText.text = model.projectJson()
        open()
    }
    function save() {
        error = ""
        info = ""
        busy = true
        const r = model.saveProjectJson(jsonText.text)
        busy = false
        if (r.error) {
            error = r.error
            return
        }
        jsonText.text = r.text
        info = r.info
    }

    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(parent.width - 40, 860)
    height: Math.min(parent.height - 40, 640)
    modal: true
    focus: true
    padding: 18
    background: Rectangle { radius: Theme.radiusLg; color: Theme.card; border.color: Theme.lineBright }

    contentItem: ColumnLayout {
        spacing: 10
        RowLayout {
            Label { text: "JSON del proyecto"; color: Theme.text; font.pixelSize: 16; font.bold: true }
            Item { Layout.fillWidth: true }
            IconBtn { objectName: "json:close"; iconName: "close"; iconSize: 20; tip: "Cerrar"; onClicked: modal.close() }
        }
        Label {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            color: Theme.muted
            font.pixelSize: 12
            text: "Puedes editarlo a mano. Al guardar se aplica al proyecto y se escribe manifest.json en su carpeta."
        }
        ScrollView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            background: Rectangle { radius: Theme.radiusSm; color: Theme.card2; border.color: Theme.lineBright }
            TextArea {
                id: jsonText
                objectName: "json:text"
                color: Theme.text
                font.family: "Consolas"
                font.pixelSize: 12
                selectByMouse: true
                wrapMode: TextEdit.NoWrap
                background: null
            }
        }
        Label { objectName: "json:error"; visible: !!modal.error; Layout.fillWidth: true; wrapMode: Text.WordWrap; text: "⚠️ " + modal.error; color: Theme.danger }
        Label { objectName: "json:info"; visible: !!modal.info; Layout.fillWidth: true; wrapMode: Text.WordWrap; text: modal.info; color: "#4ade80" }
        RowLayout {
            Layout.fillWidth: true
            GhostButton {
                objectName: "json:copy"
                iconName: "content_copy"
                text: "Copiar"
                onClicked: {
                    jsonText.selectAll()
                    jsonText.copy()
                    jsonText.deselect()
                    modal.info = "Copiado al portapapeles."
                }
            }
            Item { Layout.fillWidth: true }
            GhostButton { text: "Cerrar"; onClicked: modal.close() }
            PrimaryButton {
                objectName: "json:save"
                text: modal.busy ? "Guardando…" : "Guardar"
                enabled: !modal.busy
                onClicked: modal.save()
            }
        }
    }
}
