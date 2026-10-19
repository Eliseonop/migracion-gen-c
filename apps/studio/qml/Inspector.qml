import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// Inspector (EdInspector.jsx): mezcla (opacidad y fusión), volumen, aparición y salida, y la nota
// del clip seleccionado. Las barras aplican al soltar (un paso de deshacer por gesto).
Rectangle {
    id: panel
    required property var model
    readonly property var clip: model.selectedClip
    readonly property bool has: !!clip.id
    readonly property bool visual: has && clip.kind !== "audio" && clip.kind !== "adjustment"
    readonly property bool sound: has && (clip.kind === "audio" || clip.kind === "video")
    // Pestañas (navs de EdInspector): Clip, Color, Audio y Animación, según el tipo de clip.
    property string tab: "clip"
    property var fxOpen: null // el efecto desplegado (uid o posición)
    readonly property var tabList: [["clip", "Clip"]]
        .concat(filterable || effectable ? [["color", "Color"]] : [])
        .concat(vfxable ? [["fx", "Efectos"]] : [])
        .concat(sound ? [["audio", "Audio"]] : [])
        .concat(has && ["video", "image", "shape", "text"].indexOf(clip.kind) >= 0 ? [["mask", "Máscara"]] : [])
        .concat(has && clip.kind !== "adjustment" ? [["anim", "Animar"]] : [])
    readonly property string tabNow: tabList.some((t) => t[0] === tab) ? tab : "clip"
    readonly property var tf: has ? clip.transform : ({})
    readonly property var speeds: [0.3, 0.5, 1, 1.5, 2, 3, 5, 10]
    readonly property var trk: model.selectedTrack
    readonly property bool trackText: !has && trk.kind === "text" // estilo general de una pista de texto
    readonly property bool text: (has && clip.kind === "text") || trackText
    readonly property var st: has && clip.kind === "text" ? clip.style : trackText ? trk.style : ({})
    readonly property string themeNow: has ? clip.themeId : trackText ? trk.themeId : ""
    readonly property string lookNow: has ? clip.lookId : trackText ? trk.lookId : ""
    readonly property var fonts: ["Arial", "Arial Black", "Anton", "Segoe UI", "Segoe UI Black", "Calibri", "Bahnschrift",
        "Tahoma", "Impact", "Georgia", "Verdana", "Times New Roman", "Courier New", "Consolas", "Trebuchet MS"]
    readonly property bool shape: has && clip.kind === "shape"
    readonly property var sh: shape ? clip.shape : ({})
    readonly property bool canFill: shape && ["line", "check", "x"].indexOf(sh.type) < 0 && !(sh.type === "path" && !sh.closed)
    readonly property bool vfxable: has && ["video", "image", "motion"].indexOf(clip.kind) >= 0
    readonly property bool effectable: has && ["video", "image", "shape"].indexOf(clip.kind) >= 0
    readonly property bool filterable: has && ["video", "image", "adjustment"].indexOf(clip.kind) >= 0
    readonly property var applied: has ? clip.filters : []
    function filterIndex(id) { return applied.findIndex((f) => f.id === id) }
    function filterLabel(id) {
        const f = model.colorFilters.find((x) => x.id === id)
        return f ? f.label : id
    }
    readonly property var colorFx: [["brightness", "Brillo", -0.5, 0.5, 100, ""], ["contrast", "Contraste", -0.5, 0.5, 100, ""],
        ["saturation", "Saturación", -1, 1, 100, ""], ["exposure", "Exposición", -1, 1, 100, ""],
        ["whites", "Blancos", -1, 1, 100, ""], ["temperature", "Temperatura", -1, 1, 100, ""], ["hue", "Tono", -180, 180, 1, "°"]]
    function fxNum(id) {
        const v = has && clip.effects ? clip.effects[id] : 0
        const n = v === true ? 1 : Number(v)
        return isFinite(n) ? n : 0
    }
    // VIDEO_FX_TOGGLES de clipFx.js: [id, nombre, rango o interruptor, mín, máx, paso, al encender].
    readonly property var videoFx: [["blur", "Desenfoque", true, 0, 8, 0.5, 2], ["sharpen", "Enfoque", true, 0, 5, 0.5, 1.5],
        ["glow", "Glow", true, 0, 12, 1, 6], ["grayscale", "Grayscale", false], ["sepia", "Sepia", false],
        ["pixelate", "Pixelate", true, 0, 24, 1, 8], ["vhs", "VHS", false], ["grain", "Grain", true, 0, 40, 1, 12]]
    function fxOn(id) {
        const v = has && clip.effects ? clip.effects[id] : undefined
        return v === true || (v !== false && v != null && Number(v) > 0)
    }
    readonly property var audioFx: [["eq", "Brillo (EQ)", "effect"], ["compressor", "Compresor", "effect"],
        ["reverb", "Reverberación", "effect"], ["echo", "Eco", "effect"], ["denoise", "Reducir ruido", "effect"],
        ["distortion", "Distorsión", "effect"], ["underwater", "Bajo el agua", "filter"], ["telephone", "Teléfono", "filter"],
        ["radio", "Radio antigua", "filter"], ["megaphone", "Megáfono", "filter"], ["muffled", "Amortiguado", "filter"]]
    readonly property var mk: has && clip.mask ? clip.mask : ({})
    readonly property bool hasMask: !!mk.type
    readonly property var maskTypes: [["linear", "División"], ["film", "Película"], ["circle", "Círculo"], ["rectangle", "Rectángulo"],
        ["star", "Estrella"], ["heart", "Corazón"], ["text", "Texto"], ["brush", "Pincel"]]
    // toggleWordFx de textKaraoke.js: los efectos de la palabra activa se combinan; ninguno = "none".
    function wordFxList(fx) {
        const all = ["highlight", "glow", "pop"]
        return Array.isArray(fx) ? all.filter((x) => fx.indexOf(x) >= 0) : all.indexOf(fx) >= 0 ? [fx] : []
    }
    function toggleWordFx(fx, name) {
        const on = wordFxList(fx)
        const next = ["highlight", "glow", "pop"].filter((x) => x === name ? on.indexOf(x) < 0 : on.indexOf(x) >= 0)
        return next.length ? next : "none"
    }
    readonly property var swatches: ["#ffffff", "#111111", "#ffe400", "#ff2d2d", "#ff8a00", "#2ee83f", "#1e90ff", "#ff3d8b"]
    readonly property var blends: [
        ["normal", "Normal"], ["darken", "Oscurecer"], ["multiply", "Multiplicar"], ["color_burn", "Subexponer color"],
        ["lighten", "Aclarar"], ["screen", "Trama"], ["color_dodge", "Sobreexponer color"], ["overlay", "Superponer"],
        ["soft_light", "Luz suave"], ["hard_light", "Luz fuerte"], ["difference", "Diferencia"], ["exclusion", "Exclusión"]]
    readonly property var appears: [["none", "Ninguno"], ["fade", "Fade"], ["dissolve", "Dissolve"], ["wipe", "Wipe"],
        ["zoom", "Zoom"], ["slide_up", "Slide up"], ["slide_left", "Slide left"], ["pop", "Pop"]]
    readonly property var exits: [["none", "Ninguno"], ["fade", "Fade"], ["dissolve", "Dissolve"], ["wipe", "Wipe"],
        ["zoom", "Zoom"], ["slide_down", "Slide down"], ["slide_right", "Slide right"], ["pop", "Pop"]]

    radius: Theme.radiusLg
    color: Theme.card
    border.color: Theme.line

    component Section: Label {
        Layout.topMargin: 10
        color: Theme.muted
        font.pixelSize: 11
        font.bold: true
    }
    component Choice: ComboBox {
        id: choice
        required property var options
        Layout.preferredWidth: 100
        property string current
        signal picked(string value)
        Layout.fillWidth: true
        model: options.map((o) => o[1])
        currentIndex: Math.max(0, options.findIndex((o) => o[0] === current))
        onActivated: (i) => picked(options[i][0])
        contentItem: Label { leftPadding: 10; text: choice.displayText; color: Theme.text; verticalAlignment: Text.AlignVCenter }
        background: Rectangle { radius: Theme.radiusSm; color: Theme.card2; border.color: Theme.lineBright }
    }
    component Small: Button {
        id: small
        Layout.fillWidth: true
        font.pixelSize: 12
        contentItem: Label { text: small.text; font: small.font; color: Theme.text; horizontalAlignment: Text.AlignHCenter }
        background: Rectangle { radius: Theme.radiusSm; color: small.hovered ? Theme.card3 : Theme.card2; border.color: small.highlighted ? Theme.accent : Theme.lineBright }
    }
    component Num: RowLayout {
        id: num
        required property string title
        required property real value
        property string suffix
        signal committed(real value)
        Label { Layout.preferredWidth: 22; text: num.title; color: Theme.textSecondary }
        TextField {
            id: field
            Layout.fillWidth: true
            text: String(num.value)
            color: Theme.text
            selectByMouse: true
            validator: DoubleValidator { notation: DoubleValidator.StandardNotation }
            background: Rectangle { radius: Theme.radiusSm; color: Theme.card2; border.color: field.activeFocus ? Theme.accent : Theme.lineBright }
            onEditingFinished: if (Number(text) !== num.value) num.committed(Number(text))
        }
        Label { visible: !!num.suffix; text: num.suffix; color: Theme.muted }
    }
    component Toggle: Small {
        id: toggle
        property bool on
        Layout.fillWidth: false
        implicitWidth: 34
        highlighted: on
    }
    component SwatchRow: RowLayout {
        id: swRow
        required property string title
        property string current
        property string prefix
        signal picked(string color)
        spacing: 4
        Label { Layout.preferredWidth: 60; text: swRow.title; color: Theme.textSecondary }
        Repeater {
            model: panel.swatches
            Rectangle {
                required property string modelData
                objectName: swRow.prefix + modelData
                width: 18
                height: 18
                radius: 9
                color: modelData
                border.width: (swRow.current || "").toLowerCase() === modelData ? 2 : 1
                border.color: border.width > 1 ? Theme.accent : Theme.lineBright
                TapHandler { onTapped: swRow.picked(parent.modelData) }
            }
        }
    }
    component AaTile: Rectangle {
        id: tile
        required property var look
        property bool on
        signal picked
        width: 40
        height: 30
        radius: Theme.radiusSm
        color: look.bg && look.bg !== "none" ? look.bg : Theme.card2
        border.width: on ? 2 : 1
        border.color: on ? Theme.accent : Theme.lineBright
        Text {
            anchors.centerIn: parent
            text: "Aa"
            font.pixelSize: 15
            font.bold: true
            color: tile.look.color
            style: tile.look.stroke ? Text.Outline : tile.look.glow ? Text.Raised : Text.Normal
            styleColor: tile.look.stroke || tile.look.glow || "transparent"
        }
        TapHandler { onTapped: tile.picked() }
    }
    // Un valor del estilo del texto en una barra: se muestra ×`scale` y se guarda /`scale`.
    component StyleSlider: RowLayout {
        id: ss
        required property string title
        required property string key
        property real fallback: 0
        property real scale: 100
        property real from: 0
        property real to: 100
        readonly property real stored: { const n = Number(panel.st[key]); return isFinite(n) && panel.st[key] !== undefined && panel.st[key] !== null ? n : fallback }
        Layout.fillWidth: true
        Label { Layout.preferredWidth: 70; text: ss.title; color: Theme.textSecondary; elide: Text.ElideRight }
        Slider {
            id: ssSlider
            objectName: "insp:style:" + ss.key
            Layout.fillWidth: true
            from: ss.from
            to: ss.to
            stepSize: 1
            value: Math.round(ss.stored * ss.scale)
            onPressedChanged: {
                if (pressed)
                    return
                const patch = {}
                patch[ss.key] = Math.round(value) / ss.scale
                panel.model.changeStyle(patch)
            }
        }
        Label { Layout.preferredWidth: 32; text: Math.round(ssSlider.value); color: Theme.text; horizontalAlignment: Text.AlignRight }
    }
    component FilterSwatch: Row {
        required property var colors
        Repeater {
            model: parent.colors
            Rectangle { required property string modelData; width: 8; height: 14; color: modelData }
        }
    }
    component Percent: RowLayout {
        id: row
        required property string title
        required property real value // 0–1 (o 0–2 en volumen)
        property real maxValue: 1
        signal committed(real value)
        Label { Layout.preferredWidth: 70; text: row.title; color: Theme.textSecondary }
        Slider {
            id: slider
            Layout.fillWidth: true
            from: 0
            to: row.maxValue
            value: row.value
            onPressedChanged: if (!pressed) row.committed(value)
        }
        Label { Layout.preferredWidth: 40; text: Math.round(slider.value * 100) + " %"; color: Theme.text; horizontalAlignment: Text.AlignRight }
    }

    Flickable {
        anchors.fill: parent
        anchors.margins: 16
        contentWidth: width
        contentHeight: body.implicitHeight
        clip: true

        ColumnLayout {
            id: body
            width: parent.width
            spacing: 6

            Label { text: "Inspector"; color: Theme.text; font.pixelSize: 15; font.bold: true }
            Label {
                visible: !panel.has
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                text: panel.trackText ? "Pista " + panel.trk.name + " · General: lo que cambies aquí se aplica a todos los textos de la pista y a los nuevos."
                                      : "Selecciona un elemento para editar sus propiedades."
                color: Theme.muted
            }
            Label {
                visible: panel.has
                Layout.fillWidth: true
                elide: Text.ElideRight
                text: panel.clip.label || ""
                color: Theme.text
                font.bold: true
            }
            Label {
                visible: panel.has
                text: panel.has ? panel.clip.kind + " · " + panel.model.formatTime(panel.clip.start) + " · "
                                  + panel.model.formatTime(panel.clip.duration)
                                  + (panel.model.selection.length > 1 ? " · " + panel.model.selection.length + " clips" : "") : ""
                color: Theme.muted
                font.pixelSize: 12
            }

            RowLayout {
                visible: panel.has && panel.clip.kind !== "adjustment"
                Layout.topMargin: 8
                ToolButton {
                    objectName: "insp:keyframe"
                    // ◆ keyframe en el cabezal · ◇ animado, sin keyframe aquí · ◇ gris sin animar
                    text: panel.clip.kf === "on" ? "◆" : "◇"
                    font.pixelSize: 16
                    palette.buttonText: panel.clip.kf === "off" ? Theme.muted : Theme.folder
                    background: Rectangle { radius: Theme.radiusSm; color: parent.hovered ? Theme.card3 : "transparent" }
                    ToolTip.visible: hovered
                    ToolTip.text: panel.clip.kf === "on" ? "Quitar el keyframe del cabezal" : "Keyframe en el cabezal"
                    onClicked: panel.model.toggleKeyframe()
                }
                Label {
                    text: panel.clip.kf === "off" ? "Sin animar" : panel.clip.kf === "on" ? "Keyframe en el cabezal" : "Animado"
                    color: Theme.textSecondary
                    font.pixelSize: 12
                }
            }

            TabBar {
                id: tabs
                visible: panel.has
                Layout.fillWidth: true
                Layout.topMargin: 6
                background: Rectangle { color: "transparent" }
                Repeater {
                    model: panel.tabList
                    TabButton {
                        required property var modelData
                        objectName: "insp:tab:" + modelData[0]
                        text: modelData[1]
                        font.pixelSize: 11
                        checked: panel.tabNow === modelData[0]
                        onClicked: panel.tab = modelData[0]
                        contentItem: Label { text: parent.text; font: parent.font; color: parent.checked ? Theme.text : Theme.muted; horizontalAlignment: Text.AlignHCenter }
                        background: Rectangle { color: "transparent"; Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 2; color: parent.parent.checked ? Theme.accent : "transparent" } }
                    }
                }
            }

            ColumnLayout {
                visible: panel.tabNow === "clip"
                Layout.fillWidth: true
                spacing: 6

            Section { visible: panel.text; text: "TEXTO" }
            TextArea {
                visible: panel.text && panel.has
                objectName: "insp:text"
                Layout.fillWidth: true
                Layout.preferredHeight: 64
                wrapMode: TextEdit.Wrap
                placeholderText: "Escribe el texto…"
                placeholderTextColor: Theme.muted
                color: Theme.text
                text: panel.text ? panel.clip.text : ""
                background: Rectangle { radius: Theme.radiusSm; color: Theme.card2; border.color: Theme.lineBright }
                onActiveFocusChanged: if (!activeFocus && panel.text) panel.model.changeText(text)
            }
            RowLayout {
                visible: panel.text
                Layout.fillWidth: true
                Choice {
                    objectName: "insp:font"
                    options: panel.fonts.map((f) => [f, f])
                    current: panel.st.font || "Arial"
                    onPicked: (v) => panel.model.changeStyle({ font: v })
                }
                Num {
                    Layout.preferredWidth: 90
                    objectName: "insp:size"
                    title: "px"
                    value: Math.round((panel.st.size ?? 60 / 1280) * 1280)
                    onCommitted: (v) => panel.model.changeStyle({ size: Number((Math.max(1, v) / 1280).toFixed(6)) })
                }
            }
            RowLayout {
                visible: panel.text
                spacing: 4
                Toggle { objectName: "insp:bold"; text: "B"; font.bold: true; on: !!panel.st.bold; onClicked: panel.model.changeStyle({ bold: !panel.st.bold }) }
                Toggle { text: "U"; font.underline: true; on: !!panel.st.underline; onClicked: panel.model.changeStyle({ underline: !panel.st.underline }) }
                Toggle { text: "I"; font.italic: true; on: !!panel.st.italic; onClicked: panel.model.changeStyle({ italic: !panel.st.italic }) }
                Item { Layout.fillWidth: true }
                Repeater {
                    model: [["left", "⇤"], ["center", "≡"], ["right", "⇥"]]
                    Toggle {
                        required property var modelData
                        objectName: "insp:align:" + modelData[0]
                        text: modelData[1]
                        on: (panel.st.align || "center") === modelData[0]
                        onClicked: panel.model.changeStyle({ align: modelData[0] })
                    }
                }
            }
            RowLayout {
                visible: panel.text
                Layout.fillWidth: true
                spacing: 4
                Repeater {
                    model: [["upper", "TT"], ["lower", "tt"], ["title", "Tt"]]
                    Toggle {
                        required property var modelData
                        objectName: "insp:case:" + modelData[0]
                        text: modelData[1]
                        on: panel.st.text_case === modelData[0]
                        onClicked: panel.model.changeStyle({ text_case: on ? null : modelData[0] })
                    }
                }
                Num {
                    objectName: "insp:letterSpacing"
                    title: "Ca"
                    value: Math.round((Number(panel.st.letter_spacing) || 0) * 100)
                    onCommitted: (v) => panel.model.changeStyle({ letter_spacing: Math.max(-50, Math.min(100, v)) / 100 })
                }
                Num {
                    objectName: "insp:lineHeight"
                    title: "Lí"
                    value: { const v = Number(panel.st.line_height); return isFinite(v) ? Math.max(0.6, Math.min(3, v)) : 1.22 }
                    onCommitted: (v) => panel.model.changeStyle({ line_height: Number(Math.max(0.6, Math.min(3, v)).toFixed(2)) })
                }
            }
            Flow {
                visible: panel.text
                Layout.fillWidth: true
                spacing: 4
                Repeater {
                    model: panel.swatches
                    Rectangle {
                        required property string modelData
                        objectName: "insp:color:" + modelData
                        width: 22
                        height: 22
                        radius: 11
                        color: modelData
                        border.width: (panel.st.color || "#ffffff").toLowerCase() === modelData ? 2 : 1
                        border.color: border.width > 1 ? Theme.accent : Theme.lineBright
                        TapHandler { onTapped: panel.model.changeStyle({ color: parent.modelData }) }
                    }
                }
                TextField {
                    objectName: "insp:color"
                    width: 84
                    height: 24
                    font.pixelSize: 12
                    text: panel.st.color || "#ffffff"
                    color: Theme.text
                    validator: RegularExpressionValidator { regularExpression: /#[0-9a-fA-F]{6}/ }
                    background: Rectangle { radius: Theme.radiusSm; color: Theme.card2; border.color: Theme.lineBright }
                    onEditingFinished: if (acceptableInput) panel.model.changeStyle({ color: text.toLowerCase() })
                }
            }
            Label { visible: panel.text; text: "Estilo preestablecido"; color: Theme.muted; font.pixelSize: 11 }
            Flow {
                visible: panel.text
                Layout.fillWidth: true
                spacing: 4
                AaTile {
                    objectName: "insp:look:none"
                    look: ({ color: Theme.muted, stroke: "", bg: "none", glow: "" })
                    on: panel.text && panel.lookNow === "none"
                    onPicked: panel.model.applyTextLook("")
                    Text { anchors.fill: parent; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter; text: "⃠"; color: Theme.muted }
                }
                Repeater {
                    model: panel.model.textLooks
                    AaTile {
                        required property var modelData
                        objectName: "insp:look:" + modelData.id
                        look: modelData
                        on: panel.text && panel.lookNow === modelData.id
                        onPicked: panel.model.applyTextLook(modelData.id)
                    }
                }
            }
            Label { visible: panel.text; text: "Temas de subtítulos"; color: Theme.muted; font.pixelSize: 11 }
            Flow {
                visible: panel.text
                Layout.fillWidth: true
                spacing: 4
                Repeater {
                    model: panel.model.textThemes
                    Rectangle {
                        required property var modelData
                        readonly property bool on: panel.text && panel.themeNow === modelData.id
                        objectName: "insp:theme:" + modelData.id
                        width: themeName.implicitWidth + 16
                        height: 24
                        radius: Theme.radiusSm
                        color: modelData.bg && modelData.bg !== "none" ? modelData.bg : Theme.card2
                        border.width: on ? 2 : 1
                        border.color: on ? Theme.accent : Theme.lineBright
                        Text {
                            id: themeName
                            anchors.centerIn: parent
                            text: modelData.name
                            font.pixelSize: 11
                            font.bold: true
                            color: modelData.color
                            style: modelData.stroke ? Text.Outline : Text.Normal
                            styleColor: modelData.stroke || "transparent"
                        }
                        TapHandler { onTapped: panel.model.applyTextTheme(parent.modelData.id) }
                    }
                }
            }
            SwatchRow {
                visible: panel.text
                title: "Trazo"
                prefix: "insp:border:"
                current: (panel.st.border_width ?? 0) > 0 ? panel.st.border_color || "#000000" : ""
                onPicked: (c) => panel.model.changeStyle({ border_color: c, border_width: Math.max(4, panel.st.border_width ?? 0) })
            }
            Num {
                visible: panel.text
                objectName: "insp:border:width"
                title: "Gr"
                value: panel.st.border_width ?? 0
                onCommitted: (v) => panel.model.changeStyle({ border_width: Math.max(0, Math.min(20, Math.round(v))) })
            }
            SwatchRow {
                visible: panel.text
                title: "Fondo"
                prefix: "insp:bg:"
                current: panel.st.bg && panel.st.bg !== "none" ? panel.st.bg : ""
                onPicked: (c) => panel.model.changeStyle({ bg: (panel.st.bg || "").toLowerCase() === c ? "none" : c })
            }
            SwatchRow {
                visible: panel.text
                title: "Sombra"
                prefix: "insp:shadow:"
                current: panel.st.shadow ? panel.st.shadow_color || "#000000" : ""
                onPicked: (c) => panel.model.changeStyle({ shadow: !(panel.st.shadow && (panel.st.shadow_color || "").toLowerCase() === c), shadow_color: c })
            }
            SwatchRow {
                visible: panel.text
                title: "Brillo"
                prefix: "insp:glow:"
                current: panel.st.glow ? panel.st.glow_color || panel.st.shadow_color || "#ffffff" : ""
                onPicked: (c) => panel.model.changeStyle({ glow: !(panel.st.glow && (panel.st.glow_color || "").toLowerCase() === c), glow_color: c })
            }
            Flow { // rombo por sección: ◆ keyframe en el cabezal · ◇ animada · gris sin animar
                visible: panel.text && panel.has
                Layout.fillWidth: true
                spacing: 4
                Label { text: "Animar"; color: Theme.textSecondary; width: 60; height: 22; verticalAlignment: Text.AlignVCenter }
                Repeater {
                    model: [["color", "Color"], ["stroke", "Trazo"], ["bg", "Fondo"], ["shadow", "Sombra"], ["glow", "Brillo"], ["curve", "Curva"]]
                    Toggle {
                        required property var modelData
                        readonly property string state: panel.has && panel.clip.styleKf ? panel.clip.styleKf[modelData[0]] || "off" : "off"
                        objectName: "insp:stylekf:" + modelData[0]
                        implicitWidth: implicitContentWidth + 14
                        text: (state === "on" ? "◆ " : "◇ ") + modelData[1]
                        on: state !== "off"
                        onClicked: panel.model.toggleStyleKeyframe(modelData[0])
                    }
                }
            }
            StyleSlider { visible: panel.text && !!panel.st.bg && panel.st.bg !== "none"; title: "Fondo %"; key: "bg_opacity"; fallback: 0.55 }
            RowLayout {
                visible: panel.text && !!panel.st.bg && panel.st.bg !== "none"
                spacing: 4
                Label { Layout.preferredWidth: 70; text: "Caja"; color: Theme.textSecondary }
                Repeater {
                    model: [["line", "Por línea"], ["block", "Bloque"]]
                    Toggle {
                        required property var modelData
                        implicitWidth: implicitContentWidth + 16
                        text: modelData[1]
                        on: (panel.st.bg_style || "line") === modelData[0]
                        onClicked: panel.model.changeStyle({ bg_style: modelData[0] }, true)
                    }
                }
            }
            StyleSlider { visible: panel.text && !!panel.st.bg && panel.st.bg !== "none"; title: "Redondeo"; key: "bg_radius" }
            StyleSlider { visible: panel.text && !!panel.st.bg && panel.st.bg !== "none"; title: "Alto"; key: "bg_pad_y" }
            StyleSlider { visible: panel.text && !!panel.st.bg && panel.st.bg !== "none"; title: "Ancho"; key: "bg_pad_x" }
            StyleSlider { visible: panel.text && !!panel.st.bg && panel.st.bg !== "none"; title: "Desp. X"; key: "bg_dx"; from: -100; to: 100 }
            StyleSlider { visible: panel.text && !!panel.st.bg && panel.st.bg !== "none"; title: "Desp. Y"; key: "bg_dy"; from: -100; to: 100 }
            StyleSlider { visible: panel.text && !!panel.st.shadow; title: "Sombra %"; key: "shadow_opacity"; fallback: 0.6 }
            StyleSlider { visible: panel.text && !!panel.st.shadow; title: "Desenfoque"; key: "shadow_blur"; fallback: 0.05; scale: 200 }
            StyleSlider { visible: panel.text && !!panel.st.shadow; title: "Distancia"; key: "shadow_distance"; fallback: 0.06 }
            StyleSlider { visible: panel.text && !!panel.st.shadow; title: "Ángulo"; key: "shadow_angle"; fallback: 45; scale: 1; from: -180; to: 180 }
            RowLayout {
                visible: panel.text && !!panel.st.glow
                spacing: 4
                Label { Layout.preferredWidth: 70; text: "Brillo"; color: Theme.textSecondary }
                Repeater {
                    model: [["soft", "Suave"], ["strong", "Intenso"]]
                    Toggle {
                        required property var modelData
                        implicitWidth: implicitContentWidth + 16
                        text: modelData[1]
                        on: (panel.st.glow_style === "strong" ? "strong" : "soft") === modelData[0]
                        onClicked: panel.model.changeStyle({ glow_style: modelData[0] }, true)
                    }
                }
            }
            StyleSlider { visible: panel.text && !!panel.st.glow; title: "Brillo X"; key: "glow_dx"; from: -100; to: 100 }
            StyleSlider { visible: panel.text && !!panel.st.glow; title: "Brillo Y"; key: "glow_dy"; from: -100; to: 100 }
            StyleSlider { visible: panel.text && !!panel.st.glow; title: "Intensidad"; key: "glow_intensity"; fallback: 0.9 }
            StyleSlider { visible: panel.text && !!panel.st.glow; title: "Intervalo"; key: "glow_range"; fallback: 0.5 }
            RowLayout {
                visible: panel.text
                Toggle {
                    objectName: "insp:curveOn"
                    implicitWidth: implicitContentWidth + 16
                    text: "Curva"
                    on: !!panel.st.curve_on
                    onClicked: panel.model.changeStyle(on ? { curve_on: false }
                                                          : { curve_on: true, curve: Number(panel.st.curve) || 0.5 }, true)
                }
                StyleSlider { visible: !!panel.st.curve_on; title: ""; key: "curve"; fallback: 0.5; from: -100; to: 100 }
            }
            RowLayout {
                visible: panel.text
                Label { Layout.fillWidth: true; text: "Mis estilos"; color: Theme.muted; font.pixelSize: 11; font.bold: true }
                Small { objectName: "insp:favorite:save"; Layout.fillWidth: false; text: "★ Guardar"; onClicked: panel.model.saveTextFavorite() }
            }
            Flow {
                visible: panel.text && panel.model.textFavorites.length > 0
                Layout.fillWidth: true
                spacing: 4
                Repeater {
                    model: panel.model.textFavorites
                    Rectangle {
                        required property var modelData
                        objectName: "insp:favorite:" + modelData.id
                        width: favRow.implicitWidth + 12
                        height: 24
                        radius: Theme.radiusSm
                        color: Theme.card2
                        border.color: Theme.lineBright
                        Row {
                            id: favRow
                            anchors.centerIn: parent
                            spacing: 6
                            Label { text: parent.parent.modelData.name; color: Theme.text; font.pixelSize: 11
                                    TapHandler { onTapped: panel.model.applyTextFavorite(parent.parent.parent.modelData.id) } }
                            Label { text: "×"; color: Theme.muted; font.pixelSize: 11
                                    TapHandler { onTapped: panel.model.deleteTextFavorite(parent.parent.parent.modelData.id) } }
                        }
                    }
                }
            }
            Label { visible: panel.text; text: "Palabra activa (karaoke)"; color: Theme.muted; font.pixelSize: 11; font.bold: true }
            RowLayout {
                visible: panel.text
                spacing: 4
                Repeater {
                    model: [["highlight", "Color"], ["glow", "Brillo"], ["pop", "Pop"]]
                    Toggle {
                        required property var modelData
                        objectName: "insp:wordfx:" + modelData[0]
                        implicitWidth: implicitContentWidth + 16
                        text: modelData[1]
                        on: panel.wordFxList(panel.st.word_fx).indexOf(modelData[0]) >= 0
                        onClicked: panel.model.changeStyle({ word_fx: panel.toggleWordFx(panel.st.word_fx, modelData[0]) })
                    }
                }
            }
            SwatchRow {
                visible: panel.text
                title: "Resalte"
                prefix: "insp:highlight:"
                current: panel.st.highlight_color || "#ffe566"
                onPicked: (c) => panel.model.changeStyle({ highlight_color: c })
            }
            StyleSlider { visible: panel.text; title: "Activa %"; key: "active_opacity"; fallback: 1 }
            StyleSlider { visible: panel.text; title: "Inactiva %"; key: "inactive_opacity"; fallback: 1 }
            RowLayout {
                visible: panel.text
                Label { Layout.preferredWidth: 70; text: "Por cuadro"; color: Theme.textSecondary }
                Choice {
                    objectName: "insp:maxWords"
                    options: [1, 2, 3, 4, 5, 6, 7, 8, 9, 10].map((n) => [String(n), n + (n === 1 ? " palabra" : " palabras")])
                    current: String(panel.st.max_words ?? 8)
                    onPicked: (v) => panel.model.changeStyle({ max_words: Number(v) })
                }
                Small { objectName: "insp:fragment"; Layout.fillWidth: false; text: "Fragmentar"; onClicked: panel.model.fragmentText() }
            }
            RowLayout {
                visible: panel.text
                Label { Layout.preferredWidth: 70; text: "Aparición"; color: Theme.textSecondary }
                Choice {
                    options: [["none", "Nada"], ["fade", "Fade"], ["pop", "Pop"], ["slide_up", "Slide up"], ["typing", "Escritura"]]
                    current: panel.st.block_appear || "none"
                    onPicked: (v) => panel.model.changeStyle({ block_appear: v })
                }
            }
            Num {
                visible: panel.text
                objectName: "insp:boxwidth"
                title: "↔"
                suffix: "%"
                value: Math.round((panel.st.w ?? 0.8) * 100)
                onCommitted: (v) => panel.model.changeStyle({ w: Math.max(0.15, Math.min(1, v / 100)) })
            }

            Section { visible: panel.shape; text: "FIGURA" }
            RowLayout {
                visible: panel.shape
                Layout.fillWidth: true
                spacing: 8
                Num { objectName: "insp:shape:w"; title: "An"; suffix: "%"; value: Math.round((panel.sh.w ?? 0) * 100)
                      onCommitted: (v) => panel.model.changeShape({ w: v / 100 }) }
                Num { objectName: "insp:shape:h"; title: "Al"; suffix: "%"; value: Math.round((panel.sh.h ?? 0) * 100)
                      onCommitted: (v) => panel.model.changeShape({ h: v / 100 }) }
            }
            RowLayout {
                visible: panel.canFill
                spacing: 4
                Toggle {
                    objectName: "insp:shape:fillOn"
                    implicitWidth: 64
                    text: "Relleno"
                    on: !!panel.sh.fill && panel.sh.fill !== "none"
                    onClicked: panel.model.changeShape({ fill: on ? "none" : "#e53935" })
                }
                Repeater {
                    model: panel.swatches
                    Rectangle {
                        required property string modelData
                        objectName: "insp:shape:fill:" + modelData
                        width: 18
                        height: 18
                        radius: 9
                        color: modelData
                        border.width: (panel.sh.fill || "").toLowerCase() === modelData ? 2 : 1
                        border.color: border.width > 1 ? Theme.accent : Theme.lineBright
                        TapHandler { onTapped: panel.model.changeShape({ fill: parent.modelData }) }
                    }
                }
            }
            RowLayout {
                visible: panel.shape
                spacing: 4
                Label { Layout.preferredWidth: 60; text: "Borde"; color: Theme.textSecondary }
                Repeater {
                    model: panel.swatches
                    Rectangle {
                        required property string modelData
                        objectName: "insp:shape:stroke:" + modelData
                        width: 18
                        height: 18
                        radius: 9
                        color: modelData
                        border.width: (panel.sh.stroke || "").toLowerCase() === modelData ? 2 : 1
                        border.color: border.width > 1 ? Theme.accent : Theme.lineBright
                        TapHandler { onTapped: panel.model.changeShape({ stroke: parent.modelData }) }
                    }
                }
            }
            RowLayout {
                visible: panel.shape
                Layout.fillWidth: true
                spacing: 8
                Num { objectName: "insp:shape:stroke"; title: "Gr"; value: panel.sh.strokeWidth ?? 0
                      onCommitted: (v) => panel.model.changeShape({ strokeWidth: Math.max(0, Math.min(20, Math.round(v))) }) }
                Num {
                    visible: panel.shape && ["square", "rect", "speech"].indexOf(panel.sh.type) >= 0
                    objectName: "insp:shape:radius"
                    title: "Esq"
                    value: Math.round((panel.sh.cornerRadius ?? 0) * 100)
                    onCommitted: (v) => panel.model.changeShape({ cornerRadius: Math.max(0, Math.min(0.5, v / 100)) })
                }
                Num {
                    visible: panel.shape && ["polygon", "star"].indexOf(panel.sh.type) >= 0
                    objectName: "insp:shape:sides"
                    title: panel.sh.type === "star" ? "Pun" : "Lad"
                    value: panel.sh.sides ?? 5
                    onCommitted: (v) => panel.model.changeShape({ sides: Math.max(3, Math.min(12, Math.round(v))) })
                }
            }
            RowLayout {
                visible: panel.shape && panel.sh.type !== "letterbox"
                Label { Layout.preferredWidth: 60; text: "Trazo"; color: Theme.textSecondary }
                Choice {
                    objectName: "insp:shape:dash"
                    options: [["solid", "Continuo"], ["dash", "Discontinuo"], ["dot", "Punteado"]]
                    current: panel.sh.dash || "solid"
                    onPicked: (v) => panel.model.changeShape({ dash: v })
                }
            }
            RowLayout {
                visible: panel.shape && ["arrow", "arrow_curve", "arrow_double"].indexOf(panel.sh.type) >= 0
                Label { Layout.preferredWidth: 60; text: "Punta"; color: Theme.textSecondary }
                Choice {
                    objectName: "insp:shape:arrow"
                    options: [["filled", "Rellena"], ["line", "Línea"], ["none", "Sin punta"]]
                    current: panel.sh.arrowHead || "filled"
                    onPicked: (v) => panel.model.changeShape({ arrowHead: v })
                }
            }

            RowLayout {
                visible: !!panel.tf.show
                Layout.fillWidth: true
                Section { Layout.fillWidth: true; text: "TRANSFORMACIÓN" }
                ToolButton {
                    objectName: "insp:reset"
                    Layout.topMargin: 10
                    text: "↺"
                    palette.buttonText: Theme.muted
                    background: Rectangle { radius: Theme.radiusSm; color: parent.hovered ? Theme.card3 : "transparent" }
                    ToolTip.visible: hovered
                    ToolTip.text: "Restablecer"
                    onClicked: panel.model.resetTransform()
                }
            }
            RowLayout {
                visible: !!panel.tf.xy
                Layout.fillWidth: true
                spacing: 8
                Num {
                    objectName: "insp:x"
                    title: "X"
                    value: panel.tf.x ?? 0
                    onCommitted: (v) => panel.model.setPosition(v, panel.tf.y)
                }
                Num {
                    objectName: "insp:y"
                    title: "Y"
                    value: panel.tf.y ?? 0
                    onCommitted: (v) => panel.model.setPosition(panel.tf.x, v)
                }
            }
            RowLayout {
                visible: !!panel.tf.scale || !!panel.tf.rotation
                Layout.fillWidth: true
                spacing: 8
                Num {
                    visible: !!panel.tf.scale
                    objectName: "insp:scale"
                    title: "⤢"
                    suffix: "%"
                    value: panel.tf.scalePct ?? 100
                    onCommitted: (v) => panel.model.setScalePct(v)
                }
                Num {
                    visible: !!panel.tf.rotation
                    objectName: "insp:rotation"
                    title: "⟳"
                    suffix: "°"
                    value: panel.tf.degrees ?? 0
                    onCommitted: (v) => panel.model.setRotation(v)
                }
            }
            RowLayout { // texto 3D (#4): inclinar y girar en X/Y con perspectiva, animables
                visible: panel.text
                spacing: 8
                Num { objectName: "insp:rotX"; title: "3D↕"; suffix: "°"; value: panel.tf.rotX ?? 0
                      onCommitted: (v) => panel.model.setPose({ rot_x: Math.max(-75, Math.min(75, v)) }) }
                Num { title: "3D↔"; suffix: "°"; value: panel.tf.rotY ?? 0
                      onCommitted: (v) => panel.model.setPose({ rot_y: Math.max(-75, Math.min(75, v)) }) }
            }
            Num {
                visible: panel.text && (!!panel.tf.rotX || !!panel.tf.rotY)
                title: "Persp."
                suffix: "%"
                value: { const p = Number(panel.st.perspective); return Math.round((isFinite(p) ? p : 0.5) * 100) }
                onCommitted: (v) => panel.model.changeStyle({ perspective: Math.max(0, Math.min(100, v)) / 100 })
            }
            RowLayout {
                visible: !!panel.tf.show
                spacing: 4
                Label { Layout.preferredWidth: 60; text: "Voltear"; color: Theme.textSecondary }
                Toggle { objectName: "insp:flipH"; text: "⇋"; on: panel.has && panel.clip.flipH; onClicked: panel.model.toggleFlip("h") }
                Toggle { objectName: "insp:flipV"; text: "⇵"; on: panel.has && panel.clip.flipV; onClicked: panel.model.toggleFlip("v") }
            }

            Section { visible: panel.visual; text: "MEZCLA" + (panel.clip.animated ? " · ANIMADA" : "") }
            Percent {
                visible: panel.visual
                objectName: "insp:opacity"
                title: "Opacidad"
                value: panel.has ? panel.clip.poseOpacity : 1
                onCommitted: (v) => panel.model.setPose({ opacity: Math.round(v * 100) / 100 })
            }
            Choice {
                visible: panel.visual
                objectName: "insp:blend"
                options: panel.blends
                current: panel.has ? panel.clip.blend : "normal"
                onPicked: (v) => panel.model.setBlend(v)
            }

            RowLayout {
                visible: panel.has && panel.clip.kind !== "text"
                Section { Layout.fillWidth: true; text: "VELOCIDAD" }
                Label {
                    Layout.topMargin: 10
                    text: panel.has ? Number(panel.clip.speed.toFixed(2)) + "×" : ""
                    color: Theme.text
                    font.bold: true
                }
            }
            Flow {
                visible: panel.has && panel.clip.kind !== "text"
                Layout.fillWidth: true
                spacing: 4
                Repeater {
                    model: panel.speeds
                    Small {
                        required property real modelData
                        objectName: "insp:speed:" + modelData
                        width: 40
                        text: modelData + "×"
                        highlighted: panel.has && panel.clip.speed === modelData
                        onClicked: panel.model.setSpeed(modelData)
                    }
                }
            }
            RowLayout {
                visible: panel.has && panel.clip.kind !== "text"
                Layout.fillWidth: true
                spacing: 6
                Small { objectName: "insp:pitch"; text: panel.has && panel.clip.keepPitch ? "Tono ✓" : "Tono"; onClicked: panel.model.toggleKeepPitch() }
                Small { objectName: "insp:reverse"; text: panel.has && panel.clip.reverse ? "Reversa ✓" : "Reversa"; onClicked: panel.model.toggleReverse() }
            }

            Section { visible: panel.has; text: "NOTA" }
            TextArea {
                visible: panel.has
                objectName: "insp:note"
                Layout.fillWidth: true
                Layout.preferredHeight: 70
                wrapMode: TextEdit.Wrap
                placeholderText: "Qué significa este fragmento en la historia…"
                placeholderTextColor: Theme.muted
                color: Theme.text
                text: panel.has ? panel.clip.note : ""
                background: Rectangle { radius: Theme.radiusSm; color: Theme.card2; border.color: Theme.lineBright }
                onActiveFocusChanged: if (!activeFocus && panel.has) panel.model.setNote(text)
            }
            }

            ColumnLayout {
                visible: panel.tabNow === "color"
                Layout.fillWidth: true
                spacing: 6

            Section { visible: panel.filterable; text: panel.has && panel.clip.kind === "adjustment" ? "CAPA DE AJUSTE" : "COLOR" }
            Percent {
                visible: panel.has && panel.clip.kind === "adjustment"
                objectName: "insp:adjust:intensity"
                title: "Intensidad"
                value: panel.has ? panel.clip.opacity : 1
                onCommitted: (v) => panel.model.setAdjustIntensity(Math.round(v * 100) / 100)
            }
            Repeater {
                model: panel.filterable ? panel.colorFx : []
                RowLayout {
                    id: fxRow
                    required property var modelData
                    readonly property real unit: modelData[4]
                    Layout.fillWidth: true
                    Label { Layout.preferredWidth: 70; text: fxRow.modelData[1]; color: Theme.textSecondary }
                    Slider {
                        id: fxSlider
                        objectName: "insp:fx:" + fxRow.modelData[0]
                        Layout.fillWidth: true
                        from: fxRow.modelData[2] * fxRow.unit
                        to: fxRow.modelData[3] * fxRow.unit
                        stepSize: 1
                        value: Math.round(panel.fxNum(fxRow.modelData[0]) * fxRow.unit)
                        onPressedChanged: if (!pressed) panel.model.setEffect(fxRow.modelData[0], Math.round(value) / fxRow.unit)
                    }
                    Label { Layout.preferredWidth: 40; text: Math.round(fxSlider.value) + fxRow.modelData[5]; color: Theme.text; horizontalAlignment: Text.AlignRight }
                }
            }
            Label { visible: panel.effectable; text: "Efectos"; color: Theme.muted; font.pixelSize: 11; font.bold: true }
            Flow {
                visible: panel.effectable
                Layout.fillWidth: true
                spacing: 4
                Repeater {
                    model: panel.effectable ? panel.videoFx : []
                    Toggle {
                        required property var modelData
                        objectName: "insp:vfx:" + modelData[0]
                        implicitWidth: implicitContentWidth + 16
                        text: modelData[1]
                        on: panel.fxOn(modelData[0])
                        onClicked: panel.model.setEffect(modelData[0], modelData[2] ? (on ? 0 : modelData[6]) : !on)
                    }
                }
            }
            Repeater {
                model: panel.effectable ? panel.videoFx.filter((f) => f[2] && panel.fxOn(f[0])) : []
                RowLayout {
                    id: vfxRow
                    required property var modelData
                    Layout.fillWidth: true
                    Label { Layout.preferredWidth: 70; text: vfxRow.modelData[1]; color: Theme.textSecondary }
                    Slider {
                        id: vfxSlider
                        Layout.fillWidth: true
                        from: vfxRow.modelData[3]
                        to: vfxRow.modelData[4]
                        stepSize: vfxRow.modelData[5]
                        snapMode: Slider.SnapAlways
                        value: panel.fxNum(vfxRow.modelData[0])
                        onPressedChanged: if (!pressed) panel.model.setEffect(vfxRow.modelData[0], value)
                    }
                    Label { Layout.preferredWidth: 30; text: vfxSlider.value; color: Theme.text; horizontalAlignment: Text.AlignRight }
                }
            }
            Label { visible: panel.filterable; text: "Filtros"; color: Theme.muted; font.pixelSize: 11; font.bold: true }
            Repeater {
                model: panel.filterable ? [["color", "Color"], ["cine", "Cine"], ["retro", "Retro"], ["bn", "Blanco y negro"]] : []
                ColumnLayout {
                    required property var modelData
                    Layout.fillWidth: true
                    spacing: 3
                    Label { text: modelData[1]; color: Theme.muted; font.pixelSize: 11 }
                    Flow {
                        Layout.fillWidth: true
                        spacing: 4
                        Repeater {
                            model: panel.model.colorFilters.filter((f) => f.group === modelData[0])
                            Rectangle {
                                id: card
                                required property var modelData
                                readonly property int order: panel.filterIndex(modelData.id)
                                objectName: "insp:filter:" + modelData.id
                                width: cardRow.implicitWidth + 12
                                height: 24
                                radius: Theme.radiusSm
                                color: Theme.card2
                                border.width: order >= 0 ? 2 : 1
                                border.color: order >= 0 ? Theme.accent : Theme.lineBright
                                Row {
                                    id: cardRow
                                    anchors.centerIn: parent
                                    spacing: 5
                                    FilterSwatch { anchors.verticalCenter: parent.verticalCenter; colors: card.modelData.swatch }
                                    Label { text: card.modelData.label + (card.order >= 0 ? " " + (card.order + 1) : ""); color: Theme.text; font.pixelSize: 11 }
                                }
                                TapHandler { onTapped: panel.model.toggleFilter(card.modelData.id) }
                            }
                        }
                    }
                }
            }
            Repeater {
                model: panel.filterable ? panel.applied : []
                RowLayout {
                    id: appliedRow
                    required property var modelData
                    required property int index
                    Layout.fillWidth: true
                    spacing: 4
                    FilterSwatch { colors: appliedRow.modelData.swatch }
                    Label { Layout.preferredWidth: 70; text: panel.filterLabel(appliedRow.modelData.id); color: Theme.textSecondary; elide: Text.ElideRight; font.pixelSize: 11 }
                    Slider {
                        objectName: "insp:filterAmount:" + appliedRow.index
                        Layout.fillWidth: true
                        from: 0
                        to: 1
                        value: appliedRow.modelData.amount
                        onPressedChanged: if (!pressed) panel.model.setFilterAmount(appliedRow.index, Math.round(value * 100) / 100)
                    }
                    Label { text: Math.round(appliedRow.modelData.amount * 100) + " %"; color: Theme.text; font.pixelSize: 11 }
                    Toggle { implicitWidth: 22; text: "↑"; enabled: appliedRow.index > 0; onClicked: panel.model.moveFilter(appliedRow.index, -1) }
                    Toggle { implicitWidth: 22; text: "↓"; enabled: appliedRow.index < panel.applied.length - 1; onClicked: panel.model.moveFilter(appliedRow.index, 1) }
                    Toggle { implicitWidth: 22; text: "×"; onClicked: panel.model.toggleFilter(appliedRow.modelData.id) }
                }
            }
            }

            ColumnLayout {
                visible: panel.tabNow === "audio"
                Layout.fillWidth: true
                spacing: 6

            Section { visible: panel.sound; text: "AUDIO" }
            Percent {
                visible: panel.sound
                objectName: "insp:volume"
                title: "Volumen"
                maxValue: 2
                value: panel.has ? panel.clip.volumeAt : 1
                onCommitted: (v) => panel.model.setPose({ volume: Math.round(v * 100) / 100 })
            }
            Flow {
                visible: panel.sound
                Layout.fillWidth: true
                spacing: 4
                Repeater {
                    model: panel.sound ? panel.audioFx : []
                    Toggle {
                        required property var modelData
                        objectName: "insp:afx:" + modelData[0]
                        implicitWidth: implicitContentWidth + 16
                        text: modelData[1]
                        on: panel.clip.audioFx[modelData[0]] > 0
                        onClicked: panel.model.setAudioFx(modelData[0], on ? 0 : 1)
                    }
                }
            }
            Repeater {
                model: panel.sound ? panel.audioFx.filter((f) => panel.clip.audioFx[f[0]] > 0) : []
                Percent {
                    required property var modelData
                    title: modelData[1]
                    value: panel.clip.audioFx[modelData[0]]
                    onCommitted: (v) => panel.model.setAudioFx(modelData[0], Math.round(v * 20) / 20)
                }
            }
            RowLayout {
                visible: panel.sound
                Layout.fillWidth: true
                spacing: 6
                Small { objectName: "insp:fadein"; text: "Fade in"; onClicked: panel.model.fadeVolume("in") }
                Small { objectName: "insp:fadeout"; text: "Fade out"; onClicked: panel.model.fadeVolume("out") }
                Small { objectName: "insp:mute"; text: panel.has && panel.clip.muted ? "Activar" : "Mute"; onClicked: panel.model.toggleMuted() }
            }
            }

            ColumnLayout {
                visible: panel.tabNow === "fx"
                Layout.fillWidth: true
                spacing: 6

                RowLayout {
                    Label { text: "EFECTOS ANIMADOS"; color: Theme.muted; font.pixelSize: 11; font.bold: true }
                    Label { visible: (panel.clip.vfx || []).length > 1; text: "Se aplican de arriba abajo"; color: Theme.muted; font.pixelSize: 10 }
                }
                Label {
                    visible: !(panel.clip.vfx || []).length
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    text: "Este clip no tiene efectos. Añádelos desde la pestaña Efectos del panel de material."
                    color: Theme.muted
                }
                Repeater {
                    model: panel.vfxable ? panel.clip.vfx : []
                    ColumnLayout {
                        id: vfxItem
                        required property var modelData
                        required property int index
                        readonly property var key: modelData.uid || index
                        readonly property bool isOpen: panel.fxOpen === key || panel.clip.vfx.length === 1
                        Layout.fillWidth: true
                        spacing: 4
                        RowLayout {
                            spacing: 4
                            Label {
                                objectName: "insp:vfx:name:" + vfxItem.index
                                Layout.fillWidth: true
                                text: (vfxItem.isOpen ? "▾ " : "▸ ") + vfxItem.modelData.name
                                color: vfxItem.modelData.enabled === false ? Theme.muted : Theme.text
                                font.bold: true
                                elide: Text.ElideRight
                                TapHandler { onTapped: panel.fxOpen = vfxItem.isOpen ? null : vfxItem.key }
                            }
                            Toggle { objectName: "insp:vfx:on:" + vfxItem.index; implicitWidth: 26; text: "👁"; on: vfxItem.modelData.enabled !== false; onClicked: panel.model.patchVfx(vfxItem.index, { enabled: !on }) }
                            Toggle { implicitWidth: 22; text: "↑"; enabled: vfxItem.index > 0; onClicked: panel.model.moveVfx(vfxItem.index, -1) }
                            Toggle { implicitWidth: 22; text: "↓"; enabled: vfxItem.index < panel.clip.vfx.length - 1; onClicked: panel.model.moveVfx(vfxItem.index, 1) }
                            Toggle { objectName: "insp:vfx:remove:" + vfxItem.index; implicitWidth: 22; text: "×"; onClicked: panel.model.removeVfx(vfxItem.index) }
                        }
                        RowLayout {
                            visible: vfxItem.isOpen
                            spacing: 6
                            Num { title: "De"; suffix: "s"; value: Number(vfxItem.modelData.from) || 0
                                  onCommitted: (v) => panel.model.patchVfx(vfxItem.index, { from: Math.max(0, Math.min(panel.clip.duration, v)) }) }
                            Num { title: "a"; suffix: "s"; value: vfxItem.modelData.to == null ? Number(panel.clip.duration.toFixed(2)) : vfxItem.modelData.to
                                  onCommitted: (v) => panel.model.patchVfx(vfxItem.index, { to: v >= panel.clip.duration - 0.01 ? null : Math.max(Number(vfxItem.modelData.from) || 0, v) }) }
                        }
                        Repeater {
                            model: vfxItem.isOpen ? vfxItem.modelData.specs : []
                            RowLayout {
                                id: spec
                                required property var modelData
                                readonly property var value: vfxItem.modelData.params[modelData.id] ?? modelData.def
                                function set(v) { panel.model.setVfxParam(vfxItem.index, modelData.id, v) }
                                Layout.fillWidth: true
                                Label { Layout.preferredWidth: 70; text: spec.modelData.label; color: Theme.textSecondary; elide: Text.ElideRight }
                                Slider {
                                    id: specSlider
                                    visible: spec.modelData.type === "range"
                                    Layout.fillWidth: true
                                    from: spec.modelData.min ?? 0
                                    to: spec.modelData.max ?? 1
                                    stepSize: spec.modelData.step ?? 0.01
                                    snapMode: Slider.SnapAlways
                                    value: Number(spec.value)
                                    onPressedChanged: if (!pressed) spec.set(value)
                                }
                                Label { visible: specSlider.visible; text: Number(specSlider.value).toFixed(spec.modelData.step >= 1 ? 0 : spec.modelData.step >= 0.1 ? 1 : 2); color: Theme.text }
                                CheckBox { visible: spec.modelData.type === "toggle"; checked: !!spec.value; onToggled: spec.set(checked) }
                                TextField {
                                    visible: spec.modelData.type === "color"
                                    Layout.fillWidth: true
                                    text: String(spec.value)
                                    color: Theme.text
                                    validator: RegularExpressionValidator { regularExpression: /#[0-9a-fA-F]{6}/ }
                                    background: Rectangle { radius: Theme.radiusSm; color: Theme.card2; border.color: Theme.lineBright }
                                    onEditingFinished: if (acceptableInput) spec.set(text.toLowerCase())
                                }
                                Choice {
                                    visible: spec.modelData.type === "select"
                                    options: (spec.modelData.options || []).map((o) => [o.id, o.label])
                                    current: String(spec.value)
                                    onPicked: (v) => spec.set(v)
                                }
                                Num {
                                    visible: spec.modelData.type === "seed"
                                    title: ""
                                    value: Number(spec.value) || 1
                                    onCommitted: (v) => spec.set(Math.max(1, Math.min(9999, Math.round(v))))
                                }
                                Toggle { visible: spec.modelData.type === "seed"; implicitWidth: 26; text: "🎲"; onClicked: spec.set(1 + Math.floor(Math.random() * 9998)) }
                            }
                        }
                        Small { visible: vfxItem.isOpen; text: "Valores por defecto"; onClicked: panel.model.patchVfx(vfxItem.index, { params: {} }) }
                    }
                }
            }

            ColumnLayout {
                visible: panel.tabNow === "anim"
                Layout.fillWidth: true
                spacing: 6

            RowLayout {
                visible: panel.has && !!panel.clip.curve
                Label { Layout.preferredWidth: 70; text: "Curva"; color: Theme.textSecondary }
                Choice {
                    objectName: "insp:curve"
                    options: [["linear", "Linear"], ["ease-in", "Ease In"], ["ease-out", "Ease Out"], ["ease-in-out", "Ease In-Out"],
                              ["cubic-in", "Cúbica In"], ["cubic-out", "Cúbica Out"], ["cubic-in-out", "Cúbica In-Out"],
                              ["back-out", "Rebote"], ["bezier", "Personalizada"], ["hold", "Hold"]]
                    current: panel.has && panel.clip.curve ? panel.clip.curve.kind : "linear"
                    onPicked: (v) => panel.model.setCurve(v)
                }
            }
            CurveEditor {
                visible: panel.has && !!panel.clip.curve
                Layout.alignment: Qt.AlignHCenter
                model: panel.model
                kind: panel.has && panel.clip.curve ? panel.clip.curve.kind : "linear"
                bezier: panel.has && panel.clip.curve ? panel.clip.curve.bezier : [0.42, 0, 0.58, 1]
            }
            Label {
                visible: panel.has && !!panel.clip.curve && panel.clip.curve.kind === "bezier"
                text: visible ? "cubic-bezier(" + panel.clip.curve.bezier.map((v) => v.toFixed(2)).join(", ") + ")" : ""
                color: Theme.muted
                font.family: "monospace"
            }
            Label {
                visible: panel.has && !!panel.clip.curve && panel.clip.curve.first
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                text: "Primer keyframe: su curva no se usa. Selecciona el siguiente."
                color: Theme.folder
            }

            Section { visible: panel.has; text: "ANIMACIÓN" }
            RowLayout {
                visible: panel.has
                Label { Layout.preferredWidth: 70; text: "Entrada"; color: Theme.textSecondary }
                Choice {
                    objectName: "insp:appear"
                    options: panel.appears
                    current: panel.has ? panel.clip.appear : "none"
                    onPicked: (v) => panel.model.setTransition(v, panel.clip.exit)
                }
            }
            RowLayout {
                visible: panel.has
                Label { Layout.preferredWidth: 70; text: "Salida"; color: Theme.textSecondary }
                Choice {
                    objectName: "insp:exit"
                    options: panel.exits
                    current: panel.has ? panel.clip.exit : "none"
                    onPicked: (v) => panel.model.setTransition(panel.clip.appear, v)
                }
            }

            Repeater {
                model: panel.has && ["video", "image", "motion", "text", "shape"].indexOf(panel.clip.kind) >= 0 ? panel.model.animationSlots : []
                RowLayout {
                    required property var modelData
                    Label { Layout.preferredWidth: 70; text: modelData.label; color: Theme.textSecondary }
                    Choice {
                        objectName: "insp:anim:" + modelData.id
                        options: [["", "Ninguna"]].concat(modelData.items.map((a) => [a.id, a.name]))
                        current: panel.clip.animations[modelData.id] || ""
                        onPicked: (v) => panel.model.setAnimation(modelData.id, v)
                    }
                    Num {
                        visible: !!panel.clip.animations[modelData.id] && modelData.id !== "combo"
                        Layout.preferredWidth: 80
                        title: ""
                        suffix: "s"
                        value: panel.clip.animations[modelData.id + "Dur"] ?? 0
                        onCommitted: (v) => panel.model.setAnimationDur(modelData.id, v)
                    }
                }
            }
            }

            ColumnLayout {
                visible: panel.tabNow === "mask"
                Layout.fillWidth: true
                spacing: 6

                Label {
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                    text: "Solo se verá la parte del clip que quede dentro de la forma." + (panel.has && panel.clip.maskCount > 1
                          ? " Hay " + panel.clip.maskCount + " máscaras: se combinan y aquí se edita la primera." : "")
                    color: Theme.muted
                    font.pixelSize: 11
                }
                Flow {
                    Layout.fillWidth: true
                    spacing: 4
                    Repeater {
                        model: panel.maskTypes
                        Toggle {
                            required property var modelData
                            objectName: "insp:mask:type:" + modelData[0]
                            implicitWidth: implicitContentWidth + 16
                            text: modelData[1]
                            on: panel.mk.type === modelData[0]
                            onClicked: panel.hasMask ? panel.model.changeMask({ type: modelData[0] }) : panel.model.addMask(modelData[0])
                        }
                    }
                }
                RowLayout {
                    visible: panel.hasMask
                    spacing: 4
                    Repeater {
                        model: panel.has && panel.clip.kind !== "text" ? [["clip", "Clip"], ["adjust", "Ajustes"]] : []
                        Toggle {
                            required property var modelData
                            implicitWidth: implicitContentWidth + 16
                            text: modelData[1]
                            on: panel.mk.target === modelData[0]
                            onClicked: panel.model.changeMask({ target: modelData[0] })
                        }
                    }
                }
                RowLayout {
                    visible: panel.hasMask
                    spacing: 4
                    Toggle { objectName: "insp:mask:invert"; implicitWidth: implicitContentWidth + 16; text: "Invertir"; on: !!panel.mk.invert; onClicked: panel.model.changeMask({ invert: !on }) }
                    Toggle { implicitWidth: implicitContentWidth + 16; text: "Activa"; on: !!panel.mk.enabled; onClicked: panel.model.changeMask({ enabled: !on }) }
                    Toggle { implicitWidth: 26; text: "⧉"; onClicked: panel.model.duplicateMask() }
                    Toggle { objectName: "insp:mask:remove"; implicitWidth: 26; text: "×"; onClicked: panel.model.removeMask() }
                }
                RowLayout {
                    visible: panel.hasMask
                    spacing: 8
                    Num { objectName: "insp:mask:x"; title: "X"; value: Math.round((panel.mk.x ?? 0.5) * 1000) / 10; onCommitted: (v) => panel.model.commitMask({ mx: v / 100 }) }
                    Num { title: "Y"; value: Math.round((panel.mk.y ?? 0.5) * 1000) / 10; onCommitted: (v) => panel.model.commitMask({ my: v / 100 }) }
                }
                RowLayout {
                    visible: panel.hasMask && panel.mk.type !== "linear"
                    spacing: 8
                    Num { visible: panel.mk.type !== "film"; objectName: "insp:mask:w"; title: "An"; suffix: "%"; value: Math.round((panel.mk.w ?? 0) * 100)
                          onCommitted: (v) => panel.model.commitMask({ mw: Math.max(1, Math.min(300, v)) / 100 }) }
                    Num { title: "Al"; suffix: "%"; value: Math.round((panel.mk.h ?? 0) * 100)
                          onCommitted: (v) => panel.model.commitMask({ mh: Math.max(1, Math.min(300, v)) / 100 }) }
                }
                RowLayout {
                    visible: panel.hasMask
                    spacing: 8
                    Num { title: "EX"; suffix: "%"; value: Math.round((panel.mk.scale_x ?? 1) * 100)
                          onCommitted: (v) => panel.model.commitMask({ msx: Math.max(5, Math.min(400, v)) / 100 }) }
                    Num { title: "EY"; suffix: "%"; value: Math.round((panel.mk.scale_y ?? 1) * 100)
                          onCommitted: (v) => panel.model.commitMask({ msy: Math.max(5, Math.min(400, v)) / 100 }) }
                }
                RowLayout {
                    visible: panel.hasMask
                    spacing: 8
                    Num { title: "⟳"; suffix: "°"; value: Number((panel.mk.rotation ?? 0).toFixed(1)); onCommitted: (v) => panel.model.commitMask({ mrot: Math.max(-180, Math.min(180, v)) }) }
                    Num { objectName: "insp:mask:feather"; title: "Pl"; suffix: "%"; value: Math.round((panel.mk.feather ?? 0) / 0.25 * 100)
                          onCommitted: (v) => panel.model.commitMask({ mfeather: Math.max(0, Math.min(100, v)) / 100 * 0.25 }) }
                }
                Percent {
                    visible: panel.hasMask
                    title: "Opacidad"
                    value: panel.mk.opacity ?? 1
                    onCommitted: (v) => panel.model.changeMask({ opacity: Math.round(v * 100) / 100 })
                }
                Percent {
                    visible: panel.hasMask && panel.mk.type === "rectangle"
                    title: "Esquinas"
                    value: (panel.mk.radius ?? 0) * 2
                    onCommitted: (v) => panel.model.changeMask({ radius: Math.round(v * 100) / 200 })
                }
                TextField {
                    visible: panel.hasMask && panel.mk.type === "text"
                    Layout.fillWidth: true
                    text: panel.mk.text ? panel.mk.text.content : ""
                    color: Theme.text
                    background: Rectangle { radius: Theme.radiusSm; color: Theme.card2; border.color: Theme.lineBright }
                    onEditingFinished: panel.model.changeMask({ text: Object.assign({}, panel.mk.text, { content: text }) })
                }
            }
        }
    }
}
