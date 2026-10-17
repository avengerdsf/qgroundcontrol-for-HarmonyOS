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
function setup(result) {
  const calls = [];
  const context = { getApplicationContext() { return this; }, terminateSelf() { throw Error('must not terminate Ability directly'); } };
  class UIAbility { constructor() { this.context = context; } }
  const qpa = {
    handleAbilityOnCreate() { calls.push('create'); },
    handleAbilityOnDestroy() { calls.push('destroy'); },
    handleAbilityOnPrepareToTerminate(ability) { assert.equal(ability.context, context); calls.push('qt-close-request'); return result; },
    async handleAbilityOnPrepareToTerminateAsync(ability) { assert.equal(ability.context, context); calls.push('qt-close-request-async'); return result; }
  };
  class Policy { destroy() {} }
  const exports = {};
  vm.runInNewContext(compiled.outputText, { exports, UIAbility, QGCWindowPolicy: Policy,
    QAbilityStage: { initQtAppContextIfNeeded() {} }, qpa,
    LOG_DOMAIN: 1, LOG_TAG: 'Qt', hilog: { info() {} } });
  const ability = new exports.default();
  return { ability, calls };
}
(async () => {
  for (const result of [true, false]) {
    const h = setup(result); h.ability.onCreate({ parameters: {} }, {});
    assert.equal(h.ability.onPrepareToTerminate(), result);
    assert.equal(await h.ability.onPrepareToTerminateAsync(), result);
    assert.equal(h.ability.onPrepareToTerminate(), result, 'a cancelled close remains reusable');
    h.ability.onDestroy();
    assert.deepEqual(h.calls, ['create', 'qt-close-request', 'qt-close-request-async', 'qt-close-request', 'destroy']);
    console.log(`PASS native Ability close preserves synchronous and asynchronous Qt termination result ${result}`);
  }
})().catch(error => { console.error(error); process.exitCode = 1; });
