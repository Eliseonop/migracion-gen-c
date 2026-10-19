import QtQuick
import QtQuick.Controls.Basic

ApplicationWindow {
    id: window
    required property var home
    property string projectId: ""

    width: 1280
    height: 800
    visible: true
    title: projectId ? "GenClipPro · " + projectId : "GenClipPro"
    color: Theme.bg

    Home {
        anchors.fill: parent
        anchors.margins: 20
        visible: !window.projectId
        model: window.home
        onOpenProject: (pid) => window.projectId = pid
    }

    Column {
        anchors.centerIn: parent
        spacing: 12
        visible: !!window.projectId
        Label { text: "El editor llega en la pantalla 2 de F5"; color: Theme.text; font.pixelSize: 18 }
        Button { text: "Volver al inicio"; onClicked: window.projectId = "" }
    }

    footer: Label {
        visible: !!window.home.error
        text: window.home.error
        color: Theme.danger
        padding: 8
    }
}
