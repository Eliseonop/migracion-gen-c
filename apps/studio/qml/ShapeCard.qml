import QtQuick
import QtQuick.Controls.Basic

// Una figura del material (EdShapes.jsx): su miniatura; un clic la añade en el cabezal y se
// arrastra a la timeline como cualquier material.
Item {
    id: slot
    required property var modelData
    required property Item dragLayer
    property var geometry: ({ fills: [], strokes: [], fill: "", stroke: "#fff" }) // shapeThumb
    signal picked(string type, string label)
    width: 58
    height: 64

    Rectangle {
        id: card
        readonly property string materialKey: slot.modelData.key
        property bool dragging
        objectName: "material:" + slot.modelData.key
        width: slot.width
        height: slot.height
        radius: Theme.radiusSm
        color: Theme.card2
        border.color: hover.hovered ? Theme.accent : Theme.line
        opacity: dragging ? 0.75 : 1
        Drag.active: dragging
        Drag.keys: ["material"]
        ToolTip.visible: hover.hovered && !dragging
        ToolTip.text: slot.modelData.name
        ToolTip.delay: 600

        HoverHandler { id: hover }
        TapHandler { onTapped: slot.picked(slot.modelData.type, slot.modelData.name) }
        DragHandler {
            id: drag
            onActiveChanged: {
                if (active) {
                    card.Drag.hotSpot = drag.centroid.pressPosition
                    card.dragging = true
                    return
                }
                card.Drag.drop()
                card.dragging = false
                card.x = 0
                card.y = 0
            }
        }
        states: State {
            when: card.dragging
            ParentChange { target: card; parent: slot.dragLayer }
        }

        Canvas {
            id: thumb
            x: 9
            y: 4
            width: 40
            height: 40
            onPaint: {
                const g = slot.geometry
                const ctx = getContext("2d")
                ctx.reset()
                ctx.scale(width / 116, height / 116)
                ctx.translate(8, 8)
                ctx.lineJoin = "round"
                ctx.lineCap = "round"
                const path = (pts) => {
                    ctx.beginPath()
                    pts.forEach((p, i) => i ? ctx.lineTo(p.x, p.y) : ctx.moveTo(p.x, p.y))
                }
                ctx.strokeStyle = g.stroke
                ctx.lineWidth = 4
                for (const ring of g.fills) {
                    path(ring)
                    ctx.closePath()
                    if (g.fill) {
                        ctx.fillStyle = g.fill
                        ctx.fill()
                    }
                    ctx.stroke()
                }
                ctx.lineWidth = 8
                for (const line of g.strokes) {
                    path(line)
                    ctx.stroke()
                }
            }
        }
        Label {
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 3
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            text: slot.modelData.name
            elide: Text.ElideRight
            color: Theme.textSecondary
            font.pixelSize: 9
        }
    }
}
