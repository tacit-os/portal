import QtQuick
import Quickshell
import Quickshell.Io
import qs
import qs.services
import qs.modules.ii.sharePicker

ShellRoot {
    SharePicker { id: sp }
    Connections {
        target: DialogBridge
        function onConnectedChanged() { console.log("E2E connected=" + DialogBridge.connected) }
    }
    Connections {
        target: sp
        function onUpChanged() { console.log("E2E up=" + sp.up + " monitor=" + (sp.ask?.params.monitor ?? "-") + " windows=" + (sp.ask?.params.windows.length ?? 0)) }
    }
    IpcHandler {
        target: "e2e"
        function answer(): void { sp.finish({ kind: "window", id: "34", token: true }) }
        function dismiss(): void { sp.finish(null) }
    }
}
