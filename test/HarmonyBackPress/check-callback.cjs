'use strict';
const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');
const ts = require(process.env.QGC_TYPESCRIPT ||
  'D:/Progrem/Huawei/DevEco Studio/sdk/default/openharmony/ets/build-tools/ets-loader/node_modules/typescript');
const page = fs.readFileSync(process.argv[2], 'utf8');
const callbacks = [...page.matchAll(/^  onBackPress\([^\n]*\)\s*:\s*boolean\s*\{[\s\S]*?^  \}/gm)];
assert.equal(callbacks.length, 1, 'The Qt main page must explicitly consume system Back before ArkUI default routing');
const compiled = ts.transpileModule(`class WindowNativeNode {\n${callbacks[0][0]}\n}\nmodule.exports = WindowNativeNode;`, {
  compilerOptions: { target: ts.ScriptTarget.ES2020, module: ts.ModuleKind.CommonJS }, reportDiagnostics: true
});
assert.equal((compiled.diagnostics || []).filter(item => item.category === ts.DiagnosticCategory.Error).length, 0);
const output = { exports: {} }, messages = [];
vm.runInNewContext(compiled.outputText, { module: output, exports: output.exports, console: { info: value => messages.push(value) } });
const instance = new output.exports();
assert.equal(instance.onBackPress(), true, 'SDK page callback must consume the return action');
assert.equal(instance.onBackPress(), true, 'Repeated Back must remain consumed');
assert.equal(messages.length, 2);
console.log('PASS real staged ArkTS callback consumes repeated system Back');
