import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// Inicio (Home.jsx): carpetas a la izquierda, crear proyecto y la rejilla de la carpeta abierta.
Item {
    id: home
    required property var model
    property bool newGroup
    signal openProject(string pid)
    signal openSettings

    RowLayout {
        anchors.fill: parent
        spacing: 20

        Rectangle {
            Layout.preferredWidth: 240
            Layout.fillHeight: true
            radius: Theme.radiusLg
            color: Theme.card
            border.color: Theme.line

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 12
                spacing: 2

                SideItem {
                    Layout.fillWidth: true
                    icon: "⌂"
                    name: "Inicio"
                    count: home.model.rootCount
                    on: !home.model.groupId
                    onClicked: home.model.openGroup("")
                    onDropped: (pid) => home.model.moveProject(pid, "")
                }
                RowLayout {
                    Layout.fillWidth: true
                    Layout.topMargin: 12
                    Layout.leftMargin: 8
                    Label { Layout.fillWidth: true; text: "CARPETAS"; color: Theme.muted; font.pixelSize: 11; font.bold: true }
                    IconButton { text: "+"; tip: "Nueva carpeta"; onClicked: home.newGroup = true }
                }
                InlineEdit {
                    Layout.fillWidth: true
                    visible: home.newGroup
                    active: home.newGroup
                    placeholderText: "Nombre de la carpeta…"
                    onDone: (value) => { home.newGroup = false; if (value !== null) home.model.createGroup(value) }
                }
                Label {
                    Layout.fillWidth: true
                    visible: home.model.groups.length === 0 && !home.newGroup
                    text: "Crea una carpeta y arrastra proyectos dentro."
                    wrapMode: Text.Wrap
                    color: Theme.muted
                    font.pixelSize: 13
                    padding: 8
                }
                ListView {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    spacing: 2
                    model: home.model.groups
                    delegate: SideItem {
                        id: groupItem
                        required property var modelData
                        width: ListView.view.width
                        groupId: groupItem.modelData.id
                        icon: home.model.groupId === groupItem.modelData.id ? "📂" : "📁"
                        name: groupItem.modelData.name
                        count: groupItem.modelData.count
                        on: home.model.groupId === groupItem.modelData.id
                        editable: true
                        onClicked: home.model.openGroup(groupItem.modelData.id)
                        onRenamed: (n) => home.model.renameGroup(groupItem.modelData.id, n)
                        onRemoved: home.model.deleteGroup(groupItem.modelData.id)
                        onDropped: (pid) => home.model.moveProject(pid, groupItem.modelData.id)
                    }
                }
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 16

            Rectangle {
                Layout.fillWidth: true
                implicitHeight: create.implicitHeight + 40
                radius: Theme.radiusLg
                color: Theme.card
                border.color: Theme.line
                ColumnLayout {
                    id: create
                    anchors.fill: parent
                    anchors.margins: 20
                    spacing: 12
                    RowLayout {
                        Label { Layout.fillWidth: true; text: "Nuevo proyecto"; color: Theme.text; font.pixelSize: 17; font.bold: true }
                        IconButton { objectName: "home:settings"; text: "⚙"; tip: "Ajustes (Ctrl+,)"; onClicked: home.openSettings() }
                    }
                    RowLayout {
                        spacing: 10
                        TextField {
                            id: newName
                            Layout.fillWidth: true
                            placeholderText: "Nombre del proyecto…"
                            font.pixelSize: 15
                            topPadding: 12
                            bottomPadding: 12
                            leftPadding: 16
                            rightPadding: 16
                            color: Theme.text
                            placeholderTextColor: Theme.muted
                            background: Rectangle {
                                radius: Theme.radiusMd
                                color: Theme.card2
                                border.color: newName.activeFocus ? Theme.accent : Theme.lineBright
                            }
                            onAccepted: createButton.clicked()
                        }
                        ComboBox {
                            id: folderPick
                            visible: home.model.groups.length > 0
                            textRole: "name"
                            valueRole: "id"
                            implicitHeight: newName.implicitHeight
                            Layout.preferredWidth: 180
                            contentItem: Label {
                                leftPadding: 14
                                text: folderPick.displayText
                                color: Theme.text
                                verticalAlignment: Text.AlignVCenter
                                elide: Text.ElideRight
                            }
                            palette.buttonText: Theme.text
                            palette.button: Theme.card2
                            palette.window: Theme.card2
                            palette.text: Theme.text
                            palette.highlight: Theme.card3
                            palette.highlightedText: Theme.text
                            palette.dark: Theme.muted
                            background: Rectangle { radius: Theme.radiusMd; color: Theme.card2; border.color: Theme.lineBright }
                            model: [{ id: "", name: "Sin carpeta" }].concat(home.model.groups)
                            Component.onCompleted: currentIndex = indexOfValue(home.model.groupId)
                            Connections {
                                target: home.model
                                function onChanged() { folderPick.currentIndex = folderPick.indexOfValue(home.model.groupId) }
                            }
                        }
                        Button {
                            id: createButton
                            text: "Crear"
                            implicitHeight: newName.implicitHeight
                            leftPadding: 18
                            rightPadding: 18
                            font.pixelSize: 14
                            font.bold: true
                            palette.buttonText: "white"
                            background: Rectangle {
                                radius: Theme.radiusMd
                                gradient: Gradient {
                                    orientation: Gradient.Horizontal
                                    GradientStop { position: 0; color: Theme.accent }
                                    GradientStop { position: 1; color: Theme.accent2 }
                                }
                            }
                            onClicked: {
                                const pid = home.model.createProject(newName.text, folderPick.visible ? folderPick.currentValue : "")
                                if (pid) { newName.clear(); home.openProject(pid) }
                            }
                        }
                    }
                }
            }

            RowLayout {
                visible: !!home.model.groupId
                spacing: 6
                Button {
                    flat: true
                    text: "⌂ Inicio"
                    palette.buttonText: crumbDrop.containsDrag ? Theme.accent : Theme.textSecondary
                    onClicked: home.model.openGroup("")
                    DropArea {
                        id: crumbDrop
                        anchors.fill: parent
                        keys: ["project"]
                        onDropped: (e) => home.model.moveProject(e.source.projectId, "")
                    }
                }
                Label { text: "›"; color: Theme.muted }
                Label { text: "📂 " + home.model.groupName; color: Theme.text; font.bold: true }
            }

            Column {
                Layout.alignment: Qt.AlignHCenter
                Layout.topMargin: 40
                visible: home.model.empty || (!!home.model.groupId && home.model.projects.length === 0)
                spacing: 8
                Label {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: home.model.empty ? "👋 Aún no tienes proyectos" : "Esta carpeta está vacía"
                    color: Theme.text
                    font.pixelSize: home.model.empty ? 18 : 16
                }
                Label {
                    width: 420
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.Wrap
                    color: Theme.muted
                    text: home.model.empty ? "Crea uno arriba para empezar a guardar vídeos y audios."
                        : "Crea un proyecto con esta carpeta elegida, o vuelve a Inicio y arrastra proyectos a «"
                          + home.model.groupName + "» en la barra lateral."
                }
            }

            ScrollView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                contentWidth: availableWidth
                clip: true
                Flow {
                    width: parent.width
                    spacing: 18
                    Repeater {
                        model: home.model.groupId ? [] : home.model.groups
                        FolderCard {
                            id: folderCard
                            required property var modelData
                            group: folderCard.modelData
                            onOpen: home.model.openGroup(folderCard.modelData.id)
                            onDropped: (pid) => home.model.moveProject(pid, folderCard.modelData.id)
                        }
                    }
                    Repeater {
                        model: home.model.projects
                        ProjectCard {
                            id: projectCard
                            required property var modelData
                            project: projectCard.modelData
                            dragLayer: dragOverlay
                            onOpen: home.openProject(projectCard.modelData.id)
                            onRenamed: (n) => home.model.renameProject(projectCard.modelData.id, n)
                            onDuplicated: home.model.duplicateProject(projectCard.modelData.id)
                            onRemoved: home.model.deleteProject(projectCard.modelData.id)
                        }
                    }
                }
            }
        }
    }

    Item { id: dragOverlay; anchors.fill: parent; z: 10 }
}
