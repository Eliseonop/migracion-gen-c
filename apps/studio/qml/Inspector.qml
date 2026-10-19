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
    readonly property var tf: has ? clip.transform : ({})
    readonly property var speeds: [0.3, 0.5, 1, 1.5, 2, 3, 5, 10]
    readonly property bool text: has && clip.kind === "text"
    readonly property var st: text ? clip.style : ({})
    readonly property var fonts: ["Arial", "Arial Black", "Anton", "Segoe UI", "Segoe UI Black", "Calibri", "Bahnschrift",
        "Tahoma", "Impact", "Georgia", "Verdana", "Times New Roman", "Courier New", "Consolas", "Trebuchet MS"]
    readonly property bool shape: has && clip.kind === "shape"
    readonly property var sh: shape ? clip.shape : ({})
    readonly property bool canFill: shape && ["line", "check", "x"].indexOf(sh.type) < 0 && !(sh.type === "path" && !sh.closed)
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
                text: "Selecciona un elemento para editar sus propiedades."
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

            Section { visible: panel.text; text: "TEXTO" }
            TextArea {
                visible: panel.text
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
                    on: panel.text && panel.clip.lookId === "none"
                    onPicked: panel.model.applyTextLook("")
                    Text { anchors.fill: parent; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter; text: "⃠"; color: Theme.muted }
                }
                Repeater {
                    model: panel.model.textLooks
                    AaTile {
                        required property var modelData
                        objectName: "insp:look:" + modelData.id
                        look: modelData
                        on: panel.text && panel.clip.lookId === modelData.id
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
                        readonly property bool on: panel.text && panel.clip.themeId === modelData.id
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

            Section { visible: panel.sound; text: "AUDIO" }
            Percent {
                visible: panel.sound
                objectName: "insp:volume"
                title: "Volumen"
                maxValue: 2
                value: panel.has ? panel.clip.volumeAt : 1
                onCommitted: (v) => panel.model.setPose({ volume: Math.round(v * 100) / 100 })
            }
            RowLayout {
                visible: panel.sound
                Layout.fillWidth: true
                spacing: 6
                Small { objectName: "insp:fadein"; text: "Fade in"; onClicked: panel.model.fadeVolume("in") }
                Small { objectName: "insp:fadeout"; text: "Fade out"; onClicked: panel.model.fadeVolume("out") }
                Small { objectName: "insp:mute"; text: panel.has && panel.clip.muted ? "Activar" : "Mute"; onClicked: panel.model.toggleMuted() }
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
    }
}
