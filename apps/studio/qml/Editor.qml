import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// El editor (VideoEditor.jsx): barra de herramientas, vista previa, inspector y timeline.
Item {
    id: editor
    required property var model
    signal closed
    signal openSettings

    Shortcut { sequences: [StandardKey.Undo]; enabled: editor.visible; onActivated: editor.model.undo() }
    Shortcut { sequences: [StandardKey.Redo, "Ctrl+Y"]; enabled: editor.visible; onActivated: editor.model.redo() }
    Shortcut { sequences: ["Delete", "Backspace"]; enabled: editor.visible; onActivated: editor.model.deleteSelection() }
    Shortcut { sequence: "S"; enabled: editor.visible; onActivated: editor.model.splitAtPlayhead() }
    Shortcut { sequence: "Ctrl+D"; enabled: editor.visible; onActivated: editor.model.duplicateSelection() }
    Shortcut { sequences: [StandardKey.Copy]; enabled: editor.visible; onActivated: editor.model.copySelection() }
    Shortcut { sequences: [StandardKey.Cut]; enabled: editor.visible; onActivated: editor.model.cutSelection() }
    Shortcut { sequences: [StandardKey.Paste]; enabled: editor.visible; onActivated: editor.model.paste() }
    Shortcut { sequence: "Alt+C"; enabled: editor.visible; onActivated: editor.model.copyKeyframe() }
    Shortcut { sequence: "Alt+V"; enabled: editor.visible; onActivated: editor.model.pasteKeyframe() }
    Shortcut { sequence: "Alt+D"; enabled: editor.visible; onActivated: editor.model.duplicateKeyframe() }
    Shortcut { sequence: "Ctrl+Alt+C"; enabled: editor.visible; onActivated: editor.model.copyAttributes() }
    Shortcut { sequence: "Ctrl+Alt+V"; enabled: editor.visible; onActivated: pasteAttrs.openFor(editor.model.pasteableGroups()) }
    Shortcut { sequence: "Escape"; enabled: editor.visible; onActivated: editor.model.clearSelection() }
    Shortcut { sequence: "Space"; enabled: editor.visible; onActivated: editor.playing = !editor.playing }
    Shortcut { sequence: "Left"; enabled: editor.visible; onActivated: editor.model.nudgePlayhead(-0.1) }
    Shortcut { sequence: "Right"; enabled: editor.visible; onActivated: editor.model.nudgePlayhead(0.1) }
    Shortcut { sequence: "Shift+Left"; enabled: editor.visible; onActivated: editor.model.nudgePlayhead(-1) }
    Shortcut { sequence: "Shift+Right"; enabled: editor.visible; onActivated: editor.model.nudgePlayhead(1) }
    Shortcut { sequence: "V"; enabled: editor.visible; onActivated: editor.model.toggleDisabled() }
    Shortcut { sequence: "M"; enabled: editor.visible; onActivated: editor.model.toggleMarker() }
    Shortcut { sequence: ","; enabled: editor.visible; onActivated: editor.model.jumpSnap(-1) }
    Shortcut { sequence: "."; enabled: editor.visible; onActivated: editor.model.jumpSnap(1) }

    // Reproducir sin vídeo todavía: el cabezal avanza en tiempo real hasta el final.
    property bool playing: false
    Timer {
        interval: 16
        repeat: true
        running: editor.playing && editor.visible
        property double last: 0
        onRunningChanged: last = Date.now()
        onTriggered: {
            const now = Date.now()
            const next = editor.model.playhead + (now - last) / 1000
            last = now
            if (next >= editor.model.duration) {
                editor.model.playhead = editor.model.duration
                editor.playing = false
            } else {
                editor.model.playhead = next
            }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 12
        spacing: 10

        RowLayout {
            Layout.fillWidth: true
            spacing: 6
            ToolButton {
                text: "‹ Proyectos"
                palette.buttonText: Theme.textSecondary
                background: Rectangle { radius: Theme.radiusSm; color: parent.hovered ? Qt.rgba(1, 1, 1, 0.06) : "transparent" }
                onClicked: editor.closed()
            }
            Label {
                Layout.fillWidth: true
                text: editor.model.projectName
                color: Theme.text
                font.pixelSize: 16
                font.bold: true
                elide: Text.ElideRight
            }
            IconButton { objectName: "editor:addText"; text: "T"; tip: "Añadir texto en el cabezal"; onClicked: editor.model.addText() }
            IconButton {
                objectName: "editor:addShape"
                text: "◆"
                tip: "Figuras y capa de ajuste"
                onClicked: shapeMenu.popup()
                Menu {
                    id: shapeMenu
                    Repeater {
                        model: [["square", "Cuadrado"], ["circle", "Círculo"], ["rect", "Rectángulo"], ["line", "Línea"],
                                ["arrow", "Flecha"], ["arrow_curve", "Flecha curva"], ["arrow_double", "Flecha doble"],
                                ["triangle", "Triángulo"], ["star", "Estrella"], ["polygon", "Polígono"], ["speech", "Burbuja"],
                                ["check", "Check"], ["x", "X"], ["marker", "Marcador"], ["heart", "Corazón"]]
                        MenuItem {
                            required property var modelData
                            objectName: "shape:" + modelData[0]
                            text: modelData[1]
                            onTriggered: editor.model.addShape(modelData[0], modelData[1])
                        }
                    }
                    MenuSeparator {}
                    MenuItem { objectName: "shape:adjustment"; text: "Capa de ajuste"; onTriggered: editor.model.addAdjustment() }
                }
            }
            IconButton { text: "⚙"; tip: "Ajustes (Ctrl+,)"; onClicked: editor.openSettings() }
            IconButton { text: "↶"; tip: "Deshacer (Ctrl+Z)"; enabled: editor.model.canUndo; onClicked: editor.model.undo() }
            IconButton { text: "↷"; tip: "Rehacer (Ctrl+Y)"; enabled: editor.model.canRedo; onClicked: editor.model.redo() }
            IconButton { text: editor.playing ? "⏸" : "▶"; tip: "Reproducir (Espacio)"; onClicked: editor.playing = !editor.playing }
            IconButton { text: "⚑"; tip: "Marcador en el cabezal (M)"; onClicked: editor.model.toggleMarker() }
            IconButton { text: "✂"; tip: "Cortar en el cabezal (S)"; onClicked: editor.model.splitAtPlayhead() }
            IconButton {
                objectName: "cropButton"
                text: "⛶"; tip: "Recortar el clip seleccionado"
                enabled: editor.model.selection.length > 0
                onClicked: {
                    editor.playing = false
                    cropModal.openFor(editor.model.openCrop())
                }
            }
            IconButton {
                text: "⧉"; tip: "Duplicar (Ctrl+D)"
                enabled: editor.model.selection.length > 0
                onClicked: editor.model.duplicateSelection()
            }
            IconButton {
                text: "🗑"; tip: "Eliminar (Supr)"
                enabled: editor.model.selection.length > 0
                onClicked: editor.model.deleteSelection()
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.preferredHeight: 3
            spacing: 10

            MaterialPanel {
                Layout.preferredWidth: 260
                Layout.fillHeight: true
                model: editor.model
                dragLayer: overlay
                sfxModal: sfxModal
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                radius: Theme.radiusLg
                color: Theme.card
                border.color: Theme.line

                Rectangle {
                    id: frame
                    readonly property real fit: Math.min(parent.width - 40, (parent.height - 56) * editor.model.aspect)
                    anchors.horizontalCenter: parent.horizontalCenter
                    y: 16
                    width: fit
                    height: fit / editor.model.aspect
                    color: "black"
                    border.color: Theme.lineBright
                    Label {
                        anchors.centerIn: parent
                        width: parent.width - 20
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.Wrap
                        text: "Vista previa\n(llega con gc_render)"
                        color: Theme.muted
                    }
                }
                Label {
                    anchors.top: frame.bottom
                    anchors.topMargin: 8
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: editor.model.formatTime(editor.model.playhead) + " / " + editor.model.formatTime(editor.model.duration)
                    color: Theme.textSecondary
                    font.family: "monospace"
                }
            }

            Inspector {
                Layout.preferredWidth: 300
                Layout.fillHeight: true
                model: editor.model
            }
        }

        Timeline {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.preferredHeight: 2
            model: editor.model
            onContextClip: (id) => clipMenu.openFor(id)
        }
    }

    Item { id: overlay; anchors.fill: parent; z: 100 }

    // «Pegar atributos» (EdPasteAttrs.jsx): qué grupos del clip copiado van a los seleccionados.
    CropModal { id: cropModal; model: editor.model }
    SfxModal { id: sfxModal; model: editor.model }
    ClipMenu { id: clipMenu; model: editor.model; onPasteAttributes: pasteAttrs.openFor(editor.model.pasteableGroups()) }

    Popup {
        id: pasteAttrs
        objectName: "pasteAttrs"
        property var groups: []
        property var picked: ({})
        function openFor(list) {
            if (!list.length)
                return
            groups = list
            const all = {}
            for (const g of list)
                all[g.id] = true
            picked = all
            open()
        }
        function chosen() { return groups.filter((g) => picked[g.id]).map((g) => g.id) }
        anchors.centerIn: parent
        width: 360
        modal: true
        padding: 20
        background: Rectangle { radius: Theme.radiusLg; color: Theme.card; border.color: Theme.lineBright }
        contentItem: ColumnLayout {
            spacing: 8
            Label { text: "Pegar atributos"; color: Theme.text; font.pixelSize: 16; font.bold: true }
            Label {
                text: pasteAttrs.groups.length ? (pasteAttrs.groups[0].total === 1 ? "En 1 clip" : "En " + pasteAttrs.groups[0].total + " clips") : ""
                color: Theme.muted
            }
            Repeater {
                model: pasteAttrs.groups
                CheckBox {
                    required property var modelData
                    objectName: "attr:" + modelData.id
                    text: modelData.label + (modelData.count < modelData.total ? "  (" + modelData.count + " de " + modelData.total + ")" : "")
                    checked: !!pasteAttrs.picked[modelData.id]
                    palette.windowText: Theme.text
                    onToggled: {
                        const next = Object.assign({}, pasteAttrs.picked)
                        next[modelData.id] = checked
                        pasteAttrs.picked = next
                    }
                }
            }
            RowLayout {
                Layout.alignment: Qt.AlignRight
                Button { text: "Cancelar"; onClicked: pasteAttrs.close() }
                Button {
                    objectName: "attr:paste"
                    text: "Pegar"
                    enabled: pasteAttrs.chosen().length > 0
                    onClicked: {
                        const groups = pasteAttrs.chosen()
                        pasteAttrs.close()
                        editor.model.pasteAttributes(groups)
                    }
                }
            }
        }
    }
}
