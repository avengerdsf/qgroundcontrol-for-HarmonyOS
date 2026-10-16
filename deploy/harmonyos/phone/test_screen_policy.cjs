'use strict';
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const { test } = require('node:test');
const ts = require(process.env.QGC_TYPESCRIPT || 'D:/Progrem/Huawei/DevEco Studio/sdk/default/openharmony/ets/build-tools/ets-loader/node_modules/typescript');
const source = path.resolve(__dirname, '../bridge/QGCWindowPolicy.ets');
const compiled = ts.transpileModule(fs.readFileSync(source, 'utf8'), {
  compilerOptions: { module: ts.ModuleKind.CommonJS, target: ts.ScriptTarget.ES2020 }
}).outputText;
const tick = () => new Promise(resolve => setImmediate(resolve));
function setup(deviceType = 'phone') {
  const calls = [], orientationCalls = [], logs = [], exported = {};
  const modules = {
    '@ohos.deviceInfo': { default: { deviceType } },
    '@ohos.window': { default: { Orientation: { AUTO_ROTATION_LANDSCAPE: 7 } } },
    '@ohos.file.fs': { default: { readText() { throw Error('Phone must not read desktop geometry'); } } },
    '@ohos.hilog': { default: { info() {}, warn(...args) { logs.push(args); } } }
  };
  vm.runInNewContext(compiled, { exports: exported, require(name) {
    assert.ok(modules[name], name); return modules[name];
  } });
  const policy = new exported.QGCWindowPolicy();
  function window(id = 1) { return {
    keep: false, fail: false, destroyed: false,
    async setPreferredOrientation(value) {
      assert.equal(this.destroyed, false, 'No orientation calls after native window destruction');
      orientationCalls.push([id, value]);
      if (this.orientationGate) await this.orientationGate;
      if (this.orientationError) throw this.orientationError;
      this.orientation = value;
    },
    async setWindowKeepScreenOn(value) {
      assert.equal(this.destroyed, false, 'No operations after native window destruction');
      calls.push([id, value]);
      if (this.fail) throw Error('keep screen API rejected');
      this.keep = value;
    },
    getWindowProperties() { return { isKeepScreenOn: this.keep }; }
  }; }
  return { policy, calls, orientationCalls, logs, window, stage: win => ({ getMainWindow: async () => win }) };
}
test('phone keeps the foreground screen on without desktop window APIs', async () => {
  const h = setup(), win = h.window(); let ready = 0;
  assert.equal(await h.policy.prepare(h.stage(win), () => {
    assert.equal(win.orientation, 7, 'Phone landscape must be applied before Qt starts');
    ++ready;
  }), true); await tick();
  assert.equal(ready, 1); assert.equal(win.keep, true); assert.deepEqual(h.calls, [[1, true]]);
  assert.deepEqual(h.orientationCalls, [[1, 7]]);
  h.policy.background(); await tick(); assert.equal(win.keep, false);
  h.policy.foreground(); await tick(); assert.equal(win.keep, true);
});
test('background received during window acquisition cannot enable a hidden window', async () => {
  const h = setup(), win = h.window(); let resolve, ready = 0;
  const pending = h.policy.prepare({ getMainWindow: () => new Promise(done => { resolve = done; }) }, () => ++ready);
  h.policy.background(); resolve(win); await pending; await tick();
  assert.equal(ready, 1); assert.deepEqual(h.calls, [[1, false]]);
});
test('obsolete phone stage never resumes Qt or changes a destroyed window', async () => {
  const h = setup(), win = h.window(); let resolve, ready = 0;
  const pending = h.policy.prepare({ getMainWindow: () => new Promise(done => { resolve = done; }) }, () => ++ready);
  h.policy.destroy(); win.destroyed = true; resolve(win);
  assert.equal(await pending, false); await tick(); assert.equal(ready, 0); assert.deepEqual(h.calls, []);
});
test('stage replacement releases the old phone window before enabling the new one', async () => {
  const h = setup(), first = h.window(1), second = h.window(2);
  await h.policy.prepare(h.stage(first), () => {}); await tick();
  await h.policy.prepare(h.stage(second), () => {}); await tick();
  assert.equal(first.keep, false); assert.equal(second.keep, true);
  assert.deepEqual(h.calls, [[1, true], [1, false], [2, true]]);
});
test('screen API failure does not block Qt startup and foreground can retry', async () => {
  const h = setup(), win = h.window(); win.fail = true; let ready = 0;
  await h.policy.prepare(h.stage(win), () => ++ready); await tick();
  assert.equal(ready, 1); assert.equal(h.logs.length, 1);
  win.fail = false; h.policy.foreground(); await tick(); assert.equal(win.keep, true);
});
test('queued foreground work is cancelled when its native window is destroyed', async () => {
  const h = setup(), win = h.window();
  await h.policy.prepare(h.stage(win), () => {}); await tick();
  h.policy.foreground(); h.policy.destroy(); win.destroyed = true; await tick();
  assert.deepEqual(h.calls, [[1, true]]);
});
test('unsupported device types retain their existing startup path', async () => {
  const h = setup('tv'); let ready = 0;
  await h.policy.prepare({ getMainWindow() { throw Error('Unsupported device window'); } }, () => ++ready);
  assert.equal(ready, 1); assert.deepEqual(h.calls, []);
});

test('phone orientation failure is nonfatal and screen policy still applies', async () => {
  const h = setup(), win = h.window(); let ready = 0;
  win.orientationError = Error('orientation rejected');
  assert.equal(await h.policy.prepare(h.stage(win), () => ++ready), true); await tick();
  assert.equal(ready, 1); assert.equal(win.keep, true); assert.equal(h.logs.length, 1);
});

test('main window lookup failure still resumes Qt once', async () => {
  const h = setup(); let ready = 0;
  assert.equal(await h.policy.prepare({ getMainWindow: async () => { throw Error('window unavailable'); } },
    () => ++ready), true);
  assert.equal(ready, 1); assert.equal(h.logs.length, 1);
  assert.deepEqual(h.calls, []); assert.deepEqual(h.orientationCalls, []);
});

test('Qt waits for orientation and a destroyed orientation request cannot resume Qt', async () => {
  for (const destroy of [false, true]) {
    const h = setup(), win = h.window(); let resolve, ready = 0;
    win.orientationGate = new Promise(done => { resolve = done; });
    const pending = h.policy.prepare(h.stage(win), () => ++ready); await tick();
    assert.equal(ready, 0);
    if (destroy) { h.policy.destroy(); win.destroyed = true; }
    resolve(); assert.equal(await pending, !destroy); await tick();
    assert.equal(ready, destroy ? 0 : 1);
    assert.deepEqual(h.calls, destroy ? [] : [[1, true]]);
  }
});

test('replacement waits for orientation and only resumes the latest Qt stage', async () => {
  const h = setup(), first = h.window(1), second = h.window(2); let resolve, oldReady = 0, newReady = 0;
  first.orientationGate = new Promise(done => { resolve = done; });
  const old = h.policy.prepare(h.stage(first), () => ++oldReady); await tick();
  const replacement = h.policy.prepare(h.stage(second), () => ++newReady); await tick();
  assert.deepEqual(h.orientationCalls, [[1, 7]]);
  resolve(); assert.equal(await old, false); assert.equal(await replacement, true); await tick();
  assert.equal(oldReady, 0); assert.equal(newReady, 1);
  assert.deepEqual(h.calls, [[1, false], [2, true]]);
  assert.deepEqual(h.orientationCalls, [[1, 7], [2, 7]]);
});

test('destroy cancels an old window release queued by stage replacement', async () => {
  const h = setup(), first = h.window(1), second = h.window(2);
  await h.policy.prepare(h.stage(first), () => {}); await tick();
  const replacement = h.policy.prepare(h.stage(second), () => {});
  h.policy.destroy(); first.destroyed = true; second.destroyed = true;
  assert.equal(await replacement, false); await tick();
  assert.equal(h.logs.length, 0, 'Queued release must not touch a destroyed window');
  assert.deepEqual(h.calls, [[1, true]]);
});

test('phone can prepare a new stage after destruction', async () => {
  const h = setup(), first = h.window(1), second = h.window(2);
  await h.policy.prepare(h.stage(first), () => {}); await tick();
  h.policy.destroy(); first.destroyed = true;
  h.policy.background(); h.policy.foreground();
  let ready = 0;
  assert.equal(await h.policy.prepare(h.stage(second), () => ++ready), true); await tick();
  assert.equal(ready, 1); assert.equal(second.keep, true);
  assert.deepEqual(h.orientationCalls, [[1, 7], [2, 7]]);
});
