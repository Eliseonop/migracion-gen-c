import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// Título y descripción de un material (MaterialInfoModal.jsx): es lo que la IA lee
// para saber qué representa cada asset al dirigir escenas.
Popup {
    id: modal
    objectName: "matInfoModal"
    required property var model
    readonly property var info: model.matInfo
    readonly property bool visible_: info.key !== undefined

    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(parent.width - 40, 520)
    modal: true
    focus: true
    padding: 18
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    onClosed: model.closeMaterialInfo()
    onOpened: { titleField.forceActiveFocus(); titleField.selectAll() }

    Connections {
        target: modal.model
        function onMatInfoChanged() {
            if (modal.model.matInfo.key !== undefined) {
                titleField.text = modal.model.matInfo.label || ""
                descField.text = modal.model.matInfo.description || ""
                if (!modal.opened)
                    modal.open()
            } else if (modal.opened) {
                modal.close()
            }
        }
    }

    background: Rectangle { radius: Theme.radiusLg; color: Theme.card; border.color: Theme.lineBright }

    contentItem: ColumnLayout {
        spacing: 10
        Keys.onReturnPressed: (e) => { if (e.modifiers & Qt.ControlModifier) modal.save(); e.accepted = true }
        Keys.onEnterPressed: (e) => { if (e.modifiers & Qt.ControlModifier) modal.save(); e.accepted = true }

        RowLayout {
            MIcon { name: "edit_note"; size: 20; color: Theme.text }
            Label { text: "Información del material"; color: Theme.text; font.pixelSize: 16; font.bold: true }
            Item { Layout.fillWidth: true }
            IconBtn { objectName: "matInfo:close"; iconName: "close"; iconSize: 18; tip: "Cerrar (Esc)"; onClicked: modal.model.closeMaterialInfo() }
        }

        Flow {
            Layout.fillWidth: true
            spacing: 6
            Rectangle {
                width: kindChip.implicitWidth + 12; height: 22; radius: 4; color: Theme.card2
                Label { id: kindChip; anchors.centerIn: parent; text: modal.info.kindLabel || ""; color: Theme.text; font.pixelSize: 11 }
            }
            Rectangle {
                width: scopeChip.implicitWidth + 12; height: 22; radius: 4; color: Theme.card2
                Label { id: scopeChip; anchors.centerIn: parent; text: modal.info.scopeLabel || ""; color: Theme.text; font.pixelSize: 11 }
            }
            Rectangle {
                visible: modal.info.duration > 0
                width: durChip.implicitWidth + 12; height: 22; radius: 4; color: Theme.card2
                Label { id: durChip; anchors.centerIn: parent; text: modal.model.formatTime(modal.info.duration || 0); color: Theme.text; font.pixelSize: 11 }
            }
            Rectangle {
                visible: (modal.info.width || 0) > 0 && (modal.info.height || 0) > 0
                width: dimChip.implicitWidth + 12; height: 22; radius: 4; color: Theme.card2
                Label { id: dimChip; anchors.centerIn: parent; text: (modal.info.width || 0) + "×" + (modal.info.height || 0); color: Theme.text; font.pixelSize: 11 }
            }
            Label {
                visible: !!modal.info.filename
                text: modal.info.filename || ""
                color: Theme.muted
                font.pixelSize: 11
                elide: Text.ElideMiddle
                width: Math.min(implicitWidth, parent.width)
            }
        }

        Label { text: "Título"; color: Theme.textSecondary; font.pixelSize: 12 }
        TextField {
            id: titleField
            objectName: "matInfo:title"
            Layout.fillWidth: true
            maximumLength: 200
            placeholderText: "Nombre corto y reconocible"
            color: Theme.text
            background: Rectangle { radius: Theme.radiusSm; color: Theme.card2; border.color: Theme.lineBright }
        }

        Label { text: "Descripción"; color: Theme.textSecondary; font.pixelSize: 12 }
        ScrollView {
            Layout.fillWidth: true
            Layout.preferredHeight: 100
            background: Rectangle { radius: Theme.radiusSm; color: Theme.card2; border.color: Theme.lineBright }
            TextArea {
                id: descField
                objectName: "matInfo:description"
                placeholderText: "Qué se ve y qué representa: personajes, acción, plano, ambiente… Es lo que lee la IA para elegir este material."
                color: Theme.text
                wrapMode: TextEdit.Wrap
                background: null
            }
        }

        RowLayout {
            visible: !!modal.info.descriptionAi && modal.info.descriptionAi !== descField.text
            spacing: 6
            MIcon { name: "auto_awesome"; size: 14; color: Theme.accent }
            Label {
                Layout.fillWidth: true
                text: "IA: " + (modal.info.descriptionAi || "")
                color: Theme.textSecondary
                font.pixelSize: 12
                elide: Text.ElideRight
                wrapMode: Text.WordWrap
                maximumLineCount: 2
            }
            GhostButton { text: "Usar"; onClicked: descField.text = modal.info.descriptionAi }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.topMargin: 6
            Item { Layout.fillWidth: true }
            GhostButton { objectName: "matInfo:cancel"; text: "Cancelar"; onClicked: modal.model.closeMaterialInfo() }
            PrimaryButton {
                objectName: "matInfo:save"
                text: "Guardar"
                onClicked: modal.save()
            }
        }
    }

    function save() {
        model.saveMaterialInfo(titleField.text, descField.text)
    }
}
