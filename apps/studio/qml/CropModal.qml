import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Popup {
    id: modal
    objectName: "cropModal"
    required property var model
    readonly property var crop: model.cropEditor
    property var info: ({})
    readonly property real step: 1 / Math.max(1, model.fps || 30)
    readonly property real localT: Math.max(0, Math.min(info.duration || 0, model.playhead - (info.start || 0)))

    function openFor(start) {
        if (!start.clipId)
            return
        info = start
        open()
    }
    function confirm() {
        close()
        model.confirmCrop()
    }
    function timecode(t) {
        const f = Math.max(1, Math.round(model.fps || 30))
        const total = Math.max(0, Math.round((t || 0) * f))
        const s = Math.floor(total / f)
        const p = (n) => String(n).padStart(2, "0")
        return p(Math.floor(s / 3600)) + ":" + p(Math.floor(s / 60) % 60) + ":" + p(s % 60) + ":" + p(total % f)
    }

    anchors.centerIn: parent
    width: Math.min(parent.width - 40, 860)
    height: Math.min(parent.height - 40, 640)
    modal: true
    focus: true
    padding: 16
    background: Rectangle { radius: Theme.radiusLg; color: Theme.card; border.color: Theme.lineBright }

    Shortcut { sequences: ["Return", "Enter"]; enabled: modal.opened; onActivated: modal.confirm() }

    contentItem: ColumnLayout {
        spacing: 10
        RowLayout {
            Label { text: "Recortar"; color: Theme.text; font.pixelSize: 16; font.bold: true }
            Item { Layout.fillWidth: true }
            IconButton { text: "✕"; tip: "Cerrar (Esc)"; onClicked: modal.close() }
        }

        Item {
            id: stage
            Layout.fillWidth: true
            Layout.fillHeight: true
            readonly property real pad: 18

            Item {
                id: frame
                objectName: "cropFrame"
                readonly property real aspect: modal.crop.srcAspect
                width: Math.max(1, Math.min(stage.width - stage.pad * 2, (stage.height - stage.pad * 2) * aspect))
                height: width / aspect
                anchors.centerIn: parent
                readonly property real bx: (modal.crop.cx - modal.crop.wf / 2) * width
                readonly property real by: (modal.crop.cy - modal.crop.hf / 2) * height
                readonly property real bw: modal.crop.wf * width
                readonly property real bh: modal.crop.hf * height

                Image { anchors.fill: parent; source: modal.info.source || ""; visible: !!modal.info.source }
                Rectangle {
                    anchors.fill: parent
                    visible: !modal.info.source
                    color: Theme.card2
                    Label { anchors.centerIn: parent; text: "Fotograma del vídeo"; color: Theme.muted }
                }
                Repeater { // fuera del recorte, atenuado
                    model: [[0, 0, frame.width, frame.by], [0, frame.by + frame.bh, frame.width, frame.height - frame.by - frame.bh],
                            [0, frame.by, frame.bx, frame.bh], [frame.bx + frame.bw, frame.by, frame.width - frame.bx - frame.bw, frame.bh]]
                    Rectangle {
                        required property var modelData
                        x: modelData[0]; y: modelData[1]; width: modelData[2]; height: modelData[3]
                        color: Qt.rgba(0, 0, 0, 0.55)
                    }
                }
                Repeater { // guías de tercios
                    model: [1 / 3, 2 / 3]
                    Item {
                        required property real modelData
                        Rectangle { x: frame.bx + frame.bw * modelData; y: frame.by; width: 1; height: frame.bh; color: Qt.rgba(1, 1, 1, 0.55) }
                        Rectangle { x: frame.bx; y: frame.by + frame.bh * modelData; width: frame.bw; height: 1; color: Qt.rgba(1, 1, 1, 0.55) }
                    }
                }
                Rectangle {
                    x: frame.bx; y: frame.by; width: frame.bw; height: frame.bh
                    color: "transparent"
                    border.color: "white"
                    border.width: 1.5
                }
                Repeater { // círculo en las esquinas, píldora en los lados
                    model: modal.crop.handles
                    Rectangle {
                        required property var modelData
                        readonly property bool corner: modelData.hx !== 0 && modelData.hy !== 0
                        width: corner ? 10 : modelData.hy !== 0 ? 26 : 8
                        height: corner ? 10 : modelData.hy !== 0 ? 8 : 26
                        radius: corner ? 5 : 4
                        x: modelData.x * frame.width - width / 2
                        y: modelData.y * frame.height - height / 2
                        color: "white"
                    }
                }
                MouseArea {
                    objectName: "cropArea"
                    anchors.fill: parent
                    anchors.margins: -stage.pad
                    hoverEnabled: true
                    function nx(m) { return Math.max(0, Math.min(1, (m.x - stage.pad) / frame.width)) }
                    function ny(m) { return Math.max(0, Math.min(1, (m.y - stage.pad) / frame.height)) }
                    onPressed: (m) => m.accepted = modal.crop.press(nx(m), ny(m), frame.width, frame.height)
                    onPositionChanged: (m) => {
                        if (pressed)
                            modal.crop.drag(nx(m), ny(m))
                        else
                            cursorShape = modal.crop.cursorAt(nx(m), ny(m), frame.width, frame.height)
                    }
                    onReleased: modal.crop.release()
                }
            }
        }

        RowLayout {
            Label {
                text: "<b>" + modal.timecode(modal.localT) + "</b> | " + modal.timecode(modal.info.duration)
                color: Theme.textSecondary
                textFormat: Text.StyledText
            }
            Slider {
                objectName: "cropScrub"
                Layout.fillWidth: true
                from: 0
                to: Math.max(modal.step, (modal.info.duration || 0) - modal.step)
                stepSize: modal.step
                value: Math.min(modal.localT, to)
                enabled: modal.info.kind !== "image"
                onMoved: modal.model.playhead = modal.info.start + value
            }
        }

        RowLayout {
            spacing: 8
            Label { text: "Rotación"; color: Theme.muted }
            Slider {
                objectName: "cropRotation"
                from: -180; to: 180; stepSize: 1
                value: modal.crop.rotation
                onMoved: modal.crop.rotation = value
            }
            SpinBox {
                from: -180; to: 180
                value: Math.round(modal.crop.rotation)
                editable: true
                onValueModified: modal.crop.rotation = value
            }
            Label { text: "Recorte"; color: Theme.muted }
            ComboBox {
                objectName: "cropRatio"
                model: modal.crop.ratios
                textRole: "label"
                valueRole: "id"
                currentIndex: modal.crop.ratios.findIndex((r) => r.id === modal.crop.ratio)
                onActivated: modal.crop.pickRatio(currentValue)
            }
            Item { Layout.fillWidth: true }
            Button { text: "Restablecer"; onClicked: modal.crop.reset() }
            Button { objectName: "cropConfirm"; text: "Confirmar"; highlighted: true; onClicked: modal.confirm() }
        }
    }
}
