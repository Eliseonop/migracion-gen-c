import QtQuick
import QtQuick.Controls.Basic

// Una ficha del material que se arrastra a la timeline (la DropArea lee `materialKey`) o se
// pulsa. Lo que se pinta dentro va como hijos.
Item {
    id: slot
    required property string materialKey
    required property Item dragLayer
    property string tip
    default property alias content: card.data
    signal tapped
    width: 58
    height: 64

    Rectangle {
        id: card
        readonly property string materialKey: slot.materialKey
        property bool dragging
        objectName: "material:" + slot.materialKey
        width: slot.width
        height: slot.height
        radius: Theme.radiusSm
        color: Theme.card2
        border.color: hover.hovered ? Theme.accent : Theme.line
        opacity: dragging ? 0.75 : 1
        Drag.active: dragging
        Drag.keys: ["material"]
        ToolTip.visible: hover.hovered && !dragging && !!slot.tip
        ToolTip.text: slot.tip
        ToolTip.delay: 600

        HoverHandler { id: hover }
        TapHandler { onTapped: slot.tapped() }
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
    }
}
