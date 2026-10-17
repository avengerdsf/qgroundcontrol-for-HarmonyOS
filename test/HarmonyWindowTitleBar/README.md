# Native HarmonyOS window title bar

`run-tests.cjs` executes the production ETS controller with controlled Window, preferences and file-read providers. It covers pin state, persistence and one-time legacy INI migration, native errors and rollback, system-button avoidance in vp, floating/full-screen state, focus, drag/maximize delegation, popup isolation and asynchronous destruction.

Run with the project Node runtime:

```powershell
& 'D:/Progrem/Huawei/DevEco Studio/tools/node/node.exe' --max-old-space-size=512 test/HarmonyWindowTitleBar/run-tests.cjs
& 'D:/Progrem/Huawei/DevEco Studio/tools/node/node.exe' --max-old-space-size=512 test/HarmonyWindowTitleBar/run-tests.cjs --sdk-types
& ./test/HarmonyWindowTitleBar/test-stage-hooks.ps1
& ./test/HarmonyWindowTitleBar/test-close-hooks.ps1
```

The declaration check expands the five official Kit re-exports to their underlying SDK declarations. ArkUI rendering, system title-button overlap, vp/Qt surface alignment and color appearance require the HAP build and Pad verification.

A lightweight AST constraint compares component property names with the installed SDK's `CustomComponent` inheritance chain, including `CommonMethod`, to catch reserved-method shadowing. Full ArkUI type and rendering validation uses the HAP compiler.

The title row uses ContainerModal system color resources and the official `pin` / `pin_fill` symbols. Floating, maximized and full-screen windows retain the original native three-button area, measured with `getTitleButtonRect()` and its change event. The 37vp application title row adds only the pin button. The native button rectangle already includes the leading button spacing, so the pin has no additional right margin. While native button geometry is unavailable, the application title row stays hidden.

Restore, minimize and close use the system controls. Title double-click selects `EXIT_IMMERSIVE` when maximizing a floating window and restores an already maximized window. The close-hook tests execute the staged QAbility's original synchronous and asynchronous termination callbacks and verify that Qt receives each close request and controls the returned termination result.

Harmony desktop content overrides the ApplicationWindow automatic bottom safe-area padding; phones and other platforms keep Qt's default padding. The startup Qt test injects a 28-unit bottom safe area and verifies both behaviors alongside initial window state and geometry.

The caption controller enables native title and dock hover for fullscreen tablet and 2in1 main windows. The system shares its top touch acquisition area with title hover; retaining it allows edge gestures to reveal system bars, Control Center and Notification Center. Initial fullscreen entry and fullscreen completion reapply this setting; repeated layout events coalesce. Temporary error `1300002` can retry on a later foreground, layout or window event; permanent errors do not schedule retries. The system may briefly show its floating title during an edge gesture. The tablet window policy retains its system-bar responsibility. Phone windows skip the desktop caption controller.

API evidence: [Huawei free-window immersion and native title-button avoidance](https://developer.huawei.com/consumer/cn/doc/doccenter-dev-faq/faqs-arkui-1089), [Window API](https://developer.huawei.com/consumer/cn/doc/harmonyos-references/arkts-apis-window-window).
