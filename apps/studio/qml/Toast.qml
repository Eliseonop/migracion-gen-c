import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// Aviso flotante (components/Toast.jsx): éxito, error o información, con su cierre; se va solo a
// los 4 s. `notice` es {type, message} (vacío: no hay aviso).
Rectangle {
    id: toast
    property var notice: ({})
    signal closed

    readonly property bool shown: !!notice && !!notice.message
    readonly property color tone: notice.type === "error" ? Theme.danger
                                : notice.type === "success" ? "#22c55e" : Theme.accent2
    objectName: "toast"
    visible: shown
    width: Math.min(row.implicitWidth + 24, parent ? parent.width - 40 : 480)
    height: row.implicitHeight + 16
    radius: Theme.radiusMd
    color: Theme.card3
    border.color: tone
    border.width: 1

    onNoticeChanged: if (shown) timer.restart()
    Timer { id: timer; interval: 4000; onTriggered: toast.closed() }

    RowLayout {
        id: row
        anchors.centerIn: parent
        width: parent.width - 24
        spacing: 8
        Rectangle { implicitWidth: 8; implicitHeight: 8; radius: 4; color: toast.tone }
        Label {
            objectName: "toast:message"
            Layout.fillWidth: true
            text: toast.notice.message || ""
            color: Theme.text
            wrapMode: Text.WordWrap
        }
        Label {
            text: "✕"
            color: Theme.muted
            TapHandler { onTapped: toast.closed() }
        }
    }
}
