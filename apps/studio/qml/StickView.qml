import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Dialogs
import QtQuick.Layouts

// Un personaje stick abierto en las colecciones (EdLibraryStick.jsx): un grupo por expresión, cada uno
// con «Subir» (sus archivos entran ya en esa expresión). Las tarjetas se arrastran a la timeline o se
// añaden con «+», con el croma del personaje; el selector de cada una la cambia de expresión.
Flickable {
    id: view
    objectName: "stickView"
    required property var model
    required property Item dragLayer
    property string query: ""
    readonly property var stick: model.stick
    readonly property var words: query.toLowerCase().split(/\s+/).filter((w) => w)
    property string uploadTo: ""

    clip: true
    contentHeight: groups.implicitHeight

    function matches(item) { return view.words.every((w) => (item.title + " " + item.rel).toLowerCase().includes(w)) }

    FileDialog {
        id: upload
        title: "Subir vídeos o imágenes al stick"
        fileMode: FileDialog.OpenFiles
        nameFilters: ["Vídeos e imágenes (*.mp4 *.mov *.webm *.mkv *.png *.jpg *.jpeg *.webp *.gif)"]
        onAccepted: view.model.addStickFiles(view.uploadTo, selectedFiles)
    }

    Column {
        id: groups
        width: view.width
        spacing: 10
        Row {
            spacing: 6
            Label { text: (view.stick.emoji || "🎭") + " " + (view.stick.name || ""); color: Theme.text; font.bold: true }
            Rectangle {
                visible: !!view.stick.chroma
                anchors.verticalCenter: parent.verticalCenter
                width: 14
                height: 14
                radius: 7
                color: view.stick.chroma || "transparent"
                border.color: Theme.lineBright
                ToolTip.visible: chromaHover.hovered
                ToolTip.text: "Croma " + view.stick.chroma + ": se quita al agregarlo"
                HoverHandler { id: chromaHover }
            }
        }
        Label {
            visible: view.words.length > 0 && !(view.stick.groups || []).some((g) => g.items.some(view.matches))
            text: "Sin resultados en este stick."
            color: Theme.muted
        }
        Repeater {
            // Buscando, solo los grupos con resultados; si no, todos (los vacíos son donde subir).
            model: (view.stick.groups || []).filter((g) => !view.words.length || g.items.some(view.matches))
            Column {
                id: group
                required property var modelData
                readonly property var items: modelData.items.filter(view.matches)
                width: groups.width
                spacing: 4
                RowLayout {
                    width: parent.width
                    Label { text: group.modelData.emoji + " " + group.modelData.label; color: Theme.text; font.pixelSize: 12; font.bold: true }
                    Label { text: group.items.length; color: Theme.muted; font.pixelSize: 11 }
                    Item { Layout.fillWidth: true }
                    IconBtn {
                        objectName: "stick:upload:" + group.modelData.id
                        iconName: "upload"
                        iconSize: 15
                        tip: "Subir a " + group.modelData.label
                        onClicked: {
                            view.uploadTo = group.modelData.id
                            upload.open()
                        }
                    }
                }
                Label {
                    visible: !group.items.length
                    text: "Sube aquí vídeos o imágenes «" + group.modelData.label.toLowerCase() + "»"
                    color: Theme.muted
                    font.pixelSize: 11
                }
                Flow {
                    width: parent.width
                    spacing: 4
                    Repeater {
                        model: group.items
                        MaterialTile {
                            id: tile
                            required property var modelData
                            width: 96
                            height: 108
                            materialKey: modelData.key
                            dragLayer: view.dragLayer
                            tip: modelData.rel
                            Image {
                                visible: !!tile.modelData.thumb
                                x: 4
                                y: 4
                                width: parent.width - 8
                                height: 56
                                source: tile.modelData.thumb
                                fillMode: Image.PreserveAspectFit
                                sourceSize.width: 192
                                asynchronous: true
                            }
                            MIcon {
                                visible: !tile.modelData.thumb
                                x: (parent.width - width) / 2
                                y: 20
                                name: "movie"
                                size: 26
                                color: Theme.textSecondary
                            }
                            IconBtn {
                                objectName: "stick:add:" + tile.modelData.rel
                                x: parent.width - width - 2
                                y: 2
                                iconName: "add"
                                iconSize: 14
                                tip: "Agregar al proyecto (con croma)"
                                onClicked: view.model.addMaterial(tile.modelData.key)
                            }
                            Label {
                                x: 4
                                y: 62
                                width: parent.width - 8
                                text: tile.modelData.title
                                elide: Text.ElideRight
                                color: Theme.textSecondary
                                font.pixelSize: 10
                            }
                            MiniSelect {
                                objectName: "stick:expr:" + tile.modelData.rel
                                x: 4
                                y: 80
                                width: parent.width - 8
                                tip: "Cambiar de expresión"
                                options: (view.stick.expressions || []).map((x) => ({ value: x.id, label: x.emoji + " " + x.label }))
                                value: tile.modelData.expression
                                onPicked: (v) => { if (v !== tile.modelData.expression) view.model.setStickExpression(tile.modelData.rel, v) }
                            }
                        }
                    }
                }
            }
        }
    }
}
