import QtQuick
import QtQuick.Controls.Basic

ApplicationWindow {
    id: window
    required property var home
    required property var editor
    required property var settings
    required property var folder // la carpeta de los proyectos (projects_folder.hpp)
    required property var updater // Ajustes › Versión (updater.hpp)
    property bool settingsOpen: false
    // La de producción mira al arrancar si hay una versión nueva (en este PC o en GitHub).
    Component.onCompleted: if (updater.installed) updater.check()
    // «GenClipPro 0.23.0»: la versión de CMakeLists.txt (app.cpp la pone en la aplicación).
    readonly property string appName: Qt.application.version ? "GenClipPro " + Qt.application.version : "GenClipPro"

    width: 1280
    height: 800
    visible: true
    title: editor.isOpen ? appName + " · " + editor.projectName : appName
    color: Theme.bg

    Home {
        anchors.fill: parent
        anchors.margins: 20
        visible: !window.editor.isOpen && !window.settingsOpen
        model: window.home
        folder: window.folder
        updater: window.updater
        onOpenProject: (pid) => window.editor.openProject(pid)
        onOpenSettings: window.settingsOpen = true
    }

    Editor {
        anchors.fill: parent
        visible: window.editor.isOpen && !window.settingsOpen
        model: window.editor
        onClosed: { window.editor.closeProject(); window.home.reload() }
        onOpenSettings: window.settingsOpen = true
    }

    Settings {
        anchors.fill: parent
        visible: window.settingsOpen
        model: window.settings
        folder: window.folder
        updater: window.updater
        onClosed: window.settingsOpen = false
    }
    Shortcut { sequence: "Ctrl+,"; onActivated: window.settingsOpen = !window.settingsOpen }

    footer: Label {
        readonly property string message: window.editor.isOpen ? window.editor.error
                                                               : window.home.error || window.folder.error
        visible: !!message
        text: message
        color: Theme.danger
        padding: 8
    }
}
