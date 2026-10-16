const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const assert = require('node:assert/strict');
const ts = require(process.env.QGC_TYPESCRIPT ||
  'D:/Progrem/Huawei/DevEco Studio/sdk/default/openharmony/ets/build-tools/ets-loader/node_modules/typescript');
const root = path.resolve(__dirname, '../../deploy/harmonyos/bridge');

function loadDriver(name, cache = new Map()) {
  const file = path.resolve(root, name + '.ets');
  assert.ok(file.startsWith(root + path.sep), 'Driver imports must stay in the bridge directory');
  if (cache.has(file)) return cache.get(file).exports;
  const compiled = ts.transpileModule(fs.readFileSync(file, 'utf8'), {
    compilerOptions: { module: ts.ModuleKind.CommonJS, target: ts.ScriptTarget.ES2020 },
    fileName: file + '.ts', reportDiagnostics: true
  });
  assert.equal((compiled.diagnostics || []).filter(d => d.category === ts.DiagnosticCategory.Error).length, 0);
  const module = { exports: {} };
  cache.set(file, module);
  vm.runInNewContext(compiled.outputText, {
    exports: module.exports, module, Uint8Array,
    require: id => { assert.ok(id.startsWith('./')); return loadDriver(id.slice(2), cache); }
  }, { filename: file });
  return module.exports;
}

module.exports = { loadDriver };
