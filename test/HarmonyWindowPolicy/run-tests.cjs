'use strict';

const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const tsPath = process.env.QGC_TYPESCRIPT || 'D:/Progrem/Huawei/DevEco Studio/sdk/default/openharmony/ets/build-tools/ets-loader/node_modules/typescript';
const ts = require(tsPath);
const sourcePath = path.resolve(__dirname, '../../deploy/harmonyos/bridge/QGCWindowPolicy.ets');
assert.ok(fs.existsSync(sourcePath), 'QGCWindowPolicy must implement tablet immersive window handling');

if (process.argv.includes('--sdk-types')) {
  const virtual = (sourcePath + '.ts').replaceAll('\\', '/');
  const sdk = process.env.QGC_ETS_SDK || path.resolve(tsPath, '../../../..');
  const options = { strict: true, skipLibCheck: true, noEmit: true, target: ts.ScriptTarget.ES2020, module: ts.ModuleKind.CommonJS };
  const host = ts.createCompilerHost(options), getSource = host.getSourceFile.bind(host);
  host.getSourceFile = (file, version, onError, again) => file.replaceAll('\\', '/') === virtual ?
    ts.createSourceFile(file, fs.readFileSync(sourcePath, 'utf8'), version, true) : getSource(file, version, onError, again);
  host.resolveModuleNames = (names, containing) => names.map(name => name.startsWith('@ohos.') ?
    { resolvedFileName: path.join(sdk, 'api', name + '.d.ts') } : ts.resolveModuleName(name, containing, options, host).resolvedModule);
  const diagnostics = ts.getPreEmitDiagnostics(ts.createProgram([virtual], options, host));
  const errors = diagnostics.filter(d => d.category === ts.DiagnosticCategory.Error);
  if (errors.length) console.error(ts.formatDiagnosticsWithColorAndContext(errors, {
    getCanonicalFileName: file => file, getCurrentDirectory: () => process.cwd(), getNewLine: () => '\n'
  }));
  for (const diagnostic of diagnostics.filter(d => d.category !== ts.DiagnosticCategory.Error)) {
    const location = diagnostic.file && diagnostic.start !== undefined ? diagnostic.file.getLineAndCharacterOfPosition(diagnostic.start) : null;
    console.log(`SDK warning ${diagnostic.code}${location ? ` at ${location.line + 1}:${location.character + 1}` : ''}: ${ts.flattenDiagnosticMessageText(diagnostic.messageText, '\n')}`);
  }
  console.log(`SDK declaration type check: ${errors.length} errors, ${diagnostics.length - errors.length} warnings`);
  process.exit(errors.length ? 1 : 0);
}

const compiled = ts.transpileModule(fs.readFileSync(sourcePath, 'utf8'), {
  compilerOptions: { module: ts.ModuleKind.CommonJS, target: ts.ScriptTarget.ES2020 },
  fileName: 'QGCWindowPolicy.ts', reportDiagnostics: true
});
assert.equal((compiled.diagnostics || []).filter(d => d.category === ts.DiagnosticCategory.Error).length, 0);
const status = { UNDEFINED: 0, FULL_SCREEN: 1, MAXIMIZE: 2, MINIMIZE: 3, FLOATING: 4, SPLIT_SCREEN: 5 };
function deferred() {
  let resolve, reject;
  const promise = new Promise((yes, no) => { resolve = yes; reject = no; });
  return { promise, resolve, reject };
}
const tick = () => new Promise(resolve => setImmediate(resolve));
function setup(options = {}) {
  const state = { deviceType: 'tablet', logs: [], infoLogs: [], deviceLogs: [], calls: [], hoverCalls: [], readPaths: [],
    ini: '[MainWindowState]\nwidth=1200\nheight=800\nvisibility=5\n', ...options };
  const exported = {};
  const modules = {
    '@ohos.window': { default: { WindowStatusType: status, MaximizePresentation: { ENTER_IMMERSIVE: 2, EXIT_IMMERSIVE: 1 }, RectChangeReason: { MAXIMIZE: 1, RECOVER: 2 }, Orientation: { AUTO_ROTATION_LANDSCAPE: 7 } } },
    '@ohos.file.fs': { default: { async readText(file) {
      state.readPaths.push(file);
      if (state.readGate) await state.readGate.promise;
      if (state.readError) throw state.readError;
      return state.ini;
    } } },
    '@ohos.deviceInfo': { default: state },
    '@ohos.hilog': { default: { info: (...args) => {
      assert.ok(Number.isInteger(args[0]) && args[0] >= 0 && args[0] <= 0xffff);
      assert.equal(args[1], 'QGCWindowPolicy');
      if (args[2] === 'deviceType=%{public}s') {
        state.deviceLogs.push(args[3]);
        return;
      }
      assert.ok(args.slice(3).every(value => typeof value === 'number'), 'transition diagnostics must contain only numeric values');
      state.infoLogs.push(args);
    }, warn: (...args) => {
      assert.ok(Number.isInteger(args[0]) && args[0] >= 0 && args[0] <= 0xffff, 'hilog application domain must be within 0..0xffff');
      state.logs.push(args);
    } } }
  };
  vm.runInNewContext(compiled.outputText, { exports: exported, require: name => {
    assert.ok(modules[name], `Unexpected runtime module ${name}`);
    return modules[name];
  }, console });
  const policy = new exported.QGCWindowPolicy();
  function window(overrides = {}) {
    const listeners = new Map();
    const win = { mode: status.FULL_SCREEN, layout: false, bars: ['status', 'navigation'], rect: { left: 0, top: 0, width: 2880, height: 1920 }, listeners,
      getWindowStatus() { if (this.statusError) throw this.statusError; return this.mode; },
      getImmersiveModeEnabledState() { if (this.baselineError) throw this.baselineError; return this.layout; },
      isImmersiveLayout() { return this.immersive === undefined ? this.layout : this.immersive; },
      getWindowProperties() { return { windowRect: this.rect, isKeepScreenOn: this.keepScreenOn }; },
      async setPreferredOrientation(orientation) {
        state.calls.push(['orientation', orientation, this]);
        if (this.orientationGate) await this.orientationGate.promise;
        this.orientation = orientation;
      },
      async setWindowKeepScreenOn(enabled) {
        state.calls.push(['keep-screen', enabled, this]);
        this.keepScreenOn = enabled;
      },
      async maximize(presentation) {
        state.calls.push(['maximize', presentation, this]);
        if (this.maximizeGate) { const gate = this.maximizeGate; this.maximizeGate = null; await gate.promise; }
        if (this.maximizeError) throw this.maximizeError;
        this.layout = presentation === 2;
        this.change(presentation === 2 ? status.FULL_SCREEN : status.MAXIMIZE);
      },
      async recover() {
        state.calls.push(['recover', this]);
        if (this.recoverError) throw this.recoverError;
        this.layout = false;
        this.change(status.FLOATING);
      },
      async setWindowLayoutFullScreen(layout) {
        state.calls.push(['layout', layout, this]);
        if (this.layoutGate) { const gate = this.layoutGate; this.layoutGate = null; await gate.promise; }
        if (this.layoutError) throw this.layoutError;
        this.layout = layout;
      },
      async setWindowSystemBarEnable(bars) {
        state.calls.push(['bars', [...bars], this]);
        if (this.barGate) { const gate = this.barGate; this.barGate = null; await gate.promise; }
        if (this.barError) throw this.barError;
        this.bars = [...bars];
      },
      async setTitleAndDockHoverShown(title, dock) {
        state.hoverCalls.push([title, dock]);
        if (this.hoverError) throw Error('hover unsupported');
        this.titleHover = title;
        this.dockHover = dock;
      },
      on(event, callback) { if (this.listenError || this.listenErrorEvent === event) throw new Error('listen failed'); assert.ok(!listeners.has(event)); listeners.set(event, callback); },
      off(event, callback) { assert.equal(listeners.get(event), callback); listeners.delete(event); },
      emit(event, value) { if (listeners.has(event)) listeners.get(event)(value); },
      change(mode) { this.mode = mode; this.emit('windowStatusDidChange', mode); },
      ...overrides };
    return win;
  }
  const context = { preferencesDir: '/module/preferences', getApplicationContext: () => ({ preferencesDir: '/application/preferences' }) };
  return { policy, state, window, context, prepare: stage => policy.prepare(stage, () => {}), stage: win => ({ getMainWindow: async () => win }) };
}
const tests = [];
function test(name, body) { tests.push([name, body]); }

test('tablet title button promotes completed floating to maximized into native immersion', async () => {
  const h = setup(), win = h.window({ mode: status.FLOATING });
  await h.prepare(h.stage(win));
  win.emit('windowStatusChange', status.MAXIMIZE);
  win.emit('windowRectChange', { reason: 1, rect: win.rect });
  assert.equal(h.state.calls.length, 0, 'early and rectangle notifications must not promote');
  win.change(status.MAXIMIZE);
  await tick();
  assert.equal(h.state.calls.filter(call => call[0] === 'maximize').length, 1, 'completed floating to maximized must enter native immersion once');
  assert.deepEqual(h.state.calls[0].slice(0, 2), ['maximize', 2]);
  assert.equal(win.mode, status.FULL_SCREEN);
  assert.deepEqual(win.bars, []);
  win.change(status.FLOATING); await tick();
  assert.equal(win.layout, false, 'button promotion must restore the pre-entry layout baseline');
  assert.deepEqual(win.bars, ['status', 'navigation']);
  h.policy.destroy();
});
test('queued title button promotion cancels if user recovers before native entry starts', async () => {
  const h = setup(), win = h.window(), gate = deferred();
  await h.prepare(h.stage(win));
  win.barGate = gate;
  win.change(status.FLOATING); await tick();
  win.change(status.MAXIMIZE);
  win.change(status.FLOATING);
  gate.resolve(); await tick();
  assert.equal(h.state.calls.filter(call => call[0] === 'maximize').length, 0);
  assert.equal(win.mode, status.FLOATING);
  assert.equal(win.layout, false);
  assert.deepEqual(win.bars, ['status', 'navigation']);
  h.policy.destroy();
});
test('duplicate maximized completion and foreground cannot duplicate pending button entry', async () => {
  const h = setup(), gate = deferred(), win = h.window({ mode: status.FLOATING, maximizeGate: gate });
  await h.prepare(h.stage(win));
  win.change(status.MAXIMIZE);
  win.emit('windowStatusDidChange', status.MAXIMIZE);
  h.policy.foreground(); await tick();
  win.emit('windowStatusDidChange', status.MAXIMIZE);
  assert.equal(h.state.calls.filter(call => call[0] === 'maximize').length, 1);
  gate.resolve(); await tick();
  assert.equal(h.state.calls.filter(call => call[0] === 'maximize').length, 1);
  assert.deepEqual(win.bars, []);
  h.policy.destroy();
});
test('full screen exit initial maximized restore split and foreground never promote', async () => {
  const h = setup(), win = h.window();
  await h.prepare(h.stage(win));
  win.change(status.MAXIMIZE); await tick(); h.policy.foreground(); await tick();
  await h.prepare(h.stage(win));
  win.emit('windowStatusDidChange', status.MAXIMIZE); await tick();
  win.change(status.SPLIT_SCREEN); win.change(status.MAXIMIZE); await tick();
  assert.equal(h.state.calls.filter(call => call[0] === 'maximize').length, 0);
  assert.deepEqual(win.bars, ['status', 'navigation']);
  h.policy.destroy();
});
test('failed button native entry logs once without retrying repeated maximized status', async () => {
  const h = setup(), win = h.window({ mode: status.FLOATING, maximizeError: new Error('entry failed') });
  await h.prepare(h.stage(win));
  win.change(status.MAXIMIZE); await tick();
  win.emit('windowStatusDidChange', status.MAXIMIZE);
  h.policy.foreground(); await tick();
  assert.equal(h.state.calls.filter(call => call[0] === 'maximize').length, 1);
  assert.ok(h.state.logs.length > 0);
  assert.equal(win.mode, status.MAXIMIZE);
  assert.equal(win.layout, false);
  assert.deepEqual(win.bars, ['status', 'navigation']);
  h.policy.destroy();
});
test('stage replacement cancels queued button entry without affecting the new window', async () => {
  const h = setup(), oldWin = h.window(), gate = deferred(), nextWin = h.window({ mode: status.FLOATING });
  await h.prepare(h.stage(oldWin));
  oldWin.barGate = gate;
  oldWin.change(status.FLOATING); await tick();
  oldWin.change(status.MAXIMIZE);
  const replacement = h.prepare(h.stage(nextWin));
  gate.resolve();
  assert.equal(await replacement, true);
  assert.equal(h.state.calls.filter(call => call[0] === 'maximize').length, 0);
  assert.equal(oldWin.listeners.size, 0);
  assert.equal(nextWin.mode, status.FLOATING);
  assert.deepEqual(nextWin.bars, ['status', 'navigation']);
  h.policy.destroy();
});
test('diagnostics observe same-status presentation changes without rehiding or maximizing', async () => {
  const h = setup(), win = h.window();
  await h.prepare(h.stage(win));
  h.state.infoLogs.length = 0;
  const calls = h.state.calls.length;
  win.layout = true;
  win.immersive = false;
  win.rect = { left: 0, top: 78, width: 2880, height: 1842 };
  win.bars = ['status', 'navigation'];
  win.emit('windowStatusChange', status.FULL_SCREEN);
  win.emit('windowRectChange', { reason: 1, rect: win.rect });
  await tick();
  assert.equal(h.state.infoLogs.length, 2, 'pre-status and maximize rectangle diagnostics must capture the missing DidChange path');
  assert.deepEqual(h.state.infoLogs[0].slice(3), [1, 1, 1, 1, 0, 0, 78, 2880, 1842]);
  assert.deepEqual(h.state.infoLogs[1].slice(3), [3, 1, 1, 1, 0, 0, 78, 2880, 1842]);
  assert.equal(h.state.calls.length, calls);
  assert.deepEqual(win.bars, ['status', 'navigation']);
  const count = h.state.infoLogs.length;
  win.emit('windowRectChange', { reason: 1, rect: win.rect });
  for (const reason of [0, 3, 4, 5, 6]) win.emit('windowRectChange', { reason, rect: win.rect });
  win.emit('avoidAreaChange', {});
  assert.equal(h.state.infoLogs.length, count, 'duplicate rectangles dragging and edge avoid areas must stay quiet');
  h.policy.destroy();
  assert.equal(win.listeners.size, 0);
});
test('diagnostic listeners detach on replacement and optional subscription failure leaves policy available', async () => {
  const h = setup(), win = h.window();
  await h.prepare(h.stage(win));
  const callbacks = [...win.listeners.values()];
  await h.prepare(h.stage(h.window()));
  assert.equal(win.listeners.size, 0);
  const count = h.state.infoLogs.length;
  for (const callback of callbacks) callback({ reason: 1, rect: win.rect });
  assert.equal(h.state.infoLogs.length, count, 'obsolete listeners must not log after stage replacement');
  h.policy.destroy();
  for (const event of ['windowStatusChange', 'windowRectChange']) {
    const next = setup(), nextWin = next.window({ listenErrorEvent: event });
    assert.equal(await next.prepare(next.stage(nextWin)), true);
    assert.deepEqual(nextWin.bars, []);
    next.policy.destroy();
    assert.equal(nextWin.listeners.size, 0);
  }
});
test('tablet cold creation enters native immersion before Qt content starts', async () => {
  for (const mode of [status.FLOATING, status.MAXIMIZE, status.FULL_SCREEN]) {
    const h = setup(), win = h.window({ mode });
    assert.equal(await h.policy.prepare(h.stage(win), () => h.state.calls.push(['qt-ready']), h.context), true);
    assert.equal(h.state.calls.filter(call => call[0] === 'maximize').length, mode === status.FULL_SCREEN ? 0 : 1);
    assert.deepEqual(h.state.calls.at(-1), ['qt-ready']);
    assert.equal(win.mode, status.FULL_SCREEN);
    assert.deepEqual(win.bars, []);
    const count = h.state.calls.filter(call => call[0] === 'maximize').length;
    win.change(status.FLOATING); await tick();
    h.policy.foreground(); await tick();
    assert.equal(win.mode, status.FLOATING);
    assert.equal(win.layout, false, 'native immersive entry must not replace the original layout baseline');
    assert.equal(h.state.calls.filter(call => call[0] === 'maximize').length, count);
    assert.deepEqual(win.bars, ['status', 'navigation']);
    h.policy.destroy();
  }
});
test('cold creation waits for native immersion and ignores a destroyed preparation', async () => {
  for (const destroy of [false, true]) {
    const h = setup(), gate = deferred(), win = h.window({ mode: status.MAXIMIZE, maximizeGate: gate });
    let qtReady = 0;
    const ready = h.policy.prepare(h.stage(win), () => { ++qtReady; }, h.context);
    await tick();
    assert.equal(qtReady, 0);
    if (destroy) h.policy.destroy();
    gate.resolve();
    assert.equal(await ready, !destroy);
    assert.equal(qtReady, destroy ? 0 : 1);
    if (destroy) {
      assert.equal(win.listeners.size, 0);
      assert.equal(h.state.calls.filter(call => call[0] === 'bars').length, 0);
    } else {
      assert.deepEqual(win.bars, []);
      h.policy.destroy();
    }
  }
});
test('native startup mode failure still starts Qt once without retry', async () => {
  for (const deviceType of ['tablet', '2in1']) {
    const mode = status.FLOATING;
    const h = setup({ deviceType }), win = h.window({ mode, maximizeError: Object.assign(new Error('maximize failed'), { code: 801 }) });
    let qtReady = 0;
    assert.equal(await h.policy.prepare(h.stage(win), () => { ++qtReady; }, h.context), true);
    assert.equal(qtReady, 1);
    assert.ok(h.state.logs.length > 0);
    assert.equal(win.mode, mode);
    assert.equal(h.state.calls.filter(call => call[0] === 'maximize').length, 1);
    assert.equal(h.state.calls.filter(call => call[0] === 'bars' && call[1].length === 0).length, 0);
    h.policy.destroy();
  }
});

test('startup reads only the application Qt INI and restores each saved mode before Qt', async () => {
  for (const deviceType of ['tablet', '2in1']) {
    for (const [visibility, target, initial] of [[2, status.FLOATING, status.MAXIMIZE], [4, status.MAXIMIZE, status.FLOATING], [5, status.FULL_SCREEN, status.FLOATING]]) {
      const h = setup({ deviceType, ini: `\ufeff[Other]\nwidth=0\nvisibility=2\n[MainWindowState]\r\nx=-500\r\ny=23\r\nwidth=1200\r\nheight=800\r\nvisibility=${visibility}\r\n[Other]\nvisibility=5` });
      const win = h.window({ mode: initial });
      await h.policy.prepare(h.stage(win), () => { assert.equal(win.mode, target); h.state.calls.push(['qt-ready']); }, h.context);
      assert.deepEqual(h.state.readPaths, ['/application/preferences/QGroundControl/QGroundControl.ini']);
      assert.equal(h.state.calls.filter(call => call[0] === 'maximize' || call[0] === 'recover').length, 1);
      assert.deepEqual(h.state.calls.at(-1), ['qt-ready']);
      assert.equal(win.mode, target, 'saved Maximize must not be promoted to FullScreen');
      if (deviceType === '2in1') assert.equal(h.state.calls.filter(call => ['bars', 'layout'].includes(call[0])).length, 0);
      h.policy.destroy();
    }
  }
});

test('matching native startup mode makes no mode or geometry writes', async () => {
  for (const deviceType of ['tablet', '2in1']) for (const [visibility, mode] of [[2, status.FLOATING], [4, status.MAXIMIZE], [5, status.FULL_SCREEN]]) {
    const h = setup({ deviceType, ini: `[MainWindowState]\nwidth=1200\nheight=800\nvisibility=${visibility}` }), win = h.window({ mode });
    await h.policy.prepare(h.stage(win), () => {}, h.context);
    assert.equal(h.state.calls.filter(call => call[0] === 'maximize' || call[0] === 'recover').length, 0);
    assert.deepEqual(win.rect, { left: 0, top: 0, width: 2880, height: 1920 });
    h.policy.destroy();
  }
});

test('startup INI validation matches Qt defaults and ignores other categories', async () => {
  for (const deviceType of ['tablet', '2in1']) {
    const fallback = deviceType === 'tablet' ? status.FULL_SCREEN : status.FLOATING;
    for (const ini of ['', '[Other]\nwidth=100\nheight=100\nvisibility=5', '[MainWindowState]\nwidth=-1\nheight=800\nvisibility=5', '[MainWindowState]\nwidth=1200\nheight=NaN\nvisibility=5', '[MainWindowState]\nwidth=Infinity\nheight=800\nvisibility=5']) {
      const h = setup({ deviceType, ini }), win = h.window({ mode: status.FLOATING });
      await h.policy.prepare(h.stage(win), () => assert.equal(win.mode, fallback), h.context); h.policy.destroy();
    }
    for (const visibility of ['', '0', '1', '3', 'NaN', '100']) {
      const h = setup({ deviceType, ini: `[MainWindowState]\nwidth=1200\nheight=800\nvisibility=${visibility}` }), win = h.window({ mode: status.FULL_SCREEN });
      await h.policy.prepare(h.stage(win), () => assert.equal(win.mode, status.FLOATING), h.context); h.policy.destroy();
    }
  }
});

test('missing INI uses desktop device defaults and phone skips saved desktop geometry', async () => {
  for (const deviceType of ['tablet', '2in1']) {
    const h = setup({ deviceType, readError: new Error('file missing') }), win = h.window({ mode: status.FLOATING });
    await h.policy.prepare(h.stage(win), () => {}, h.context);
    assert.equal(win.mode, deviceType === 'tablet' ? status.FULL_SCREEN : status.FLOATING); h.policy.destroy();
  }
  const h = setup({ deviceType: 'phone' }), win = h.window(); let ready = 0;
  await h.policy.prepare(h.stage(win), () => ++ready, h.context); await tick();
  assert.equal(ready, 1); assert.equal(h.state.readPaths.length, 0);
  assert.deepEqual(h.state.calls.map(call => call.slice(0, 2)), [['orientation', 7], ['keep-screen', true]]);
  h.policy.destroy();
});

test('destroy or replacement while startup INI is pending cancels the obsolete window', async () => {
  for (const replacement of [false, true]) {
    const gate = deferred(), h = setup({ readGate: gate }), win = h.window({ mode: status.FLOATING }); let ready = 0;
    const old = h.policy.prepare(h.stage(win), () => ++ready, h.context); await tick();
    if (replacement) { h.state.readGate = null; await h.prepare(h.stage(h.window({ mode: status.FLOATING })));
    } else h.policy.destroy();
    gate.resolve(); assert.equal(await old, false); assert.equal(ready, 0);
    assert.equal(h.state.calls.filter(call => call[0] === 'maximize' || call[0] === 'recover').length, 0);
    assert.equal(win.listeners.size, 0); h.policy.destroy();
  }
});

test('replacement waits for an in-flight startup mode before applying its own saved mode', async () => {
  const gate = deferred(), h = setup({ deviceType: '2in1' }), win = h.window({ mode: status.FLOATING, maximizeGate: gate });
  let obsoleteReady = 0, nextReady = 0;
  const old = h.policy.prepare(h.stage(win), () => ++obsoleteReady, h.context); await tick();
  h.state.ini = '[MainWindowState]\nwidth=1200\nheight=800\nvisibility=2';
  const next = h.policy.prepare(h.stage(win), () => ++nextReady, h.context); await tick();
  assert.equal(nextReady, 0); gate.resolve();
  assert.equal(await old, false); assert.equal(await next, true);
  assert.equal(obsoleteReady, 0); assert.equal(nextReady, 1); assert.equal(win.mode, status.FLOATING);
  assert.deepEqual(h.state.calls.map(call => call[0]), ['maximize', 'recover']);
});

test('context and window API failures allow Qt loading once', async () => {
  for (const deviceType of ['tablet', '2in1']) {
    for (const code of [801, 1300002]) {
      const h = setup({ deviceType, ini: '[MainWindowState]\nwidth=1200\nheight=800\nvisibility=2' });
      const win = h.window({ mode: status.FULL_SCREEN, recoverError: Object.assign(Error('recover failed'), { code }) });
      let ready = 0;
      assert.equal(await h.policy.prepare(h.stage(win), () => ++ready, h.context), true);
      assert.equal(ready, 1); assert.equal(h.state.calls.filter(call => call[0] === 'recover').length, 1);
      assert.ok(h.state.logs.length); h.policy.destroy();
    }
  }
  const h = setup(), win = h.window({ mode: status.FLOATING }); let ready = 0;
  assert.equal(await h.policy.prepare(h.stage(win), () => ++ready, { getApplicationContext() { throw Error('context unavailable'); } }), true);
  assert.equal(ready, 1); h.policy.destroy();
});
test('tablet full screen hides both bars and extends layout before Qt content starts', async () => {
  const h = setup(), win = h.window();
  const active = await h.prepare(h.stage(win));
  if (active) h.state.calls.push(['qt-ready']);
  assert.equal(active, true);
  assert.deepEqual(h.state.calls.map(call => call.slice(0, 2)), [['layout', true], ['bars', []], ['qt-ready']]);
  assert.deepEqual([...win.listeners.keys()], ['windowStatusDidChange', 'windowStatusChange', 'windowRectChange']);
  h.policy.destroy();
  assert.equal(win.listeners.size, 0);
});
test('2in1 and phone never receive tablet system bar changes', async () => {
  for (const deviceType of ['2in1', 'phone']) {
    const h = setup({ deviceType }), win = h.window();
    assert.equal(await h.prepare(h.stage(win)), true);
    h.policy.foreground(); await tick();
    assert.equal(h.state.calls.filter(call => ['layout', 'bars', 'maximize', 'recover'].includes(call[0])).length, 0);
    assert.equal(win.listeners.size, 0);
    h.policy.destroy();
  }
});
test('floating split maximized and undefined windows retain their original chrome', async () => {
  for (const mode of [status.FLOATING, status.SPLIT_SCREEN, status.MAXIMIZE, status.UNDEFINED, status.MINIMIZE]) {
    const h = setup(), win = h.window({ mode });
    assert.equal(await h.prepare(h.stage(win)), true);
    assert.equal(h.state.calls.length, 0);
    h.policy.destroy();
  }
});
test('entering full screen hides chrome and leaving restores baseline layout and bars', async () => {
  for (const layout of [false, true]) {
    const h = setup(), win = h.window({ mode: status.FLOATING, layout });
    await h.prepare(h.stage(win));
    win.change(status.FULL_SCREEN); await tick();
    assert.deepEqual(win.bars, []);
    win.change(status.MAXIMIZE); await tick();
    assert.equal(win.layout, layout);
    assert.deepEqual(win.bars, ['status', 'navigation']);
    h.policy.destroy();
  }
});
test('a delayed hide cannot overwrite restoration after leaving full screen', async () => {
  const h = setup(), gate = deferred(), win = h.window({ barGate: gate });
  const ready = h.prepare(h.stage(win));
  await tick();
  win.change(status.FLOATING);
  gate.resolve();
  assert.equal(await ready, true);
  await tick();
  assert.equal(win.layout, false);
  assert.deepEqual(win.bars, ['status', 'navigation']);
  h.policy.destroy();
});
test('leaving while layout is pending skips the obsolete hide', async () => {
  const h = setup(), gate = deferred(), win = h.window({ layoutGate: gate });
  const ready = h.prepare(h.stage(win));
  await tick(); win.change(status.SPLIT_SCREEN); gate.resolve();
  await ready; await tick();
  assert.equal(h.state.calls.filter(call => call[0] === 'bars' && call[1].length === 0).length, 0);
  assert.equal(win.layout, false);
  assert.deepEqual(win.bars, ['status', 'navigation']);
  h.policy.destroy();
});
test('foreground reapplies immersion but temporary edge bars and duplicate status events do not', async () => {
  const h = setup(), win = h.window();
  await h.prepare(h.stage(win));
  const count = h.state.calls.length;
  win.bars = ['status', 'navigation'];
  win.emit('avoidAreaChange', { topRect: { height: 40 } });
  win.emit('windowStatusDidChange', status.FULL_SCREEN);
  await tick();
  assert.equal(h.state.calls.length, count);
  assert.deepEqual(win.bars, ['status', 'navigation']);
  h.policy.foreground(); await tick();
  assert.deepEqual(win.bars, []);
  h.policy.destroy();
});
test('Qt startup waits for foreground reapplication queued during initial preparation', async () => {
  const h = setup(), layoutGate = deferred(), barGate = deferred(), win = h.window({ layoutGate, barGate });
  let ready = false;
  const preparation = h.prepare(h.stage(win)).then(active => { ready = active; });
  await tick(); h.policy.foreground(); layoutGate.resolve(); await tick();
  assert.equal(ready, false, 'Qt content must wait until the current immersive request finishes');
  barGate.resolve(); await preparation;
  assert.equal(ready, true);
  assert.deepEqual(win.bars, []);
  h.policy.destroy();
});
test('destroy during main window lookup cancels Qt startup and leaves no listener', async () => {
  const h = setup(), gate = deferred(), win = h.window();
  const ready = h.prepare({ getMainWindow: () => gate.promise });
  h.policy.destroy(); h.policy.destroy(); gate.resolve(win);
  assert.equal(await ready, false);
  assert.equal(win.listeners.size, 0);
  assert.equal(h.state.calls.length, 0);
});
test('destroy during preparation detaches listener and cancels Qt startup', async () => {
  const h = setup(), gate = deferred(), win = h.window({ layoutGate: gate });
  const ready = h.prepare(h.stage(win));
  await tick(); h.policy.destroy(); gate.resolve();
  assert.equal(await ready, false);
  assert.equal(win.listeners.size, 0);
  assert.equal(h.state.calls.filter(call => call[0] === 'bars').length, 0);
});
test('restoring or replacing a stage cancels the old startup and listener', async () => {
  const h = setup(), gate = deferred(), oldWin = h.window({ barGate: gate }), nextWin = h.window();
  const oldReady = h.prepare(h.stage(oldWin));
  await tick(); const nextReady = h.prepare(h.stage(nextWin)); gate.resolve();
  assert.equal(await oldReady, false);
  assert.equal(await nextReady, true);
  assert.equal(oldWin.listeners.size, 0);
  assert.deepEqual(oldWin.bars, ['status', 'navigation']);
  assert.equal(oldWin.layout, false);
  assert.deepEqual(nextWin.bars, []);
  assert.equal(nextWin.listeners.size, 3);
  await h.prepare(h.stage(nextWin));
  assert.equal(nextWin.listeners.size, 3);
  h.policy.destroy();
  assert.equal(nextWin.listeners.size, 0);
});
test('restore reusing a window waits out old requests and preserves its original layout baseline', async () => {
  for (const gateProperty of ['layoutGate', 'barGate']) {
    const h = setup(), gate = deferred(), win = h.window({ [gateProperty]: gate });
    const oldStage = { getMainWindow: async () => win, getMainWindowSync: () => win };
    const newStage = { getMainWindow: async () => win, getMainWindowSync: () => win };
    let oldQtReady = 0, newQtReady = 0;
    const oldReady = h.policy.prepare(oldStage, () => { ++oldQtReady; });
    await tick();
    win.mode = status.FLOATING;
    const nextReady = h.policy.prepare(newStage, () => { ++newQtReady; });
    gate.resolve();
    assert.equal(await oldReady, false);
    assert.equal(await nextReady, true);
    assert.equal(oldQtReady, 0);
    assert.equal(newQtReady, 1);
    assert.equal(win.listeners.size, 3);
    assert.equal(win.layout, false);
    assert.deepEqual(win.bars, ['status', 'navigation']);
    win.change(status.FULL_SCREEN); await tick();
    win.change(status.MAXIMIZE); await tick();
    assert.equal(win.layout, false, 'old policy immersion must not become the new baseline');
    assert.deepEqual(win.bars, ['status', 'navigation']);
    h.policy.destroy();
    assert.equal(win.listeners.size, 0);
  }
});
test('failed status subscription keeps Qt available and never enables unmonitored immersion', async () => {
  const h = setup(), win = h.window({ listenError: new Error('listen failed') });
  let qtReady = 0;
  assert.equal(await h.policy.prepare(h.stage(win), () => { ++qtReady; }), true);
  assert.equal(qtReady, 1);
  assert.equal(win.listeners.size, 0);
  h.policy.foreground(); await tick();
  win.change(status.FLOATING); await tick();
  assert.equal(h.state.calls.length, 0, 'a session without a status listener must never change layout or bars');
  assert.equal(win.layout, false);
  assert.deepEqual(win.bars, ['status', 'navigation']);
  h.policy.destroy();
});
test('a failed stale window lookup cannot detach the replacement session', async () => {
  const h = setup(), gate = deferred(), win = h.window();
  let oldQtReady = 0;
  const oldReady = h.policy.prepare({ getMainWindow: () => gate.promise }, () => { ++oldQtReady; });
  assert.equal(await h.prepare(h.stage(win)), true);
  gate.reject(new Error('stale lookup failed'));
  assert.equal(await oldReady, false);
  assert.equal(oldQtReady, 0);
  assert.equal(win.listeners.size, 3);
  win.change(status.FLOATING); await tick();
  assert.equal(win.layout, false);
  assert.deepEqual(win.bars, ['status', 'navigation']);
  h.policy.destroy();
});
test('window failures are logged and allow Qt startup', async () => {
  for (const options of [{ statusError: new Error('status') }, { baselineError: new Error('baseline') },
    { layoutError: new Error('layout') }, { barError: new Error('bars') }, { listenError: new Error('listen') }]) {
    const h = setup(), win = h.window(options);
    assert.equal(await h.prepare(h.stage(win)), true);
    assert.ok(h.state.logs.length > 0);
    h.policy.destroy();
  }
  const h = setup();
  assert.equal(await h.prepare({ getMainWindow: async () => { throw new Error('main window'); } }), true);
  assert.ok(h.state.logs.length > 0);
});
test('failed restoration still attempts both settings and can recover on foreground', async () => {
  for (const property of ['barError', 'layoutError']) {
    const h = setup(), win = h.window();
    await h.prepare(h.stage(win));
    win[property] = new Error('restore failed');
    const count = h.state.calls.length;
    win.change(status.FLOATING); await tick();
    assert.deepEqual(h.state.calls.slice(count).map(call => call[0]), ['bars', 'layout']);
    assert.ok(h.state.logs.length > 0);
    win[property] = null;
    h.policy.foreground(); await tick();
    assert.equal(win.layout, false);
    assert.deepEqual(win.bars, ['status', 'navigation']);
    h.policy.destroy();
  }
});

if (process.argv.includes('--ability-fixture')) {
  const abilitySource = fs.readFileSync(path.resolve(__dirname, '../../tmp/verification/window-policy-QAbility.ets'), 'utf8');
  const abilityCompiled = ts.transpileModule(abilitySource, {
    compilerOptions: { module: ts.ModuleKind.CommonJS, target: ts.ScriptTarget.ES2020 },
    fileName: 'QAbility.ts', reportDiagnostics: true
  });
  assert.equal((abilityCompiled.diagnostics || []).filter(d => d.category === ts.DiagnosticCategory.Error).length, 0);
  function ability(h) {
    const exported = {}, forwarded = [];
    class UIAbility { constructor() { this.launchWant = { parameters: {} }; this.context = h.context; } }
    const qpa = Object.fromEntries(['WindowStageCreate', 'WindowStageRestore', 'Foreground', 'Background', 'WindowStageDestroy', 'Destroy']
      .map(event => ['handleAbilityOn' + event, (...args) => forwarded.push([event, ...args])]));
    const modules = {
      '@ohos.app.ability.UIAbility': { default: UIAbility },
      '../qabilitystage/QAbilityStage': { default: {} },
      '@ohos.hilog': { default: { info: () => {} } },
      'libqohos.so': { default: qpa },
      '../common/QtAppConstants': { LOG_DOMAIN: 1, LOG_TAG: 'Qt' },
      './QGCWindowPolicy': { QGCWindowPolicy: h.policy.constructor }
    };
    vm.runInNewContext(abilityCompiled.outputText, { exports: exported, require: name => {
      assert.ok(modules[name], `Unexpected ability runtime module ${name}`); return modules[name];
    } });
    return { instance: new exported.default(), forwarded };
  }
  test('staged QAbility create waits for policy and foreground while preparation is pending', async () => {
    const h = setup(), a = ability(h), gate = deferred(), win = h.window({ barGate: gate });
    a.instance.onWindowStageCreate(h.stage(win)); await tick();
    assert.equal(a.forwarded.length, 0);
    assert.deepEqual(h.state.calls[0].slice(0, 2), ['layout', true]);
    a.instance.onForeground(); gate.resolve(); await tick();
    assert.deepEqual(win.bars, []);
    assert.deepEqual(a.forwarded.map(call => call[0]), ['Foreground', 'WindowStageCreate']);
    a.instance.onWindowStageDestroy(); a.instance.onDestroy();
    assert.equal(win.listeners.size, 0);
  });
  test('staged QAbility drops old create on restore and cancels a destroyed restore', async () => {
    const h = setup(), a = ability(h), gate = deferred(), oldWin = h.window(), nextWin = h.window();
    a.instance.onWindowStageCreate({ getMainWindow: () => gate.promise });
    a.instance.onWindowStageRestore(h.stage(nextWin)); await tick();
    gate.resolve(oldWin); await tick();
    assert.deepEqual(a.forwarded.map(call => call[0]), ['WindowStageRestore']);
    assert.equal(oldWin.listeners.size, 0);
    const restoreGate = deferred();
    a.instance.onWindowStageRestore({ getMainWindow: () => restoreGate.promise });
    a.instance.onWindowStageDestroy(); restoreGate.resolve(oldWin); await tick();
    assert.deepEqual(a.forwarded.map(call => call[0]), ['WindowStageRestore', 'WindowStageDestroy']);
    assert.equal(nextWin.listeners.size, 0);
    assert.equal(oldWin.listeners.size, 0);
  });
  test('staged QAbility still starts Qt if window policy fails', async () => {
    const h = setup(), a = ability(h);
    a.instance.onWindowStageCreate({ getMainWindow: async () => { throw new Error('window unavailable'); } });
    await tick();
    assert.deepEqual(a.forwarded.map(call => call[0]), ['WindowStageCreate']);
    assert.ok(h.state.logs.length > 0);
    a.instance.onDestroy();
  });
  test('staged create waits for saved mode and restore preserves the system current mode', async () => {
    const h = setup({ deviceType: '2in1' }), a = ability(h), gate = deferred();
    const win = h.window({ mode: status.FLOATING, maximizeGate: gate });
    a.instance.onWindowStageCreate(h.stage(win)); await tick();
    assert.equal(a.forwarded.length, 0); assert.equal(h.state.calls[0][0], 'maximize');
    gate.resolve(); await tick();
    assert.deepEqual(a.forwarded.map(call => call[0]), ['WindowStageCreate']);
    win.change(status.FLOATING); a.instance.onWindowStageRestore(h.stage(win)); await tick();
    assert.equal(win.mode, status.FLOATING); assert.equal(h.state.readPaths.length, 1);
    assert.deepEqual(a.forwarded.map(call => call[0]), ['WindowStageCreate', 'WindowStageRestore']);
    a.instance.onDestroy();
  });
  test('staged phone lifecycle waits for orientation and keeps a background window asleep', async () => {
    const h = setup({ deviceType: 'phone' }), a = ability(h), gate = deferred();
    const win = h.window({ orientationGate: gate });
    a.instance.onWindowStageCreate(h.stage(win)); await tick();
    assert.equal(a.forwarded.length, 0);
    a.instance.onBackground();
    assert.deepEqual(a.forwarded.map(call => call[0]), ['Background']);
    gate.resolve(); await tick();
    assert.equal(win.orientation, 7); assert.equal(win.keepScreenOn, false);
    assert.deepEqual(a.forwarded.map(call => call[0]), ['Background', 'WindowStageCreate']);
    a.instance.onForeground(); await tick(); assert.equal(win.keepScreenOn, true);
    a.instance.onBackground(); await tick(); assert.equal(win.keepScreenOn, false);
    a.instance.onWindowStageDestroy(); a.instance.onDestroy();
    assert.deepEqual(a.forwarded.map(call => call[0]),
      ['Background', 'WindowStageCreate', 'Foreground', 'Background', 'WindowStageDestroy', 'Destroy']);
    assert.equal(h.state.readPaths.length, 0);
    assert.equal(h.state.calls.filter(call => ['layout', 'bars', 'maximize', 'recover'].includes(call[0])).length, 0);
  });
}

test('runtime device type is logged once across stage replacement', async () => {
  for (const deviceType of ['phone', 'tablet', '2in1']) {
    const h = setup({ deviceType });
    await h.prepare(h.stage(h.window()));
    await h.prepare(h.stage(h.window()));
    assert.deepEqual(h.state.deviceLogs, [deviceType]);
    h.policy.destroy();
  }
});

test('tablet system-bar policy leaves native title hover under caption ownership', async () => {
  for (const deviceType of ['tablet', '2in1']) {
    const h = setup({ deviceType }), win = h.window(); await h.prepare(h.stage(win));
    win.change(status.FLOATING); await tick(); h.policy.foreground(); await tick();
    assert.equal(h.state.hoverCalls.length, 0);
  }
});

(async () => {
  for (const [name, body] of tests) { await body(); console.log(`PASS ${name}`); }
  console.log(`${tests.length} window policy tests passed`);
})().catch(error => { console.error(error); process.exitCode = 1; });
