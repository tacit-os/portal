import QtQuick
import Quickshell
import Quickshell.Io
import qs
import qs.services
import qs.modules.ii.keyring

ShellRoot {
    Keyring { id: k }
    Connections {
        target: k
        function onUpChanged() { console.log("E2E up=" + k.up) }
        function onAskingChanged() { console.log("E2E asking=" + k.asking + " kind=" + (k.ask?.kind ?? "-") + " retry=" + (k.ask?.params.retry ?? "-")) }
    }
    IpcHandler {
        target: "e2e"
        function password(pw: string): void { k.answer({ reply: "continue", password: pw, choice: false }) }
        function confirm(): void { k.answer({ reply: "continue", choice: false }) }
        function refuse(): void { k.refuse("refused") }
        function lock(on: bool): void { GlobalStates.screenLocked = on }
    }
}
