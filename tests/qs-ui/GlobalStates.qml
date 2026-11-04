pragma Singleton
import QtQuick
import Quickshell
// Test stand-in: locked for the first 4 s so the test can check that a dialog waits.
Singleton {
    property bool screenLocked: true
    property Timer t: Timer { running: true; interval: 4000; onTriggered: GlobalStates.screenLocked = false }
}
