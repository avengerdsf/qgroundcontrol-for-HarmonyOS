# Qt main-page system Back regression

```powershell
./test/HarmonyBackPress/run-tests.ps1 -Baseline
./test/HarmonyBackPress/run-tests.ps1
```

The baseline checks the unmodified Qt 6.12 `MainWindowNativeNode.ets`
fixture and must fail because the page has no explicit system Back handler.
The fixture matches Qt's `qtbase/src/harmonyos/templates/entry/src/main/ets/pages`
template. The green run extracts the actual staging function from production
`package.ps1`, patches an owned temporary copy, transpiles its real ArkTS
callback with the local SDK TypeScript package, and invokes that callback.
It also checks restaging, CRLF templates, preservation of all existing Qt
surface bindings, and rejection without writes for unsupported templates.
Temporary copies stay in project `tmp/verification` and are removed.

The API contract is the `@Entry` component's `onBackPress(): void | boolean`:
returning `true` retains page handling instead of default ArkUI routing.
Evidence: installed API `ets/component/common.d.ts:24683`, local ArkUI
knowledge `01-basics/ts-custom-component-lifecycle.md#onBackPress`, and the
official DevEco local FAQ `faqs-arkui-531`. This differs from the UIAbility
`onBackPressed` callback, whose boolean controls background/destruction.

The observed UI regression is preserved in
`tmp/verification/harmony-application/run-20261015-041359-251/11-mock-filled.jpeg`,
`12-mock-ready.jpeg`, and `13-after-back.jpeg`: a live Add Link dialog with
IME is followed by a sliding QGC surface and persistent white page while
the process remains present. Screenshots alone do not identify every native
lifecycle transition. The software tests do not run ArkUI routing or certify
the runtime fix.

After root builds and installs the updated independent `.tests` HAP on the
existing `127.0.0.1:5555` emulator, repeat the same Add Mock Link, text-input,
and system Back sequence. Confirm the page and dialog remain visible and
interactive, repeated Back does not expose a white page, and the ArkTS log
records `QGC main page: system Back consumed`. Then verify normal QGC
Cancel/Save and menu navigation still work.

The handler preserves the Qt surface. It does not forward Qt Back, close a
QML popup, or promise to hide the IME. Use the visible IME dismiss control or
a separately verified Qt input-method path for keyboard dismissal. The
regression runner performs no device, build, installation, or emulator action.
