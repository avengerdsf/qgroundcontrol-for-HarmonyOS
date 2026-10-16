'use strict';

const fs = require('node:fs');
const path = require('node:path');
const tsPath = process.env.QGC_TYPESCRIPT ||
  'D:/Progrem/Huawei/DevEco Studio/sdk/default/openharmony/ets/build-tools/ets-loader/node_modules/typescript';
const ts = require(tsPath);
const sdk = process.env.QGC_ETS_SDK || path.resolve(tsPath, '../../../..');
const sourcePath = process.env.QGC_SPP_SOURCE ? path.resolve(process.env.QGC_SPP_SOURCE) :
  path.resolve(__dirname, '../../deploy/harmonyos/bridge/QGCBluetoothBridge.ets');
const normalized = file => file.split(path.sep).join('/');
const source = normalized(sourcePath + '.ts');
const basicKit = normalized(path.join(__dirname, 'spp-basic-kit.d.ts'));
const abilityKit = normalized(path.join(__dirname, 'spp-ability-kit.d.ts'));
const connectivityKit = normalized(path.join(__dirname, 'spp-connectivity-kit.d.ts'));
const virtual = new Map([
  [source, fs.readFileSync(sourcePath, 'utf8') + '\ndeclare function canIUse(capability: string): boolean;'],
  [basicKit, 'export { BusinessError } from "@ohos.base";'],
  [abilityKit, 'export { default as abilityAccessCtrl, PermissionRequestResult } from "@ohos.abilityAccessCtrl"; export { default as common } from "@ohos.app.ability.common";'],
  [connectivityKit, 'export { default as access } from "@ohos.bluetooth.access"; export { default as connection } from "@ohos.bluetooth.connection"; export { default as socket } from "@ohos.bluetooth.socket";']
]);
const kits = new Map([['@kit.BasicServicesKit', basicKit], ['@kit.AbilityKit', abilityKit], ['@kit.ConnectivityKit', connectivityKit]]);
const options = { strict: true, skipLibCheck: true, noEmit: true,
  target: ts.ScriptTarget.ES2020, module: ts.ModuleKind.CommonJS };
const host = ts.createCompilerHost(options);
const originalGetSource = host.getSourceFile.bind(host);
host.getSourceFile = (file, version, onError, again) => virtual.has(normalized(file)) ?
  ts.createSourceFile(file, virtual.get(normalized(file)), version, true) : originalGetSource(file, version, onError, again);
host.resolveModuleNames = (names, containing) => names.map(name => kits.has(name) ? { resolvedFileName: kits.get(name) } :
  name.startsWith('@ohos.') ? { resolvedFileName: path.join(sdk, 'api', name + '.d.ts') } :
    ts.resolveModuleName(name, containing, options, host).resolvedModule);
const diagnostics = ts.getPreEmitDiagnostics(ts.createProgram([source], options, host));
const errors = diagnostics.filter(item => item.category === ts.DiagnosticCategory.Error);
if (diagnostics.length) console.error(ts.formatDiagnosticsWithColorAndContext(diagnostics, {
  getCanonicalFileName: file => file, getCurrentDirectory: () => process.cwd(), getNewLine: () => '\n'
}));
console.log(`SPP SDK declaration type check: ${errors.length} errors, ${diagnostics.length - errors.length} warnings`);
process.exitCode = errors.length ? 1 : 0;
