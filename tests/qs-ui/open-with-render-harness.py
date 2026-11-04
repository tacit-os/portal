#!/usr/bin/env python3
"""Writes ~/.cache/tacit-dialogs/render/end4-pC/harness-open.qml: the v2 mock's sample desktop (Pinax over the wallpaper)
with the real OpenWithContent.qml on top. Scenes (env MOCK): open-pdf, open-none, open-always, open-center, open-many.
Run with open-with-render.sh SCENE OUT.png. Needs ~/.cache/tacit-mocks (mock-dialogs2.qml, wall.jpg) and the render tree."""
import os
src = open(os.path.expanduser("~/.cache/tacit-mocks/end4-pC/mock-dialogs2.qml")).read().split("\n")
head = "\n".join(src[:333])
head = head.replace('import Quickshell\n', 'import Quickshell\nimport qs.modules.ii.openWith\n', 1)
body = r'''
    readonly property var appsPdf: [
        { id: "tacit-viewer", name: "Viewer", icon: "document-viewer", tacit: true },
        { id: "browser", name: "Opsis", icon: "web-browser", tacit: true },
        { id: "code", name: "Graphis", icon: "accessories-text-editor", tacit: true },
        { id: "claude-app", name: "Praxis", icon: "utilities-terminal", tacit: true }]
    readonly property var appsNone: [
        { id: "browser", name: "Opsis", icon: "web-browser", tacit: false },
        { id: "code", name: "Graphis", icon: "accessories-text-editor", tacit: false },
        { id: "foot", name: "Terminal", icon: "utilities-terminal", tacit: false }]
    readonly property var appsMany: appsPdf.concat([1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18].map(i => ({ id: "x" + i, name: "Another application " + i, icon: "", tacit: false })))
    readonly property bool none: scene === "open-none"
    readonly property var req: ({
        file: none ? "backup.7z" : "q3-report.pdf", kind: none ? ".7z" : "PDF", apps: none ? appsNone : scene === "open-many" ? appsMany : appsPdf,
        selected: 0, asker: scene === "open-center" ? null : { x: 240, y: 156, w: 1440 * 0 + 1200, h: 740, monitor: 0 } })

    Item {
        id: r
        objectName: "root"
        anchors.fill: parent
        Image { anchors.fill: parent; source: "file://WALL"; fillMode: Image.PreserveAspectCrop }
        PinaxWin { x: 200; y: 130; width: 1200; height: 740; picked: true; count: 4; u: 1.2 }
        OpenWithContent {
            id: ow
            objectName: "ow"
            anchors.fill: parent
            request: win.req
            always: win.scene === "open-always"
            index: win.scene === "open-none" ? 0 : win.scene === "open-always" ? 2 : 0
        }
    }
    Timer {
        interval: 2500; running: true
        onTriggered: r.grabToImage(res => { res.saveToFile(win.outPath); Qt.quit(); })
    }
}
'''
body = body.replace("file://WALL", "file://" + os.path.expanduser("~/.cache/tacit-mocks/wall.jpg"))
open(os.path.expanduser("~/.cache/tacit-dialogs/render/end4-pC/harness-open.qml"), "w").write(head + body)
