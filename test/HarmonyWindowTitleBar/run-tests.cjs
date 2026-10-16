'use strict';
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const tsPath = process.env.QGC_TYPESCRIPT || 'D:/Progrem/Huawei/DevEco Studio/sdk/default/openharmony/ets/build-tools/ets-loader/node_modules/typescript';
const ts = require(tsPath);
const sourcePath = path.resolve(__dirname, '../../deploy/harmonyos/bridge/QGCWindowTitleBar.ets');
const source = fs.readFileSync(sourcePath, 'utf8').split('\n@Component\n')[0];

if (process.argv.includes('--sdk-types')) {
  // Expand only these official Kit re-exports, avoiding unrelated ArkTS annotation declarations
  // which cannot be parsed by the TypeScript declaration checker. The ArkUI component is built by hvigor.
  const aliases = { window: '@ohos.window', common: '@ohos.app.ability.common',
    preferences: '@ohos.data.preferences', fileIo: '@ohos.file.fs', hilog: '@ohos.hilog' };
  const sdkSource = source.replace("import { BusinessError } from '@kit.BasicServicesKit';", "import { BusinessError } from '@ohos.base';")
    .replace(/import \{ (\w+) \} from '@kit\.[^']+';/g,
    (_line, name) => `import ${name} from '${aliases[name]}';`);
  const virtual = (sourcePath + '.ts').replaceAll('\\', '/');
  const sdk = process.env.QGC_ETS_SDK || path.resolve(tsPath, '../../../..');
  const options = { strict: true, skipLibCheck: true, noEmit: true, target: ts.ScriptTarget.ES2020, module: ts.ModuleKind.CommonJS };
  const host = ts.createCompilerHost(options), original = host.getSourceFile.bind(host);
  host.getSourceFile = (file, version, onError, again) => file.replaceAll('\\', '/') === virtual ?
    ts.createSourceFile(file, sdkSource, version, true) : original(file, version, onError, again);
  host.resolveModuleNames = (names, containing) => names.map(name => name.startsWith('@kit.') ?
    { resolvedFileName: path.join(sdk, 'kits', name + '.d.ts') } : name.startsWith('@ohos.') ?
      { resolvedFileName: path.join(sdk, 'api', name + '.d.ts') } : ts.resolveModuleName(name, containing, options, host).resolvedModule);
  const diagnostics = ts.getPreEmitDiagnostics(ts.createProgram([virtual], options, host));
  const errors = diagnostics.filter(item => item.category === ts.DiagnosticCategory.Error);
  if (errors.length) console.error(ts.formatDiagnosticsWithColorAndContext(errors, {
    getCanonicalFileName: file => file, getCurrentDirectory: () => process.cwd(), getNewLine: () => '\n'
  }));
  console.log(`Controller SDK declaration check: ${errors.length} errors, ${diagnostics.length - errors.length} warnings`);
  process.exit(errors.length ? 1 : 0);
}

const compiled = ts.transpileModule(source, { compilerOptions: {
  module: ts.ModuleKind.CommonJS, target: ts.ScriptTarget.ES2020
}, fileName: 'QGCWindowTitleBar.ts', reportDiagnostics: true });
assert.equal((compiled.diagnostics || []).filter(item => item.category === ts.DiagnosticCategory.Error).length, 0);
const status = { FULL_SCREEN: 1, MAXIMIZE: 2, MINIMIZE: 3, FLOATING: 4, SPLIT_SCREEN: 5 };
const events = { WINDOW_SHOWN: 1, WINDOW_ACTIVE: 2, WINDOW_INACTIVE: 3 };
const tick = () => new Promise(resolve => setImmediate(resolve));
function deferred() {
  let resolve, reject;
  const promise = new Promise((yes, no) => { resolve = yes; reject = no; });
  return { promise, resolve, reject };
}
function setup(options = {}) {
  const calls = [], states = [], listeners = new Map(), data = new Map();
  const hoverCalls = [];
  const modulePreferencesDir = '/data/storage/el2/base/haps/entry/preferences';
  const appPreferencesDir = '/data/storage/el2/base/preferences';
  if (Object.hasOwn(options, 'saved')) data.set('topmost', options.saved);
  const store = {
    async has(key) { if (options.hasGate) await options.hasGate.promise; return data.has(key); },
    async get(key, fallback) { return data.has(key) ? data.get(key) : fallback; },
    async put(key, value) { calls.push(['put', value]); if (options.putError) { options.putError = false; throw Error('put failed'); } data.set(key, value); },
    async flush() { calls.push(['flush']); if (options.flushError) { options.flushError = false; throw Error('flush failed'); } }
  };
  const win = {
    mode: options.mode ?? status.FLOATING,
    rect: options.rect ?? { right: 14, top: 4, width: 120, height: 28 },
    type: options.type ?? 1,
    focused: options.focused ?? true,
    topmost: false,
    decorVisible: true,
    titleHover: options.titleHover ?? true,
    dockHover: true,
    getWindowProperties() { calls.push(['properties']); return { id: 1, type: this.type }; },
    getWindowStatus() {
      calls.push(['status']);
      if (options.statusErrorCode) throw Object.assign(Error('window state unavailable'), { code: options.statusErrorCode });
      return this.mode;
    },
    getTitleButtonRect() {
      calls.push(['rect']);
      if (options.rectError) throw Object.assign(Error('rect failed'), { code: options.rectErrorCode });
      return this.rect;
    },
    isFocused() { calls.push(['focus']); return this.focused; },
    on(name, callback) { calls.push(['on', name]); if (options.listenError === name) throw Error('listen failed'); assert.ok(!listeners.has(name)); listeners.set(name, callback); },
    off(name, callback) { calls.push(['off', name]); assert.equal(listeners.get(name), callback); listeners.delete(name); },
    emit(name, value) { listeners.get(name)?.(value); },
    getWindowDecorVisible() {
      calls.push(['decorVisible']);
      if (options.immersiveGetter && this.mode === status.FULL_SCREEN) return false;
      return this.decorVisible;
    },
    setWindowDecorVisible(value) {
      calls.push(['decor', value]);
      if (options.decorError && !value) throw Error('decor 801');
      if (options.decorReenter && calls.filter(call => call[0] === 'decor').length < 10) {
        this.emit('windowTitleButtonRectChange', this.rect);
      }
      this.decorVisible = value;
    },
    async setWindowTopmost(value) {
      calls.push(['topmost', value]);
      if (options.topmostGate) { const gate = options.topmostGate; options.topmostGate = null; await gate.promise; }
      if (options.topmostError) { options.topmostError = false; throw Error('topmost 801'); }
      this.topmost = value;
    },
    async startMoving() { calls.push(['move']); },
    async maximize() { calls.push(['maximize']); },
    async minimize() { calls.push(['minimize']); if (options.minimizeError) throw Error('minimize rejected'); },
    async recover() { calls.push(['recover']); if (options.recoverError) throw Error('recover rejected'); },
    async setTitleAndDockHoverShown(title, dock) {
      hoverCalls.push([title, dock]);
      if (options.hoverGate) { const gate = options.hoverGate; options.hoverGate = null; await gate.promise; }
      if (options.hoverError) throw Object.assign(Error('hover unsupported'), { code: 801 });
      if (title && options.hoverRestoreErrorCode) throw Object.assign(Error('hover restore failed'), { code: options.hoverRestoreErrorCode });
      this.titleHover = title; this.dockHover = dock;
    }
  };
  const modules = {
    '@ohos.deviceInfo': { default: { deviceType: options.deviceType ?? 'tablet' } },
    '@kit.ArkUI': { window: { WindowType: { TYPE_MAIN: 32 }, WindowStatusType: status, WindowEventType: events } },
    '@kit.AbilityKit': {},
    '@kit.ArkData': { preferences: { async getPreferences(context, config) {
      assert.equal(context.preferencesDir, modulePreferencesDir); assert.equal(config.name, 'qgc_window');
      calls.push(['preferences', context.preferencesDir, config.name]);
      if (options.preferencesGate) await options.preferencesGate.promise;
      if (options.preferencesError) throw Error('preferences failed');
      return store;
    } } },
    '@kit.CoreFileKit': { fileIo: { async readText(file) {
      calls.push(['legacy', file]);
      if (!options.legacy || file !== `${appPreferencesDir}/QGroundControl/QGroundControl.ini`) throw Error('ENOENT');
      return options.legacy;
    } } },
    '@kit.PerformanceAnalysisKit': { hilog: { info(...args) { calls.push(['info', ...args]); }, warn(...args) { calls.push(['warn', ...args]); } } }
  };
  const exports = {};
  vm.runInNewContext(compiled.outputText, { exports, require: name => { assert.ok(modules[name], name); return modules[name]; }, console });
  const controller = new exports.QGCTitleBarController();
  return { controller, win, calls, states, data, options, listeners, hoverCalls,
    start: () => controller.start(win, { preferencesDir: modulePreferencesDir,
      eventHub: { emit(event) { calls.push(['emit', event]); } },
      getApplicationContext() { calls.push(['applicationContext']); return { preferencesDir: appPreferencesDir }; }
    }, state => states.push({ ...state })),
    last: () => states.at(-1) };
}
const tests = [];
function test(name, body) { tests.push([name, body]); }

for (const deviceType of ['tablet', '2in1']) test(`${deviceType} fullscreen enables the system top touch hotzone and coalesces rect refreshes`, async () => {
  const h = setup({ mode: status.FULL_SCREEN, deviceType, titleHover: false }); await h.start(); await tick();
  assert.equal(h.last().visible, true); assert.equal(h.win.titleHover, true); assert.equal(h.win.dockHover, true);
  assert.deepEqual(h.hoverCalls, [[true, true]], 'cold fullscreen must explicitly enable the shared native top touch hotzone');
  for (let index = 0; index < 100; ++index) h.controller.layoutChanged();
  await tick(); assert.equal(h.hoverCalls.length, 1, 'stable layout must not write hover again');
  h.win.mode = status.FLOATING; h.win.emit('windowStatusDidChange', h.win.mode); await tick();
  h.win.titleHover = false; h.win.mode = status.FULL_SCREEN; h.win.emit('windowStatusDidChange', h.win.mode); await tick();
  assert.equal(h.win.titleHover, true); assert.equal(h.hoverCalls.length, 2, 'fullscreen completion must repair a native hotzone reset');
  for (let index = 0; index < 100; ++index) h.win.emit('windowTitleButtonRectChange', h.win.rect);
  await tick(); assert.equal(h.hoverCalls.length, 2);
  assert.ok(h.hoverCalls.every(call => call[0] && call[1]), 'desktop caption must preserve top and dock edge access');
});

test('actual phone keeps desktop title controller hidden without native window or preferences calls', async () => {
  const h = setup({ deviceType: 'phone', mode: status.FULL_SCREEN, saved: true }); await h.start();
  assert.equal(h.last().visible, false); assert.equal(h.last().customControls, false);
  h.controller.foreground(); h.controller.layoutChanged(); await h.controller.toggle();
  h.controller.move(); h.controller.maximize(); await h.controller.restore(); await h.controller.minimize();
  h.controller.close(); h.controller.destroy(); await tick();
  assert.equal(h.last().visible, false); assert.equal(h.last().customControls, false);
  assert.deepEqual(h.calls, [], 'phone must not enter desktop Window, preferences, or event-hub APIs');
  assert.equal(h.listeners.size, 0); assert.equal(h.hoverCalls.length, 0); assert.equal(h.data.get('topmost'), true);
});
test('fresh installation defaults off and initializes dedicated preferences once', async () => {
  const h = setup(); await h.start();
  assert.equal(h.last().topmost, false); assert.equal(h.last().visible, true);
  assert.equal(h.data.get('topmost'), false); assert.equal(h.calls.filter(call => call[0] === 'legacy').length, 1);
  assert.deepEqual(h.calls.filter(call => call[0] === 'decor'), [['decor', false]]);
});
test('saved on applies to native window without reading or writing Qt INI', async () => {
  const h = setup({ saved: true, legacy: '[General]\nwindowTopmost=false' }); await h.start();
  assert.equal(h.win.topmost, true); assert.equal(h.last().topmost, true);
  assert.equal(h.calls.filter(call => call[0] === 'legacy' || call[0] === 'put').length, 0);
});
test('legacy General preference migrates true to a separate store', async () => {
  const h = setup({ legacy: '[AppSettings]\nwindowTopmost=false\n[General]\r\nwindowTopmost=true\r\n' }); await h.start();
  assert.equal(h.data.get('topmost'), true); assert.equal(h.win.topmost, true);
  assert.equal(h.calls.find(call => call[0] === 'legacy')[1], '/data/storage/el2/base/preferences/QGroundControl/QGroundControl.ini');
});
test('legacy migration reads app preferences while dedicated native preferences stay in module scope', async () => {
  const h = setup({ legacy: '[General]\nwindowTopmost=true' }); await h.start();
  assert.equal(h.last().topmost, true);
  assert.deepEqual(h.calls.find(call => call[0] === 'preferences'), ['preferences', '/data/storage/el2/base/haps/entry/preferences', 'qgc_window']);
  assert.equal(h.calls.filter(call => call[0] === 'applicationContext').length, 1);
  assert.deepEqual(h.calls.filter(call => call[0] === 'legacy'), [['legacy', '/data/storage/el2/base/preferences/QGroundControl/QGroundControl.ini']]);
});
test('legacy key from another INI group does not migrate', async () => {
  const h = setup({ legacy: '[AppSettings]\nwindowTopmost=true\n[General]\nmuteAudio=true' }); await h.start();
  assert.equal(h.last().topmost, false);
});
test('invalid saved value defaults off', async () => {
  const h = setup({ saved: 'true' }); await h.start(); assert.equal(h.last().topmost, false);
});
test('pin toggles off to on and on to off only after native acceptance and flush', async () => {
  const h = setup({ saved: false }); await h.start(); await h.controller.toggle();
  assert.equal(h.win.topmost, true); assert.equal(h.last().topmost, true); assert.equal(h.data.get('topmost'), true);
  await h.controller.toggle(); assert.equal(h.win.topmost, false); assert.equal(h.last().topmost, false);
});
test('rapid duplicate pin activation cannot enqueue a second native toggle', async () => {
  const h = setup({ saved: false }); await h.start(); const gate = deferred(); h.options.topmostGate = gate;
  const first = h.controller.toggle(); h.controller.toggle(); h.controller.toggle(); await tick();
  assert.equal(h.last().busy, true); assert.equal(h.calls.filter(call => call[0] === 'topmost').length, 2);
  gate.resolve(); await first; assert.equal(h.last().busy, false); assert.equal(h.last().topmost, true);
});
test('topmost failure retains previous preference and restores native title', async () => {
  const h = setup({ saved: false }); await h.start(); h.options.topmostError = true; await h.controller.toggle();
  assert.equal(h.last().topmost, false); assert.equal(h.data.get('topmost'), false); assert.equal(h.last().visible, false);
  assert.deepEqual(h.calls.filter(call => call[0] === 'decor').at(-1), ['decor', true]);
});
for (const failure of ['putError', 'flushError']) test(`${failure} rolls native and stored state back`, async () => {
  const h = setup({ saved: false }); await h.start(); h.options[failure] = true; await h.controller.toggle();
  assert.equal(h.win.topmost, false); assert.equal(h.data.get('topmost'), false); assert.equal(h.last().topmost, false);
});
test('dedicated saved value restores after a new controller session', async () => {
  const h = setup({ saved: false }); await h.start(); await h.controller.toggle(); h.controller.destroy(); await h.start();
  assert.equal(h.last().topmost, true); assert.equal(h.win.topmost, true);
});
test('foreground restores native topmost preference and refreshes layout', async () => {
  const h = setup({ saved: true }); await h.start(); h.win.topmost = false;
  h.win.emit('windowEvent', events.WINDOW_SHOWN); await tick(); assert.equal(h.win.topmost, true);
});
test('repeated foreground events coalesce while native restoration is pending', async () => {
  const h = setup({ saved: true }); await h.start(); const gate = deferred(); h.options.topmostGate = gate;
  h.controller.foreground(); h.controller.foreground(); h.controller.foreground(); await tick();
  assert.equal(h.calls.filter(call => call[0] === 'topmost').length, 2);
  gate.resolve(); await tick(); assert.equal(h.last().topmost, true);
});
test('actual three-button right inset and width are vp, including changed layout', async () => {
  const h = setup(); await h.start(); assert.equal(h.last().controlsInset, 134);
  h.win.rect = { right: 25, top: 4, width: 171, height: 28 };
  h.win.emit('windowTitleButtonRectChange', h.win.rect); assert.equal(h.last().controlsInset, 196);
});
test('empty native button rectangle hides header until a valid floating rectangle arrives', async () => {
  const h = setup({ rect: { right: 0, top: 0, width: 0, height: 0 } }); await h.start(); assert.equal(h.last().visible, false);
  h.win.rect = { right: 0, top: 4, width: 180, height: 28 }; h.win.emit('windowTitleButtonRectChange', h.win.rect);
  assert.equal(h.last().visible, true); assert.equal(h.last().controlsInset, 180);
});
test('temporary refresh error 1300002 recovers and retains an accessible way to cancel topmost', async () => {
  for (const recovery of ['rect', 'status', 'foreground']) {
    const h = setup({ saved: true }); await h.start(); h.options.rectError = true; h.options.rectErrorCode = 1300002;
    h.win.emit('windowTitleButtonRectChange', h.win.rect);
    assert.equal(h.last().visible, false); assert.equal(h.win.topmost, true); assert.equal(h.data.get('topmost'), true);
    assert.deepEqual(h.calls.filter(call => call[0] === 'decor').at(-1), ['decor', true]);
    assert.deepEqual(h.calls.filter(call => call[0] === 'topmost'), [['topmost', true]], 'temporary layout error must not retry or change native topmost');
    h.options.rectError = false;
    if (recovery === 'status') {
      h.win.mode = status.FULL_SCREEN; h.win.emit('windowStatusDidChange', h.win.mode); assert.equal(h.last().visible, true);
      h.win.mode = status.FLOATING; h.win.emit('windowStatusDidChange', h.win.mode);
    } else if (recovery === 'foreground') {
      h.controller.foreground(); await tick();
    } else {
      h.win.emit('windowTitleButtonRectChange', h.win.rect);
    }
    assert.equal(h.last().visible, true, `${recovery} must restore the floating title and pin`);
    assert.deepEqual(h.calls.filter(call => call[0] === 'decor').at(-1), ['decor', false]);
    await h.controller.toggle(); assert.equal(h.win.topmost, false); assert.equal(h.data.get('topmost'), false);
  }
});
for (const code of [201, 801]) test(`permanent refresh error ${code} retains the native fallback without retries`, async () => {
  const h = setup({ saved: true }); await h.start(); h.options.rectError = true; h.options.rectErrorCode = code;
  h.win.emit('windowTitleButtonRectChange', h.win.rect); h.options.rectError = false;
  h.win.emit('windowTitleButtonRectChange', h.win.rect); h.controller.foreground();
  h.win.mode = status.FULL_SCREEN; h.win.emit('windowStatusDidChange', h.win.mode);
  h.win.mode = status.FLOATING; h.win.emit('windowStatusDidChange', h.win.mode); await h.controller.toggle();
  assert.equal(h.last().visible, false); assert.equal(h.win.topmost, true);
  assert.deepEqual(h.calls.filter(call => call[0] === 'topmost'), [['topmost', true]]);
  assert.deepEqual(h.calls.filter(call => call[0] === 'decor').at(-1), ['decor', true]);
});
test('caption stays visible through floating maximized and fullscreen transitions', async () => {
  const h = setup(); await h.start();
  for (const mode of [status.FULL_SCREEN, status.MAXIMIZE, status.MINIMIZE, status.SPLIT_SCREEN]) {
    h.win.mode = mode; h.win.emit('windowStatusDidChange', mode);
    assert.equal(h.last().visible, mode === status.FULL_SCREEN || mode === status.MAXIMIZE);
  }
  h.win.mode = status.FLOATING; h.win.emit('windowStatusDidChange', h.win.mode); assert.equal(h.last().visible, true);
  assert.equal(h.calls.filter(call => call[0] === 'maximize').length, 0);
  assert.deepEqual(h.calls.filter(call => call[0] === 'decor'), [['decor', false], ['decor', true], ['decor', false], ['decor', true], ['decor', false]]);
});
test('full-screen cold entry keeps original system decor until entering a floating window', async () => {
  const h = setup({ mode: status.FULL_SCREEN }); await h.start();
  assert.equal(h.last().visible, true); assert.equal(h.last().customControls, true); assert.equal(h.last().controlsInset, 20);
  assert.equal(h.calls.filter(call => call[0] === 'decor').length, 0);
  h.win.mode = status.FLOATING; h.win.emit('windowStatusDidChange', h.win.mode);
  h.win.emit('windowStatusDidChange', h.win.mode); h.win.emit('windowTitleButtonRectChange', h.win.rect);
  assert.deepEqual(h.calls.filter(call => call[0] === 'decor'), [['decor', false]]);
});
test('cold floating page repairs the Qt first frameless false sync without changing topmost', async () => {
  const h = setup({ saved: true }); await h.start(); assert.equal(h.win.decorVisible, false);
  // Actual QOhosView::updateWindowFrameless(false) calls setWindowDecorVisible(true).
  h.win.setWindowDecorVisible(true); h.win.emit('windowTitleButtonRectChange', h.win.rect);
  assert.equal(h.win.decorVisible, false, 'Qt first sync must not leave native and ArkUI title rows visible together');
  assert.equal(h.last().visible, true); assert.equal(h.last().topmost, true);
  assert.deepEqual(h.calls.filter(call => call[0] === 'topmost'), [['topmost', true]], 'layout reconciliation must not restore topmost again');
  const writes = h.calls.filter(call => call[0] === 'decor').length;
  for (let index = 0; index < 100; ++index) h.controller.layoutChanged();
  assert.equal(h.calls.filter(call => call[0] === 'decor').length, writes, 'stable area events must not generate another native update');
});
test('decor reconciliation remains bounded when native writes synchronously emit title rectangle events', async () => {
  const h = setup({ decorReenter: true }); await h.start(); assert.equal(h.win.decorVisible, false);
  assert.equal(h.calls.filter(call => call[0] === 'decor').length, 1);
  h.win.decorVisible = true; h.controller.layoutChanged();
  assert.equal(h.win.decorVisible, false); assert.equal(h.calls.filter(call => call[0] === 'decor').length, 2);
  h.win.mode = status.FULL_SCREEN; h.win.emit('windowStatusDidChange', h.win.mode);
  assert.equal(h.win.decorVisible, true); assert.equal(h.last().visible, true);
  assert.equal(h.calls.filter(call => call[0] === 'decor').length, 3);
});
test('immersive edge title visibility does not cause repeated native decor writes', async () => {
  const h = setup({ mode: status.FULL_SCREEN, immersiveGetter: true }); await h.start();
  for (let index = 0; index < 100; ++index) h.controller.layoutChanged();
  assert.equal(h.last().visible, true); assert.equal(h.calls.filter(call => call[0] === 'decor').length, 0);
  h.win.mode = status.FLOATING; h.win.emit('windowStatusDidChange', h.win.mode); assert.equal(h.win.decorVisible, false);
  h.win.mode = status.FULL_SCREEN; h.win.emit('windowStatusDidChange', h.win.mode); assert.equal(h.win.decorVisible, true);
  const writes = h.calls.filter(call => call[0] === 'decor').length;
  for (let index = 0; index < 100; ++index) h.controller.layoutChanged();
  assert.equal(h.last().visible, true); assert.equal(h.calls.filter(call => call[0] === 'decor').length, writes);
});
test('focus events drive native title opacity state independently of pin state', async () => {
  const h = setup({ saved: true, focused: false }); await h.start(); assert.equal(h.last().focused, false);
  h.win.emit('windowEvent', events.WINDOW_ACTIVE); assert.equal(h.last().focused, true);
  h.win.emit('windowEvent', events.WINDOW_INACTIVE); assert.equal(h.last().focused, false); assert.equal(h.last().topmost, true);
});
test('fullscreen disables title dragging and double click restores through the native API', async () => {
  const h = setup(); await h.start(); h.controller.move(); h.controller.maximize(); await tick();
  assert.equal(h.calls.filter(call => call[0] === 'move').length, 1); assert.equal(h.calls.filter(call => call[0] === 'maximize').length, 1);
  h.win.mode = status.FULL_SCREEN; h.win.emit('windowStatusDidChange', h.win.mode); h.controller.move(); h.controller.maximize();
  assert.equal(h.calls.filter(call => call[0] === 'move').length, 1);
  assert.equal(h.calls.filter(call => call[0] === 'recover').length, 1);
});
test('fullscreen restore and minimize buttons delegate to native Window APIs', async () => {
  const h = setup({ mode: status.FULL_SCREEN }); await h.start(); await h.controller.restore(); await h.controller.minimize();
  assert.equal(h.calls.filter(call => call[0] === 'recover').length, 1);
  assert.equal(h.calls.filter(call => call[0] === 'minimize').length, 1);
  assert.equal(h.last().visible, true, 'native status notifications own displayed window state');
  h.controller.close(); assert.deepEqual(h.calls.filter(call => call[0] === 'emit'), [['emit', 'qgc.window.close.request']]);
});
test('window action rejection leaves fullscreen caption available without changing stored state', async () => {
  const h = setup({ mode: status.FULL_SCREEN, saved: true, recoverError: true, minimizeError: true }); await h.start();
  await h.controller.restore(); await h.controller.minimize();
  assert.equal(h.last().visible, true); assert.equal(h.data.get('topmost'), true); assert.equal(h.calls.filter(call => call[0] === 'warn').length, 2);
});
test('maximized transition keeps one caption even before native button geometry is available', async () => {
  const h = setup({ mode: status.FULL_SCREEN }); await h.start();
  h.win.rect = { right: 0, top: 0, width: 0, height: 0 }; h.win.mode = status.MAXIMIZE; h.win.emit('windowStatusDidChange', h.win.mode);
  assert.equal(h.last().visible, true); assert.equal(h.last().customControls, true);
});
test('destroyed caption cannot dispatch close or native actions', async () => {
  const h = setup({ mode: status.FULL_SCREEN }); await h.start(); h.controller.destroy();
  h.controller.close(); await h.controller.restore(); await h.controller.minimize();
  assert.equal(h.calls.filter(call => ['emit', 'recover', 'minimize'].includes(call[0])).length, 0);
});
test('fullscreen failure retains native edge access', async () => {
  const h = setup({ mode: status.FULL_SCREEN }); await h.start(); await tick(); assert.equal(h.win.titleHover, true);
  h.options.statusErrorCode = 201; h.controller.layoutChanged(); await tick();
  assert.equal(h.last().visible, false); assert.equal(h.win.titleHover, true); assert.equal(h.win.dockHover, true);
  h.options.statusErrorCode = 0; h.controller.foreground(); await tick();
  assert.equal(h.win.titleHover, true, 'disabled custom caption must preserve native fallback on foreground');
});

test('late in-flight hotzone enable remains safe when native fallback takes over', async () => {
  const gate = deferred(), h = setup({ mode: status.FULL_SCREEN, hoverGate: gate }); await h.start(); await tick();
  assert.deepEqual(h.hoverCalls, [[true, true]]);
  h.options.statusErrorCode = 201; h.controller.layoutChanged(); assert.equal(h.last().visible, false);
  gate.resolve(); await tick(); await tick();
  assert.equal(h.win.titleHover, true); assert.deepEqual(h.hoverCalls, [[true, true]]);
});
test('queued fullscreen hotzone enable remains available after caption fallback', async () => {
  const h = setup({ saved: true }); await h.start(); const gate = deferred(); h.options.topmostGate = gate;
  const toggle = h.controller.toggle(); await tick();
  h.win.mode = status.FULL_SCREEN; h.win.emit('windowStatusDidChange', h.win.mode);
  h.options.statusErrorCode = 201; h.controller.layoutChanged();
  gate.resolve(); await toggle; await tick(); await tick();
  assert.equal(h.last().visible, false); assert.equal(h.win.titleHover, true);
  assert.deepEqual(h.hoverCalls, [[true, true]], 'queued work must preserve system edge access');
});
test('temporary fullscreen refresh failure preserves native hotzone while the custom caption recovers', async () => {
  const h = setup({ mode: status.FULL_SCREEN }); await h.start(); await tick();
  h.options.statusErrorCode = 1300002; h.controller.layoutChanged(); await tick();
  assert.equal(h.last().visible, false); assert.equal(h.win.titleHover, true);
  h.options.statusErrorCode = 0; h.controller.layoutChanged(); await tick();
  assert.equal(h.last().visible, true); assert.equal(h.win.titleHover, true);
});
test('unsupported native hotzone enable retains custom caption and logs the rejection', async () => {
  const h = setup({ mode: status.FULL_SCREEN, hoverError: true }); await h.start(); await tick(); await tick();
  assert.equal(h.last().visible, true); assert.equal(h.win.titleHover, true);
  assert.equal(h.calls.filter(call => call[0] === 'warn').length, 1); assert.deepEqual(h.hoverCalls, [[true, true]]);
});
test('repeated fullscreen completion repairs native hover without repeated layout work', async () => {
  const h = setup({ mode: status.FULL_SCREEN }); await h.start(); await tick();
  h.win.titleHover = false; h.win.emit('windowStatusDidChange', status.FULL_SCREEN); await tick();
  assert.equal(h.win.titleHover, true); assert.equal(h.hoverCalls.length, 2);
  assert.equal(h.calls.filter(call => call[0] === 'decor').length, 0);
});
test('destroyed fullscreen caption releases hover after pending native work', async () => {
  const gate = deferred(), h = setup({ mode: status.FULL_SCREEN, hoverGate: gate }); await h.start(); await tick();
  h.controller.destroy(); gate.resolve(); await tick(); await tick();
  assert.equal(h.win.titleHover, true); assert.equal(h.listeners.size, 0);
  assert.deepEqual(h.hoverCalls, [[true, true], [true, true]]);
});
for (const event of ['foreground', 'layout', 'status', 'focus']) test(`disabled caption retries transient native fallback restoration on ${event}`, async () => {
  const h = setup({ mode: status.FULL_SCREEN, titleHover: false, hoverRestoreErrorCode: 1300002 });
  await h.start(); await tick(); assert.equal(h.win.titleHover, false);
  h.options.statusErrorCode = 201;
  h.controller.layoutChanged(); await tick(); await tick();
  assert.equal(h.last().visible, false); assert.equal(h.win.titleHover, false);
  h.options.hoverRestoreErrorCode = 0; h.options.statusErrorCode = 0;
  if (event === 'foreground') h.controller.foreground();
  else if (event === 'layout') h.controller.layoutChanged();
  else if (event === 'status') h.win.emit('windowStatusDidChange', status.FULL_SCREEN);
  else h.win.emit('windowEvent', events.WINDOW_ACTIVE);
  await tick(); await tick();
  assert.equal(h.last().visible, false, 'disabled caption must remain in native fallback');
  assert.equal(h.win.titleHover, true); assert.equal(h.win.dockHover, true);
  assert.deepEqual(h.hoverCalls, [[true, true], [true, true], [true, true]]);
  h.controller.foreground(); h.controller.layoutChanged(); h.win.emit('windowEvent', events.WINDOW_ACTIVE); await tick();
  assert.equal(h.hoverCalls.length, 3, 'successful restoration must clear pending retry state');
});
for (const code of [201, 801]) test(`permanent native fallback restore error ${code} is not retried by later events`, async () => {
  const h = setup({ mode: status.FULL_SCREEN, titleHover: false, hoverRestoreErrorCode: 1300002 });
  await h.start(); await tick();
  h.options.hoverRestoreErrorCode = code; h.options.statusErrorCode = 201;
  h.controller.layoutChanged(); await tick(); await tick(); assert.equal(h.hoverCalls.length, 2);
  h.options.hoverRestoreErrorCode = 0; h.options.statusErrorCode = 0;
  h.controller.foreground(); h.controller.layoutChanged(); h.win.emit('windowStatusDidChange', status.FULL_SCREEN);
  h.win.emit('windowEvent', events.WINDOW_ACTIVE); await tick(); await tick();
  assert.equal(h.hoverCalls.length, 2, 'permanent native error must not trigger automatic retries');
});
function setupPage(options = {}) {
  const packageSource = fs.readFileSync(path.resolve(__dirname, '../../deploy/harmonyos/package.ps1'), 'utf8');
  const target = packageSource.match(/Target = @'\r?\n([\s\S]*?)\r?\n'@/);
  assert.ok(target, 'Production native page lifecycle must be available');
  const pageSource = `class StagedMainPage { ${target[1].replaceAll('@State ', '')}\n getUIContext() { return uiContext; } } exports.Page = StagedMainPage;`;
  const pageCompiled = ts.transpileModule(pageSource, { compilerOptions: { module: ts.ModuleKind.CommonJS, target: ts.ScriptTarget.ES2020 } }).outputText;
  const calls = [], exports = {};
  const mainWindowId = options.mainWindowId ?? 1;
  const nativeWindow = { getWindowProperties: () => ({ id: options.nativeWindowId ?? mainWindowId }) };
  const context = { windowStage: { getMainWindowSync: () => ({ getWindowProperties: () => ({ id: mainWindowId }) }) } };
  const uiContext = { getWindowName: () => options.missingName ? undefined : 'exact-current-window',
    getWindowId: () => options.currentWindowId ?? mainWindowId, getHostContext: () => context };
  class Controller {
    async start(win) { assert.equal(win, nativeWindow); calls.push(['start']); }
    layoutChanged() { calls.push(['layout']); }
    destroy() {}
  }
  vm.runInNewContext(pageCompiled, { exports, uiContext, QGCTitleBarController: Controller, QGCTitleBarState: class {},
    window: { findWindow(name) { assert.equal(name, 'exact-current-window'); return nativeWindow; } },
    console: { info() {}, warn() {} } });
  const page = new exports.Page(); page.createInfo = { xComponentId: '__nnMainWindow_unit' };
  return { page, calls, options };
}
test('native page verifies stage main window identity and excludes child and popup windows', async () => {
  for (const options of [{ currentWindowId: 2 }, { nativeWindowId: 2 }]) {
    const h = setupPage(options); h.page.startTitleBar(); assert.equal(h.calls.length, 0);
  }
  const h = setupPage(); h.page.startTitleBar(); h.page.startTitleBar(); assert.equal(h.calls.length, 1);
  const waiting = setupPage({ missingName: true }); waiting.page.startTitleBar(); assert.equal(waiting.calls.length, 0);
  waiting.options.missingName = false; waiting.page.startTitleBar(); assert.equal(waiting.calls.length, 1);
});
test('production page area changes reconcile native decor after a single main-window binding', async () => {
  const h = setupPage(); h.page.refreshTitleBarLayout(); h.page.refreshTitleBarLayout();
  assert.deepEqual(h.calls, [['start'], ['layout'], ['layout']]);
  const child = setupPage({ currentWindowId: 2 }); child.page.refreshTitleBarLayout(); assert.equal(child.calls.length, 0);
});
for (const failure of ['preferencesError', 'decorError', 'rectError', 'topmostError']) test(`${failure} safely restores system title fallback`, async () => {
  const h = setup({ [failure]: true }); await h.start(); assert.equal(h.last().visible, false);
  assert.deepEqual(h.calls.filter(call => call[0] === 'decor').at(-1), ['decor', true]);
});
test('partially registered listeners are removed on destruction without affecting other listeners', async () => {
  const h = setup({ listenError: 'windowTitleButtonRectChange' }); await h.start(); h.controller.destroy(); assert.equal(h.listeners.size, 0);
  assert.equal(h.calls.filter(call => call[0] === 'off').length, 1);
});
test('destruction unbinds each owned callback and ignores subsequent events', async () => {
  const h = setup(); await h.start(); h.controller.destroy(); assert.equal(h.listeners.size, 0);
  const count = h.states.length; h.win.emit('windowEvent', events.WINDOW_ACTIVE); assert.equal(h.states.length, count);
  h.controller.destroy(); assert.equal(h.calls.filter(call => call[0] === 'off').length, 3);
});
test('late preferences initialization after destruction cannot change window or store', async () => {
  const gate = deferred(), h = setup({ preferencesGate: gate }); const starting = h.start(); await tick(); h.controller.destroy();
  gate.resolve(); await starting; assert.equal(h.calls.filter(call => call[0] === 'topmost' || call[0] === 'put').length, 0);
});
test('late native toggle completion after destruction cannot persist or publish stale state', async () => {
  const h = setup({ saved: false }); await h.start(); const gate = deferred(); h.options.topmostGate = gate;
  const toggling = h.controller.toggle(); await tick(); h.controller.destroy(); const count = h.states.length;
  gate.resolve(); await toggling; assert.equal(h.data.get('topmost'), false); assert.equal(h.states.length, count);
});
test('ArkUI component properties do not shadow inherited SDK CustomComponent members', async () => {
  const sdk = process.env.QGC_ETS_SDK || path.resolve(tsPath, '../../../..');
  const common = ts.createSourceFile('common.d.ts', fs.readFileSync(path.join(sdk, 'component/common.d.ts'), 'utf8'), ts.ScriptTarget.Latest, true);
  const classes = new Map(common.statements.filter(node => ts.isClassDeclaration(node) && node.name).map(node => [node.name.text, node]));
  const reserved = new Set();
  function inherit(name) {
    const declaration = classes.get(name); assert.ok(declaration, `SDK class ${name} must exist`);
    for (const member of declaration.members) if (member.name) reserved.add(member.name.getText(common));
    for (const clause of declaration.heritageClauses || []) {
      if (clause.token === ts.SyntaxKind.ExtendsKeyword) for (const type of clause.types) inherit(type.expression.getText(common));
    }
  }
  inherit('CustomComponent'); assert.ok(reserved.has('enabled'), 'SDK inherited enabled() API must be covered');
  const componentSource = ts.createSourceFile('QGCWindowTitleBar.ets', fs.readFileSync(sourcePath, 'utf8'), ts.ScriptTarget.Latest, true, ts.ScriptKind.ETS);
  const components = componentSource.statements.filter(node => node.kind === ts.SyntaxKind.StructDeclaration);
  assert.ok(components.length > 0, 'Production ArkUI components must be discovered');
  const collisions = components.flatMap(component => component.members
    .filter(member => ts.isPropertyDeclaration(member) && reserved.has(member.name.getText(componentSource)))
    .map(member => `${component.name.text}.${member.name.getText(componentSource)}`));
  assert.deepEqual(collisions, [], 'Component state/parameters must not override inherited ArkUI methods');
});
(async () => {
  const results = [];
  for (const [name, body] of tests) {
    try { await body(); console.log(`PASS ${name}`); results.push({ name, status: 'pass' }); }
    catch (error) { console.error(`FAIL ${name}\n${error.stack}`); results.push({ name, status: 'fail', error: error.message }); }
  }
  const report = { source: sourcePath, scope: 'Production ETS controller executed with controlled Window/preferences providers; ArkUI rendering requires SDK build and Pad validation', passed: results.filter(item => item.status === 'pass').length, failed: results.filter(item => item.status === 'fail').length, skipped: 0, results };
  console.log(JSON.stringify(report));
  const output = process.argv.find(arg => arg.startsWith('--output='));
  if (output) fs.writeFileSync(output.slice(9), JSON.stringify(report, null, 2) + '\n');
  process.exitCode = report.failed ? 1 : 0;
})();
