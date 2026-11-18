import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// «Crear clip» del Clip Editor (SegmentConfirmModal.jsx): revisar el título y la descripción del tramo marcado
// (Z/X) y confirmar; «Agregar a material» es lo mismo con el vídeo entero y la descripción a mano. El clip se
// guarda por referencia (sin render), así que no hay fase de progreso: si falla, el error y «Reintentar».
Popup {
    id: modal
    objectName: "segModal"
    required property var model
    property var fmtSecs: (s) => String(s)
    readonly property var ask: model.segAsk
    readonly property bool canConfirm: String(ask.title || "").trim().length > 0

    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(parent.width - 40, 520)
    modal: true
    focus: true
    padding: 18
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    onClosed: if (model.segAsk.phase) model.cancelSegment()
    onOpened: { titleField.forceActiveFocus(); titleField.selectAll() }
    // Se abre con segAsk y se cierra cuando se vacía (Confirmar, Cancelar o Esc).
    Connections {
        target: modal.model
        function onSegAskChanged() {
            if (modal.model.segAsk.phase) {
                if (!modal.opened)
                    modal.open()
            } else if (modal.opened) {
                modal.close()
            }
        }
    }
    background: Rectangle { radius: Theme.radiusLg; color: Theme.card; border.color: Theme.lineBright }

    contentItem: ColumnLayout {
        spacing: 12
        Keys.onReturnPressed: (e) => { if (modal.canConfirm) modal.model.confirmSegment(); e.accepted = true }
        Keys.onEnterPressed: (e) => { if (modal.canConfirm) modal.model.confirmSegment(); e.accepted = true }
        RowLayout {
            MIcon { name: "add_to_photos"; size: 20; color: Theme.text }
            Label { text: modal.ask.simple ? "Agregar a material" : "Crear clip"; color: Theme.text; font.pixelSize: 16; font.bold: true }
            Item { Layout.fillWidth: true }
            IconBtn { objectName: "seg:close"; iconName: "close"; iconSize: 18; tip: "Cerrar"; onClicked: modal.model.cancelSegment() }
        }
        // .seg-confirm-range: el tramo en segundos del archivo, su duración y que no se renderiza nada.
        RowLayout {
            spacing: 8
            MIcon { name: "first_page"; size: 15; color: Theme.textSecondary }
            Label { objectName: "seg:start"; text: modal.fmtSecs(modal.ask.start); color: Theme.text; font.pixelSize: 12 }
            Label { text: "→"; color: Theme.muted }
            MIcon { name: "last_page"; size: 15; color: Theme.textSecondary }
            Label { objectName: "seg:end"; text: modal.fmtSecs(modal.ask.end); color: Theme.text; font.pixelSize: 12 }
            Label { text: modal.fmtSecs(Math.max(0, modal.ask.end - modal.ask.start)); color: Theme.accent; font.pixelSize: 12; font.bold: true }
            Item { Layout.fillWidth: true }
            Label {
                text: "Sin render"
                color: Theme.muted
                font.pixelSize: 11
                ToolTip.visible: kindHover.hovered
                ToolTip.text: "Usa el tramo del vídeo original por referencia: no se renderiza nada."
                HoverHandler { id: kindHover }
            }
        }
        Label { text: "Título"; color: Theme.textSecondary; font.pixelSize: 12 }
        TextField {
            id: titleField
            objectName: "seg:title"
            Layout.fillWidth: true
            maximumLength: 160
            placeholderText: "Título del clip"
            text: modal.ask.title || ""
            color: Theme.text
            background: Rectangle { radius: Theme.radiusSm; color: Theme.card2; border.color: Theme.lineBright }
            onTextEdited: modal.model.patchSegAsk({ title: text })
        }
        Label { text: "Descripción"; color: Theme.textSecondary; font.pixelSize: 12 }
        // DESC_MODES: qué descripción lleva el clip nuevo («la del vídeo original», solo si tiene).
        ColumnLayout {
            visible: !modal.ask.simple
            spacing: 2
            Repeater {
                model: [["manual", "Escribir una descripción"], ["source", "Usar la del vídeo original"], ["none", "Sin descripción"]]
                RadioButton {
                    required property var modelData
                    objectName: "seg:mode:" + modelData[0]
                    text: modelData[1]
                    checkable: false // lo marca el modelo: así el enlace no se rompe
                    checked: modal.ask.descMode === modelData[0]
                    enabled: modelData[0] !== "source" || String(modal.ask.sourceDescription || "").trim().length > 0
                    onClicked: modal.model.patchSegAsk({ descMode: modelData[0] })
                }
            }
        }
        ScrollView {
            visible: !!modal.ask.simple || modal.ask.descMode === "manual"
            Layout.fillWidth: true
            Layout.preferredHeight: 72
            background: Rectangle { radius: Theme.radiusSm; color: Theme.card2; border.color: Theme.lineBright }
            TextArea {
                objectName: "seg:description"
                placeholderText: "¿Qué pasa en este clip?"
                text: modal.ask.description || ""
                color: Theme.text
                wrapMode: TextEdit.Wrap
                background: null
                onTextChanged: if (activeFocus) modal.model.patchSegAsk({ description: text }) // Enter: salto de línea
            }
        }
        Label { // .seg-desc-preview
            visible: !modal.ask.simple && modal.ask.descMode === "source"
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            text: modal.ask.sourceDescription || ""
            color: Theme.textSecondary
            font.pixelSize: 12
        }
        Label {
            objectName: "seg:error"
            visible: modal.ask.phase === "error"
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            text: "⚠️ " + (modal.ask.error || "")
            color: Theme.danger
        }
        RowLayout {
            Layout.fillWidth: true
            Item { Layout.fillWidth: true }
            GhostButton { objectName: "seg:cancel"; text: "Cancelar"; onClicked: modal.model.cancelSegment() }
            PrimaryButton {
                objectName: "seg:confirm"
                iconName: "check"
                text: modal.ask.phase === "error" ? "Reintentar" : "Confirmar"
                enabled: modal.canConfirm
                onClicked: modal.model.confirmSegment()
            }
        }
    }
}
