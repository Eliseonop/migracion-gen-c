import QtQuick
import QtQuick.Controls.Basic

// Timeline (EdTimeline.jsx): cabeceras de pista, regla, carriles, clips y cabezal. Arrastrar un
// clip lo mueve (también a otra pista compatible) y sus bordes lo recortan; el modelo acota el
// gesto como React (sin solapes, sin invadir al vecino) y lo aplica al soltar.
Rectangle {
    id: timeline
    required property var model
    signal contextClip(string clipId) // clic derecho en un clip (ya elegido)
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
    property var trackDrop: null // {id, targetId, below} al arrastrar una cabecera
    property point autoPoint // puntero (escena) mientras se arrastra
    property var autoTick: null // rehace la vista previa del gesto tras desplazar

    // edgeSpeed de EdTimeline: más deprisa cuanto más cerca del borde.
    function edgeSpeed(fromStart, fromEnd) {
        const edge = 44
        const max = 22
        if (fromStart < edge)
            return -max * Math.min(1, (edge - fromStart) / edge)
        if (fromEnd < edge)
            return max * Math.min(1, (edge - fromEnd) / edge)
        return 0
    }
    function follow(scenePoint, tick) {
        autoPoint = scenePoint
        autoTick = tick
    }

    // Autodesplazamiento (autoScrollAt): con el puntero cerca de un borde de los carriles, la vista
    // corre sola y el gesto se recalcula donde caerá.
    Timer {
        interval: 16
        repeat: true
        running: !!timeline.autoTick
        onTriggered: {
            const p = flick.mapFromItem(null, timeline.autoPoint.x, timeline.autoPoint.y)
            const near = p.x > -60 && p.x < flick.width + 60 && p.y > -60 && p.y < flick.height + 60
            let vx = near ? timeline.edgeSpeed(p.x, flick.width - p.x) : 0
            let vy = near ? timeline.edgeSpeed(p.y - timeline.rulerHeight, flick.height - p.y) : 0
            const maxX = flick.contentWidth - flick.width
            const maxY = flick.contentHeight - flick.height
            if ((vx < 0 && flick.contentX <= 0) || (vx > 0 && flick.contentX >= maxX - 1))
                vx = 0
            if ((vy < 0 && flick.contentY <= 0) || (vy > 0 && flick.contentY >= maxY - 1))
                vy = 0
            if (!vx && !vy)
                return
            flick.contentX = Math.max(0, Math.min(maxX, flick.contentX + vx))
            flick.contentY = Math.max(0, Math.min(maxY, flick.contentY + vy))
            timeline.autoTick()
        }
    }

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
            delegate: Rectangle {
                id: head
                required property var modelData
                property bool editing: false
                objectName: "track:" + modelData.id
                width: heads.width
                height: modelData.height
                y: modelData.y - flick.contentY
                color: timeline.trackDrop && timeline.trackDrop.targetId === modelData.id ? Qt.rgba(1, 0.23, 0.36, 0.12)
                     : timeline.model.selectedTrack.id === modelData.id ? Qt.rgba(1, 1, 1, 0.07)
                     : modelData.kind === "audio" ? Qt.rgba(0.39, 0.4, 0.95, 0.06) : "transparent"
                opacity: modelData.hidden ? 0.55 : 1
                Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.line }
                Rectangle { // dónde caerá la pista que se arrastra
                    visible: !!timeline.trackDrop && timeline.trackDrop.targetId === head.modelData.id
                    y: visible && timeline.trackDrop.below ? parent.height - 2 : 0
                    width: parent.width
                    height: 2
                    color: Theme.accent
                }

                // Arrastrar la cabecera arriba o abajo cambia el orden de las capas (solo en su grupo).
                MouseArea {
                    anchors.fill: parent
                    property real pressY
                    onPressed: (m) => pressY = m.y
                    onPositionChanged: (m) => {
                        if (!pressed || Math.abs(m.y - pressY) < 4)
                            return
                        const y = head.modelData.y + m.y
                        const stack = (k) => k === "video" || k === "text"
                        const target = timeline.model.tracks.find((t) => y >= t.y && y < t.y + t.height)
                        timeline.trackDrop = target && target.id !== head.modelData.id && stack(target.kind) === stack(head.modelData.kind)
                            ? { id: head.modelData.id, targetId: target.id, below: y >= target.y + target.height / 2 } : null
                    }
                    onReleased: {
                        const drop = timeline.trackDrop
                        timeline.trackDrop = null
                        if (drop)
                            Qt.callLater(() => timeline.model.reorderTrack(drop.id, drop.targetId, drop.below))
                        else
                            timeline.model.selectTrack(head.modelData.id)
                    }
                    onDoubleClicked: head.editing = true
                }

                Row {
                    anchors.verticalCenter: parent.verticalCenter
                    x: 8
                    spacing: 2
                    Label {
                        visible: !head.editing
                        width: 34
                        anchors.verticalCenter: parent.verticalCenter
                        text: head.modelData.name.slice(0, 3)
                        color: Theme.textSecondary
                        font.pixelSize: 12
                        font.bold: true
                    }
                    InlineEdit {
                        visible: head.editing
                        width: 34
                        active: head.editing
                        initial: head.modelData.name
                        maximumLength: 3
                        onDone: (value) => {
                            head.editing = false
                            if (value !== null)
                                Qt.callLater(() => timeline.model.renameTrack(head.modelData.id, value))
                        }
                    }
                    Label {
                        visible: head.modelData.magnetic
                        anchors.verticalCenter: parent.verticalCenter
                        text: "⧲"
                        color: Theme.folder
                        font.pixelSize: 11
                    }
                    Repeater {
                        model: [
                            { prop: "hidden", on: head.modelData.hidden, text: head.modelData.hidden ? "◌" : "◉", tip: "Visibilidad", off: head.modelData.kind === "audio" },
                            { prop: "muted", on: head.modelData.muted, text: head.modelData.muted ? "🔇" : "🔊", tip: "Silenciar", off: head.modelData.kind === "text" },
                            { prop: "compact", on: false, text: "⇤", tip: "Juntar clips al inicio (sin huecos ni solapes)", off: head.modelData.locked },
                            { prop: "locked", on: head.modelData.locked, text: head.modelData.locked ? "🔒" : "🔓", tip: "Bloquear", off: false }
                        ]
                        delegate: ToolButton {
                            required property var modelData
                            objectName: "track:" + head.modelData.id + ":" + modelData.prop
                            implicitWidth: 18
                            implicitHeight: 18
                            padding: 0
                            enabled: !modelData.off
                            text: modelData.text
                            font.pixelSize: 10
                            palette.buttonText: modelData.on ? Theme.accent : Theme.muted
                            ToolTip.visible: hovered
                            ToolTip.text: modelData.tip
                            background: Rectangle { radius: 4; color: parent.hovered ? Qt.rgba(1, 1, 1, 0.08) : "transparent" }
                            onClicked: {
                                const id = head.modelData.id
                                const prop = modelData.prop
                                Qt.callLater(() => prop === "compact" ? timeline.model.compactTrack(id) : timeline.model.toggleTrack(id, prop))
                            }
                        }
                    }
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

            // Rectángulo de selección en un hueco: los clips que corta (Ctrl suma); un clic sin
            // arrastre limpia la selección.
            MouseArea {
                id: marquee
                anchors.fill: parent
                property point from
                property bool moved
                property bool additive
                onPressed: (m) => {
                    from = Qt.point(m.x, m.y)
                    moved = false
                    additive = (m.modifiers & Qt.ControlModifier) !== 0
                }
                onPositionChanged: (m) => {
                    if (!moved && Math.hypot(m.x - from.x, m.y - from.y) < 4)
                        return
                    moved = true
                    band.x = Math.min(from.x, m.x)
                    band.y = Math.min(from.y, m.y)
                    band.width = Math.abs(m.x - from.x)
                    band.height = Math.abs(m.y - from.y)
                    timeline.model.selectInRect(from.x, from.y, m.x, m.y, additive)
                }
                onReleased: {
                    if (!moved && !additive)
                        timeline.model.clearSelection()
                    moved = false
                }
            }
            Rectangle {
                id: band
                visible: marquee.moved
                z: 9
                color: Qt.rgba(1, 0.23, 0.36, 0.12)
                border.color: Theme.accent
            }

            // Material soltado desde el panel: el clip empieza bajo el puntero.
            DropArea {
                id: materialDrop
                anchors.fill: parent
                keys: ["material"]
                property var hint: null
                property string key
                property point scene
                function update() {
                    const p = lanes.mapFromItem(null, scene.x, scene.y)
                    hint = timeline.model.previewMaterialDrop(key, p.x, p.y, timeline.model.altHeld())
                }
                onPositionChanged: (d) => {
                    key = d.source.materialKey
                    scene = lanes.mapToItem(null, d.x, d.y)
                    update()
                    timeline.follow(scene, update)
                }
                onExited: { hint = null; timeline.autoTick = null }
                onDropped: (d) => {
                    const key = d.source.materialKey
                    const x = d.x
                    const y = d.y
                    const alt = timeline.model.altHeld()
                    hint = null
                    timeline.autoTick = null
                    Qt.callLater(() => timeline.model.dropMaterial(key, x, y, alt))
                }
            }
            Rectangle { // dónde caerá
                visible: !!materialDrop.hint && !materialDrop.hint.newTrack && !!materialDrop.hint.trackId
                x: visible ? materialDrop.hint.start * timeline.pps : 0
                y: visible ? timeline.rowY(materialDrop.hint.trackId) + 3 : 0
                width: visible ? Math.max(6, materialDrop.hint.duration * timeline.pps) : 0
                height: 40
                z: 6
                radius: 6
                color: visible && materialDrop.hint.replace ? Qt.rgba(0.96, 0.62, 0.04, 0.25) : Qt.rgba(0.13, 0.77, 0.37, 0.22)
                border.color: visible && materialDrop.hint.replace ? "#f59e0b" : "#22c55e"
            }
            Rectangle {
                visible: !!materialDrop.hint && materialDrop.hint.newTrack
                y: visible ? materialDrop.hint.lineY - 1 : 0
                width: lanes.width
                height: 2
                z: 8
                color: "#22c55e"
            }

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
                model: timeline.model.clipModel
                delegate: Rectangle {
                    id: block
                    required property string clipId
                    required property string trackId
                    required property string kind
                    required property string label
                    required property string speed
                    required property real start
                    required property real duration
                    required property real clipY
                    required property real clipHeight
                    required property bool muted
                    required property bool disabled
                    required property bool locked
                    required property var kfs
                    required property var beats
                    objectName: "clip:" + clipId
                    readonly property var palette3: timeline.colorsOf(kind)
                    readonly property var dp: timeline.drag
                    readonly property bool selected: timeline.model.selection.indexOf(clipId) >= 0
                    readonly property bool inGroup: !!dp && dp.group && selected
                    readonly property bool lifted: !!dp && !dp.group && timeline.dragId === clipId
                    property string mode: ""
                    property real pressX
                    property real pressY
                    property var trim: null

                    x: (inGroup ? start + dp.delta : lifted ? dp.start : trim ? trim.start : start) * timeline.pps
                    y: !lifted ? clipY : dp.newTrack ? dp.lineY - height / 2 : timeline.rowY(dp.trackId) + 3
                    width: Math.max(6, (trim ? trim.duration : duration) * timeline.pps)
                    height: clipHeight
                    z: mode ? 5 : 1
                    radius: 6
                    opacity: disabled ? 0.45 : lifted ? 0.85 : 1
                    border.width: 1.5
                    border.color: selected ? palette3[2] : Qt.rgba(1, 1, 1, 0.14)
                    gradient: Gradient {
                        orientation: Gradient.Horizontal
                        GradientStop { position: 0; color: block.palette3[0] }
                        GradientStop { position: 1; color: block.palette3[1] }
                    }

                    Repeater { // beats del clip (uno de cada `every`)
                        model: block.beats
                        Rectangle {
                            required property real modelData
                            x: modelData * block.width - 0.5
                            y: block.height - 8
                            width: 1
                            height: 6
                            color: Qt.rgba(1, 1, 1, 0.55)
                        }
                    }
                    Repeater { // keyframes del clip seleccionado, con los colores de kfColor: clic va a él, arrastrar lo mueve
                        model: block.selected ? block.kfs : []
                        Rectangle {
                            id: kfDot
                            required property var modelData
                            required property int index
                            property real dragF: -1
                            readonly property real f: dragF >= 0 ? dragF : modelData.f
                            objectName: "kf:" + block.clipId + ":" + index
                            visible: x > -7 && x < block.width + 3
                            x: f * block.width - 5
                            y: block.height / 2 - 5
                            width: 10
                            height: 10
                            rotation: 45
                            z: 6
                            border.width: dragF >= 0 ? 1.5 : 0
                            border.color: "white"
                            color: ["#e8a34d", "#5aa9e6", "#67b99a", "#c98bb9", "#e0757c", "#8f8ff0", "#c9b458", "#6ac4c9"][index % 8]
                            MouseArea {
                                anchors.fill: parent
                                anchors.margins: -3
                                cursorShape: Qt.SizeHorCursor
                                property real pressX
                                onPressed: (m) => pressX = mapToItem(block, m.x, m.y).x
                                onPositionChanged: (m) => {
                                    const x = mapToItem(block, m.x, m.y).x
                                    if (pressed && (kfDot.dragF >= 0 || Math.abs(x - pressX) >= 3))
                                        kfDot.dragF = Math.max(0, Math.min(1, kfDot.modelData.f + (x - pressX) / block.width))
                                }
                                onReleased: {
                                    const model = timeline.model
                                    const clipId = block.clipId
                                    const id = kfDot.modelData.id
                                    const f = kfDot.dragF >= 0 ? kfDot.dragF : kfDot.modelData.f
                                    kfDot.dragF = -1
                                    Qt.callLater(() => model.moveKeyframe(clipId, id, f))
                                }
                            }
                        }
                    }
                    Label {
                        anchors.fill: parent
                        anchors.leftMargin: 8
                        anchors.rightMargin: 8
                        verticalAlignment: Text.AlignVCenter
                        text: (block.muted ? "🔇 " : "") + block.label
                              + (block.speed ? "  " + block.speed : "")
                        color: Theme.text
                        font.pixelSize: 12
                        elide: Text.ElideRight
                    }

                    MouseArea {
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: block.mode === "move" ? Qt.ClosedHandCursor
                                   : mouseX < 6 || mouseX > width - 6 ? Qt.SizeHorCursor : Qt.OpenHandCursor
                        // En px del carril: el clip se mueve con el gesto y sus coordenadas también, y la
                        // vista puede desplazarse sola bajo el puntero.
                        property point scene
                        function lane(m) { return mapToItem(lanes, m.x, m.y) }
                        function at() { return lanes.mapFromItem(null, scene.x, scene.y) }
                        function delta() { return (at().x - block.pressX) / timeline.pps }
                        function centerY() { return block.clipY + block.height / 2 + at().y - block.pressY }
                        function update() {
                            if (block.mode === "move") {
                                timeline.dragId = block.clipId
                                timeline.drag = timeline.model.previewDrag(block.clipId, delta(), centerY())
                                timeline.guides = timeline.drag.times || []
                            } else {
                                block.trim = timeline.model.previewTrim(block.clipId, block.mode === "trim-left", delta())
                                timeline.guides = block.trim.times || []
                            }
                        }
                        acceptedButtons: Qt.LeftButton | Qt.RightButton
                        onPressed: (m) => {
                            if (m.button === Qt.RightButton) {
                                block.mode = ""
                                if (!block.selected)
                                    timeline.model.select(block.clipId, false, false)
                                const id = block.clipId
                                Qt.callLater(() => timeline.contextClip(id))
                                return
                            }
                            block.pressX = lane(m).x
                            block.pressY = lane(m).y
                            block.mode = block.locked ? "" : m.x < 6 ? "trim-left" : m.x > width - 6 ? "trim-right" : "move"
                            const keep = block.selected && !(m.modifiers & (Qt.ControlModifier | Qt.ShiftModifier))
                            if (!keep)
                                timeline.model.select(block.clipId, (m.modifiers & Qt.ControlModifier) !== 0,
                                                      (m.modifiers & Qt.ShiftModifier) !== 0)
                        }
                        onPositionChanged: (m) => {
                            if (!pressed || !block.mode)
                                return
                            if (block.mode === "move" && !timeline.dragId
                                    && Math.hypot(lane(m).x - block.pressX, lane(m).y - block.pressY) < 3)
                                return
                            scene = mapToItem(null, m.x, m.y)
                            update()
                            timeline.follow(scene, update)
                        }
                        // El cambio se aplica fuera del manejador: rehace la lista de clips y este
                        // delegado desaparece.
                        onReleased: (m) => {
                            const model = timeline.model
                            const id = block.clipId
                            scene = mapToItem(null, m.x, m.y)
                            const d = delta()
                            const cy = centerY()
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
                            timeline.autoTick = null
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
            Repeater { // marcadores: arrastrar mueve, clic va a él, doble clic nombra, clic derecho quita
                model: timeline.model.markers
                Item {
                    id: marker
                    required property var modelData
                    property real dragT: -1
                    objectName: "marker:" + modelData.id
                    z: 3
                    x: (dragT >= 0 ? dragT : modelData.t) * timeline.pps - flick.contentX - 7
                    width: 14
                    height: ruler.height
                    Rectangle {
                        x: 3
                        y: 2
                        width: 8
                        height: 8
                        rotation: 45
                        color: marker.modelData.color
                    }
                    Label {
                        x: 14
                        y: 1
                        visible: !!marker.modelData.label
                        text: marker.modelData.label
                        color: marker.modelData.color
                        font.pixelSize: 10
                    }
                    MouseArea {
                        anchors.fill: parent
                        acceptedButtons: Qt.LeftButton | Qt.RightButton
                        cursorShape: Qt.SizeHorCursor
                        property real pressX
                        onPressed: (m) => pressX = m.x
                        onPositionChanged: (m) => {
                            if (pressed && (marker.dragT >= 0 || Math.abs(m.x - pressX) >= 3))
                                marker.dragT = Math.max(0, (mapToItem(ruler, m.x, 0).x + flick.contentX) / timeline.pps)
                        }
                        onReleased: (m) => {
                            const model = timeline.model
                            const id = marker.modelData.id
                            const t = marker.dragT
                            const at = marker.modelData.t
                            marker.dragT = -1
                            if (m.button === Qt.RightButton)
                                Qt.callLater(() => model.removeMarker(id))
                            else if (t >= 0)
                                Qt.callLater(() => model.moveMarker(id, t))
                            else
                                model.playhead = at
                        }
                        onDoubleClicked: {
                            markerName.markerId = marker.modelData.id
                            markerName.text = marker.modelData.label
                            markerName.x = marker.x
                            markerName.visible = true
                            markerName.forceActiveFocus()
                        }
                    }
                }
            }
            TextField {
                id: markerName
                property string markerId
                objectName: "marker:name"
                visible: false
                z: 20
                y: 0
                width: 140
                height: ruler.height
                font.pixelSize: 11
                placeholderText: "Nombre del marcador"
                onAccepted: { timeline.model.renameMarker(markerId, text); visible = false }
                onActiveFocusChanged: if (!activeFocus) visible = false
                Keys.onEscapePressed: visible = false
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
