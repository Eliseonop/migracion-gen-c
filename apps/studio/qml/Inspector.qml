import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// Inspector (EdInspector.jsx): de momento, lo que es el clip seleccionado.
Rectangle {
    id: panel
    required property var model
    readonly property var clip: model.selectedClip

    radius: Theme.radiusLg
    color: Theme.card
    border.color: Theme.line

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 16
        spacing: 8
        Label { text: "Inspector"; color: Theme.text; font.pixelSize: 15; font.bold: true }
        Label {
            visible: !panel.clip.id
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            text: "Selecciona un clip en la timeline."
            color: Theme.muted
        }
        GridLayout {
            visible: !!panel.clip.id
            columns: 2
            columnSpacing: 12
            rowSpacing: 6
            Repeater {
                model: panel.clip.id ? [
                    ["Clip", panel.clip.label],
                    ["Tipo", panel.clip.kind],
                    ["Pista", panel.clip.trackId],
                    ["Inicio", panel.model.formatTime(panel.clip.start)],
                    ["Duración", panel.model.formatTime(panel.clip.duration)],
                    ["Entrada", panel.clip.inPoint.toFixed(3) + " s"],
                    ["Salida", panel.clip.outPoint.toFixed(3) + " s"],
                    ["Velocidad", panel.clip.speed + "x"],
                    ["Opacidad", Math.round(panel.clip.opacity * 100) + " %"]
                ] : []
                delegate: Item {
                    required property var modelData
                    Layout.columnSpan: 2
                    Layout.fillWidth: true
                    implicitHeight: row.implicitHeight
                    RowLayout {
                        id: row
                        width: parent.width
                        Label { Layout.preferredWidth: 80; text: modelData[0]; color: Theme.muted }
                        Label { Layout.fillWidth: true; text: modelData[1]; color: Theme.text; elide: Text.ElideRight }
                    }
                }
            }
        }
        Label {
            visible: panel.model.selection.length > 1
            text: panel.model.selection.length + " clips seleccionados"
            color: Theme.textSecondary
        }
        Item { Layout.fillHeight: true }
    }
}
