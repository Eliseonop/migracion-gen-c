import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import GenClip.Preview

// El editor (VideoEditor.jsx): barra de herramientas, vista previa, inspector y timeline.
Item {
    id: editor
    required property var model
    signal closed
    signal openSettings
    // El marco de plataforma del visor (platformOverlay de VideoEditor.jsx): solo vista previa.
    property string platformOverlay: "none"

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
    Shortcut { sequence: "Space"; enabled: editor.visible; onActivated: editor.model.player.toggle() }
    Shortcut { sequence: "Left"; enabled: editor.visible; onActivated: editor.model.nudgePlayhead(-0.1) }
    Shortcut { sequence: "Right"; enabled: editor.visible; onActivated: editor.model.nudgePlayhead(0.1) }
    Shortcut { sequence: "Shift+Left"; enabled: editor.visible; onActivated: editor.model.nudgePlayhead(-1) }
    Shortcut { sequence: "Shift+Right"; enabled: editor.visible; onActivated: editor.model.nudgePlayhead(1) }
    Shortcut { sequence: "V"; enabled: editor.visible; onActivated: editor.model.toggleDisabled() }
    Shortcut { sequence: "M"; enabled: editor.visible; onActivated: editor.model.toggleMarker() }
    Shortcut { sequence: ","; enabled: editor.visible; onActivated: editor.model.jumpSnap(-1) }
    Shortcut { sequence: "."; enabled: editor.visible; onActivated: editor.model.jumpSnap(1) }

    // Reproducir: el cabezal sigue al reloj del audio (player.hpp). Fuera del editor, parado.
    onVisibleChanged: if (!visible) editor.model.player.pause()

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
            IconButton {
                objectName: "editor:play"
                text: editor.model.player.playing ? "⏸" : "▶"
                tip: editor.model.player.audioError !== "" ? "Reproducir (Espacio) · " + editor.model.player.audioError
                                                            : "Reproducir (Espacio)"
                onClicked: editor.model.player.toggle()
            }
            IconButton { text: "⚑"; tip: "Marcador en el cabezal (M)"; onClicked: editor.model.toggleMarker() }
            IconButton { text: "✂"; tip: "Cortar en el cabezal (S)"; onClicked: editor.model.splitAtPlayhead() }
            IconButton {
                objectName: "cropButton"
                text: "⛶"; tip: "Recortar el clip seleccionado"
                enabled: editor.model.selection.length > 0
                onClicked: {
                    editor.model.player.pause()
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
            IconButton {
                objectName: "editor:export"
                text: "⤓"; tip: "Exportar el vídeo final"
                enabled: !editor.model.exporter.running
                onClicked: editor.model.exportVideo()
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
                    readonly property real fit: Math.min(parent.width - 40, (parent.height - 72) * editor.model.aspect)
                    anchors.horizontalCenter: parent.horizontalCenter
                    y: 16
                    width: fit
                    height: fit / editor.model.aspect
                    color: "black"
                    border.color: Theme.lineBright
                    // El fotograma del cabezal, el mismo que saca el export (gc::exporter::FrameRenderer).
                    FramePreview {
                        id: preview
                        objectName: "preview"
                        anchors.fill: parent
                        anchors.margins: 1 // dentro del borde
                        source: editor.model.preview
                    }
                    Label {
                        objectName: "preview:message"
                        anchors.centerIn: parent
                        width: parent.width - 20
                        visible: !editor.model.preview.hasFrame && text !== ""
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.Wrap
                        text: !editor.model.preview.available ? "Vista previa\n(sin gc_export: compila con MSVC + vcpkg)"
                                                              : editor.model.preview.error
                        color: Theme.muted
                    }
                    // Marco de TikTok o Shorts encima del recuadro exportable (drawPlatformChrome).
                    PlatformChrome {
                        id: chrome
                        objectName: "preview:chrome"
                        anchors.fill: preview
                        kind: editor.platformOverlay
                    }
                }
                // La guía de la máscara (drawMaskOverlay) y su arrastre (handleMaskPointer), con la pestaña
                // Máscara abierta. Más grande que el recuadro: los tiradores pueden caer fuera.
                MaskOverlay {
                    id: maskOverlay
                    objectName: "maskOverlay"
                    readonly property real pad: 24
                    readonly property rect out: preview.frameRect.width > 0 ? preview.frameRect
                                                                            : Qt.rect(0, 0, preview.width, preview.height)
                    anchors.fill: frame
                    anchors.margins: -pad
                    frame: Qt.rect(pad + preview.x + out.x, pad + preview.y + out.y, out.width, out.height)
                    mask: editor.model.editMask
                    visible: editor.model.maskMode && !!editor.model.editMask.type
                }
                MouseArea {
                    objectName: "maskArea"
                    anchors.fill: maskOverlay
                    enabled: maskOverlay.visible
                    hoverEnabled: enabled
                    onPressed: (m) => m.accepted = editor.model.maskPress(m.x, m.y, maskOverlay.frame, maskOverlay.canvasScale)
                    onPositionChanged: (m) => {
                        if (pressed)
                            editor.model.maskMove(m.x, m.y)
                        else
                            cursorShape = editor.model.maskCursor(m.x, m.y, maskOverlay.frame, maskOverlay.canvasScale)
                    }
                    onReleased: editor.model.maskRelease()
                    onCanceled: editor.model.maskRelease()
                }
                // El selector del marco de EdViewerTools (solo vista previa).
                ComboBox {
                    id: overlayPicker
                    objectName: "preview:overlay"
                    visible: chrome.overlays.length > 0
                    anchors.top: frame.bottom
                    anchors.topMargin: 4
                    anchors.right: frame.right
                    width: 150
                    height: 26
                    font.pixelSize: 11
                    model: chrome.overlays
                    textRole: "label"
                    valueRole: "id"
                    currentIndex: Math.max(0, chrome.overlays.findIndex((o) => o.id === editor.platformOverlay))
                    onActivated: editor.platformOverlay = currentValue
                    ToolTip.visible: hovered
                    ToolTip.delay: 600
                    ToolTip.text: "Marco de plataforma (solo vista previa): simula cómo se verá dentro de TikTok o YouTube Shorts"
                }
                Label {
                    id: timeLabel
                    anchors.top: frame.bottom
                    anchors.topMargin: 8
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: editor.model.formatTime(editor.model.playhead) + " / " + editor.model.formatTime(editor.model.duration)
                    color: Theme.textSecondary
                    font.family: "monospace"
                }
                // Los proxies que se están haciendo (proxy_maker.hpp); vacío si no hay ninguno.
                Label {
                    objectName: "preview:proxies"
                    anchors.top: timeLabel.bottom
                    anchors.topMargin: 2
                    anchors.horizontalCenter: parent.horizontalCenter
                    visible: text !== ""
                    text: editor.model.proxies.status
                    color: Theme.muted
                    font.pixelSize: 11
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

    // Los avisos de las acciones del editor (los beats detectados, un error…).
    Toast {
        z: 95
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 24
        notice: editor.model.notice
        onClosed: editor.model.clearNotice()
    }

    // El job de subtítulos del backend (el ed-sub-toast de useSubtitles): cómo va y, si falla, por qué.
    Rectangle {
        id: subtitleToast
        objectName: "subtitles:toast"
        readonly property var job: editor.model.subtitleJob
        readonly property bool working: job.status === "pending" || job.status === "running"
        readonly property bool failed: job.status === "error"
        visible: working || failed
        z: 96
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 72
        width: Math.min(subtitleLabel.implicitWidth + 28, parent.width - 40)
        height: subtitleLabel.implicitHeight + 16
        radius: Theme.radiusMd
        color: Theme.card3
        border.color: failed ? Theme.danger : Theme.accent2
        Label {
            id: subtitleLabel
            objectName: "subtitles:message"
            anchors.centerIn: parent
            width: parent.width - 28
            wrapMode: Text.WordWrap
            color: Theme.text
            text: subtitleToast.failed ? "⚠️ " + (subtitleToast.job.error || "")
                                       : (subtitleToast.job.message
                                          || (subtitleToast.job.video ? "Transcribiendo…" : "Generando subtítulos…"))
        }
        MouseArea { anchors.fill: parent; enabled: subtitleToast.failed; onClicked: editor.model.dismissSubtitleJob() }
    }

    // El export (export_job.hpp): su progreso y, al acabar, el archivo, la cancelación o el error.
    Rectangle {
        id: exportCard
        objectName: "export:card"
        readonly property var job: editor.model.exporter
        visible: job.running || job.message !== ""
        z: 90
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: 24
        width: 340
        height: exportColumn.implicitHeight + 24
        radius: Theme.radiusLg
        color: Theme.card
        border.color: Theme.lineBright
        ColumnLayout {
            id: exportColumn
            anchors.fill: parent
            anchors.margins: 12
            spacing: 8
            Label {
                objectName: "export:message"
                Layout.fillWidth: true
                text: exportCard.job.message
                color: exportCard.job.error !== "" ? Theme.danger : Theme.text
                wrapMode: Text.Wrap
            }
            ProgressBar {
                objectName: "export:progress"
                Layout.fillWidth: true
                visible: exportCard.job.running
                from: 0
                to: 1
                value: exportCard.job.progress
            }
            Label {
                objectName: "export:warnings"
                Layout.fillWidth: true
                visible: !exportCard.job.running && exportCard.job.warnings.length > 0
                text: "Se omitió: " + exportCard.job.warnings.join(" · ")
                color: Theme.muted
                font.pixelSize: 11
                wrapMode: Text.Wrap
            }
            RowLayout {
                Layout.alignment: Qt.AlignRight
                spacing: 6
                SmallButton {
                    objectName: "export:cancel"
                    visible: exportCard.job.running
                    text: "Cancelar"
                    onClicked: exportCard.job.cancel()
                }
                SmallButton {
                    objectName: "export:open"
                    visible: !exportCard.job.running && exportCard.job.file !== ""
                    text: "Abrir carpeta"
                    onClicked: Qt.openUrlExternally(exportCard.job.folderUrl)
                }
                SmallButton {
                    objectName: "export:close"
                    visible: !exportCard.job.running
                    text: "Cerrar"
                    onClicked: exportCard.job.dismiss()
                }
            }
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
