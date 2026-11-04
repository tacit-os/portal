import re,os
src=open(os.path.expanduser("~/.cache/tacit-mocks/end4-pC/mock-dialogs2.qml")).read().split("\n")
head="\n".join(src[:333])   # through Desk and the colour tokens, before Thumb
head=head.replace('import Quickshell\n','import Quickshell\nimport QtTest\nimport qs.modules.ii.sharePicker\n',1)
body=r'''
    readonly property bool wantRegion: scene === "share-region" || scene === "share-region-empty"
    readonly property var rowsMany: {
        const l = [];
        const names = [["foot", "~/work"], ["opsis", "Inbox"], ["pinax", "Documents"], ["huddle", "Standup"], ["foot", "~/dev/tacit-portal"], ["opsis", "Pull request #412"], ["pinax", "Downloads"], ["foot", "journalctl"], ["opsis", "Docs"], ["pinax", "Pictures"]];
        names.forEach((n, i) => l.push({ id: String(10 + i), class: n[0], title: n[1], address: "" }));
        return l;
    }
    readonly property var reqBase: ({
        monitor: "eDP-1", screen: { name: "eDP-1", x: 0, y: 0, w: 1600, h: 1000 }, screens: [{ name: "eDP-1", w: 1600, h: 1000 }],
        windows: [{ id: "11", class: "opsis", title: "Inbox (3) - Fwd: Q3 offsite_v2 FINAL.pdf", address: "" }, { id: "12", class: "foot", title: "", address: "" }, { id: "13", class: "org.gnome.Nautilus", title: "Documents", address: "" }, { id: "14", class: "pinax", title: "", address: "" }],
        asker: { x: 802, y: 12, w: 786, h: 620 }, allowToken: true, app: "Huddle" })
    readonly property var reqTwo: Object.assign({}, reqBase, { screens: [{ name: "eDP-1", w: 1600, h: 1000 }, { name: "DP-2", w: 2560, h: 1440 }] })
    readonly property var req: scene === "share-multi" ? reqTwo : scene === "share-many" ? Object.assign({}, reqBase, { windows: rowsMany })
        : scene === "share-notoken" ? Object.assign({}, reqBase, { asker: null, allowToken: false, app: "" }) : reqBase

    Item {
        id: r
        objectName: "root"
        anchors.fill: parent
        Desk { anchors.fill: parent }
        SharePickerContent {
            id: sp
            objectName: "sp"
            anchors.fill: parent
            request: win.req
            entered: true
            startStep: win.wantRegion ? "region" : "list"
            index: win.scene === "share" ? 2 : 0
            region: win.scene === "share-region" ? Qt.rect(330, 250, 760, 470) : Qt.rect(0, 0, 0, 0)
            thumbOverride: Component {
                Item {
                    property var row: ({})
                    readonly property string c: row.kind === "screen" ? "screen" : row.cls ?? ""
                    anchors.fill: parent
                    clip: true
                    Loader {
                        active: parent.c !== "screen"
                        sourceComponent: parent.c === "opsis" ? browser : parent.c === "foot" ? term : pinax
                        readonly property var w: parent.c === "foot" ? [780, 346] : parent.c === "opsis" ? [780, 620] : [786, 526]
                        width: w[0]; height: w[1]
                        scale: Math.min(112 / w[0], 68 / w[1]); transformOrigin: Item.TopLeft
                        x: (112 - w[0] * scale) / 2; y: (68 - w[1] * scale) / 2
                    }
                    Desk { visible: parent.c === "screen"; anchors.fill: parent }
                }
            }
        }
    }
    Timer {
        interval: 2500; running: scene !== "interact"
        onTriggered: r.grabToImage(res => { res.saveToFile(win.outPath); Qt.quit(); })
    }
}
'''
open("end4-pC/harness-share.qml","w").write(head+body)
