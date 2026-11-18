import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// La tarjeta de un vídeo o una imagen del material (.ed-card.grid de MaterialClipGrid.jsx): la miniatura
// (el primer fotograma o la imagen, recortados como `object-fit: cover`), el ▶ encima de los vídeos, las
// marcas (seguimiento de caras), la duración y el nombre. Se arrastra a la timeline como la fila de la
// lista (clave «material»); clic derecho en un vídeo del proyecto, su menú.
Item {
    id: slot
    required property var modelData
    required property var host
    property string thumb: ""
    readonly property bool isVideo: modelData.kind === "video"

    Rectangle {
        id: card
        readonly property string materialKey: slot.modelData.key
        property bool dragging
        objectName: "material:" + slot.modelData.key
        width: slot.width
        height: slot.height
        radius: Theme.radiusMd
        color: Theme.card2
        border.color: hover.hovered ? Theme.lineBright : Theme.line
        opacity: dragging ? 0.75 : 1
        Drag.active: dragging
        Drag.keys: ["material"]

        HoverHandler { id: hover; cursorShape: card.dragging ? Qt.ClosedHandCursor : Qt.OpenHandCursor }
        TapHandler {
            acceptedButtons: Qt.RightButton
            enabled: slot.modelData.source === "project" && slot.isVideo
            onTapped: matMenu.popup()
        }
        Menu {
            id: matMenu
            objectName: "material:menu:" + slot.modelData.key
            MenuItem {
                objectName: "material:faceTrack:" + slot.modelData.key
                enabled: !slot.host.model.faceTrackBusy
                text: slot.modelData.faceTrack ? "Aplicar seguimiento de caras" : "Seguimiento de caras"
                onTriggered: slot.host.model.faceTrackMaterial(slot.modelData.key, false)
            }
            MenuItem {
                objectName: "material:faceRetrack:" + slot.modelData.key
                visible: !!slot.modelData.faceTrack
                height: visible ? implicitHeight : 0
                enabled: !slot.host.model.faceTrackBusy
                text: "Volver a analizar caras"
                onTriggered: slot.host.model.faceTrackMaterial(slot.modelData.key, true)
            }
            // canUpgradeFrom: un vídeo del proyecto (no un segmento) que viene de YouTube.
            MenuItem {
                objectName: "material:upgrade:" + slot.modelData.key
                visible: !!slot.modelData.canUpgrade
                height: visible ? implicitHeight : 0
                text: "Usar esta calidad en la timeline"
                onTriggered: slot.host.model.checkUpgrade(slot.modelData.key)
            }
        }
        DragHandler {
            id: drag
            onActiveChanged: {
                if (active) {
                    card.Drag.hotSpot = drag.centroid.pressPosition // el clip empieza bajo el puntero
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
            ParentChange { target: card; parent: slot.host.dragLayer }
        }

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 3
            spacing: 3
            // .ed-card-media
            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                radius: 6
                color: "#111111"
                clip: true
                Image {
                    id: thumbImage
                    objectName: "material:thumb:" + slot.modelData.key
                    anchors.fill: parent
                    source: slot.thumb
                    fillMode: Image.PreserveAspectCrop
                    asynchronous: true
                    cache: true
                    sourceSize.width: 240
                    visible: status === Image.Ready
                }
                MIcon {
                    anchors.centerIn: parent
                    visible: thumbImage.status !== Image.Ready && !slot.isVideo
                    name: "image"
                    size: 26
                    color: Theme.muted
                }
                // .ed-play-ov: el ▶ de los vídeos (aquí no reproduce: es la marca de vídeo).
                Rectangle {
                    visible: slot.isVideo
                    anchors.centerIn: parent
                    width: 30
                    height: 30
                    radius: 15
                    color: Qt.rgba(0, 0, 0, 0.55)
                    MIcon { anchors.centerIn: parent; name: "play_arrow"; size: 20; color: "white" }
                }
                // .ed-edit-corner: «Preparar clip», el vídeo del proyecto entero en el Clip Editor.
                AbstractButton {
                    id: editCorner
                    objectName: "material:clipEdit:" + slot.modelData.key
                    visible: slot.isVideo && slot.modelData.source === "project"
                    anchors.top: parent.top
                    anchors.left: parent.left
                    anchors.margins: 4
                    width: 22
                    height: 22
                    hoverEnabled: true
                    ToolTip.visible: hovered
                    ToolTip.delay: 500
                    ToolTip.text: "Preparar clip"
                    onClicked: slot.host.model.openClipEditor(slot.modelData.key)
                    contentItem: MIcon { name: "movie_edit"; size: 15; color: "white" }
                    background: Rectangle {
                        radius: 6
                        color: editCorner.hovered ? Theme.accent : Qt.rgba(0, 0, 0, 0.55)
                        border.color: editCorner.hovered ? Qt.rgba(1, 1, 1, 0.35) : Qt.rgba(1, 1, 1, 0.2)
                    }
                }
                // .ed-add-corner: el + que lo pone en el cabezal (addAsset).
                AbstractButton {
                    id: addCorner
                    objectName: "material:add:" + slot.modelData.key
                    anchors.top: parent.top
                    anchors.right: parent.right
                    anchors.margins: 4
                    width: 22
                    height: 22
                    hoverEnabled: true
                    scale: hovered ? 1.08 : 1
                    ToolTip.visible: hovered
                    ToolTip.delay: 500
                    ToolTip.text: "Añadir en el cursor"
                    onClicked: slot.host.model.addMaterial(slot.modelData.key)
                    contentItem: MIcon { name: "add"; size: 16; color: "white" }
                    background: Rectangle {
                        radius: 6
                        border.color: Qt.rgba(1, 1, 1, 0.35)
                        gradient: Gradient {
                            orientation: Gradient.Horizontal
                            GradientStop { position: 0; color: Theme.accent }
                            GradientStop { position: 1; color: Theme.accent2 }
                        }
                    }
                }
                // .ed-card-badges: seguimiento de caras guardado.
                Rectangle {
                    visible: !!slot.modelData.faceTrack
                    anchors.left: parent.left
                    anchors.bottom: parent.bottom
                    anchors.margins: 4
                    width: 17
                    height: 17
                    radius: 4
                    color: Qt.rgba(0, 0, 0, 0.62)
                    MIcon { anchors.centerIn: parent; name: "face"; size: 12; color: "#7fd4ff" }
                    ToolTip.visible: faceHover.hovered
                    ToolTip.text: "Seguimiento de caras guardado (se aplica al agregarlo)"
                    HoverHandler { id: faceHover }
                }
                Label {
                    visible: slot.isVideo
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    anchors.margins: 4
                    leftPadding: 3
                    rightPadding: 3
                    text: slot.host.model.formatTime(slot.modelData.duration)
                    color: "white"
                    font.pixelSize: 10
                    background: Rectangle { radius: 3; color: Qt.rgba(0, 0, 0, 0.6) }
                }
            }
            Label {
                Layout.fillWidth: true
                leftPadding: 3
                rightPadding: 3
                text: slot.modelData.name
                color: Theme.text
                font.pixelSize: 11
                elide: Text.ElideRight
                ToolTip.visible: nameHover.hovered && truncated
                ToolTip.text: slot.modelData.name
                HoverHandler { id: nameHover }
            }
            Label {
                Layout.fillWidth: true
                Layout.bottomMargin: 2
                leftPadding: 3
                visible: slot.modelData.source === "library"
                text: "Biblioteca"
                color: Theme.muted
                font.pixelSize: 10
            }
        }
    }
}
