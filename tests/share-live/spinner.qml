// a Qt Quick window that never stops moving: a shared window that froze shows the same frame over and over
import QtQuick
Window {
    width: 480; height: 320; visible: true; color: "#1d1a19"
    Rectangle {
        width: 80; height: 80; color: "#dfc0b2"; y: 120
        NumberAnimation on x { from: 0; to: 400; duration: 1000; loops: Animation.Infinite }
    }
}
