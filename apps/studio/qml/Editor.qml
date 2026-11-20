import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQuick.Window
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
    Shortcut {
        sequence: "Escape"
        enabled: editor.visible
        // En el Clip Editor, Esc quita antes la marca Z/X.
        onActivated: editor.stageFullscreen ? editor.toggleStageFullscreen()
                                            : (editor.model.clearClipMark() || editor.model.clearSelection())
    }
    // El Clip Editor como extractor: Z = inicio, X = fin, Enter = Crear clip.
    readonly property bool clipMode: editor.model.canvasTab === "clip"
    // fmt de lib/utils.js: «m:ss» o «h:mm:ss», con los segundos hacia abajo.
    function fmtSecs(s) {
        const n = Math.max(0, Math.floor(s || 0))
        const ss = String(n % 60).padStart(2, "0")
        const h = Math.floor(n / 3600)
        const m = Math.floor((n % 3600) / 60)
        return h > 0 ? h + ":" + String(m).padStart(2, "0") + ":" + ss : m + ":" + ss
    }
    Shortcut { sequence: "Z"; enabled: editor.visible && editor.clipMode; onActivated: editor.model.markClip("in") }
    Shortcut { sequence: "X"; enabled: editor.visible && editor.clipMode; onActivated: editor.model.markClip("out") }
    Shortcut {
        sequences: ["Return", "Enter"]
        enabled: editor.visible && editor.clipMode
        onActivated: editor.model.createSegmentFromMark()
    }
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

    // Pantalla completa del reproductor (toggleFullscreen): solo la columna central, en toda la ventana.
    property bool stageFullscreen: false
    property int previousVisibility: Window.Windowed
    function toggleStageFullscreen() {
        const win = editor.Window.window
        if (!stageFullscreen) {
            previousVisibility = win ? win.visibility : Window.Windowed
            stageFullscreen = true
            if (win)
                win.visibility = Window.FullScreen
        } else {
            stageFullscreen = false
            if (win)
                win.visibility = previousVisibility === Window.FullScreen ? Window.Windowed : previousVisibility
        }
    }
    // El zoom del canvas (EdViewerTools): solo visual, no toca el clip ni el export. 25–400 %.
    property real viewZoom: 1
    function setZoomPct(pct) { viewZoom = Math.min(400, Math.max(25, Math.round(pct))) / 100 }
    // El tamaño a mano («Personalizado…»): {w, h} mientras se edita.
    property var customSize: null

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: editor.stageFullscreen ? 0 : 8
        spacing: 6

        // EdTopBar.jsx: volver, el proyecto y su estado, deshacer y rehacer; a la derecha, ajustes, ayuda y Exportar.
        Rectangle {
            objectName: "editor:topbar"
            visible: !editor.stageFullscreen
            Layout.fillWidth: true
            implicitHeight: 44
            radius: 6
            color: Theme.topbar
            border.color: Theme.line
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 6
                anchors.rightMargin: 10
                spacing: 6
                AbstractButton {
                    id: backButton
                    objectName: "editor:back"
                    implicitWidth: 34
                    implicitHeight: 34
                    hoverEnabled: true
                    ToolTip.visible: hovered
                    ToolTip.delay: 500
                    ToolTip.text: "Volver a proyectos"
                    contentItem: MIcon { name: "arrow_back"; size: 20; color: backButton.hovered ? Theme.accent : Theme.text }
                    background: Rectangle { radius: 8; color: backButton.hovered ? Qt.rgba(1, 1, 1, 0.06) : "transparent" }
                    onClicked: editor.closed()
                }
                ColumnLayout {
                    spacing: 1
                    Label {
                        id: projectNameLabel
                        objectName: "editor:projectName"
                        Layout.preferredWidth: Math.min(220, implicitWidth)
                        text: editor.model.projectName || "Proyecto"
                        color: Theme.text
                        font.pixelSize: 13
                        font.weight: Font.Bold
                        elide: Text.ElideRight
                    }
                    Label { text: "Guardado"; color: Theme.muted; font.pixelSize: 10 }
                }
                Rectangle { implicitWidth: 1; implicitHeight: 18; color: Theme.lineBright; Layout.leftMargin: 4; Layout.rightMargin: 4 }
                IconBtn { objectName: "editor:undo"; iconName: "undo"; tip: "Deshacer (Ctrl+Z)"; enabled: editor.model.canUndo; onClicked: editor.model.undo() }
                IconBtn { objectName: "editor:redo"; iconName: "redo"; tip: "Rehacer (Ctrl+Y)"; enabled: editor.model.canRedo; onClicked: editor.model.redo() }
                GhostButton { // onLeaveClip
                    objectName: "editor:leaveClip"
                    visible: editor.clipMode
                    iconName: "movie"
                    text: "Proyecto"
                    tip: "Volver al proyecto"
                    onClicked: editor.model.canvasTab = "main"
                }
                Item { Layout.fillWidth: true }
                IconBtn { objectName: "editor:settings"; iconName: "settings"; tip: "Configuración (Ctrl+,)"; onClicked: editor.openSettings() }
                IconBtn {
                    objectName: "editor:help"
                    iconName: "help_outline"
                    tip: "Atajos: Espacio reproduce, S divide, Supr elimina, Ctrl+Z deshace, M marca, ← → mueven el cursor"
                }
                GhostButton { objectName: "editor:json"; iconName: "data_object"; text: "JSON"; tip: "Ver / editar el JSON del proyecto"; onClicked: jsonModal.openNow() }
                Label {
                    objectName: "editor:exportPct"
                    visible: editor.model.exporter.running
                    text: Math.round(Math.max(0.05, editor.model.exporter.progress) * 100) + "%"
                    color: Theme.accent2
                    font.pixelSize: 11
                    font.weight: Font.Bold
                }
                PrimaryButton {
                    objectName: "editor:export"
                    // En el Clip Editor, video-yt pone aquí «Guardar clip» (lo renderiza el backend): aún no está.
                    visible: !editor.model.exporter.running && !editor.clipMode
                    iconName: "movie"
                    text: "Exportar"
                    tip: "Exportar el resultado"
                    onClicked: editor.model.exportVideo()
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.preferredHeight: 3
            spacing: 6

            MaterialPanel {
                visible: !editor.stageFullscreen
                // MAT_NAV_WIDTH: el ancho en el que caben todos los iconos (si no caben, la barra se desplaza).
                Layout.preferredWidth: Math.min(571, Math.max(300, editor.width * 0.34))
                onOpenSettings: editor.openSettings()
                Layout.fillHeight: true
                model: editor.model
                dragLayer: overlay
                sfxModal: sfxModal
                infoModal: infoModal
            }

            // .ed-canvas-col: las pestañas, el escenario con la vista previa y la fila del reproductor.
            Rectangle {
                id: canvasCol
                Layout.fillWidth: true
                Layout.fillHeight: true
                radius: editor.stageFullscreen ? 0 : 6
                color: Theme.stage
                border.color: editor.stageFullscreen ? "transparent" : Theme.line
                clip: true

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 1
                    spacing: 0

                    // .ed-col-tabs: el editor principal y el Clip Editor.
                    Rectangle {
                        visible: !editor.stageFullscreen
                        Layout.fillWidth: true
                        implicitHeight: 38
                        color: Theme.panelBar
                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 10
                            anchors.rightMargin: 10
                            anchors.topMargin: 8
                            spacing: 5
                            Repeater {
                                // Sin Motion Studio (se rehará de cero) ni Paper Animator (es el efecto paper_cutout).
                                model: [["Main Editor", "main"], ["Clip Editor", "clip"]]
                                AbstractButton {
                                    id: colTab
                                    required property var modelData
                                    required property int index
                                    readonly property bool on: editor.model.canvasTab === modelData[1]
                                    Layout.fillWidth: true
                                    implicitHeight: 28
                                    objectName: "editor:colTab:" + index
                                    hoverEnabled: true
                                    onClicked: editor.model.canvasTab = modelData[1]
                                    contentItem: Label {
                                        text: colTab.modelData[0]
                                        horizontalAlignment: Text.AlignHCenter
                                        verticalAlignment: Text.AlignVCenter
                                        font.pixelSize: 12
                                        font.weight: colTab.on ? Font.Bold : Font.Normal
                                        color: colTab.on ? "white" : Theme.textSecondary
                                    }
                                    background: Rectangle {
                                        radius: Theme.radiusSm
                                        color: colTab.on ? "transparent" : Theme.card2
                                        border.color: colTab.on ? Theme.accent : Theme.line
                                        gradient: colTab.on ? accentGradient : null
                                        Gradient {
                                            id: accentGradient
                                            orientation: Gradient.Horizontal
                                            GradientStop { position: 0; color: Theme.accent }
                                            GradientStop { position: 1; color: Theme.accent2 }
                                        }
                                    }
                                }
                            }
                        }
                    }

                    // .ed-clip-banner: el título y la descripción del vídeo del Clip Editor (los del clip que se crea).
                    Rectangle {
                        objectName: "clip:banner"
                        visible: editor.clipMode && !editor.stageFullscreen
                        Layout.fillWidth: true
                        implicitHeight: bannerRow.implicitHeight + 12
                        color: Qt.rgba(234 / 255, 88 / 255, 12 / 255, 0.12)
                        Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Qt.rgba(251 / 255, 146 / 255, 60 / 255, 0.35) }
                        RowLayout {
                            id: bannerRow
                            anchors.fill: parent
                            anchors.leftMargin: 10
                            anchors.rightMargin: 10
                            spacing: 8
                            Label {
                                text: "PREPARAR CLIP"
                                color: "#fb923c"
                                font.pixelSize: 10
                                font.weight: Font.ExtraBold
                                font.letterSpacing: 0.8
                            }
                            TextField {
                                objectName: "clip:title"
                                Layout.fillWidth: true
                                Layout.preferredWidth: 10
                                Layout.minimumWidth: 120
                                placeholderText: "Título"
                                text: editor.model.clipMeta.title || ""
                                font.pixelSize: 12
                                color: Theme.text
                                background: Rectangle { radius: Theme.radiusSm; color: Theme.card; border.color: Theme.lineBright }
                                onTextEdited: editor.model.setClipTitle(text)
                            }
                            TextField {
                                objectName: "clip:description"
                                Layout.fillWidth: true
                                Layout.preferredWidth: 14
                                Layout.minimumWidth: 120
                                placeholderText: "Descripción"
                                text: editor.model.clipMeta.description || ""
                                font.pixelSize: 12
                                color: Theme.text
                                background: Rectangle { radius: Theme.radiusSm; color: Theme.card; border.color: Theme.lineBright }
                                onTextEdited: editor.model.setClipDescription(text)
                            }
                        }
                    }

                    // El escenario: el recuadro de salida centrado, a la escala del zoom del canvas.
                    Item {
                        id: stage
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true

                        Label { // .ed-stage-empty del Clip Editor
                            objectName: "clip:empty"
                            z: 5
                            anchors.centerIn: parent
                            visible: editor.clipMode && editor.model.duration <= 0
                            text: "Elige un vídeo en el material (botón de recortar de su tarjeta) para prepararlo"
                            color: Theme.muted
                            font.pixelSize: 13
                        }

                        Rectangle {
                            id: frame
                            readonly property real fit: Math.max(10, Math.min(stage.width - 24, (stage.height - 24) * editor.model.aspect))
                            anchors.centerIn: parent
                            width: fit * editor.viewZoom
                            height: width / editor.model.aspect
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
                        // Eliminar fondo: la selección de la personalizada, el cursor del pincel y sus trazos.
                        BgMagicOverlay {
                            id: bgMagic
                            objectName: "bgMagicOverlay"
                            readonly property rect out: preview.frameRect.width > 0 ? preview.frameRect
                                                                                    : Qt.rect(0, 0, preview.width, preview.height)
                            anchors.fill: frame
                            model: editor.model
                            frame: Qt.rect(preview.x + out.x, preview.y + out.y, out.width, out.height)
                            visible: editor.model.bgMagicOn
                        }
                        MouseArea {
                            objectName: "bgBrushArea"
                            anchors.fill: frame
                            enabled: !!editor.model.bgBrush.on
                            visible: enabled
                            hoverEnabled: true
                            clip: true
                            cursorShape: Qt.BlankCursor
                            onPressed: (m) => m.accepted = editor.model.bgPress(m.x, m.y, bgMagic.frame)
                            onPositionChanged: (m) => {
                                editor.model.bgHover(m.x, m.y, bgMagic.frame)
                                if (pressed)
                                    editor.model.bgMove(m.x, m.y)
                            }
                            onReleased: editor.model.bgRelease()
                            onCanceled: editor.model.bgRelease()
                            onExited: editor.model.bgLeave()
                            Rectangle { // drawBgBrushCursor: blanco en la personalizada; verde conservar, rojo eliminar
                                readonly property var brush: editor.model.bgBrush
                                readonly property real r: brush.r || 0
                                visible: r > 0 && brush.px !== undefined
                                x: (brush.px || 0) - r
                                y: (brush.py || 0) - r
                                width: r * 2
                                height: r * 2
                                radius: r
                                color: editor.model.bgMagicOn ? Qt.rgba(1, 1, 1, 0.16) : "transparent"
                                border.width: 1.5
                                border.color: editor.model.bgMagicOn ? Qt.rgba(1, 1, 1, 0.95) : brush.op === "keep" ? "#22c55e" : "#ef4444"
                            }
                        }
                        // El cuentagotas del croma (pickChromaAt): con él, un clic toma el color y no llega a nada más.
                        MouseArea {
                            objectName: "chromaPickArea"
                            anchors.fill: frame
                            enabled: editor.model.bgPicking
                            visible: enabled
                            cursorShape: Qt.CrossCursor
                            readonly property rect out: preview.frameRect.width > 0 ? preview.frameRect
                                                                                    : Qt.rect(0, 0, preview.width, preview.height)
                            onPressed: (m) => m.accepted = editor.model.pickChromaAt(m.x, m.y, Qt.rect(preview.x + out.x, preview.y + out.y, out.width, out.height))
                        }
                        // Los proxies que se están haciendo (proxy_maker.hpp); vacío si no hay ninguno.
                        Label {
                            objectName: "preview:proxies"
                            anchors.bottom: parent.bottom
                            anchors.bottomMargin: 6
                            anchors.horizontalCenter: parent.horizontalCenter
                            visible: text !== ""
                            text: editor.model.proxies.status
                            color: Theme.muted
                            font.pixelSize: 11
                        }
                    }

                    // .ed-transport: atrás, ▶, adelante, al inicio, la barra de avance, el tiempo, pantalla completa
                    // y EdViewerTools (formato de salida, marco de plataforma y zoom del canvas).
                    Rectangle {
                        Layout.fillWidth: true
                        implicitHeight: 38
                        color: Theme.panelBar
                        Rectangle { anchors.top: parent.top; width: parent.width; height: 1; color: Theme.line }
                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 10
                            anchors.rightMargin: 10
                            anchors.topMargin: 4
                            anchors.bottomMargin: 4
                            spacing: 6
                            IconBtn { objectName: "transport:back"; iconName: "fast_rewind"; tip: "Atrás 0,1s (← · Shift = 1s)"; onClicked: editor.model.nudgePlayhead(-0.1) }
                            IconBtn {
                                objectName: "editor:play"
                                big: true
                                iconSize: 24
                                iconName: editor.model.player.playing ? "pause_circle" : "play_circle"
                                tip: editor.model.player.audioError !== "" ? "Reproducir / Pausa (Espacio) · " + editor.model.player.audioError
                                                                            : "Reproducir / Pausa (Espacio)"
                                onClicked: editor.model.player.toggle()
                            }
                            IconBtn { objectName: "transport:forward"; iconName: "fast_forward"; tip: "Adelante 0,1s (→ · Shift = 1s)"; onClicked: editor.model.nudgePlayhead(0.1) }
                            IconBtn { objectName: "transport:start"; iconName: "first_page"; tip: "Al inicio"; onClicked: editor.model.playhead = 0 }
                            // .ed-scrub: clic o arrastre llevan el cursor ahí.
                            Item {
                                objectName: "transport:scrub"
                                Layout.fillWidth: true
                                Layout.minimumWidth: 40
                                implicitHeight: 14
                                readonly property real ratio: editor.model.duration > 0 ? Math.min(1, editor.model.playhead / editor.model.duration) : 0
                                Rectangle {
                                    anchors.verticalCenter: parent.verticalCenter
                                    width: parent.width
                                    height: 6
                                    radius: 3
                                    color: Theme.card3
                                    Rectangle {
                                        width: parent.width * parent.parent.ratio
                                        height: parent.height
                                        radius: 3
                                        gradient: Gradient {
                                            orientation: Gradient.Horizontal
                                            GradientStop { position: 0; color: Theme.accent }
                                            GradientStop { position: 1; color: Theme.accent2 }
                                        }
                                    }
                                }
                                Rectangle {
                                    x: parent.width * parent.ratio - width / 2
                                    anchors.verticalCenter: parent.verticalCenter
                                    width: 11
                                    height: 11
                                    radius: 5.5
                                    color: "white"
                                }
                                MouseArea {
                                    anchors.fill: parent
                                    cursorShape: Qt.PointingHandCursor
                                    function seekAt(x) { editor.model.playhead = Math.max(0, Math.min(1, x / width)) * Math.max(editor.model.duration, 1) }
                                    onPressed: (m) => seekAt(m.x)
                                    onPositionChanged: (m) => { if (pressed) seekAt(m.x) }
                                }
                            }
                            Label {
                                objectName: "transport:time"
                                text: editor.model.formatTime(editor.model.playhead) + " / " + editor.model.formatTime(editor.model.duration)
                                color: Theme.textSecondary
                                font.pixelSize: 11
                            }
                            IconBtn {
                                objectName: "transport:fullscreen"
                                iconName: editor.stageFullscreen ? "fullscreen_exit" : "fullscreen"
                                tip: editor.stageFullscreen ? "Salir de pantalla completa (Esc)" : "Pantalla completa"
                                onClicked: editor.toggleStageFullscreen()
                            }
                            // EdViewerTools: aspecto y resolución (o el tamaño a mano), fps, marco y zoom.
                            RowLayout {
                                spacing: 6
                                visible: !editor.customSize
                                MiniSelect {
                                    objectName: "viewer:aspect"
                                    readonly property string size: editor.model.outWidth + "×" + editor.model.outHeight
                                    tip: "Relación de aspecto (" + size + ")"
                                    value: editor.model.formatAspect
                                    options: (editor.model.hasVisualClip ? [{ value: "original", label: "Original" }] : [])
                                             .concat(["16:9", "9:16", "1:1", "4:3", "3:4", "4:5"].map((a) => ({ value: a, label: a })))
                                             .concat(editor.model.formatAspect === "custom" ? [{ value: "custom", label: size }] : [])
                                             .concat([{ value: "edit", label: "Personalizado…" }])
                                    onPicked: (v) => {
                                        if (v === "edit")
                                            editor.customSize = { w: editor.model.outWidth, h: editor.model.outHeight }
                                        else if (v !== "custom")
                                            editor.model.pickAspect(v)
                                    }
                                }
                                MiniSelect {
                                    objectName: "viewer:resolution"
                                    tip: "Resolución de salida (" + editor.model.outWidth + "×" + editor.model.outHeight + ")"
                                    value: editor.model.formatResolution || "custom"
                                    options: [{ value: 480, label: "480p" }, { value: 720, label: "720p" }, { value: 1080, label: "1080p" },
                                              { value: 2160, label: "4K" }]
                                             .concat(editor.model.formatResolution ? []
                                                     : [{ value: "custom", label: Math.min(editor.model.outWidth, editor.model.outHeight) + "p" }])
                                             .concat([{ value: "edit", label: "Personalizado…" }])
                                    onPicked: (v) => {
                                        if (v === "edit")
                                            editor.customSize = { w: editor.model.outWidth, h: editor.model.outHeight }
                                        else if (v !== "custom")
                                            editor.model.pickResolution(v)
                                    }
                                }
                            }
                            // .ed-vt-custom: ancho × alto a mano (144–4096, se redondea a par).
                            RowLayout {
                                spacing: 3
                                visible: !!editor.customSize
                                ToolTip.visible: customW.hovered || customH.hovered
                                ToolTip.text: "Tamaño de salida (144–4096 px, se redondea a par)"
                                DimField {
                                    id: customW
                                    objectName: "viewer:customW"
                                    text: editor.customSize ? String(editor.customSize.w) : ""
                                    onTextEdited: editor.customSize = Object.assign({}, editor.customSize, { w: Number(text) })
                                }
                                Label { text: "×"; color: Theme.textSecondary; font.pixelSize: 11 }
                                DimField {
                                    id: customH
                                    objectName: "viewer:customH"
                                    text: editor.customSize ? String(editor.customSize.h) : ""
                                    onTextEdited: editor.customSize = Object.assign({}, editor.customSize, { h: Number(text) })
                                }
                                ResetChip { objectName: "viewer:customOk"; text: "OK"; onClicked: editor.commitCustomSize() }
                                ResetChip { text: "✕"; onClicked: editor.customSize = null }
                            }
                            MiniSelect {
                                objectName: "viewer:fps"
                                tip: "Fotogramas por segundo del proyecto"
                                value: editor.model.fps
                                options: [24, 25, 30, 50, 60].map((n) => ({ value: n, label: n + " fps" }))
                                onPicked: (v) => editor.model.setFps(v)
                            }
                            // El selector del marco (solo vista previa).
                            MiniSelect {
                                id: overlayPicker
                                objectName: "preview:overlay"
                                visible: chrome.overlays.length > 0
                                on: editor.platformOverlay !== "none"
                                tip: "Marco de plataforma (solo vista previa): simula cómo se verá dentro de TikTok o YouTube Shorts"
                                value: editor.platformOverlay
                                options: chrome.overlays.map((o) => ({ value: o.id, label: o.label }))
                                onPicked: (v) => editor.platformOverlay = v
                            }
                            // .ed-vt-zoom: el zoom del canvas (25–400 %, de 5 en 5) y volver al 100 %.
                            Rectangle {
                                objectName: "viewer:zoom"
                                implicitWidth: zoomRow.implicitWidth + 2
                                implicitHeight: 22
                                radius: 4
                                color: Theme.card2
                                border.color: Theme.line
                                clip: true
                                ToolTip.visible: zoomHover.hovered
                                ToolTip.delay: 500
                                ToolTip.text: "Zoom del canvas (solo visual, no afecta a la exportación)"
                                HoverHandler { id: zoomHover }
                                RowLayout {
                                    id: zoomRow
                                    anchors.fill: parent
                                    anchors.margins: 1
                                    spacing: 0
                                    Label {
                                        objectName: "viewer:zoomValue"
                                        Layout.preferredWidth: 30
                                        horizontalAlignment: Text.AlignRight
                                        text: Math.round(editor.viewZoom * 100)
                                        color: Theme.text
                                        font.pixelSize: 11
                                    }
                                    Label { text: "%"; color: Theme.muted; font.pixelSize: 10; leftPadding: 1; rightPadding: 3 }
                                    ColumnLayout {
                                        spacing: 0
                                        Layout.fillHeight: true
                                        StepArrow { objectName: "viewer:zoomIn"; glyph: "arrow_drop_up"; onClicked: editor.setZoomPct(editor.viewZoom * 100 + 5) }
                                        StepArrow { objectName: "viewer:zoomOut"; glyph: "arrow_drop_down"; onClicked: editor.setZoomPct(editor.viewZoom * 100 - 5) }
                                    }
                                }
                            }
                            ResetChip { objectName: "viewer:zoomReset"; text: "100%"; tip: "Restablecer a 100%"; onClicked: editor.viewZoom = 1 }
                        }
                    }
                }
            }

            Inspector {
                visible: !editor.stageFullscreen
                Layout.preferredWidth: 300
                Layout.fillHeight: true
                model: editor.model
            }
        }

        // La fila de abajo: la timeline con su barra (.ed-timeline-wrap) y el panel de Keyframes (EdCrops).
        RowLayout {
          visible: !editor.stageFullscreen
          Layout.fillWidth: true
          Layout.fillHeight: true
          Layout.preferredHeight: 2
          spacing: 6
          Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            radius: 6
            color: Theme.panelBar
            border.color: Theme.line
            clip: true
            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 1
                spacing: 0
                // .ed-tl-toolbar
                RowLayout {
                    Layout.fillWidth: true
                    Layout.leftMargin: 8
                    Layout.rightMargin: 8
                    Layout.topMargin: 5
                    Layout.bottomMargin: 5
                    spacing: 4
                    readonly property bool hasSel: editor.model.selection.length > 0
                    GhostButton { objectName: "tl:split"; iconName: "content_cut"; iconSize: 14; tip: "Dividir en el cursor (S)"; enabled: parent.hasSel; onClicked: editor.model.splitAtPlayhead() }
                    GhostButton { objectName: "tl:duplicate"; iconName: "content_copy"; iconSize: 14; tip: "Duplicar (Ctrl+D)"; enabled: parent.hasSel; onClicked: editor.model.duplicateSelection() }
                    GhostButton {
                        objectName: "cropButton"
                        iconName: "crop"
                        iconSize: 14
                        tip: "Recortar el clip seleccionado"
                        enabled: parent.hasSel
                        onClicked: {
                            editor.model.player.pause()
                            cropModal.openFor(editor.model.openCrop())
                        }
                    }
                    GhostButton { objectName: "tl:marker"; iconName: "bookmark_add"; iconSize: 14; tip: "Marcador en el cursor (M) · , y . saltan entre marcadores y beats"; onClicked: editor.model.toggleMarker() }
                    GhostButton { objectName: "tl:freeze"; iconName: "ac_unit"; iconSize: 14; tip: "Congelar fotograma: inserta una imagen fija del fotograma del cursor (3 s)"; enabled: editor.model.canFreeze; onClicked: editor.model.freezeAtPlayhead() }
                    GhostButton { objectName: "tl:delete"; iconName: "delete"; iconSize: 14; danger: true; tip: "Eliminar clip (Supr)"; enabled: parent.hasSel; onClicked: editor.model.deleteSelection() }
                    // .ed-create-seg (Clip Editor): el tramo Z/X a «Crear clip», con su duración.
                    Rectangle { visible: editor.model.canCreateSegment; implicitWidth: 1; implicitHeight: 18; color: Theme.line; Layout.leftMargin: 3; Layout.rightMargin: 3 }
                    GhostButton {
                        objectName: "tl:createSeg"
                        visible: editor.model.canCreateSegment
                        iconName: "add_to_photos"
                        iconSize: 14
                        on: editor.model.clipMark.range
                        enabled: editor.model.clipMark.range && !editor.model.segAsk.phase
                        text: editor.model.clipMark.range ? editor.fmtSecs(editor.model.clipMark.out - editor.model.clipMark.in) : ""
                        tip: "Revisa título y descripción del rango marcado (Z inicio · X fin) y confírmalo para añadirlo a Mis materiales (Enter)"
                        onClicked: editor.model.createSegmentFromMark()
                    }
                    Label {
                        objectName: "tl:segHint"
                        visible: editor.model.canCreateSegment && !editor.model.clipMark.hasIn && !editor.model.clipMark.hasOut
                        text: "Z inicio · X fin"
                        color: Theme.muted
                        font.pixelSize: 11
                    }
                    Rectangle { implicitWidth: 1; implicitHeight: 18; color: Theme.line; Layout.leftMargin: 3; Layout.rightMargin: 3 }
                    // PreviewVolButton: el volumen de escucha, con su deslizador vertical en un desplegable.
                    IconBtn {
                        id: volBtn
                        objectName: "tl:volume"
                        readonly property int pct: Math.round(editor.model.player.volume * 100)
                        iconName: pct <= 0 ? "volume_off" : pct < 50 ? "volume_down" : "volume_up"
                        iconSize: 15
                        tip: "Volumen de escucha (solo el editor, no el export)"
                        onClicked: volPop.visible ? volPop.close() : volPop.open()
                        Popup {
                            id: volPop
                            objectName: "tl:volumePop"
                            x: (volBtn.width - width) / 2
                            y: -height - 4
                            width: 42
                            height: 138
                            padding: 0
                            topPadding: 8
                            bottomPadding: 10
                            background: Rectangle { radius: 10; color: Theme.card2; border.color: Theme.line }
                            contentItem: ColumnLayout {
                                spacing: 4
                                Label {
                                    Layout.alignment: Qt.AlignHCenter
                                    text: volBtn.pct
                                    color: Theme.textSecondary
                                    font.pixelSize: 10
                                }
                                Slider {
                                    objectName: "tl:volumeSlider"
                                    Layout.alignment: Qt.AlignHCenter
                                    Layout.fillHeight: true
                                    orientation: Qt.Vertical
                                    from: 0
                                    to: 100
                                    stepSize: 1
                                    value: volBtn.pct
                                    onMoved: editor.model.player.volume = value / 100
                                }
                            }
                        }
                    }
                    Item { Layout.fillWidth: true }
                    GhostButton {
                        objectName: "tl:match"
                        iconName: "straighten"
                        iconSize: 14
                        enabled: editor.model.selection.length >= 2
                        tip: "Copiar el rango de tiempo del primer clip (mismo inicio y mismo fin). Cada uno se queda en su pista (si chocara con otro, sube a la de encima). Un vídeo o audio no se alarga más que su fuente."
                        onClicked: editor.model.matchDuration()
                    }
                    GhostButton {
                        objectName: "tl:magnet"
                        iconName: "@magnet"
                        iconSize: 14
                        on: editor.model.magnetOn
                        tip: editor.model.magnetOn
                             ? "Pista principal magnética ACTIVADA: sin huecos; al soltar se inserta en el corte más cercano y empuja lo de detrás. Clic para desactivar."
                             : "Pista principal magnética (como CapCut): la pista de vídeo de abajo pasa a no tener huecos y soltar en ella inserta y empuja."
                        onClicked: editor.model.toggleMagnet()
                    }
                    // .ed-overlap-fix: solo si hay clips montados en una pista (proyectos viejos).
                    GhostButton {
                        objectName: "tl:separate"
                        visible: editor.model.overlapCount > 0
                        iconName: "layers_clear"
                        iconSize: 14
                        text: editor.model.overlapCount
                        tip: "Hay " + (editor.model.overlapCount === 1 ? "un clip montado" : editor.model.overlapCount + " clips montados")
                             + " sobre otro en la misma pista (proyecto antiguo). Clic: cada clip de delante sube a su propia pista y la imagen no cambia."
                        onClicked: editor.model.separateOverlaps()
                    }
                    Rectangle { implicitWidth: 1; implicitHeight: 18; color: Theme.line; Layout.leftMargin: 3; Layout.rightMargin: 3 }
                    GhostButton { objectName: "tl:addVideo"; iconName: "video_call"; iconSize: 15; tip: "Añadir pista de vídeo"; onClicked: editor.model.addTrack("video") }
                    GhostButton { objectName: "tl:addAudio"; iconName: "library_music"; iconSize: 14; tip: "Añadir pista de audio"; onClicked: editor.model.addTrack("audio") }
                    GhostButton { objectName: "tl:addText"; iconName: "text_fields"; iconSize: 14; tip: "Añadir pista de texto"; onClicked: editor.model.addTextTrack() }
                    IconBtn { objectName: "tl:zoomOut"; iconName: "zoom_out"; iconSize: 15; tip: "Alejar"; onClicked: editor.model.zoomBy(1 / 1.4, timelineView.width - timelineView.headWidth) }
                    IconBtn { objectName: "tl:zoomIn"; iconName: "zoom_in"; iconSize: 15; tip: "Acercar (o Ctrl + rueda)"; onClicked: editor.model.zoomBy(1.4, timelineView.width - timelineView.headWidth) }
                }
                Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: Theme.line }
                Timeline {
                    id: timelineView
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    radius: 0
                    border.color: "transparent"
                    model: editor.model
                    onContextClip: (id) => clipMenu.openFor(id)
                }
            }
          }
          KeyframesPanel {
              Layout.preferredWidth: 300
              Layout.fillHeight: true
              model: editor.model
          }
        }
    }

    function commitCustomSize() {
        if (customSize)
            editor.model.setOutputSize(customSize.w, customSize.h)
        customSize = null
    }


    // El ancho o el alto a mano de .ed-vt-custom.
    component DimField: TextField {
        implicitWidth: 52
        implicitHeight: 22
        padding: 2
        leftPadding: 4
        font.pixelSize: 11
        color: Theme.text
        validator: IntValidator { bottom: 0; top: 99999 }
        background: Rectangle { radius: 4; color: Theme.card2; border.color: Theme.accent }
        onAccepted: editor.commitCustomSize()
        Keys.onEscapePressed: editor.customSize = null
    }

    // Las flechas del NumberStepper del zoom.
    component StepArrow: AbstractButton {
        id: arrow
        property string glyph
        Layout.preferredWidth: 15
        Layout.fillHeight: true
        hoverEnabled: true
        contentItem: MIcon { name: arrow.glyph; size: 12; color: arrow.hovered ? "white" : Theme.muted }
        background: Rectangle { color: arrow.hovered ? Theme.accent : Theme.card3 }
    }

    // .ed-vt-reset: el botón pequeño de la fila del reproductor (100 %, OK, ✕).
    component ResetChip: AbstractButton {
        id: chip
        property string tip
        implicitHeight: 22
        leftPadding: 6
        rightPadding: 6
        hoverEnabled: true
        ToolTip.visible: hovered && !!tip
        ToolTip.delay: 500
        ToolTip.text: tip
        contentItem: Label { text: chip.text; font.pixelSize: 10; font.weight: Font.Bold; color: chip.hovered ? "white" : Theme.textSecondary; verticalAlignment: Text.AlignVCenter }
        background: Rectangle { radius: 4; color: Theme.card2; border.color: chip.hovered ? Theme.accent : Theme.line }
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
    JsonModal { id: jsonModal; model: editor.model }
    MaterialInfoModal { id: infoModal; model: editor.model }
    SegmentModal { model: editor.model; fmtSecs: editor.fmtSecs }
    ClipMenu { id: clipMenu; model: editor.model; onPasteAttributes: pasteAttrs.openFor(editor.model.pasteableGroups()) }

    // «Subir calidad en la timeline» (el ConfirmModal de EdMaterial.jsx).
    Connections {
        target: editor.model
        function onUpgradeAskChanged() {
            if (editor.model.upgradeAsk.message)
                upgradeModal.open()
            else
                upgradeModal.close()
        }
    }
    Popup {
        id: upgradeModal
        objectName: "upgradeModal"
        readonly property var ask: editor.model.upgradeAsk
        anchors.centerIn: parent
        width: 420
        modal: true
        padding: 20
        closePolicy: ask.busy ? Popup.NoAutoClose : (Popup.CloseOnEscape | Popup.CloseOnPressOutside)
        onClosed: editor.model.cancelUpgrade()
        background: Rectangle { radius: Theme.radiusLg; color: Theme.card; border.color: Theme.lineBright }
        contentItem: ColumnLayout {
            spacing: 12
            Label { text: "Subir calidad en la timeline"; color: Theme.text; font.pixelSize: 16; font.bold: true }
            Label {
                objectName: "upgrade:message"
                Layout.fillWidth: true
                text: upgradeModal.ask.message || ""
                color: Theme.muted
                wrapMode: Text.WordWrap
            }
            RowLayout {
                Layout.alignment: Qt.AlignRight
                Button {
                    objectName: "upgrade:cancel"
                    text: "Ahora no"
                    enabled: !upgradeModal.ask.busy
                    onClicked: editor.model.cancelUpgrade()
                }
                Button {
                    objectName: "upgrade:confirm"
                    text: upgradeModal.ask.busy ? "Cambiando…" : "Cambiar"
                    enabled: !upgradeModal.ask.busy
                    onClicked: editor.model.runUpgrade()
                }
            }
        }
    }

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
