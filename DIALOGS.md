# DialogBridge protocol

One socket lets anything that needs a system dialog (the portal's AppChooser, the keyring prompter, the share
picker) ask the shell to draw it. tacit-portal listens and brokers; the shell singleton `services/DialogBridge.qml`
connects as the UI. Code: `src/dialogbridge.{h,cpp}`. Tests: `tests/bridge-test.py` (fake clients),
`tests/qs-test.py` (the real singleton under `qs -p`).

Socket: `$XDG_RUNTIME_DIR/tacit-dialogs.sock`, mode 0600 (`$TACIT_DIALOGS_SOCK` overrides it, for tests).
Framing: one JSON object per line, UTF-8, `\n` terminated, at most 1 MiB per line (a longer line without a newline
drops the connection). Every object has `op`. Unknown ops get `{"op":"error","reason":"bad-op"}`; the first line must be
`hello` (`hello-first`); invalid JSON gets `bad-json` and the connection stays open.

## Handshake
Both roles send first: `{"op":"hello","role":"ui"|"client","proto":1}`.
Reply: `{"op":"hello","proto":1,"ui":true|false}`, where `ui` says whether a shell is connected right now.
One ui at a time: a new ui replaces the old one, and the old one's open asks are rejected `shell-gone`.

## Client (requester) side
```
-> {"op":"ask","id":"<yours>","kind":"keyring.unlock","params":{...}}
<- {"op":"answer","id":"<yours>","result":{...}}
<- {"op":"reject","id":"<yours>","reason":"..."}
-> {"op":"cancel","id":"<yours>"}          # you no longer need the answer; you then get reject/cancelled
```
`id` is yours and only needs to be unique on your connection. Exactly one `answer` or `reject` comes back per ask,
unless you cancel and the answer was already on its way (ignore a second reply for an id you no longer hold).
Reject reasons:
- `no-shell`: no ui connected. Sent at once, never waits. Fail the call the way the caller would expect.
- `shell-gone`: the ui disconnected or was replaced while the ask was open.
- `unsupported`: a shell is connected but no loaded sheet claims this kind (not installed, failed to load, older shell).
  Sent about 3 s after the ask (startup grace). Fall back like `no-shell`.
- `cancelled`: the sheet was dismissed, or you cancelled.
- `refused`: the user said no explicitly (a ui may send any other short reason; treat unknown ones as a refusal).
- `bad-ask`: missing `id` or `kind`.
If the socket is not there at all, the portal is not running: start it through D-Bus
(`org.freedesktop.DBus.StartServiceByName("org.freedesktop.impl.portal.desktop.tacit")`), retry the connect once, and
if it still fails treat it as `no-shell`. If your connection closes, the ask is withdrawn and the shell is told to drop it.
Code in the portal process itself uses `DialogBridge::ask(kind, params, callback)` and `cancel(id)` with the same outcomes.

## UI (shell) side
```
<- {"op":"ask","id":"<bridge id>","kind":"...","params":{...}}     # id is the bridge's, not the client's
-> {"op":"answer","id":"<bridge id>","result":{...}}
-> {"op":"reject","id":"<bridge id>","reason":"cancelled"}
<- {"op":"cancel","id":"<bridge id>"}                               # requester gave up or left: drop the sheet
```
Answers for ids the bridge no longer holds are ignored.

### The singleton
`DialogBridge` (QML, `services/DialogBridge.qml`):
- `queue`: waiting asks `{id, kind, params}`, oldest first. `current` is the first, `asking` is
  `current !== null && !GlobalStates.screenLocked`: **a dialog waits while the screen is locked** and appears when it
  unlocks. Only one is shown at a time.
- Show your sheet while `DialogBridge.asking && DialogBridge.current.kind === "<your kind>"`.
  Finish with `DialogBridge.answer(id, resultObject)` or `DialogBridge.refuse(id, "refused"|"cancelled")`. Both drop
  it from the queue; do not call them from inside an `onAskingChanged` handler (binding loop), call from a click.
- `connected` is the socket state. Quickshell's Socket never retries, so the singleton rebuilds it every 2 s from an
  Instantiator until the portal answers (pattern of shell 656c6e3). When the portal goes away the queue is emptied; the
  portal has already rejected those asks for their callers.
- It is a singleton, so it is lazy: something that loads at startup must reference it (for example a
  `readonly property bool x: DialogBridge.connected` in IslandController) or it never connects. Adding the file needs
  a shell restart, hot reload does not register new singletons.

## Kinds
Each dialog builder defines its kinds and documents them here; keep `params` and `result` small JSON, no secrets in
`params` beyond what the sheet must show. Passwords go only in `result`, and are never logged by the bridge or the shell.
Reserved prefixes: `keyring.*`, `share.*`, `open.*` (AppChooser). `test.*` is used by the tests only.

### share.pick (screen-share picker)
Asked by `share-picker/tacit-share-picker`, xdph's `custom_picker_binary` (one ask per cast, id `p1`). Sheet:
`modules/ii/sharePicker/SharePicker.qml` (window, answers) and `SharePickerContent.qml` (the dialog).
params: `{app, appClass, monitor, screen:{name,x,y,w,h}, screens:[{name,w,h}], windows:[{id,class,title,address}],
asker:{x,y,w,h}|null, allowToken}`. Geometry is in layout (scaled) pixels. `asker` is the focused window when the
picker started (xdph does not say who asked); `address` is the Hyprland address matched from the list, used for the
thumbnail. `allowToken` is true when xdph passed `--allow-token`.
result: `{kind:"screen",name}` | `{kind:"window",id}` | `{kind:"region",output,x,y,w,h}` (output-local), each with
`token` (remember). The picker prints `[SELECTION]<r if token>/screen:NAME | /window:ID | /region:OUT@x,y,w,h`.
Unverified against a live xdph: the `window:` id (list field 1), the `r` flag and output-local region coordinates; try one share of each kind first.
Cancel or Escape = reject `cancelled`: nothing printed, xdph cancels the cast. `no-shell`, `shell-gone` or `unsupported`: the picker
execs the stock `hyprland-share-picker` instead. Install/undo: `install/share-picker-install.sh`.
The sheet shows plain names: a laptop panel is "Built-in screen", other screens their model or "Screen N" (the connector
name never shows); a window's subtitle is its app's name (desktop entry, else the class's last dotted part, capitalised).
"Draw a region" works on the monitor that asked only.

### keyring.password / keyring.confirm (keyring prompter)
Asked by `keyring-prompter/tacit-keyring-prompter`, which owns `org.gnome.keyring.SystemPrompter` and `.PrivatePrompter`
(what `/usr/lib/gcr-prompter` does; gnome-keyring stays the store, pinentry-gnome3 for GPG goes through it too). One ask
per `gcr_prompt_password` / `gcr_prompt_confirm` call, sent as a client on the bridge. Sheet:
`modules/ii/keyring/Keyring.qml` (window, answers) and `KeyringContent.qml` (the notch sheet).
params: `{mode:"password"|"confirm", title, message, description, warning, choiceLabel, choiceChosen, passwordNew,
continueLabel, cancelLabel, retry}`, all strings except the booleans; they are gcr's prompt properties as the daemon set
them. `retry` is true when the daemon asks again on the same prompt after a wrong password (`warning` is then set).
`passwordNew` stays true on the confirm that follows a new password; the sheet ignores it in confirm mode.
result: `{reply:"continue", choice:bool}` plus `password` in password mode. Reject (`cancelled`, `refused`) ends the prompt as a
dismissed dialog: no password, no error. Passwords travel only in `result`.
No shell (also `shell-gone`, a shell restarting under the ask, and `unsupported`, its sheet not loaded yet): the ask is
retried every 2 s for 30 s, then ends as dismissed. After a password answer the sheet stays up, busy, for up to 1.5 s,
because the retry after a wrong password comes only once gnome-keyring has checked it. The sheet is solid (the layer's
blur ignores anything under 79 % alpha). Enter does nothing on "Store passwords unencrypted?". The bridge socket carries
passwords in `result` (0600, same user); the prompter wipes its line buffer, the JSON tree and QML strings are not wiped.
gnome-keyring reads `password-strength` after a new password and treats 0 as blank ("Store passwords unencrypted?"), so the
prompter sets it (>= 1 for any non-empty password). Install/undo: `install/keyring-prompter-install.sh`.
Tests: `tests/keyring-test.py` (real gnome-keyring on a private bus, fake shell), `tests/keyring-e2e.py` (plus kwin
--virtual and the real Keyring.qml), `tests/qs-ui/keyring-interact.qml` (keys and state of the sheet).

### open.choose (Open with)
Asked by tacit-portal itself (`src/appchooser.cpp`, in-process `DialogBridge::ask`) for
`org.freedesktop.impl.portal.AppChooser.ChooseApplication`. Sheet: `modules/ii/openWith/OpenWith.qml` (window, answers)
and `OpenWithContent.qml` (the menu).
params: `{file, kind, contentType, apps:[{id,name,icon,tacit}], selected, asker:{x,y,w,h,monitor}|null}`. `kind` is the
"Always use for <kind>" label (PDF, .7z). `apps` is the portal's `choices` (or, when empty, the Pinax set: xdg default
plus every visible entry whose MimeType lists the type), unknown desktop entries dropped, Tacit apps first in a fixed
order (tacit-viewer, browser, code, claude-app, quiet-files). `selected` = `last_choice` when offered, else the first Tacit app, else
the xdg default, else 0. `kind` falls back to the MIME database's name ("Compressed archive") for tarballs and files with no short extension. `asker` is the focused window (the call does not say who asked).
result: `{id, always}`; `id` must be one of `apps` (else the call ends with response 2). `always` runs
`xdg-mime default <id>.desktop <contentType>`. `cancelled`/`refused` = response 1, other rejects = 2.
No shell, `shell-gone`, `unsupported`, or no app opens the type: the call is forwarded unchanged to the GTK backend. `Request.Close` cancels the
sheet. Install/undo: `install/open-with-install.sh`. Test: `tests/open-with-test.sh` (private bus, fake shell, fake GTK).
