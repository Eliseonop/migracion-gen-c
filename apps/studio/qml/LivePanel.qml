import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Dialogs
import QtQuick.Layouts

// Vídeo › Directo (de GenClipPro: video-yt no lo tiene). Pegas el enlace de un directo de TikTok o de YouTube,
// «Grabar» lo graba y «Marcar momento» (o Ctrl+M) apunta el instante; al parar, la grabación y un clip por marca
// entran en el material (live_recorder.hpp). Abajo, con qué yt-dlp se busca el directo, y elegir otro.
Flickable {
    id: live
    required property var model
    readonly property var rec: model.live
    readonly property bool idle: rec.status === "" || rec.status === "done" || rec.status === "error"
    clip: true
    contentHeight: body.implicitHeight
    ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
    // Al volver al panel, yt-dlp se busca otra vez: puede que se acabe de instalar.
    onVisibleChanged: if (visible) rec.refreshTools()

    // Los segundos de antes y de después de cada marca. Compacto: el SpinBox Basic tiene flechas de 40 px y en 86 px
    // no se veía el número.
    component Seconds: SpinBox {
        id: spin
        from: 0
        to: 600
        editable: true
        implicitWidth: 84
        implicitHeight: 26
        leftPadding: 4 + (down.indicator ? down.indicator.width : 0)
        rightPadding: 4 + (up.indicator ? up.indicator.width : 0)
        topPadding: 0
        bottomPadding: 0
        font.pixelSize: 12
        contentItem: TextInput {
            text: spin.displayText
            font: spin.font
            color: Theme.text
            selectionColor: Theme.accent
            selectedTextColor: "white"
            horizontalAlignment: Qt.AlignHCenter
            verticalAlignment: Qt.AlignVCenter
            readOnly: !spin.editable
            validator: spin.validator
            inputMethodHints: Qt.ImhDigitsOnly
            selectByMouse: true
        }
        down.indicator: Rectangle {
            x: 0
            implicitWidth: 22
            height: spin.height
            radius: Theme.radiusSm
            color: spin.down.pressed ? Theme.card3 : "transparent"
            MIcon { anchors.centerIn: parent; name: "remove"; size: 14; color: spin.down.hovered ? Theme.text : Theme.muted }
        }
        up.indicator: Rectangle {
            x: spin.width - width
            implicitWidth: 22
            height: spin.height
            radius: Theme.radiusSm
            color: spin.up.pressed ? Theme.card3 : "transparent"
            MIcon { anchors.centerIn: parent; name: "add"; size: 14; color: spin.up.hovered ? Theme.text : Theme.muted }
        }
        background: Rectangle {
            radius: Theme.radiusSm
            color: Theme.card2
            border.color: spin.activeFocus ? Theme.accent : Theme.lineBright
        }
    }

    // El yt-dlp que ya tengas (el de una venv, el de winget…): se recuerda para la próxima vez.
    FileDialog {
        id: ytdlpDialog
        title: "¿Dónde está yt-dlp?"
        nameFilters: Qt.platform.os === "windows" ? ["yt-dlp (yt-dlp*.exe)", "Programas (*.exe)"]
                                                  : ["yt-dlp (yt-dlp*)", "Todos los archivos (*)"]
        onAccepted: live.rec.chooseYtdlp(selectedFile.toString())
    }

    function fmt(t) {
        const v = Math.max(0, Math.floor(Number(t) || 0))
        const h = Math.floor(v / 3600)
        const m = Math.floor((v % 3600) / 60)
        const s = v % 60
        return (h ? h + ":" + String(m).padStart(2, "0") : m) + ":" + String(s).padStart(2, "0")
    }
    function size(b) {
        return b >= 1e9 ? (b / 1e9).toFixed(2) + " GB" : b >= 1e6 ? (b / 1e6).toFixed(1) + " MB" : Math.round(b / 1e3) + " KB"
    }

    // Marcar con el teclado mientras graba (M es la marca de la timeline).
    Shortcut {
        sequence: "Ctrl+M"
        enabled: live.visible && live.rec.status === "recording"
        onActivated: live.rec.mark()
    }

    ColumnLayout {
        id: body
        width: live.width - 12
        spacing: 8

        Label {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            text: "Graba un directo de TikTok o de YouTube y marca los mejores momentos: al parar, la grabación y un clip por marca entran en el material."
            color: Theme.muted
            font.pixelSize: 11
        }
        TextField {
            id: urlField
            objectName: "live:url"
            Layout.fillWidth: true
            placeholderText: "https://www.tiktok.com/@usuario/live o un enlace de YouTube"
            text: live.rec.url
            enabled: live.idle
            color: Theme.text
            selectByMouse: true
            background: Rectangle { radius: Theme.radiusSm; color: Theme.card2; border.color: urlField.activeFocus ? Theme.accent : Theme.lineBright }
            onTextEdited: live.rec.url = text
            onAccepted: if (live.idle && text.trim()) live.rec.start(text)
        }
        PrimaryButton {
            objectName: "live:record"
            visible: live.idle
            Layout.fillWidth: true
            iconName: "fiber_manual_record"
            text: "Grabar"
            enabled: urlField.text.trim().length > 0
            tip: "Busca el directo con yt-dlp y lo graba con FFmpeg desde ahora"
            onClicked: live.rec.start(urlField.text)
        }

        // Buscando el directo.
        RowLayout {
            visible: live.rec.status === "resolving"
            Layout.fillWidth: true
            BusyIndicator { implicitWidth: 22; implicitHeight: 22; running: parent.visible }
            Label { Layout.fillWidth: true; text: "Buscando el directo…"; color: Theme.text; font.pixelSize: 12 }
            SmallButton { objectName: "live:cancelSearch"; text: "Dejarlo"; onClicked: live.rec.stop() }
        }

        // Grabando (o guardando): de quién es, cuánto lleva y las marcas.
        ColumnLayout {
            visible: live.rec.status === "recording" || live.rec.status === "saving"
            Layout.fillWidth: true
            spacing: 6
            RowLayout {
                Layout.fillWidth: true
                spacing: 6
                Rectangle {
                    width: 10; height: 10; radius: 5
                    color: live.rec.status === "recording" ? Theme.danger : Theme.muted
                    SequentialAnimation on opacity {
                        running: live.rec.status === "recording"
                        loops: Animation.Infinite
                        NumberAnimation { to: 0.25; duration: 600 }
                        NumberAnimation { to: 1; duration: 600 }
                    }
                }
                Label {
                    objectName: "live:status"
                    Layout.fillWidth: true
                    text: live.rec.status === "saving" ? "Guardando la grabación…"
                        : "Grabando · " + live.fmt(live.rec.seconds) + " · " + live.size(live.rec.bytes)
                    color: Theme.text
                    font.pixelSize: 12
                    font.bold: true
                }
            }
            Label {
                Layout.fillWidth: true
                visible: !!live.rec.info.title || !!live.rec.info.uploader
                elide: Text.ElideRight
                text: live.rec.info.platform + (live.rec.info.uploader ? " · " + live.rec.info.uploader : "")
                      + (live.rec.info.title ? " · " + live.rec.info.title : "")
                color: Theme.muted
                font.pixelSize: 11
            }
            PrimaryButton {
                objectName: "live:mark"
                Layout.fillWidth: true
                iconName: "flag"
                text: "Marcar momento"
                enabled: live.rec.status === "recording"
                tip: "Ctrl+M · guarda " + live.rec.before + " s antes y " + live.rec.after + " s después"
                onClicked: live.rec.mark()
            }
            Repeater {
                model: live.rec.marks
                RowLayout {
                    required property var modelData
                    required property int index
                    objectName: "live:markRow:" + index
                    Layout.fillWidth: true
                    MIcon { name: "flag"; size: 14; color: Theme.accent }
                    Label { Layout.fillWidth: true; text: "Marca en " + parent.modelData.label; color: Theme.text; font.pixelSize: 12 }
                    IconBtn {
                        objectName: "live:removeMark:" + parent.index
                        iconName: "close"
                        iconSize: 14
                        tip: "Quitar esta marca"
                        onClicked: live.rec.removeMark(parent.index)
                    }
                }
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: 6
                SmallButton {
                    objectName: "live:stop"
                    text: "Parar y guardar"
                    enabled: live.rec.status === "recording"
                    onClicked: live.rec.stop()
                }
                SmallButton {
                    objectName: "live:discard"
                    text: "Descartar"
                    enabled: live.rec.status === "recording"
                    onClicked: live.rec.cancel()
                }
            }
        }

        // Cuánto se guarda de cada marca (en un panel estrecho, en dos líneas).
        Flow {
            Layout.fillWidth: true
            spacing: 6
            Label { height: 26; verticalAlignment: Text.AlignVCenter; text: "Cada marca guarda"; color: Theme.muted; font.pixelSize: 11 }
            Seconds {
                objectName: "live:before"
                value: Math.round(live.rec.before)
                onValueModified: live.rec.before = value
            }
            Label { height: 26; verticalAlignment: Text.AlignVCenter; text: "s antes y"; color: Theme.muted; font.pixelSize: 11 }
            Seconds {
                objectName: "live:after"
                value: Math.round(live.rec.after)
                onValueModified: live.rec.after = value
            }
            Label { height: 26; verticalAlignment: Text.AlignVCenter; text: "s después"; color: Theme.muted; font.pixelSize: 11 }
        }

        // Al acabar: lo que entró en el material (o el yt-dlp elegido), o el error.
        Label {
            objectName: "live:message"
            Layout.fillWidth: true
            visible: live.idle && !!live.rec.message
            wrapMode: Text.WordWrap
            text: live.rec.message
            color: Theme.success
            font.pixelSize: 12
        }
        Label {
            objectName: "live:error"
            Layout.fillWidth: true
            visible: live.rec.status === "error" && !!live.rec.error
            wrapMode: Text.WordWrap
            text: live.rec.error
            color: Theme.danger
            font.pixelSize: 12
        }

        // Con qué yt-dlp se buscan los directos (o que falta), y elegir otro.
        RowLayout {
            Layout.fillWidth: true
            visible: live.idle
            spacing: 6
            MIcon { name: "build"; size: 14; color: live.rec.ytdlpPath ? Theme.muted : Theme.warning }
            Label {
                id: ytdlpLabel
                objectName: "live:ytdlp"
                Layout.fillWidth: true
                elide: Text.ElideMiddle
                text: "yt-dlp: " + (live.rec.ytdlpPath || "no lo encuentro")
                color: live.rec.ytdlpPath ? Theme.muted : Theme.warning
                font.pixelSize: 11
                HoverHandler { id: ytdlpHover }
                ToolTip.visible: ytdlpHover.hovered && ytdlpLabel.truncated
                ToolTip.text: live.rec.ytdlpPath
            }
            SmallButton {
                objectName: "live:chooseYtdlp"
                text: live.rec.ytdlpPath ? "Cambiar…" : "Elegir…"
                onClicked: ytdlpDialog.open()
            }
        }
        Label {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            text: "Se graba desde que pulsas «Grabar» (lo anterior del directo no) y solo mientras está en marcha. Graba tus directos o los que tengas permiso para usar."
            color: Theme.muted
            font.pixelSize: 10
        }
    }
}
