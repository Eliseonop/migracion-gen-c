import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// El panel junto a la timeline (EdCrops.jsx): los keyframes del clip seleccionado, numerados con el color
// de su rombo (kfColor) y su tiempo; clic lleva el cabezal al keyframe, y cada fila se copia o se quita.
// Sin keyframes, «Animar» crea el primero en el cabezal; con uno copiado, «Pegar keyframe».
// Divergencia: la pestaña «Clip» (velocidad y volumen) de React está en el inspector del studio.
Rectangle {
    id: kfPanel
    required property var model
    readonly property var info: model.keyframePanel
    readonly property string clipId: info.clipId || ""
    readonly property string kind: info.kind || ""
    readonly property var items: (info.kfs || []).slice().sort((a, b) => a.t - b.t)
    readonly property bool canKeyframe: ["video", "image", "text", "shape", "audio"].indexOf(kind) >= 0
    readonly property var colors: ["#e8a34d", "#5aa9e6", "#67b99a", "#c98bb9", "#e0757c", "#8f8ff0", "#c9b458", "#6ac4c9"]
    property string selKf: ""

    objectName: "kfPanel"
    radius: 6
    color: Theme.topbar
    border.color: Theme.line
    clip: true

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 1
        spacing: 0

        // .ed-crops-tabs: «Keyframes» (la única aquí).
        Item {
            Layout.fillWidth: true
            implicitHeight: 28
            Label {
                anchors.centerIn: parent
                text: "Keyframes"
                color: Theme.text
                font.pixelSize: 11
                font.weight: Font.ExtraBold
            }
            Rectangle { anchors.bottom: parent.bottom; anchors.horizontalCenter: parent.horizontalCenter; width: 90; height: 2; color: Theme.accent }
            Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.line; z: -1 }
        }

        // .ed-crops-empty
        ColumnLayout {
            visible: !kfPanel.canKeyframe || kfPanel.items.length === 0
            Layout.fillWidth: true
            Layout.margins: 12
            spacing: 10
            Label {
                objectName: "kfPanel:empty"
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                color: Theme.muted
                font.pixelSize: 12
                text: !kfPanel.canKeyframe ? "Selecciona un clip, imagen, figura o texto."
                    : "Pulsa Animar para crear el primer keyframe. Después, mover "
                      + (kfPanel.kind === "audio" ? "el volumen" : kfPanel.kind === "text" ? "el texto" : "el clip")
                      + " creará keyframes en el cabezal."
            }
            GhostButton {
                objectName: "kfPanel:animate"
                visible: kfPanel.canKeyframe
                Layout.alignment: Qt.AlignHCenter
                text: "Animar"
                onClicked: kfPanel.model.toggleKeyframe()
            }
        }

        Label {
            objectName: "kfPanel:count"
            visible: kfPanel.canKeyframe && kfPanel.items.length > 0
            Layout.leftMargin: 10
            Layout.topMargin: 6
            Layout.bottomMargin: 3
            text: kfPanel.items.length + (kfPanel.items.length === 1 ? " keyframe" : " keyframes")
            color: Theme.muted
            font.pixelSize: 11
        }
        // .ed-crops-list
        ListView {
            visible: kfPanel.canKeyframe && kfPanel.items.length > 0
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.leftMargin: 6
            Layout.rightMargin: 6
            clip: true
            spacing: 3
            model: kfPanel.canKeyframe ? kfPanel.items : []
            delegate: Rectangle {
                id: row
                required property var modelData
                required property int index
                readonly property color tone: kfPanel.colors[index % kfPanel.colors.length]
                readonly property bool sel: kfPanel.selKf === modelData.id
                objectName: "kfPanel:row:" + index
                width: ListView.view.width
                height: 24
                radius: 5
                color: sel ? Qt.rgba(1, 0.23, 0.36, 0.08) : rowHover.hovered ? Theme.card3 : Theme.card2
                border.color: sel ? Theme.accent : Theme.line
                HoverHandler { id: rowHover }
                TapHandler {
                    onTapped: {
                        kfPanel.selKf = row.modelData.id
                        kfPanel.model.moveKeyframe(kfPanel.clipId, row.modelData.id, row.modelData.f)
                    }
                }
                Rectangle { width: 3; height: parent.height; radius: 2; color: row.tone } // border-left
                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 9
                    anchors.rightMargin: 3
                    spacing: 6
                    Rectangle { implicitWidth: 8; implicitHeight: 8; radius: 4; color: row.tone }
                    Label { Layout.minimumWidth: 14; text: row.index + 1; color: Theme.text; font.pixelSize: 12; font.weight: Font.DemiBold }
                    Label {
                        Layout.fillWidth: true
                        text: kfPanel.model.formatTime(row.modelData.t)
                        color: Theme.muted
                        font.pixelSize: 11
                        elide: Text.ElideRight
                    }
                    IconBtn {
                        objectName: "kfPanel:copy:" + row.index
                        iconName: "content_copy"
                        iconSize: 12
                        tip: "Copiar keyframe (Alt+C)"
                        onClicked: {
                            kfPanel.selKf = row.modelData.id
                            kfPanel.model.copyKeyframeOf(kfPanel.clipId, row.modelData.id, row.modelData.f)
                        }
                    }
                    IconBtn {
                        objectName: "kfPanel:delete:" + row.index
                        iconName: "delete"
                        iconSize: 12
                        tip: "Eliminar keyframe"
                        onClicked: kfPanel.model.deleteKeyframe(kfPanel.clipId, row.modelData.id, row.modelData.f)
                    }
                }
            }
        }
        // .ed-crops-add
        GhostButton {
            objectName: "kfPanel:paste"
            visible: kfPanel.canKeyframe && kfPanel.items.length > 0 && kfPanel.model.hasKeyframeBoard
            Layout.leftMargin: 8
            Layout.bottomMargin: 10
            text: "Pegar keyframe"
            tip: "Pegar en el cabezal (Alt+V)"
            onClicked: kfPanel.model.pasteKeyframe()
        }
        Item { Layout.fillHeight: true; visible: !kfPanel.canKeyframe || kfPanel.items.length === 0 }
    }
}
