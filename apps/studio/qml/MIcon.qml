import QtQuick

// <Icon name size /> de video-yt: un icono de Material Icons por su nombre (la ligadura de la fuente).
Text {
    property string name
    property int size: 18
    text: name
    font.family: Theme.iconFamily
    font.pixelSize: size
    color: Theme.muted
    horizontalAlignment: Text.AlignHCenter
    verticalAlignment: Text.AlignVCenter
}
