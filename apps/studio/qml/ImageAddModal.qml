import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Dialogs
import QtQuick.Layouts

// «Nueva imagen» (ImageAddModal.jsx): seleccionar imágenes y darles una descripción
// antes de importarlas al proyecto. Cada fila se importa sola o todas a la vez.
Popup {
    id: modal
    objectName: "imageAddModal"
    required property var model
    property var rows: []
    property string error: ""
    readonly property int pending: rows.filter((r) => !r.saved).length

    function openNew() {
        rows = []
        error = ""
        open()
    }

    function addFiles(urls) {
        const seen = new Set(rows.filter((r) => r.url).map((r) => r.url.toString()))
        let next = [...rows]
        for (const u of urls) {
            const s = u.toString()
            if (seen.has(s)) continue
            seen.add(s)
            next.push({ url: u, description: "", saving: false, saved: false, error: "" })
        }
        rows = next
    }

    function removeRow(index) {
        let next = [...rows]
        next.splice(index, 1)
        rows = next
    }

    function patchRow(index, patch) {
        let next = [...rows]
        next[index] = Object.assign({}, next[index], patch)
        rows = next
    }

    function saveRow(index) {
        const row = rows[index]
        if (!row || row.saved || row.saving) return
        patchRow(index, { saving: true, error: "" })
        const ok = model.importImageWithDescription(row.url, row.description)
        if (ok) {
            patchRow(index, { saved: true, saving: false })
        } else {
            patchRow(index, { saving: false, error: "No se pudo guardar." })
        }
    }

    function saveAll() {
        error = ""
        let allOk = true
        for (let i = 0; i < rows.length; ++i) {
            if (!rows[i].saved) {
                saveRow(i)
                if (!rows[i].saved) allOk = false
            }
        }
        if (allOk) modal.close()
    }

    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(parent ? parent.width - 40 : 500, 520)
    height: Math.min(parent ? parent.height - 60 : 460, 460)
    modal: true
    dim: true
    padding: 0
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    Overlay.modal: Rectangle { color: Qt.rgba(0, 0, 0, 0.55) }

    background: Rectangle {
        radius: Theme.radiusMd
        color: Theme.bg
        border.color: Theme.line
    }

    contentItem: ColumnLayout {
        spacing: 0

        // Cabecera.
        RowLayout {
            Layout.fillWidth: true
            Layout.margins: 12
            spacing: 8
            MIcon { name: "image"; size: 18; color: Theme.text }
            Label { text: "Nueva imagen"; color: Theme.text; font.pixelSize: 14; font.weight: Font.Bold; Layout.fillWidth: true }
            AbstractButton {
                objectName: "imageAdd:close"
                width: 26; height: 26
                onClicked: modal.close()
                contentItem: MIcon { name: "close"; size: 16; color: Theme.muted }
                background: Rectangle { radius: 4; color: parent.hovered ? Theme.card3 : "transparent" }
                hoverEnabled: true
            }
        }
        Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: Theme.line }

        // Zona de selección de archivos.
        AbstractButton {
            objectName: "imageAdd:pick"
            Layout.fillWidth: true
            Layout.margins: 12
            implicitHeight: modal.rows.length > 0 ? 36 : 70
            hoverEnabled: true
            onClicked: imgPickDialog.open()
            background: Rectangle {
                radius: Theme.radiusSm
                color: parent.hovered ? Theme.card3 : Theme.card2
                border.color: Theme.lineBright
                border.width: 1
            }
            contentItem: ColumnLayout {
                spacing: 2
                Row {
                    Layout.alignment: Qt.AlignHCenter
                    spacing: 6
                    MIcon { name: "unarchive"; size: modal.rows.length > 0 ? 15 : 22; color: Theme.muted; anchors.verticalCenter: parent.verticalCenter }
                    Label {
                        text: modal.rows.length > 0 ? "Seleccionar más imágenes" : "Selecciona imágenes"
                        color: Theme.text; font.pixelSize: 12; anchors.verticalCenter: parent.verticalCenter
                    }
                }
                Label {
                    visible: modal.rows.length === 0
                    Layout.alignment: Qt.AlignHCenter
                    text: "PNG, JPG, WebP, GIF"
                    color: Theme.muted; font.pixelSize: 11
                }
            }
        }
        FileDialog {
            id: imgPickDialog
            title: "Seleccionar imágenes"
            fileMode: FileDialog.OpenFiles
            nameFilters: ["Imágenes (*.png *.jpg *.jpeg *.webp *.gif *.bmp *.avif)", "Todos (*)"]
            onAccepted: modal.addFiles(selectedFiles)
        }

        // Tabla de filas.
        Flickable {
            visible: modal.rows.length > 0
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.leftMargin: 12; Layout.rightMargin: 12
            clip: true
            contentHeight: imgColumn.implicitHeight
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
            Column {
                id: imgColumn
                width: parent.width
                spacing: 6
                Repeater {
                    model: modal.rows
                    RowLayout {
                        id: imgRow
                        required property var modelData
                        required property int index
                        width: imgColumn.width
                        spacing: 6
                        opacity: modelData.saved ? 0.5 : 1
                        Image {
                            Layout.preferredWidth: 48; Layout.preferredHeight: 48
                            source: imgRow.modelData.url || ""
                            fillMode: Image.PreserveAspectCrop
                            asynchronous: true
                            sourceSize.width: 96
                        }
                        TextField {
                            Layout.fillWidth: true
                            objectName: "imageAdd:desc:" + imgRow.index
                            placeholderText: "De qué trata"
                            text: imgRow.modelData.description || ""
                            enabled: !imgRow.modelData.saved && !imgRow.modelData.saving
                            color: Theme.text
                            background: Rectangle { radius: Theme.radiusSm; color: Theme.card2; border.color: Theme.lineBright }
                            onTextChanged: {
                                if (text !== imgRow.modelData.description)
                                    modal.patchRow(imgRow.index, { description: text })
                            }
                        }
                        MIcon {
                            visible: imgRow.modelData.saved
                            name: "check"; size: 16; color: Theme.accent
                        }
                        AbstractButton {
                            visible: !imgRow.modelData.saved
                            width: 24; height: 24
                            hoverEnabled: true
                            enabled: !imgRow.modelData.saving
                            onClicked: modal.removeRow(imgRow.index)
                            contentItem: MIcon { name: "delete"; size: 15; color: Theme.muted }
                            background: Rectangle { radius: 4; color: parent.hovered ? Theme.card3 : "transparent" }
                        }
                    }
                }
            }
        }

        // Error.
        Label {
            visible: !!modal.error
            Layout.fillWidth: true
            Layout.margins: 12
            text: modal.error
            color: "#ff5555"
            wrapMode: Text.Wrap
        }

        // Acciones.
        Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: Theme.line }
        RowLayout {
            Layout.fillWidth: true
            Layout.margins: 12
            spacing: 8
            Item { Layout.fillWidth: true }
            SmallButton {
                objectName: "imageAdd:cancel"
                text: "Cancelar"
                onClicked: modal.close()
            }
            SmallButton {
                objectName: "imageAdd:save"
                text: modal.pending > 1 ? "Guardar todas (" + modal.pending + ")" : "Guardar"
                enabled: modal.pending > 0
                onClicked: modal.saveAll()
            }
        }
    }
}
