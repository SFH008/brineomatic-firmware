// Original Brineomatic plugin scaffold, targeting GUI-v2 1.3.22 / Venus OS 3.81.
import QtQuick
import Victron.VenusOS

Page {
    id: root
    title: "Brineomatic"
    readonly property string serviceUid: BackendConnection.serviceUidForType("brineomatic")
    readonly property bool online: connected.valid && connected.value === 1
    readonly property bool canWrite: online && commands.valid && commands.value === 1
                                    && Global.systemSettings.canAccess(VenusOS.User_AccessType_User)
    readonly property var commandFeedback: parseFeedback(result.value)
    readonly property bool commandPending: commandFeedback.status === "pending"
    readonly property bool activeOperation: ["RUNNING", "FLUSHING", "PICKLING", "DEPICKLING"].indexOf(status.value) >= 0
    readonly property bool stopping: status.value === "STOPPING"
    readonly property bool canStart: canWrite && !commandPending && status.value === "IDLE"

    function parseFeedback(value) {
        try { return JSON.parse(value) || {} } catch (error) { return {} }
    }

    function measurement(item, decimals, unit) {
        return root.online && item.valid ? Number(item.value).toFixed(decimals) + " " + unit : "—"
    }

    function requestOperation(operation, target) {
        if (!root.canWrite || root.commandPending
                || (operation === "start" && !root.canStart)
                || (operation === "stop" && !root.activeOperation)
                || (operation === "flush" && status.value !== "IDLE" && status.value !== "PICKLED")) {
            return
        }
        const payload = {
            id: Date.now().toString() + "-" + Math.random().toString(36).slice(2),
            operation: operation,
            expires_at: Date.now() / 1000 + 10
        }
        if (target && target.duration_ms !== undefined) payload.duration_ms = target.duration_ms
        if (target && target.volume_l !== undefined) payload.volume_l = target.volume_l
        request.setValue(JSON.stringify(payload))
    }

    VeQuickItem { id: connected; uid: root.serviceUid + "/Connected" }
    VeQuickItem { id: commands; uid: root.serviceUid + "/Commands/Enabled" }
    VeQuickItem { id: status; uid: root.serviceUid + "/Status" }
    VeQuickItem { id: simulation; uid: root.serviceUid + "/Simulation" }
    VeQuickItem { id: request; uid: root.serviceUid + "/Commands/Request" }
    VeQuickItem { id: result; uid: root.serviceUid + "/Commands/Result" }
    VeQuickItem { id: flow; uid: root.serviceUid + "/GenericInput/product_flowrate/Value" }
    VeQuickItem { id: salinity; uid: root.serviceUid + "/GenericInput/product_salinity/Value" }
    VeQuickItem { id: pressure; uid: root.serviceUid + "/GenericInput/membrane_pressure/Value" }
    VeQuickItem { id: volume; uid: root.serviceUid + "/GenericInput/volume/Value" }

    Component {
        id: startDialogComponent
        BrineomaticStartDialog {
            allowed: root.canStart
            onStartRequested: target => root.requestOperation("start", target)
        }
    }

    GradientListView {
        model: VisibleItemModel {
            ListText {
                text: "Connection"
                secondaryText: root.online ? "Connected" : "Disconnected"
                caption: simulation.value === 1 ? "SIMULATION — no hardware connected · Controls UI 0.2.0" : "Controls UI 0.2.0"
            }
            ListText {
                text: "Operation"
                secondaryText: root.online ? status.value : "Unavailable"
            }
            ListButton {
                text: root.activeOperation || root.stopping ? "Stop watermaker" : "Start production"
                secondaryText: root.stopping ? "Stopping…" : root.activeOperation ? "Stop" : "Start…"
                caption: root.activeOperation ? "Stop the current operation using the normal shutdown sequence."
                         : root.stopping ? "Waiting for the shutdown sequence to finish."
                         : "Continuous production, duration or target litres."
                buttonBackgroundColor: root.activeOperation || root.stopping ? Theme.color_dimRed : Theme.color_dimGreen
                enabled: root.canWrite && !root.commandPending && (root.activeOperation || root.canStart)
                onClicked: {
                    if (root.activeOperation) root.requestOperation("stop")
                    else Global.dialogLayer.open(startDialogComponent)
                }
            }
            ListButton {
                text: "Freshwater flush"
                secondaryText: "Flush"
                caption: "Run the configured automatic flush cycle."
                preferredVisible: !root.activeOperation && !root.stopping
                enabled: root.canWrite && !root.commandPending && (status.value === "IDLE" || status.value === "PICKLED")
                onClicked: root.requestOperation("flush")
            }
            ListItem {
                contentItem: MetricsPanel {
                    readings: [root.measurement(flow, 1, "L/h"),
                               root.measurement(salinity, 0, "ppm"),
                               root.measurement(pressure, 1, "bar"),
                               root.measurement(volume, 1, "L")]
                }
            }
            ListText {
                text: "Command result"
                secondaryText: ({pending: "Sending…", accepted: "Acknowledged", unconfirmed: "Not confirmed", cancelled: "Cancelled"})[root.commandFeedback.status] || "Ready"
                caption: "Operation status confirms progress."
            }
            ListText {
                text: "Connection diagnostic"
                dataItem.uid: root.serviceUid + "/Connection/Error"
            }
        }
    }
}
