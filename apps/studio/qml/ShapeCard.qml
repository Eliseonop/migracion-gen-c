import QtQuick
import QtQuick.Controls.Basic

// Una figura del material (EdShapes.jsx): su miniatura; un clic la añade en el cabezal.
MaterialTile {
    id: tile
    required property var modelData
    property var geometry: ({ fills: [], strokes: [], fill: "", stroke: "#fff" }) // shapeThumb
    signal picked(string type, string label)
    materialKey: modelData.key
    tip: modelData.name
    onTapped: picked(modelData.type, modelData.name)

    Canvas {
        x: 9
        y: 4
        width: 40
        height: 40
        onPaint: {
            const g = tile.geometry
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
        text: tile.modelData.name
        elide: Text.ElideRight
        color: Theme.textSecondary
        font.pixelSize: 9
    }
}
