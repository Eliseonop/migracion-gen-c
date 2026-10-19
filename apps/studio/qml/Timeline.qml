import QtQuick
import QtQuick.Controls.Basic

// Timeline (EdTimeline.jsx): cabeceras de pista, regla, carriles, clips y cabezal. Arrastrar un
// clip lo mueve (también a otra pista compatible) y sus bordes lo recortan; el modelo acota el
// gesto como React (sin solapes, sin invadir al vecino) y lo aplica al soltar.
Rectangle {
    id: timeline
    required property var model
    readonly property int headWidth: 132
    readonly property int rulerHeight: 22
    readonly property real pps: model.pps
    readonly property var colors: ({
        "video": ["#3a2740", "#26203a", "#ff7a93"], "image": ["#1f3d42", "#1a3038", "#ffffff"],
        "shape": ["#3a2a18", "#2a2214", "#f5b942"], "adjustment": ["#1f2d3a", "#243646", "#38bdf8"],
        "audio": ["#1e2a4d", "#182138", "#818cf8"], "text": ["#16352c", "#12241f", "#34d399"]
    })

    radius: Theme.radiusLg
    color: Theme.card
    border.color: Theme.line
    clip: true

    property string dragId: ""
    property var drag: null // previewDrag del clip que se arrastra
    property var guides: [] // instantes de las guías del imán

    function colorsOf(kind) { return colors[kind] || ["#2b3350", "#1d2436", "#ffffff"] }
    function rowY(trackId) {
        for (const t of model.tracks)
            if (t.id === trackId)
                return t.y
        return 0
    }

    Item {
        id: heads
        x: 1
        y: 1
        width: timeline.headWidth
        height: parent.height - 2
        clip: true
        Rectangle {
            z: 1
            width: parent.width
            height: timeline.rulerHeight
            color: Theme.card2
            Label {
                anchors.verticalCenter: parent.verticalCenter
                x: 10
                text: timeline.model.formatTime(timeline.model.playhead)
                color: Theme.muted
                font.pixelSize: 11
            }
        }
        Repeater {
            model: timeline.model.tracks
            Rectangle {
                required property var modelData
                width: heads.width
                height: modelData.height
                y: modelData.y - flick.contentY
                color: modelData.kind === "audio" ? Qt.rgba(0.39, 0.4, 0.95, 0.06) : "transparent"
                border.color: Theme.line
                Label {
                    anchors.verticalCenter: parent.verticalCenter
                    x: 10
                    text: (modelData.kind === "audio" ? "♪ " : modelData.kind === "text" ? "T " : "▣ ") + modelData.name
                          + (modelData.magnetic ? "  ⧲" : "") + (modelData.locked ? "  🔒" : "")
                    color: Theme.textSecondary
                    font.pixelSize: 12
                }
            }
        }
    }

    Flickable {
        id: flick
        anchors.fill: parent
        anchors.leftMargin: timeline.headWidth + 1
        anchors.topMargin: 1
        clip: true
        contentWidth: Math.max(width, (timeline.model.duration + 30) * timeline.pps)
        contentHeight: Math.max(height, lanes.height)
        boundsBehavior: Flickable.StopAtBounds
        interactive: false
        ScrollBar.horizontal: ScrollBar { policy: ScrollBar.AsNeeded }
        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

        WheelHandler {
            acceptedModifiers: Qt.ControlModifier
            onWheel: (e) => timeline.model.zoomBy(e.angleDelta.y > 0 ? 1.15 : 1 / 1.15, flick.width)
        }
        WheelHandler {
            acceptedModifiers: Qt.NoModifier
            onWheel: (e) => {
                const dx = e.angleDelta.x !== 0 ? e.angleDelta.x : e.angleDelta.y
                flick.contentX = Math.max(0, Math.min(flick.contentWidth - flick.width, flick.contentX - dx))
            }
        }

        Item {
            id: lanes
            width: flick.contentWidth
            height: {
                const ts = timeline.model.tracks
                return ts.length ? ts[ts.length - 1].y + ts[ts.length - 1].height : timeline.rulerHeight
            }

            TapHandler { onTapped: timeline.model.clearSelection() }

            Repeater {
                model: timeline.model.tracks
                Rectangle {
                    required property var modelData
                    y: modelData.y
                    width: lanes.width
                    height: modelData.height
                    color: modelData.kind === "audio" ? Qt.rgba(0.39, 0.4, 0.95, 0.04) : "transparent"
                    border.color: Theme.line
                    border.width: 0
                    Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.line }
                }
            }

            Repeater {
                model: timeline.model.clips
                delegate: Rectangle {
                    id: block
                    required property var modelData
                    objectName: "clip:" + modelData.id
                    readonly property var palette3: timeline.colorsOf(modelData.kind)
                    readonly property var dp: timeline.drag
                    readonly property bool selected: timeline.model.selection.indexOf(modelData.id) >= 0
                    readonly property bool inGroup: !!dp && dp.group && selected
                    readonly property bool lifted: !!dp && !dp.group && timeline.dragId === modelData.id
                    property string mode: ""
                    property real pressX
                    property real pressY
                    property var trim: null

                    x: (inGroup ? modelData.start + dp.delta : lifted ? dp.start : trim ? trim.start : modelData.start) * timeline.pps
                    y: !lifted ? modelData.y : dp.newTrack ? dp.lineY - height / 2 : timeline.rowY(dp.trackId) + 3
                    width: Math.max(6, (trim ? trim.duration : modelData.duration) * timeline.pps)
                    height: modelData.height
                    z: mode ? 5 : 1
                    radius: 6
                    opacity: modelData.disabled ? 0.45 : lifted ? 0.85 : 1
                    border.width: 1.5
                    border.color: selected ? palette3[2] : Qt.rgba(1, 1, 1, 0.14)
                    gradient: Gradient {
                        orientation: Gradient.Horizontal
                        GradientStop { position: 0; color: block.palette3[0] }
                        GradientStop { position: 1; color: block.palette3[1] }
                    }

                    Label {
                        anchors.fill: parent
                        anchors.leftMargin: 8
                        anchors.rightMargin: 8
                        verticalAlignment: Text.AlignVCenter
                        text: (block.modelData.muted ? "🔇 " : "") + block.modelData.label
                              + (block.modelData.speed ? "  " + block.modelData.speed : "")
                        color: Theme.text
                        font.pixelSize: 12
                        elide: Text.ElideRight
                    }

                    MouseArea {
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: block.mode === "move" ? Qt.ClosedHandCursor
                                   : mouseX < 6 || mouseX > width - 6 ? Qt.SizeHorCursor : Qt.OpenHandCursor
                        // En px del carril: el clip se mueve con el gesto y sus coordenadas también.
                        function lane(m) { return mapToItem(lanes, m.x, m.y) }
                        function delta(m) { return (lane(m).x - block.pressX) / timeline.pps }
                        function centerY(m) { return block.modelData.y + block.height / 2 + lane(m).y - block.pressY }
                        onPressed: (m) => {
                            block.pressX = lane(m).x
                            block.pressY = lane(m).y
                            block.mode = m.x < 6 ? "trim-left" : m.x > width - 6 ? "trim-right" : "move"
                            const keep = block.selected && !(m.modifiers & (Qt.ControlModifier | Qt.ShiftModifier))
                            if (!keep)
                                timeline.model.select(block.modelData.id, (m.modifiers & Qt.ControlModifier) !== 0,
                                                      (m.modifiers & Qt.ShiftModifier) !== 0)
                        }
                        onPositionChanged: (m) => {
                            if (!pressed || !block.mode)
                                return
                            if (block.mode === "move") {
                                if (!timeline.dragId && Math.hypot(lane(m).x - block.pressX, lane(m).y - block.pressY) < 3)
                                    return
                                timeline.dragId = block.modelData.id
                                timeline.drag = timeline.model.previewDrag(block.modelData.id, delta(m), centerY(m))
                                timeline.guides = timeline.drag.times || []
                            } else {
                                block.trim = timeline.model.previewTrim(block.modelData.id, block.mode === "trim-left", delta(m))
                                timeline.guides = block.trim.times || []
                            }
                        }
                        // El cambio se aplica fuera del manejador: rehace la lista de clips y este
                        // delegado desaparece.
                        onReleased: (m) => {
                            const model = timeline.model
                            const id = block.modelData.id
                            const d = delta(m)
                            const cy = centerY(m)
                            const left = block.mode === "trim-left"
                            const action = block.mode === "move" && timeline.dragId ? () => model.dropDrag(id, d, cy)
                                         : block.mode !== "move" && block.trim ? () => model.trimClip(id, left, d)
                                         : block.mode === "move" && !(m.modifiers & (Qt.ControlModifier | Qt.ShiftModifier))
                                           ? () => model.select(id, false, false) : null
                            block.mode = ""
                            block.trim = null
                            timeline.dragId = ""
                            timeline.drag = null
                            timeline.guides = []
                            if (action)
                                Qt.callLater(action)
                        }
                    }
                }
            }

            Repeater {
                model: timeline.guides
                Rectangle {
                    required property real modelData
                    x: modelData * timeline.pps - 1
                    width: 2
                    height: lanes.height
                    z: 7
                    color: "#f5d76e"
                }
            }

            Rectangle {
                visible: !!timeline.drag && !timeline.drag.group && timeline.drag.newTrack
                y: visible ? timeline.drag.lineY - 1 : 0
                width: lanes.width
                height: 2
                z: 8
                color: "#22c55e"
            }

            Rectangle {
                id: playhead
                x: timeline.model.playhead * timeline.pps - 1
                width: 2
                height: Math.max(lanes.height, flick.height)
                z: 10
                color: "#ff4d6d"
            }
        }

        Rectangle {
            id: ruler
            parent: flick
            y: 0
            x: 0
            width: flick.width
            height: timeline.rulerHeight
            z: 20
            color: Theme.card2
            Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.line }

            Repeater {
                model: timeline.model.ticks(flick.contentX, flick.width)
                Item {
                    required property var modelData
                    x: modelData.x - flick.contentX
                    height: ruler.height
                    Rectangle {
                        anchors.bottom: parent.bottom
                        width: 1
                        height: modelData.major ? 8 : 4
                        color: modelData.major ? Theme.muted : Theme.line
                    }
                    Label {
                        x: 3
                        y: 2
                        visible: modelData.major
                        text: modelData.label
                        color: Theme.muted
                        font.pixelSize: 10
                    }
                }
            }
            Rectangle {
                x: timeline.model.playhead * timeline.pps - flick.contentX - 5
                y: ruler.height - 10
                width: 10
                height: 10
                radius: 2
                color: "#ff4d6d"
            }
            MouseArea {
                anchors.fill: parent
                cursorShape: Qt.SizeHorCursor
                onPressed: (m) => timeline.model.playhead = (m.x + flick.contentX) / timeline.pps
                onPositionChanged: (m) => { if (pressed) timeline.model.playhead = (m.x + flick.contentX) / timeline.pps }
            }
        }
    }
}
