import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// «Cargar audio» (AudioTab.jsx y TtsControls.jsx): el narrador y extraer el audio de un enlace. Los hace el
// backend de video-yt (narrator.hpp).
Flickable {
    id: panel
    required property var model
    readonly property var narrator: model.narrator
    property string mode: "narrator"
    readonly property var form: narrator.form
    readonly property var engine: narrator.current
    readonly property var voices: engine.voices || []
    readonly property bool available: engine.available === undefined || engine.available === null ? true : engine.available
    readonly property bool isKokoro: form.engine === "kokoro"
    readonly property bool isGemini: form.engine === "gemini"
    readonly property bool isAzure: form.engine === "azure"
    readonly property string link: (form.url || "").trim()
    readonly property bool alreadyExtracted: !!narrator.extracted && narrator.extracted === link
    readonly property int percent: Math.max(4, Math.min(100, Math.round((narrator.job.progress || 0) * 100)))

    // voiceOptionLabel de TtsControls.jsx.
    function voiceLabel(v) {
        const g = v.gender === "male" ? "hombre" : v.gender === "female" ? "mujer" : ""
        if (!g || (v.label || "").includes(g))
            return v.label
        return v.label + " — " + g
    }
    function voiceOptions(list) { return list.map((v) => ({ id: v.id, text: panel.voiceLabel(v), enabled: true })) }
    function engineOptions() {
        // Los de nube se pueden elegir sin credencial: así se ve el hueco para pegarla.
        return narrator.engines.map((e) => {
            const cloud = e.id === "gemini" || e.needs_key
            return { id: e.id, text: e.label + (e.available || cloud ? "" : " — no instalado"), enabled: e.available || !!cloud }
        })
    }

    onVisibleChanged: if (visible) narrator.load()
    Component.onCompleted: if (visible) narrator.load()

    clip: true
    contentHeight: column.implicitHeight
    boundsBehavior: Flickable.StopAtBounds

    ScrollBar.vertical: ScrollBar { policy: panel.contentHeight > panel.height ? ScrollBar.AlwaysOn : ScrollBar.AlwaysOff }

    component Title: Label { color: Theme.muted; font.pixelSize: 11 }
    // <details> de TtsControls y AudioTab: plegado, con su flecha.
    component Fold: SmallButton {
        required property string title
        Layout.fillWidth: true
        checkable: true
        text: (checked ? "▾ " : "▸ ") + title
    }
    component Field: TextField {
        Layout.fillWidth: true
        color: Theme.text
        placeholderTextColor: Theme.muted
        font.pixelSize: 12
        background: Rectangle { radius: Theme.radiusSm; color: Theme.card2; border.color: Theme.lineBright }
    }
    component Pick: ComboBox {
        id: pick
        property var options: []
        property string current
        signal picked(string value)
        Layout.fillWidth: true
        font.pixelSize: 12
        model: options
        textRole: "text"
        currentIndex: Math.max(0, options.findIndex((o) => o.id === current))
        onActivated: (i) => picked(options[i].id)
        delegate: ItemDelegate {
            required property var modelData
            required property int index
            width: ListView.view ? ListView.view.width : 0
            enabled: modelData.enabled
            text: modelData.text
            font.pixelSize: 12
            highlighted: pick.highlightedIndex === index
        }
        contentItem: Label { leftPadding: 8; text: pick.displayText; font: pick.font; color: Theme.text; elide: Text.ElideRight; verticalAlignment: Text.AlignVCenter }
        background: Rectangle { radius: Theme.radiusSm; color: Theme.card2; border.color: Theme.lineBright }
    }
    component Range: ColumnLayout {
        id: range
        required property string title
        required property string field
        required property real from
        required property real to
        spacing: 2
        Layout.fillWidth: true
        Title { text: range.title }
        Slider {
            objectName: "narrator:" + range.field
            Layout.fillWidth: true
            from: range.from
            to: range.to
            stepSize: 0.05
            value: panel.form[range.field]
            onMoved: panel.narrator.set(range.field, value)
        }
    }

    ColumnLayout {
        id: column
        width: panel.width - 12 // el hueco de la barra
        spacing: 8

        RowLayout {
            spacing: 4
            Repeater {
                model: [["narrator", "Narrador"], ["youtube", "Link video"]]
                SmallButton {
                    required property var modelData
                    objectName: "narrator:mode:" + modelData[0]
                    text: modelData[1]
                    highlighted: panel.mode === modelData[0]
                    onClicked: panel.mode = modelData[0]
                }
            }
        }

        // Narrador
        ColumnLayout {
            visible: panel.mode === "narrator"
            Layout.fillWidth: true
            spacing: 8

            SmallButton {
                objectName: "narrator:generate"
                Layout.fillWidth: true
                highlighted: true
                enabled: !panel.narrator.busy && panel.available
                text: panel.narrator.busy && panel.narrator.kind === "tts" ? "Generando…" : "Generar audio"
                onClicked: panel.narrator.generate()
            }
            Title { text: "Motor" }
            Pick {
                objectName: "narrator:engine"
                options: panel.engineOptions()
                current: panel.form.engine
                onPicked: (value) => panel.narrator.set("engine", value)
            }
            Title { text: "Voz" }
            Pick {
                objectName: "narrator:voice"
                options: panel.voiceOptions(panel.voices)
                current: panel.form.voice
                onPicked: (value) => panel.narrator.set("voice", value)
            }
            Title { visible: panel.isGemini; text: "Estilo" }
            Pick {
                objectName: "narrator:style"
                visible: panel.isGemini
                options: ((panel.engine.styles && panel.engine.styles.length) ? panel.engine.styles
                          : [{ id: "documentary", label: "Documental" }, { id: "close", label: "Cercano" }])
                         .map((s) => ({ id: s.id, text: s.label, enabled: true }))
                current: panel.form.style
                onPicked: (value) => panel.narrator.set("style", value)
            }
            Fold {
                id: more
                objectName: "narrator:more"
                title: "Ajustes"
            }
            ColumnLayout {
                visible: more.checked
                Layout.fillWidth: true
                spacing: 6
                Title { visible: panel.isKokoro; text: "Mezclar con (voz 2)" }
                Pick {
                    objectName: "narrator:voice2"
                    visible: panel.isKokoro
                    options: [{ id: "", text: "— sin mezcla —", enabled: true }]
                             .concat(panel.voiceOptions(panel.voices.filter((v) => v.id !== panel.form.voice)))
                    current: panel.form.voice2
                    onPicked: (value) => panel.narrator.set("voice2", value)
                }
                Range {
                    visible: panel.isKokoro && !!panel.form.voice2
                    title: "Mezcla: " + Math.round(panel.form.blend * 100) + "% / " + Math.round((1 - panel.form.blend) * 100) + "%"
                    field: "blend"
                    from: 0
                    to: 1
                }
                Range {
                    visible: !panel.isGemini
                    title: "Velocidad: " + Number(panel.form.speed).toFixed(2) + "×"
                    field: "speed"
                    from: 0.5
                    to: 1.5
                }
                Range {
                    visible: !panel.isGemini
                    title: "Pausa entre frases: " + Number(panel.form.pause).toFixed(2) + "s"
                    field: "pause"
                    from: 0
                    to: 1
                }
                Title { text: "Nombre (opcional)" }
                Field {
                    objectName: "narrator:name"
                    placeholderText: "intro"
                    text: panel.form.name
                    onTextEdited: panel.narrator.set("name", text)
                }
            }
            Title { text: "Guion" }
            TextArea {
                objectName: "narrator:text"
                Layout.fillWidth: true
                Layout.minimumHeight: 96
                wrapMode: TextEdit.Wrap
                color: Theme.text
                placeholderTextColor: Theme.muted
                font.pixelSize: 12
                placeholderText: "Pega o escribe aquí el guion…"
                text: panel.form.text
                onTextChanged: if (text !== panel.form.text) panel.narrator.set("text", text)
                background: Rectangle { radius: Theme.radiusSm; color: Theme.card2; border.color: Theme.lineBright }
            }

            Fold {
                id: promptBox
                objectName: "narrator:promptBox"
                title: "Prompt para el guion"
            }
            ColumnLayout {
                visible: promptBox.checked
                Layout.fillWidth: true
                spacing: 6
                Title { text: "Tema" }
                Field {
                    id: topic
                    objectName: "narrator:topic"
                    placeholderText: "ej. cómo el silicio de la arena termina en una GPU"
                }
                SmallButton {
                    id: copyPrompt
                    objectName: "narrator:copyPrompt"
                    property bool copied: false
                    text: copied ? "✓ Copiado" : "Copiar prompt"
                    onClicked: {
                        panel.model.copyScriptPrompt(topic.text)
                        copied = true
                        copiedTimer.restart()
                    }
                    Timer { id: copiedTimer; interval: 2000; onTriggered: copyPrompt.copied = false }
                }
            }

            // Lo que le falta al motor: la clave de los de nube o, en los locales, instalarlo.
            ColumnLayout {
                visible: panel.isGemini && !panel.available
                Layout.fillWidth: true
                spacing: 6
                Label { Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.muted; font.pixelSize: 11; text: panel.engine.reason || "Gemini necesita una API key." }
                Field {
                    objectName: "narrator:geminiKey"
                    echoMode: TextInput.Password
                    placeholderText: "AIza…"
                    text: panel.form.geminiKey
                    onTextEdited: panel.narrator.set("geminiKey", text)
                    onAccepted: panel.narrator.saveGeminiKey()
                }
                SmallButton {
                    objectName: "narrator:saveGemini"
                    enabled: panel.form.geminiKey.trim().length > 0
                    text: "Guardar key"
                    onClicked: panel.narrator.saveGeminiKey()
                }
            }
            ColumnLayout {
                visible: panel.isAzure && !panel.available
                Layout.fillWidth: true
                spacing: 6
                Label { Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.muted; font.pixelSize: 11; text: panel.engine.reason || "Azure necesita clave y región." }
                Field {
                    objectName: "narrator:azureRegion"
                    placeholderText: "Región (ej. eastus)"
                    text: panel.form.azureRegion
                    onTextEdited: panel.narrator.set("azureRegion", text)
                }
                Field {
                    objectName: "narrator:azureKey"
                    echoMode: TextInput.Password
                    placeholderText: "Clave de Azure Speech"
                    text: panel.form.azureKey
                    onTextEdited: panel.narrator.set("azureKey", text)
                    onAccepted: panel.narrator.saveAzure()
                }
                SmallButton {
                    objectName: "narrator:saveAzure"
                    enabled: panel.form.azureKey.trim().length > 0 && panel.form.azureRegion.trim().length > 0
                    text: "Guardar"
                    onClicked: panel.narrator.saveAzure()
                }
            }
            Label {
                objectName: "narrator:missing"
                visible: !panel.available && !panel.isGemini && !panel.isAzure
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                color: Theme.danger
                font.pixelSize: 11
                text: "El motor " + (panel.engine.label || panel.form.engine) + " no está instalado. "
                      + (panel.form.engine === "piper" ? "Ejecuta python get_piper.py en la carpeta backend/ y reinicia el backend."
                                                       : "Faltan los modelos en backend/models.")
            }
        }

        // Link video
        ColumnLayout {
            visible: panel.mode === "youtube"
            Layout.fillWidth: true
            spacing: 8

            SmallButton {
                objectName: "narrator:extract"
                Layout.fillWidth: true
                highlighted: true
                enabled: !panel.narrator.busy && panel.link.length > 0 && !panel.alreadyExtracted
                text: panel.narrator.busy && panel.narrator.kind === "youtube" ? "Extrayendo…"
                      : panel.alreadyExtracted ? "Audio extraído" : "Extraer audio"
                onClicked: panel.narrator.extractYoutube()
            }
            Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                color: Theme.muted
                font.pixelSize: 11
                text: "Extrae el audio del vídeo. Queda en este proyecto; para reutilizarlo en otros, Guardar en la card."
            }
            Title { text: "Enlace de YouTube" }
            Field {
                objectName: "narrator:url"
                enabled: !panel.narrator.busy
                placeholderText: "https://www.youtube.com/watch?v=…"
                text: panel.form.url
                onTextEdited: panel.narrator.set("url", text)
                onAccepted: if (!panel.narrator.busy && !panel.alreadyExtracted) panel.narrator.extractYoutube()
            }
        }

        Label {
            objectName: "narrator:error"
            visible: !!panel.narrator.error
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            color: Theme.danger
            font.pixelSize: 12
            text: panel.narrator.error
        }
        // JobStatusBar
        ColumnLayout {
            objectName: "narrator:status"
            visible: panel.narrator.busy
            Layout.fillWidth: true
            spacing: 4
            Rectangle {
                Layout.fillWidth: true
                height: 4
                radius: 2
                color: Theme.card2
                Rectangle { width: parent.width * panel.percent / 100; height: parent.height; radius: 2; color: Theme.accent }
            }
            RowLayout {
                Layout.fillWidth: true
                Label {
                    objectName: "narrator:message"
                    Layout.fillWidth: true
                    elide: Text.ElideRight
                    color: Theme.textSecondary
                    font.pixelSize: 11
                    text: panel.narrator.job.message || (panel.mode === "youtube" ? "Extrayendo audio…" : "Generando voz…")
                }
                Label {
                    color: Theme.muted
                    font.pixelSize: 11
                    text: (panel.narrator.elapsed > 0 ? panel.narrator.elapsed + "s" : "") + " · " + panel.percent + "%"
                }
            }
        }
        Label {
            objectName: "narrator:jobError"
            visible: panel.narrator.job.status === "error"
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            color: Theme.danger
            font.pixelSize: 12
            text: panel.narrator.job.error || ""
        }
    }
}
