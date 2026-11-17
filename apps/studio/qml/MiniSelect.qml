import QtQuick
import QtQuick.Controls.Basic

// FlipSelect mini de la fila del reproductor (.ed-vt-sel): 22 px de alto, 11 px y el triángulo.
// `options`: [{value, label}]; `value`: el elegido; `picked(value)` al elegir otro. `on`: resaltado con
// el acento (el marco de plataforma activo).
ComboBox {
    id: box
    property var options: []
    property var value
    property string tip
    property bool on: false
    signal picked(var value)

    model: options
    textRole: "label"
    valueRole: "value"
    currentIndex: options.findIndex((o) => o.value === value)
    displayText: currentIndex >= 0 ? options[currentIndex].label : ""
    onActivated: (index) => box.picked(options[index].value)
    implicitHeight: 22
    implicitWidth: Math.max(48, labelText.implicitWidth + 22)
    font.pixelSize: 11
    font.weight: Font.DemiBold
    hoverEnabled: true
    ToolTip.visible: hovered && !!tip && !popup.visible
    ToolTip.delay: 500
    ToolTip.text: tip
    contentItem: Label {
        id: labelText
        leftPadding: 6
        rightPadding: 4
        text: box.displayText
        font: box.font
        color: box.on ? Theme.accent : Theme.text
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }
    indicator: Canvas {
        x: box.width - width - 6
        y: (box.height - height) / 2
        width: 8
        height: 5
        onPaint: {
            const ctx = getContext("2d")
            ctx.reset()
            ctx.fillStyle = Theme.muted
            ctx.beginPath()
            ctx.moveTo(0, 0)
            ctx.lineTo(width, 0)
            ctx.lineTo(width / 2, height)
            ctx.closePath()
            ctx.fill()
        }
    }
    background: Rectangle {
        radius: Theme.radiusSm
        color: box.on ? Qt.rgba(1, 0.23, 0.36, 0.16) : Theme.card2
        border.color: box.on || box.popup.visible ? Theme.accent : Theme.lineBright
    }
    delegate: ItemDelegate {
        id: row
        required property var modelData
        required property int index
        width: ListView.view ? ListView.view.width : box.width
        height: 26
        highlighted: box.highlightedIndex === index
        contentItem: Label {
            text: row.modelData.label
            font: box.font
            color: box.currentIndex === row.index ? Theme.accent : Theme.text
            verticalAlignment: Text.AlignVCenter
        }
        background: Rectangle { radius: 6; color: row.highlighted ? Qt.rgba(1, 1, 1, 0.08) : "transparent" }
    }
    popup: Popup {
        y: box.height + 2
        width: Math.max(box.width, 120)
        padding: 4
        implicitHeight: Math.min(contentItem.implicitHeight + 8, 280)
        contentItem: ListView {
            clip: true
            implicitHeight: contentHeight
            model: box.popup.visible ? box.delegateModel : null
            currentIndex: box.highlightedIndex
        }
        background: Rectangle { radius: Theme.radiusMd; color: Theme.card3; border.color: Theme.lineBright }
    }
}
