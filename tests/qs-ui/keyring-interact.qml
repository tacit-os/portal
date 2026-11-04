import QtQuick
import QtQuick.Controls
import QtTest
import Quickshell
import qs.modules.ii.keyring

// Keys and state of the keyring sheet, off-screen. Run with the sheet's tree: qs -p keyring-interact.qml
ApplicationWindow {
    id: top
    visible: true
    width: 1600; height: 1000
    property var got: []
    readonly property var base: ({ mode: "password", title: "Unlock Keyring", message: "Authentication required",
        description: "An application wants access to the keyring “Login”, but it is locked", warning: "", choiceLabel: "",
        choiceChosen: false, passwordNew: false, continueLabel: "Unlock", cancelLabel: "Cancel", retry: false })
    KeyringContent {
        id: kc
        width: 1600; height: 1000
        active: true
        entered: true
        ask: ({ id: "a1", kind: "keyring.password", params: top.base })
        onAnswered: r => top.got.push(JSON.stringify(r))
        onRefused: why => top.got.push("refused:" + why)
    }
    function setAsk(id, kind, over) { kc.ask = { id: id, kind: kind, params: Object.assign({}, base, over) }; }
    TestCase {
        name: "keyring"; when: true
        function typ(t) { for (let i = 0; i < t.length; i++) keyClick(t[i]); }
        function test_all() { try { run(); } catch (e) { console.log('FAIL exception ' + e + ' ' + e.stack); Qt.quit(); } }
        function run() {
            const ok = (n, c, d) => console.log((c ? "ok   " : "FAIL ") + n + (!c && d ? "  " + d : ""));
            wait(400); kc.focusEntry();
            ok("login keyring words", kc.headTitle === "Unlock your keyring" && kc.headLine.indexOf("login password") > 0, kc.headTitle + " / " + kc.headLine);
            keyClick(Qt.Key_Return); ok("empty password does nothing", top.got.length === 0);
            typ("abc"); keyClick(Qt.Key_Return);
            ok("enter answers with the password", top.got.pop() === '{"reply":"continue","choice":false,"password":"abc"}', top.got);
            keyClick(Qt.Key_Escape); ok("escape = cancelled", top.got.pop() === "refused:cancelled");

            // a wrong password: the same sheet asks again, typing is cleared, the error shows
            typ("zzz");
            top.setAsk("a2", "keyring.password", { warning: "The unlock password was incorrect", retry: true });
            wait(50);
            ok("retry clears the field", kc.password === "");
            ok("retry shows the error line", kc.headIsError && kc.headLine === "Wrong password. Try again.");
            typ("right"); keyClick(Qt.Key_Return);
            ok("retry answers", top.got.pop() === '{"reply":"continue","choice":false,"password":"right"}');

            // a long choice is a check, ticked state is sent
            top.setAsk("a3", "keyring.password", { choiceLabel: "Automatically unlock this keyring whenever I'm logged in" });
            wait(50);
            ok("long choice is a check, not a button", !kc.choiceIsButton);
            kc.choice = true; typ("pw"); keyClick(Qt.Key_Return);
            ok("choice is sent", top.got.pop() === '{"reply":"continue","choice":true,"password":"pw"}');
            top.setAsk("a3b", "keyring.password", { choiceLabel: "x", choiceChosen: true });
            wait(50); ok("choiceChosen from the daemon pre-ticks", kc.choice === true);

            // new keyring password: asked twice, must match
            top.setAsk("a4", "keyring.password", { passwordNew: true, message: "Choose password for new keyring", description: "An application wants to create a new keyring called “Default keyring”." });
            wait(50);
            ok("new: title is the daemon's message", kc.headTitle === "Choose a password" && kc.isNew);
            typ("abc"); keyClick(Qt.Key_Return);     // moves to the repeat field
            typ("abd"); keyClick(Qt.Key_Return);
            ok("new: mismatch blocks the answer", top.got.length === 0 && kc.mismatch);
            kc.repeat = ""; typ("abc"); keyClick(Qt.Key_Return);
            ok("new: match answers", top.got.pop() === '{"reply":"continue","choice":false,"password":"abc"}', top.got);
            top.setAsk("a4b", "keyring.password", { passwordNew: true });
            wait(50); keyClick(Qt.Key_Return); keyClick(Qt.Key_Return);
            ok("new: blank twice is allowed (the daemon then asks to confirm)", top.got.pop() === '{"reply":"continue","choice":false,"password":""}');

            // confirm: Enter only after the sheet has been up a moment
            top.setAsk("a5", "keyring.confirm", { mode: "confirm", message: "Store passwords unencrypted?", description: "x", continueLabel: "Continue", passwordNew: true });
            wait(50); kc.forceActiveFocus();
            keyClick(Qt.Key_Return); ok("confirm: early enter is ignored", top.got.length === 0);
            wait(700); keyClick(Qt.Key_Return);
            ok("unencrypted confirm: enter never picks the weak option", top.got.length === 0 && kc.weakConfirm, top.got);
            ok("confirm ignores passwordNew", !kc.isNew && kc.confirm);
            top.setAsk("a5b", "keyring.confirm", { mode: "confirm", message: "Let Opsis read a saved password?", description: "x", continueLabel: "Allow once" });
            wait(700); kc.forceActiveFocus(); keyClick(Qt.Key_Return);
            ok("confirm: enter continues", top.got.pop() === '{"reply":"continue","choice":false}', top.got);
            keyClick(Qt.Key_Escape); ok("confirm: escape = refused", top.got.pop() === "refused:refused");

            // an app asks for a secret: a short choice is a button of its own
            top.setAsk("a6", "keyring.confirm", { mode: "confirm", message: "Let Opsis read a saved password?", description: "you@example.org on mail.example.org", choiceLabel: "Always", continueLabel: "Allow once", cancelLabel: "Refuse" });
            wait(700);
            ok("secret: short choice is a button, account in mono", kc.choiceIsButton && kc.monoLine && kc.cancelText === "Refuse" && kc.continueText === "Allow once");
            kc.answerChoice(); ok("secret: Always answers with the choice", top.got.pop() === '{"reply":"continue","choice":true}');

            // answered, waiting for the retry: busy label, nothing more can be sent
            top.setAsk("a7", "keyring.password", {}); wait(700); kc.password = "abc"; kc.busy = true;
            kc.submit(); kc.cancel();
            ok("busy: no second answer, no cancel, label says checking", top.got.length === 0 && kc.continueText === "Checking…", top.got);
            kc.busy = false;

            // not shown (locked screen / folding): nothing is emitted
            kc.active = false; kc.submit(); kc.cancel(); kc.answerChoice();
            ok("inactive sheet emits nothing", top.got.length === 0);
            Qt.quit();
        }
    }
}
