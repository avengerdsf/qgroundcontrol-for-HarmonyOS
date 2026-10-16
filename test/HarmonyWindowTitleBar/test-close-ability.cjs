'use strict';
const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');
const ts = require('D:/Progrem/Huawei/DevEco Studio/sdk/default/openharmony/ets/build-tools/ets-loader/node_modules/typescript');
const source = fs.readFileSync(process.argv[2], 'utf8');
const compiled = ts.transpileModule(source.replace(/^import .*;?\r?\n/gm, ''), {
  compilerOptions: { module: ts.ModuleKind.CommonJS, target: ts.ScriptTarget.ES2020 },
  fileName: 'QAbility.ts', reportDiagnostics: true
});
assert.equal((compiled.diagnostics || []).filter(item => item.category === ts.DiagnosticCategory.Error).length, 0);
const event = 'qgc.window.close.request';
function setup(result) {
  const listeners = new Map(), calls = [];
  const eventHub = {
    on(name, callback) { if (!listeners.has(name)) listeners.set(name, new Set()); listeners.get(name).add(callback); },
    off(name, callback) { assert.ok(listeners.get(name)?.has(callback)); listeners.get(name).delete(callback); },
    emit(name) { for (const callback of [...(listeners.get(name) || [])]) callback(); }
  };
  const context = { eventHub, getApplicationContext() { return this; }, terminateSelf() { throw Error('must not terminate Ability directly'); } };
  class UIAbility { constructor() { this.context = context; } }
  const qpa = {
    handleAbilityOnCreate() { calls.push('create'); },
    handleAbilityOnDestroy() { calls.push('destroy'); },
    handleAbilityOnPrepareToTerminate(ability) { assert.equal(ability.context, context); calls.push('qt-close-request'); return result; }
  };
  class Policy { destroy() {} }
  const exports = {};
  vm.runInNewContext(compiled.outputText, { exports, UIAbility, QGCWindowPolicy: Policy,
    QGC_WINDOW_CLOSE_REQUEST: event, QAbilityStage: { initQtAppContextIfNeeded() {} }, qpa,
    LOG_DOMAIN: 1, LOG_TAG: 'Qt', hilog: { info() {} } });
  const ability = new exports.default();
  return { ability, eventHub, listeners, calls };
}
for (const result of [true, false]) {
  const h = setup(result); h.ability.onCreate({ parameters: {} }, {}); h.eventHub.emit(event);
  assert.deepEqual(h.calls, ['create', 'qt-close-request']);
  h.eventHub.emit(event); assert.deepEqual(h.calls, ['create', 'qt-close-request', 'qt-close-request']);
  assert.equal(h.listeners.get(event).size, 1, 'Qt refusal must leave the caption close action reusable');
  console.log(`PASS staged QAbility delegates close and ignores native termination-block flag ${result}`);
}
const h = setup(true), unrelated = () => h.calls.push('unrelated');
h.eventHub.on(event, unrelated); h.ability.onCreate({ parameters: {} }, {}); h.ability.onDestroy();
assert.equal(h.listeners.get(event).size, 1); assert.ok(h.listeners.get(event).has(unrelated));
h.eventHub.emit(event); assert.deepEqual(h.calls, ['create', 'destroy', 'unrelated']);
console.log('PASS staged QAbility destroys only its own close-request listener');
