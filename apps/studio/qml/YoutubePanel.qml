import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// «Cargar clips» de YouTube (EdMaterial.jsx): enlace, historial, vídeo completo en una calidad y lo que dice el
// análisis (título, duración y tramos recomendados). Editar un tramo es del Clip Editor, que aún no está.
Flickable {
    id: yt
    required property var model
    readonly property var loader: model.youtube
    readonly property var res: loader.result
    readonly property bool hasResult: !!res.title || !!res.id
    readonly property var quals: hasResult ? res.qualities : []
    clip: true
    contentHeight: body.implicitHeight
    ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

    function fmt(t) {
        const v = Math.max(0, Math.floor(Number(t) || 0))
        const h = Math.floor(v / 3600)
        const m = Math.floor((v % 3600) / 60)
        const s = v % 60
        return (h ? h + ":" + String(m).padStart(2, "0") : m) + ":" + String(s).padStart(2, "0")
    }

    ColumnLayout {
        id: body
        width: yt.width - 12
        spacing: 6

        TextField {
            id: urlField
            objectName: "yt:url"
            Layout.fillWidth: true
            placeholderText: "https://www.youtube.com/watch?v=…"
            text: yt.loader.url
            enabled: !yt.loader.analyzing
            color: Theme.text
            selectByMouse: true
            background: Rectangle { radius: Theme.radiusSm; color: Theme.card2; border.color: urlField.activeFocus ? Theme.accent : Theme.lineBright }
            onTextEdited: yt.loader.url = text
            onAccepted: if (!yt.loader.analyzing) yt.loader.load()
        }
        Flow {
            Layout.fillWidth: true
            spacing: 4
            SmallButton {
                objectName: "yt:load"
                text: yt.loader.analyzing ? "Cargando…" : "Cargar"
                enabled: !yt.loader.analyzing
                onClicked: yt.loader.load()
            }
            SmallButton {
                objectName: "yt:history"
                text: "Historial"
                enabled: !yt.loader.analyzing
                onClicked: { yt.loader.reloadHistory(); historyMenu.popup() }
                Menu {
                    id: historyMenu
                    MenuItem { enabled: false; visible: yt.loader.history.length === 0; height: visible ? implicitHeight : 0; text: "Aún no hay enlaces consultados." }
                    Repeater {
                        model: yt.loader.history
                        MenuItem {
                            required property var modelData
                            text: modelData.title || modelData.url
                            onTriggered: yt.loader.load(modelData.url)
                        }
                    }
                    MenuSeparator { visible: yt.loader.history.length > 0 }
                    Menu {
                        title: "Quitar del historial"
                        enabled: yt.loader.history.length > 0
                        Repeater {
                            model: yt.loader.history
                            MenuItem {
                                required property var modelData
                                text: modelData.title || modelData.url
                                onTriggered: yt.loader.removeHistory(modelData)
                            }
                        }
                    }
                }
            }
            SmallButton {
                objectName: "yt:full"
                enabled: yt.loader.canAddFull
                text: "Vídeo completo" + (yt.quals.length ? " · " + yt.quals[0].short : "")
                ToolTip.visible: hovered
                ToolTip.delay: 500
                ToolTip.text: yt.loader.canAddFull ? "Agregar el vídeo completo al proyecto en " + (yt.quals.length ? yt.quals[0].short : "la mejor calidad")
                            : (yt.loader.download.title ? "Ya se está descargando un vídeo" : "Carga un vídeo para agregarlo completo al proyecto")
                onClicked: yt.loader.addFull(yt.quals.length ? yt.quals[0].quality : 0)
            }
            SmallButton {
                objectName: "yt:quality"
                text: "▾"
                enabled: yt.loader.canAddFull && yt.quals.length > 0
                onClicked: qualityMenu.popup()
                Menu {
                    id: qualityMenu
                    Repeater {
                        model: yt.quals
                        MenuItem {
                            required property var modelData
                            required property int index
                            text: modelData.quality + "p" + (modelData.tag ? " · " + modelData.tag : "") + (index === 0 ? " · Mejor" : "")
                                  + (modelData.meta ? " · " + modelData.meta : "")
                            onTriggered: yt.loader.addFull(modelData.quality)
                        }
                    }
                }
            }
        }
        RowLayout { // la descarga en curso
            visible: !!yt.loader.download.title
            Layout.fillWidth: true
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2
                Label {
                    objectName: "yt:download"
                    Layout.fillWidth: true
                    elide: Text.ElideRight
                    text: (yt.loader.download.message || "Preparando descarga…") + " · " + yt.fmt(yt.loader.download.elapsed)
                    color: Theme.textSecondary
                    font.pixelSize: 11
                }
                ProgressBar { Layout.fillWidth: true; from: 0; to: 1; value: yt.loader.download.progress || 0 }
            }
            SmallButton { objectName: "yt:cancel"; text: "✕"; onClicked: yt.loader.cancelDownload() }
        }
        ColumnLayout { // analizando
            visible: yt.loader.analyzing
            Layout.fillWidth: true
            ProgressBar { Layout.fillWidth: true; indeterminate: true }
            Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                font.pixelSize: 11
                color: Theme.textSecondary
                text: yt.loader.elapsed < 8 ? "Consultando YouTube y descargando metadatos…"
                    : yt.loader.elapsed < 25 ? "Sigue cargando (heatmap / descarga). No está colgado."
                    : "Lleva " + yt.loader.elapsed + "s. YouTube a veces tarda; espera o revisa la URL."
            }
        }
        Label { objectName: "yt:error"; visible: !!yt.loader.error; Layout.fillWidth: true; wrapMode: Text.Wrap; text: yt.loader.error; color: Theme.danger; font.pixelSize: 11 }
        Label { objectName: "yt:message"; visible: !!yt.loader.message; Layout.fillWidth: true; wrapMode: Text.Wrap; text: yt.loader.message; color: Theme.success; font.pixelSize: 11 }
        ColumnLayout {
            visible: yt.hasResult
            Layout.fillWidth: true
            spacing: 4
            Label { objectName: "yt:title"; Layout.fillWidth: true; wrapMode: Text.Wrap; text: yt.res.title || "Vídeo cargado"; color: Theme.text; font.bold: true }
            Label {
                objectName: "yt:meta"
                text: (yt.res.duration ? yt.fmt(yt.res.duration) : "")
                      + ((yt.res.segments || []).length ? " · " + yt.res.segments.length + " recomendados" : " · Sin tramos recomendados")
                color: Theme.muted
                font.pixelSize: 11
            }
            Repeater {
                model: yt.hasResult ? yt.res.segments : []
                Rectangle {
                    required property var modelData
                    Layout.fillWidth: true
                    implicitHeight: 46
                    radius: Theme.radiusSm
                    color: Theme.card2
                    border.color: Theme.line
                    readonly property int pct: Math.round((modelData.score || 0) * 100)
                    Column {
                        anchors.fill: parent
                        anchors.margins: 6
                        spacing: 2
                        Label { text: "Tramo #" + parent.parent.modelData.index + "   🔥 " + parent.parent.pct + "%"; color: Theme.text; font.pixelSize: 12 }
                        Label {
                            text: yt.fmt(parent.parent.modelData.start) + " → " + yt.fmt(parent.parent.modelData.end)
                                  + " (" + yt.fmt(parent.parent.modelData.end - parent.parent.modelData.start) + ")"
                            color: Theme.muted
                            font.pixelSize: 11
                        }
                        Rectangle { width: parent.width * parent.parent.pct / 100; height: 3; radius: 1.5; color: Theme.accent }
                    }
                }
            }
            Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                text: "Recortar un tramo llega con el Clip Editor; de momento, agrega el vídeo completo y córtalo en la timeline."
                color: Theme.muted
                font.pixelSize: 11
            }
        }
    }
}
