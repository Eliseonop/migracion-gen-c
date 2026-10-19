import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Dialogs
import QtQuick.Layouts

// «Nuevo SFX» / «Editar SFX» (SfxClassifyModal.jsx): una fila por audio con nombre, categoría
// (o una nueva) y uso típico; cada fila se guarda sola o todas a la vez.
Popup {
    id: modal
    objectName: "sfxModal"
    required property var model
    property var rows: []
    property bool editing: false
    property string defaultCategory: ""
    property string error: ""
    readonly property var cats: model.sfxCategories.filter((c) => c.id && c.id !== "favoritos")
    readonly property int pending: rows.filter((r) => !r.saved).length

    function openNew(category) {
        editing = false
        defaultCategory = category
        rows = []
        error = ""
        open()
    }
    function openEdit(key) {
        const draft = model.sfxDraft(key)
        if (!draft.sfxId)
            return
        editing = true
        rows = [draft]
        error = ""
        open()
    }
    function addFiles(urls) {
        const known = new Set(rows.map((r) => r.file))
        const added = model.sfxDrafts(urls, defaultCategory).filter((r) => !known.has(r.file))
        error = added.length || !urls.length ? "" : "Elige un MP3, WAV u OGG."
        rows = rows.concat(added)
    }
    function patch(i, extra) {
        const next = rows.slice()
        next[i] = Object.assign({}, next[i], extra)
        rows = next
    }
    function save(i) {
        if (rows[i].saved)
            return true
        const err = model.saveSfx(rows[i])
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
    width: Math.min(parent.width - 40, 820)
    modal: true
    focus: true
    padding: 18
    background: Rectangle { radius: Theme.radiusLg; color: Theme.card; border.color: Theme.lineBright }

    FileDialog {
        id: picker
        title: "Audios para la biblioteca de SFX"
        fileMode: FileDialog.OpenFiles
        nameFilters: ["Audio (*.mp3 *.wav *.ogg *.m4a *.aac *.flac)"]
        onAccepted: modal.addFiles(selectedFiles)
    }

    contentItem: ColumnLayout {
        spacing: 10
        RowLayout {
            Label { text: modal.editing ? "Editar SFX" : "Nuevo SFX"; color: Theme.text; font.pixelSize: 16; font.bold: true }
            Item { Layout.fillWidth: true }
            IconButton { text: "✕"; tip: "Cerrar"; onClicked: modal.close() }
        }
        SmallButton {
            visible: !modal.editing
            Layout.fillWidth: true
            text: modal.rows.length ? "Añadir más audios…" : "Elige los audios (MP3, WAV, OGG, M4A)…"
            onClicked: picker.open()
        }
        GridLayout {
            visible: modal.rows.length > 0
            Layout.fillWidth: true
            columns: 4
            columnSpacing: 8
            rowSpacing: 6
            Repeater {
                model: ["Nombre", "Categoría", "Uso típico", ""]
                Label { required property string modelData; text: modelData; color: Theme.muted; font.pixelSize: 11 }
            }
            Repeater {
                model: modal.rows.length * 4
                Loader {
                    required property int index
                    readonly property int row: Math.floor(index / 4)
                    readonly property var entry: modal.rows[row]
                    Layout.fillWidth: index % 4 !== 3
                    Layout.preferredWidth: index % 4 === 1 ? 220 : -1
                    sourceComponent: [nameCell, categoryCell, usoCell, saveCell][index % 4]
                }
            }
        }
        Label { visible: !!modal.error; text: modal.error; color: Theme.danger }
        RowLayout {
            Layout.alignment: Qt.AlignRight
            SmallButton { text: "Cancelar"; onClicked: modal.close() }
            SmallButton {
                objectName: "sfx:saveAll"
                text: modal.pending > 1 ? "Guardar todos (" + modal.pending + ")" : "Guardar todos"
                enabled: modal.pending > 0
                onClicked: modal.saveAll()
            }
        }
    }

    Component {
        id: nameCell
        TextField {
            objectName: "sfx:name:" + parent.row
            text: parent.entry.name
            enabled: !parent.entry.saved
            color: Theme.text
            background: Rectangle { radius: Theme.radiusSm; color: Theme.card2; border.color: Theme.lineBright }
            onEditingFinished: if (text !== parent.entry.name) modal.patch(parent.row, { name: text })
        }
    }
    Component {
        id: usoCell
        TextField {
            text: parent.entry.uso
            enabled: !parent.entry.saved
            placeholderText: "Para qué sirve"
            color: Theme.text
            background: Rectangle { radius: Theme.radiusSm; color: Theme.card2; border.color: Theme.lineBright }
            onEditingFinished: if (text !== parent.entry.uso) modal.patch(parent.row, { uso: text })
        }
    }
    Component {
        id: categoryCell
        RowLayout {
            id: cell
            readonly property int row: parent.row
            readonly property var entry: parent.entry
            readonly property bool known: modal.cats.some((c) => c.id === entry.categoryId)
            readonly property var options: !entry.categoryId || known ? modal.cats
                : modal.cats.concat([{ id: entry.categoryId, label: entry.categoryLabel || entry.categoryId }])
            property bool creating: false
            property string hint: ""
            ComboBox {
                visible: !cell.creating
                Layout.fillWidth: true
                enabled: !cell.entry.saved
                model: cell.options
                textRole: "label"
                valueRole: "id"
                displayText: currentIndex < 0 ? "Elige…" : currentText
                currentIndex: cell.options.findIndex((c) => c.id === cell.entry.categoryId)
                onActivated: modal.patch(cell.row, { categoryId: currentValue })
            }
            TextField {
                id: draft
                visible: cell.creating
                Layout.fillWidth: true
                placeholderText: cell.hint || "Nueva categoría"
                color: Theme.text
                background: Rectangle { radius: Theme.radiusSm; color: Theme.card2; border.color: Theme.lineBright }
                onAccepted: {
                    if (!text.trim()) {
                        cell.hint = "Ponle nombre."
                        return
                    }
                    const cat = modal.model.createSfxCategory(text)
                    if (cat.error) {
                        text = ""
                        cell.hint = cat.error
                        return
                    }
                    cell.creating = false
                    modal.patch(cell.row, { categoryId: cat.id })
                }
            }
            SmallButton {
                text: cell.creating ? "✕" : "＋"
                enabled: !cell.entry.saved
                onClicked: {
                    cell.creating = !cell.creating
                    cell.hint = ""
                    draft.text = ""
                }
            }
        }
    }
    Component {
        id: saveCell
        RowLayout {
            Label { visible: parent.parent.entry.saved; text: "✓"; color: "#4ade80" }
            SmallButton {
                visible: !parent.parent.entry.saved
                text: "Guardar"
                onClicked: modal.save(parent.parent.row)
            }
            Label {
                visible: !!parent.parent.entry.error
                Layout.maximumWidth: 160
                text: parent.parent.entry.error || ""
                elide: Text.ElideRight
                color: Theme.danger
                font.pixelSize: 11
            }
        }
    }
}
