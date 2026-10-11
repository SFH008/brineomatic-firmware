// Original Brineomatic controls using the stock Venus modal and input components.
import QtQuick
import Victron.VenusOS

ModalDialog {
    id: root
    property bool allowed: false
    property string runMode: "continuous"
    signal startRequested(var target)

    title: "Brineomatic"
    secondaryTitle: "Start production"
    acceptText: "Start now"
    canAccept: allowed
    tryAccept: function() { return root.allowed }
    onAccepted: startRequested(runMode === "duration" ? {duration_ms: minutes.value * 60000}
                             : runMode === "volume" ? {volume_l: litres.value} : {})

    contentItem: ModalDialog.FocusableContentItem {
        implicitHeight: contentColumn.implicitHeight
        Column {
            id: contentColumn
            x: Theme.geometry_modalDialog_content_horizontalMargin
            width: parent.width - 2 * x
            spacing: Theme.geometry_modalDialog_content_spacing

            Row {
                width: parent.width
                spacing: 8
                Button {
                    width: (parent.width - 16) / 3
                    text: "Continuous"
                    checked: root.runMode === "continuous"
                    backgroundColor: checked ? Theme.color_dimGreen : Theme.color_background_secondary
                    flat: false
                    focus: true
                    onClicked: root.runMode = "continuous"
                    KeyNavigation.right: durationButton
                }
                Button {
                    id: durationButton
                    width: (parent.width - 16) / 3
                    text: "Duration"
                    checked: root.runMode === "duration"
                    backgroundColor: checked ? Theme.color_dimGreen : Theme.color_background_secondary
                    flat: false
                    onClicked: root.runMode = "duration"
                    KeyNavigation.right: volumeButton
                }
                Button {
                    id: volumeButton
                    width: (parent.width - 16) / 3
                    text: "Litres"
                    checked: root.runMode === "volume"
                    backgroundColor: checked ? Theme.color_dimGreen : Theme.color_background_secondary
                    flat: false
                    onClicked: root.runMode = "volume"
                }
            }
            SpinBox {
                id: minutes
                anchors.horizontalCenter: parent.horizontalCenter
                visible: root.runMode === "duration"
                from: 1
                to: 1440
                value: 60
                secondaryText: "minutes"
                editable: true
                KeyNavigation.down: root.footer
            }
            SpinBox {
                id: litres
                anchors.horizontalCenter: parent.horizontalCenter
                visible: root.runMode === "volume"
                from: 1
                to: 100000
                value: 100
                secondaryText: "litres"
                editable: true
                KeyNavigation.down: root.footer
            }
            Label {
                width: parent.width
                wrapMode: Text.Wrap
                horizontalAlignment: Text.AlignHCenter
                font.pixelSize: Theme.font_dialog_body_size
                text: !root.allowed ? "Start is unavailable. Check connection and operation status."
                      : root.runMode === "duration" ? "Produce for " + minutes.value + " minutes."
                      : root.runMode === "volume" ? "Produce " + litres.value + " litres."
                      : "Run until stopped or a firmware stop condition is reached."
            }
        }
    }
}
