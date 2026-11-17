import QtQuick

// MagnetIcon de EdTimeline.jsx: el imán en U (el SVG de 24 × 24 con trazo de 2,4).
Canvas {
    id: magnet
    property int size: 14
    property color color: Theme.text
    implicitWidth: size
    implicitHeight: size
    onColorChanged: requestPaint()
    onPaint: {
        const ctx = getContext("2d")
        ctx.reset()
        const k = width / 24
        ctx.scale(k, k)
        ctx.strokeStyle = magnet.color
        ctx.lineWidth = 2.4
        ctx.lineCap = "round"
        ctx.beginPath() // M6 3v8a6 6 0 0 0 12 0V3
        ctx.moveTo(6, 3)
        ctx.lineTo(6, 11)
        ctx.arc(12, 11, 6, Math.PI, 0, true)
        ctx.lineTo(18, 3)
        ctx.stroke()
        ctx.beginPath() // M6 7h3M15 7h3
        ctx.moveTo(6, 7)
        ctx.lineTo(9, 7)
        ctx.moveTo(15, 7)
        ctx.lineTo(18, 7)
        ctx.stroke()
    }
}
