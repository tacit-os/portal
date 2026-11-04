import QtQuick
import QtQuick.Controls
import QtTest
import Quickshell
import qs.modules.ii.sharePicker

ApplicationWindow {
    id: top
    visible: true
    width: 1600; height: 1000
    property var got: []
    SharePickerContent {
        id: sp
        width: 1600; height: 1000
        entered: true
        request: ({ monitor: "eDP-1", screen: { name: "eDP-1", x: 0, y: 0, w: 1600, h: 1000 }, screens: [{ name: "eDP-1", w: 1600, h: 1000 }],
            windows: [{ id: "11", class: "a", title: "A", address: "" }, { id: "12", class: "b", title: "B", address: "" }],
            asker: { x: 802, y: 12, w: 786, h: 620 }, allowToken: true, app: "Huddle" })
        onDone: r => top.got.push(JSON.stringify(r))
        onCancelled: top.got.push("cancelled")
    }
    TestCase {
        name: "picker"; when: true
        function test_all() {
            const ok = (n, c) => console.log((c ? "ok   " : "FAIL ") + n);
            wait(500); sp.forceActiveFocus();
            keyClick(Qt.Key_Down); keyClick(Qt.Key_Down); keyClick(Qt.Key_Down);   // clamps at the last row
            ok("down clamps", sp.index === 2);
            keyClick(Qt.Key_Up); ok("up", sp.index === 1);
            keyClick(Qt.Key_Return);
            ok("enter shares the window", top.got.pop() === '{"kind":"window","id":"11","token":false}');
            sp.index = 0; sp.token = true; keyClick(Qt.Key_Return);
            ok("screen + token", top.got.pop() === '{"kind":"screen","name":"eDP-1","token":true}');
            keyClick(Qt.Key_Escape); ok("escape cancels the list", top.got.pop() === "cancelled");
            sp.step = "region";
            keyClick(Qt.Key_Return); ok("share with no region does nothing", top.got.length === 0);
            mouseDrag(sp, 100, 100, 300, 200);   // press at 100,100, move by +300,+200
            ok("drag draws 300x200 at 100,100", sp.region.x === 100 && sp.region.y === 100 && sp.region.width === 300 && sp.region.height === 200);
            mouseDrag(sp, 400, 300, 100, 50);    // grab the bottom-right corner (400,300), pull to 500,350
            ok("corner resize", sp.region.width === 400 && sp.region.height === 250 && sp.region.x === 100);
            mouseDrag(sp, 250, 200, 50, 20);     // inside: move
            ok("move", sp.region.x === 150 && sp.region.y === 120 && sp.region.width === 400);
            mouseDrag(sp, 150, 250, -80, 0);     // left edge out
            ok("left edge resize", sp.region.x === 70 && sp.region.width === 480);
            mouseDrag(sp, 1000, 800, 900, 500);  // new region, dragged past the screen
            ok("clamped to screen", sp.region.x + sp.region.width <= 1600 && sp.region.y + sp.region.height <= 1000);
            mouseDrag(sp, 300, 300, -200, -150); // draw up-left
            ok("up-left drag normalised", sp.region.x === 100 && sp.region.y === 150 && sp.region.width === 200);
            keyClick(Qt.Key_Return);
            const r = JSON.parse(top.got.pop());
            ok("region result", r.kind === "region" && r.output === "eDP-1" && r.w === 200 && r.h === 150 && r.token === true);
            sp.step = "region"; keyClick(Qt.Key_Escape);
            ok("escape in region = back to the list", sp.step === "list" && top.got.length === 0);
            Qt.quit();
        }
    }
}
