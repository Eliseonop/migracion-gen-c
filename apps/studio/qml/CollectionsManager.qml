import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Dialogs
import QtQuick.Layouts

// «Gestionar carpetas» de EdLibrary.jsx: la raíz de material, crear carpetas y, por carpeta,
// favorita y visible.
ColumnLayout {
    id: manager
    required property var model
    signal created(string id)
    signal stickCreated(string id)
    property bool stickForm: false
    spacing: 4

    RowLayout {
        Label { Layout.fillWidth: true; text: "Carpetas de material"; color: Theme.text; font.bold: true; font.pixelSize: 12 }
        SmallButton { text: "Raíz"; onClicked: rootDialog.open() }
    }
    Label {
        visible: !!manager.model.collectionsRoot
        Layout.fillWidth: true
        text: manager.model.collectionsRoot
        elide: Text.ElideMiddle
        color: Theme.muted
        font.pixelSize: 10
    }
    Label {
        visible: manager.model.collections.length === 0
        text: "No hay carpetas todavía."
        color: Theme.muted
        font.pixelSize: 11
    }
    Repeater {
        model: manager.model.collections
        RowLayout {
            required property var modelData
            Layout.fillWidth: true
            opacity: modelData.enabled ? 1 : 0.5
            ToolButton {
                objectName: "col:fav:" + parent.modelData.id
                implicitWidth: 24
                implicitHeight: 24
                text: parent.modelData.favorite ? "★" : "☆"
                palette.buttonText: parent.modelData.favorite ? Theme.folder : Theme.muted
                background: null
                ToolTip.visible: hovered
                ToolTip.text: parent.modelData.favorite ? "Quitar de favoritas" : "Marcar como favorita"
                onClicked: manager.model.setCollectionPrefs(parent.modelData.id, { favorite: !parent.modelData.favorite })
            }
            Label { Layout.fillWidth: true; text: (parent.modelData.stick ? (parent.modelData.emoji || "🎭") + " " : "") + parent.modelData.name; elide: Text.ElideRight; color: Theme.text; font.pixelSize: 12 }
            Label { text: parent.modelData.count; color: Theme.muted; font.pixelSize: 11 }
            CheckBox {
                objectName: "col:on:" + parent.modelData.id
                scale: 0.7
                checked: parent.modelData.enabled
                ToolTip.visible: hovered
                ToolTip.text: checked ? "Activa" : "Oculta"
                onToggled: manager.model.setCollectionPrefs(parent.modelData.id, { enabled: checked })
            }
        }
    }
    RowLayout {
        TextField {
            id: newName
            objectName: "col:newName"
            Layout.fillWidth: true
            placeholderText: "Nueva carpeta…"
            color: Theme.text
            font.pixelSize: 12
            background: Rectangle { radius: Theme.radiusSm; color: Theme.card2; border.color: Theme.lineBright }
            onAccepted: create.clicked()
        }
        SmallButton {
            id: create
            objectName: "col:create"
            text: "Crear"
            enabled: newName.text.trim().length > 0
            onClicked: {
                const id = manager.model.createCollection(newName.text)
                if (id) {
                    newName.text = ""
                    manager.created(id)
                }
            }
        }
    }

    // «Nuevo stick (personaje)» (CreateForm de EdLibrary.jsx): nombre, emoji y el color del croma.
    SmallButton {
        objectName: "col:newStick"
        visible: !manager.stickForm
        text: "Nuevo stick"
        onClicked: manager.stickForm = true
    }
    ColumnLayout {
        visible: manager.stickForm
        Layout.fillWidth: true
        spacing: 4
        Label { text: "Nuevo stick (personaje)"; color: Theme.text; font.bold: true; font.pixelSize: 12 }
        RowLayout {
            TextField {
                id: stickEmoji
                objectName: "stick:emoji"
                Layout.preferredWidth: 40
                maximumLength: 4
                placeholderText: "🙂"
                color: Theme.text
                background: Rectangle { radius: Theme.radiusSm; color: Theme.card2; border.color: Theme.lineBright }
            }
            TextField {
                id: stickName
                objectName: "stick:name"
                Layout.fillWidth: true
                placeholderText: "Nombre (p. ej. Robot)"
                color: Theme.text
                background: Rectangle { radius: Theme.radiusSm; color: Theme.card2; border.color: Theme.lineBright }
            }
            TextField {
                id: stickColor
                objectName: "stick:color"
                Layout.preferredWidth: 74
                text: "#00ff00"
                color: Theme.text
                validator: RegularExpressionValidator { regularExpression: /#[0-9a-fA-F]{6}/ }
                ToolTip.visible: hovered
                ToolTip.text: "Color del croma (el fondo que se quita al agregarlo)"
                background: Rectangle { radius: Theme.radiusSm; color: Theme.card2; border.color: stickColor.acceptableInput ? stickColor.text : Theme.danger }
            }
        }
        Label { text: "Se crea su carpeta con una subcarpeta por expresión (feliz, triste…)."; color: Theme.muted; font.pixelSize: 10; wrapMode: Text.WordWrap; Layout.fillWidth: true }
        RowLayout {
            Layout.alignment: Qt.AlignRight
            SmallButton { text: "Cancelar"; onClicked: manager.stickForm = false }
            SmallButton {
                objectName: "stick:create"
                text: "Crear"
                enabled: stickName.text.trim().length > 0 && stickColor.acceptableInput
                onClicked: {
                    const id = manager.model.createStick(stickName.text, stickEmoji.text, stickColor.text)
                    if (id) {
                        manager.stickForm = false
                        stickName.text = ""
                        stickEmoji.text = ""
                        manager.stickCreated(id)
                    }
                }
            }
        }
    }

    FolderDialog {
        id: rootDialog
        title: "Carpeta raíz de material"
        onAccepted: manager.model.setCollectionsRoot(selectedFolder.toString())
    }
}
