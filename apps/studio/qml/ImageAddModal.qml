import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Dialogs
import QtQuick.Layouts

// «Nueva imagen» (ImageAddModal.jsx): una fila por imagen elegida, con su vista y su descripción;
// cada una se sube al material del proyecto y se puede quitar de la lista antes de guardar.
Popup {
    id: modal
    objectName: "imageAdd"
    required property var model
    property var rows: []
    property string error: ""
    readonly property int pending: rows.filter((r) => !r.saved).length
    readonly property var imageRe: /\.(png|jpe?g|webp|gif|bmp|avif|heic)$/i // isImageFile

    function openNew() {
        rows = []
        error = ""
        open()
        picker.open()
    }
    function addFiles(urls) {
        const known = new Set(rows.map((r) => r.file))
        const images = urls.map((u) => u.toString()).filter((u) => modal.imageRe.test(u))
        if (urls.length && !images.length) {
            error = "Elige un PNG, JPG, WebP o una captura."
            return
        }
        error = ""
        rows = rows.concat(images.filter((u) => !known.has(u)).map((u) => ({ file: u, description: "", saved: false, error: "" })))
    }
    function patch(i, extra) {
        const next = rows.slice()
        next[i] = Object.assign({}, next[i], extra)
        rows = next
    }
    function save(i) {
        if (rows[i].saved)
            return true
        const err = model.addImage(rows[i].file, rows[i].description)
        patch(i, err ? { error: err } : { saved: true, error: "" })
        return !err
    }
    function saveAll() {
        let ok = true
        for (let i = 0; i < rows.length; ++i)
            ok = save(i) && ok
        if (ok)
            close()
    }

    anchors.centerIn: parent
    width: Math.min(parent.width - 40, 640)
    modal: true
    focus: true
    padding: 18
    background: Rectangle { radius: Theme.radiusLg; color: Theme.card; border.color: Theme.lineBright }

    FileDialog {
        id: picker
        title: "Imágenes para el material"
        fileMode: FileDialog.OpenFiles
        nameFilters: ["Imágenes (*.png *.jpg *.jpeg *.webp *.gif *.bmp *.avif *.heic)"]
        onAccepted: modal.addFiles(selectedFiles)
    }

    contentItem: ColumnLayout {
        spacing: 10
        RowLayout {
            Label { text: "Nueva imagen"; color: Theme.text; font.pixelSize: 16; font.bold: true }
            Item { Layout.fillWidth: true }
            IconBtn { iconName: "close"; iconSize: 20; tip: "Cerrar"; onClicked: modal.close() }
        }
        SmallButton {
            Layout.fillWidth: true
            text: modal.rows.length ? "Añadir más imágenes…" : "Elige las imágenes (PNG, JPG, WebP, GIF)…"
            onClicked: picker.open()
        }
        Repeater {
            model: modal.rows
            RowLayout {
                id: row
                required property var modelData
                required property int index
                Layout.fillWidth: true
                spacing: 8
                opacity: modelData.saved ? 0.6 : 1
                Image {
                    Layout.preferredWidth: 56
                    Layout.preferredHeight: 40
                    source: row.modelData.file
                    fillMode: Image.PreserveAspectCrop
                    sourceSize.width: 112
                    asynchronous: true
                }
                TextField {
                    objectName: "imageAdd:description:" + row.index
                    Layout.fillWidth: true
                    text: row.modelData.description
                    enabled: !row.modelData.saved
                    placeholderText: "De qué trata"
                    color: Theme.text
                    background: Rectangle { radius: Theme.radiusSm; color: Theme.card2; border.color: Theme.lineBright }
                    onEditingFinished: if (text !== row.modelData.description) modal.patch(row.index, { description: text })
                }
                Label { visible: row.modelData.saved; text: "✓"; color: "#4ade80" }
                IconBtn {
                    visible: !row.modelData.saved
                    iconName: "delete"
                    iconSize: 16
                    tip: "Quitar de la lista"
                    onClicked: modal.rows = modal.rows.filter((_, j) => j !== row.index)
                }
                Label {
                    visible: !!row.modelData.error
                    Layout.maximumWidth: 180
                    text: row.modelData.error || ""
                    elide: Text.ElideRight
                    color: Theme.danger
                    font.pixelSize: 11
                }
            }
        }
        Label { visible: !!modal.error; text: modal.error; color: Theme.danger }
        RowLayout {
            Layout.alignment: Qt.AlignRight
            GhostButton { text: "Cancelar"; onClicked: modal.close() }
            PrimaryButton {
                objectName: "imageAdd:saveAll"
                text: modal.pending > 1 ? "Guardar todos (" + modal.pending + ")" : "Guardar todos"
                enabled: modal.pending > 0
                onClicked: modal.saveAll()
            }
        }
    }
}
