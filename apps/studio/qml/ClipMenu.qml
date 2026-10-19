import QtQuick
import QtQuick.Controls.Basic

// Menú contextual de un clip (ctxMenu de VideoEditor.jsx). El clip ya está elegido, así que las
// acciones de la selección valen para él.
Menu {
    id: menu
    objectName: "clipMenu"
    width: 260
    required property var model
    property string clipId
    property var info: ({})
    signal pasteAttributes

    function openFor(id) {
        clipId = id
        info = model.clipMenu(id)
        if (info.kind)
            popup()
    }
    function only(action) { // las de un solo clip: deja elegido solo este
        model.select(clipId, false, false)
        action()
    }

    component Entry: MenuItem {
        height: visible ? implicitHeight : 0
    }

    Entry {
        objectName: "ctx:favorite"
        visible: !!menu.info.sfx
        text: menu.info.favorite ? "Quitar de favoritos" : "Favorito"
        onTriggered: menu.model.toggleClipFavorite(menu.clipId)
    }
    Entry {
        visible: menu.info.kind === "text"
        text: "Aplicar como plantilla global"
        onTriggered: menu.model.applyGlobalTemplate(menu.clipId)
    }
    Entry { objectName: "ctx:forward"; visible: !!menu.info.canLayer; enabled: !!menu.info.canFront; text: "Adelante"; onTriggered: menu.model.moveLayer(menu.clipId, "forward") }
    Entry { objectName: "ctx:backward"; visible: !!menu.info.canLayer; enabled: !!menu.info.canBack; text: "Atrás"; onTriggered: menu.model.moveLayer(menu.clipId, "backward") }
    Entry { objectName: "ctx:front"; visible: !!menu.info.canLayer; enabled: !!menu.info.canFront; text: "Al frente"; onTriggered: menu.model.moveLayer(menu.clipId, "front") }
    Entry { objectName: "ctx:back"; visible: !!menu.info.canLayer; enabled: !!menu.info.canBack; text: "Al fondo"; onTriggered: menu.model.moveLayer(menu.clipId, "back") }
    Entry {
        objectName: "ctx:disable"
        text: (menu.info.disabled ? "Activar clip" : "Desactivar clip") + "\tV"
        onTriggered: menu.only(() => menu.model.toggleDisabled())
    }
    Entry {
        visible: !!menu.info.hasBeats
        text: "Quitar beats"
        onTriggered: menu.model.removeBeats(menu.clipId)
    }
    Entry { visible: menu.info.kind !== "audio"; text: "Voltear horizontal"; onTriggered: menu.only(() => menu.model.toggleFlip("h")) }
    Entry { visible: menu.info.kind !== "audio"; text: "Voltear vertical"; onTriggered: menu.only(() => menu.model.toggleFlip("v")) }
    MenuSeparator {}
    Entry { text: "Copiar atributos\tCtrl+Alt+C"; onTriggered: menu.only(() => menu.model.copyAttributes()) }
    Entry {
        enabled: !!menu.info.canPasteAttrs
        text: "Pegar atributos…\tCtrl+Alt+V"
        onTriggered: menu.pasteAttributes()
    }
    Entry { objectName: "ctx:split"; text: "Dividir aquí"; onTriggered: menu.only(() => menu.model.splitAtPlayhead()) }
    Entry { text: "Duplicar"; onTriggered: menu.model.duplicateSelection() }
    Entry { objectName: "ctx:delete"; text: "Eliminar"; onTriggered: menu.only(() => menu.model.deleteSelection()) }
}
