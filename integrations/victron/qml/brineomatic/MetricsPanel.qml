// Original Brineomatic telemetry layout. No hardware controls in this component.
pragma ComponentBehavior: Bound
import QtQuick

Item {
    id: root
    property var readings: ["—", "—", "—", "—"]
    property var labels: ["Product flow", "Product salinity", "Membrane pressure", "Produced this run"]
    property color cardColor: "#183347"
    property color textColor: "#ffffff"
    implicitHeight: 180
    implicitWidth: 440

    Grid {
        anchors.fill: parent
        columns: 2
        spacing: 8
        Repeater {
            model: 4
            Rectangle {
                required property int index
                width: (root.width - 8) / 2
                height: (root.height - 8) / 2
                radius: 8
                color: root.cardColor
                Text {
                    anchors { left: parent.left; right: parent.right; top: parent.top; margins: 12 }
                    text: root.labels[parent.index]
                    color: root.textColor
                    opacity: 0.75
                    font.pixelSize: 14
                    elide: Text.ElideRight
                    textFormat: Text.PlainText
                }
                Text {
                    anchors { left: parent.left; right: parent.right; bottom: parent.bottom; margins: 12 }
                    text: root.readings[parent.index]
                    color: root.textColor
                    font.pixelSize: 26
                    elide: Text.ElideRight
                    textFormat: Text.PlainText
                }
            }
        }
    }
}
