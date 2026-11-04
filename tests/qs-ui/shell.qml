import QtQuick
import Quickshell
import qs
// Test shell: logs what the singleton does and answers like a dialog would.
ShellRoot {
    // a sheet claims its kinds; "test.unclaimed" is deliberately left out
    Component.onCompleted: ["test.echo", "test.no", "test.hold"].forEach(k => DialogBridge.claim(k))
    Connections { target: DialogBridge
        function onConnectedChanged() { console.log("T connected=" + DialogBridge.connected) }
        function onAskingChanged() {
            const c = DialogBridge.current;
            console.log("T asking=" + DialogBridge.asking + " kind=" + (c ? c.kind : "-"));
            if (!DialogBridge.asking) return;
            const id = c.id, kind = c.kind, x = c.params.x;   // a click answers later, never inside the change
            Qt.callLater(() => {
                if (kind === "test.echo") DialogBridge.answer(id, { v: x });
                else if (kind === "test.no") DialogBridge.refuse(id, "refused");
            });
        }
        function onCurrentChanged() { if (DialogBridge.current) console.log("T current=" + DialogBridge.current.kind) }
    }
}
