import QtQuick
import QtQuick.Controls.Basic

ApplicationWindow {
    id: window
    required property var home
    required property var editor

    width: 1280
    height: 800
    visible: true
    title: editor.isOpen ? "GenClipPro · " + editor.projectName : "GenClipPro"
    color: Theme.bg

    Home {
        anchors.fill: parent
        anchors.margins: 20
        visible: !window.editor.isOpen
        model: window.home
        onOpenProject: (pid) => window.editor.openProject(pid)
    }

    Editor {
        anchors.fill: parent
        visible: window.editor.isOpen
        model: window.editor
        onClosed: { window.editor.closeProject(); window.home.reload() }
    }

    footer: Label {
        readonly property string message: window.editor.isOpen ? window.editor.error : window.home.error
        visible: !!message
        text: message
        color: Theme.danger
        padding: 8
    }
}
