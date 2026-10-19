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
        background: Rectangle { radius: Theme.radiusSm; color: small.hovered ? Theme.card3 : Theme.card2; border.color: Theme.lineBright }
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
