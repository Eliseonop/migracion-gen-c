import QtQuick

Item {
    id: editor
    objectName: "curveEditor"
    required property var model
    property string kind: "linear"
    property var bezier: [0.42, 0, 0.58, 1]
    property var live: bezier // los tiradores mientras se arrastran
    property int dragging: 0
    readonly property real yMin: -0.45 // deja sitio a los rebotes
    readonly property real yMax: 1.45
    readonly property real pad: 14
    implicitWidth: 220
    implicitHeight: 170

    function clamp(v, lo, hi) { return Math.min(hi, Math.max(lo, v)) }
    function toPx(x, y) { return Qt.point(pad + x * (width - pad * 2), height - (y - yMin) / (yMax - yMin) * height) }
    function fromPx(px, py) {
        return [clamp((px - pad) / (width - pad * 2), 0, 1), clamp(yMin + (1 - py / height) * (yMax - yMin), -1, 2)]
    }
    function handle(which) { return which === 1 ? toPx(live[0], live[1]) : toPx(live[2], live[3]) }

    onBezierChanged: if (!dragging) live = bezier
    onLiveChanged: canvas.requestPaint()
    onKindChanged: canvas.requestPaint()
    onWidthChanged: canvas.requestPaint()

    Canvas {
        id: canvas
        anchors.fill: parent
        onPaint: {
            const ctx = getContext("2d")
            ctx.reset()
            const o = editor.toPx(0, 0)
            const e = editor.toPx(1, 1)
            ctx.fillStyle = Qt.rgba(1, 1, 1, 0.04)
            ctx.fillRect(o.x, e.y, e.x - o.x, o.y - e.y)
            ctx.strokeStyle = Qt.rgba(1, 1, 1, 0.15)
            ctx.lineWidth = 1
            ctx.beginPath(); ctx.moveTo(o.x, o.y); ctx.lineTo(e.x, e.y); ctx.stroke()
            ctx.strokeStyle = Theme.accent
            ctx.lineWidth = 2
            ctx.beginPath()
            const pts = editor.model.curvePoints(editor.dragging ? "bezier" : editor.kind, editor.live)
            pts.forEach((p, i) => {
                const q = editor.toPx(p.x, p.y)
                i ? ctx.lineTo(q.x, q.y) : ctx.moveTo(q.x, q.y)
            })
            ctx.stroke()
            ctx.strokeStyle = Qt.rgba(1, 1, 1, 0.45)
            ctx.lineWidth = 1
            for (const [from, which] of [[o, 1], [e, 2]]) {
                const h = editor.handle(which)
                ctx.beginPath(); ctx.moveTo(from.x, from.y); ctx.lineTo(h.x, h.y); ctx.stroke()
            }
            ctx.fillStyle = Theme.muted
            for (const p of [o, e]) {
                ctx.beginPath(); ctx.arc(p.x, p.y, 3, 0, Math.PI * 2); ctx.fill()
            }
        }
    }

    Repeater {
        model: [1, 2]
        Rectangle {
            required property int modelData
            objectName: "curveHandle:" + modelData
            readonly property point at: editor.handle(modelData)
            x: at.x - 6.5
            y: at.y - 6.5
            width: 13
            height: 13
            radius: 6.5
            color: "white"
            border.color: Theme.accent
            border.width: 2
        }
    }

    MouseArea {
        anchors.fill: parent
        preventStealing: true
        onPressed: (m) => {
            const near = (w) => { const h = editor.handle(w); return Math.hypot(h.x - m.x, h.y - m.y) <= 10 }
            editor.dragging = near(1) ? 1 : near(2) ? 2 : 0
            m.accepted = editor.dragging !== 0
        }
        onPositionChanged: (m) => {
            const [x, y] = editor.fromPx(m.x, m.y)
            const next = editor.live.slice()
            next.splice(editor.dragging === 1 ? 0 : 2, 2, x, y)
            editor.live = next
        }
        onReleased: {
            editor.dragging = 0
            editor.model.setCurve("bezier", editor.live)
        }
    }
}
