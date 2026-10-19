import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// El editor (VideoEditor.jsx): barra de herramientas, vista previa, inspector y timeline.
Item {
    id: editor
    required property var model
    signal closed

    Shortcut { sequences: [StandardKey.Undo]; enabled: editor.visible; onActivated: editor.model.undo() }
    Shortcut { sequences: [StandardKey.Redo, "Ctrl+Y"]; enabled: editor.visible; onActivated: editor.model.redo() }
    Shortcut { sequences: ["Delete", "Backspace"]; enabled: editor.visible; onActivated: editor.model.deleteSelection() }
    Shortcut { sequence: "S"; enabled: editor.visible; onActivated: editor.model.splitAtPlayhead() }
    Shortcut { sequence: "Ctrl+D"; enabled: editor.visible; onActivated: editor.model.duplicateSelection() }
    Shortcut { sequences: [StandardKey.Copy]; enabled: editor.visible; onActivated: editor.model.copySelection() }
    Shortcut { sequences: [StandardKey.Cut]; enabled: editor.visible; onActivated: editor.model.cutSelection() }
    Shortcut { sequences: [StandardKey.Paste]; enabled: editor.visible; onActivated: editor.model.paste() }
    Shortcut { sequence: "Escape"; enabled: editor.visible; onActivated: editor.model.clearSelection() }
    Shortcut { sequence: "Space"; enabled: editor.visible; onActivated: editor.playing = !editor.playing }
    Shortcut { sequence: "Left"; enabled: editor.visible; onActivated: editor.model.nudgePlayhead(-0.1) }
    Shortcut { sequence: "Right"; enabled: editor.visible; onActivated: editor.model.nudgePlayhead(0.1) }
    Shortcut { sequence: "Shift+Left"; enabled: editor.visible; onActivated: editor.model.nudgePlayhead(-1) }
    Shortcut { sequence: "Shift+Right"; enabled: editor.visible; onActivated: editor.model.nudgePlayhead(1) }
    Shortcut { sequence: "V"; enabled: editor.visible; onActivated: editor.model.toggleDisabled() }
    Shortcut { sequence: "M"; enabled: editor.visible; onActivated: editor.model.toggleMarker() }
    Shortcut { sequence: ","; enabled: editor.visible; onActivated: editor.model.jumpSnap(-1) }
    Shortcut { sequence: "."; enabled: editor.visible; onActivated: editor.model.jumpSnap(1) }

    // Reproducir sin vídeo todavía: el cabezal avanza en tiempo real hasta el final.
    property bool playing: false
    Timer {
        interval: 16
        repeat: true
        running: editor.playing && editor.visible
        property double last: 0
        onRunningChanged: last = Date.now()
        onTriggered: {
            const now = Date.now()
            const next = editor.model.playhead + (now - last) / 1000
            last = now
            if (next >= editor.model.duration) {
                editor.model.playhead = editor.model.duration
                editor.playing = false
            } else {
                editor.model.playhead = next
            }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 12
        spacing: 10

        RowLayout {
            Layout.fillWidth: true
            spacing: 6
            ToolButton {
                text: "‹ Proyectos"
                palette.buttonText: Theme.textSecondary
                background: Rectangle { radius: Theme.radiusSm; color: parent.hovered ? Qt.rgba(1, 1, 1, 0.06) : "transparent" }
                onClicked: editor.closed()
            }
            Label {
                Layout.fillWidth: true
                text: editor.model.projectName
                color: Theme.text
                font.pixelSize: 16
                font.bold: true
                elide: Text.ElideRight
            }
            IconButton { text: "↶"; tip: "Deshacer (Ctrl+Z)"; enabled: editor.model.canUndo; onClicked: editor.model.undo() }
            IconButton { text: "↷"; tip: "Rehacer (Ctrl+Y)"; enabled: editor.model.canRedo; onClicked: editor.model.redo() }
            IconButton { text: editor.playing ? "⏸" : "▶"; tip: "Reproducir (Espacio)"; onClicked: editor.playing = !editor.playing }
            IconButton { text: "⚑"; tip: "Marcador en el cabezal (M)"; onClicked: editor.model.toggleMarker() }
            IconButton { text: "✂"; tip: "Cortar en el cabezal (S)"; onClicked: editor.model.splitAtPlayhead() }
            IconButton {
                text: "⧉"; tip: "Duplicar (Ctrl+D)"
                enabled: editor.model.selection.length > 0
                onClicked: editor.model.duplicateSelection()
            }
            IconButton {
                text: "🗑"; tip: "Eliminar (Supr)"
                enabled: editor.model.selection.length > 0
                onClicked: editor.model.deleteSelection()
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.preferredHeight: 3
            spacing: 10

            MaterialPanel {
                Layout.preferredWidth: 260
                Layout.fillHeight: true
                model: editor.model
                dragLayer: overlay
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                radius: Theme.radiusLg
                color: Theme.card
                border.color: Theme.line

                Rectangle {
                    id: frame
                    readonly property real fit: Math.min(parent.width - 40, (parent.height - 56) * editor.model.aspect)
                    anchors.horizontalCenter: parent.horizontalCenter
                    y: 16
                    width: fit
                    height: fit / editor.model.aspect
                    color: "black"
                    border.color: Theme.lineBright
                    Label {
                        anchors.centerIn: parent
                        width: parent.width - 20
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.Wrap
                        text: "Vista previa\n(llega con gc_render)"
                        color: Theme.muted
                    }
                }
                Label {
                    anchors.top: frame.bottom
                    anchors.topMargin: 8
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: editor.model.formatTime(editor.model.playhead) + " / " + editor.model.formatTime(editor.model.duration)
                    color: Theme.textSecondary
                    font.family: "monospace"
                }
            }

            Inspector {
                Layout.preferredWidth: 300
                Layout.fillHeight: true
                model: editor.model
            }
        }

        Timeline {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.preferredHeight: 2
            model: editor.model
        }
    }

    Item { id: overlay; anchors.fill: parent; z: 100 }
}
