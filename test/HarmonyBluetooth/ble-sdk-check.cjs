'use strict';
const fs = require('node:fs'), path = require('node:path');
const tsPath = process.env.QGC_TYPESCRIPT || 'D:/Progrem/Huawei/DevEco Studio/sdk/default/openharmony/ets/build-tools/ets-loader/node_modules/typescript';
const ts = require(tsPath), sdk = process.env.QGC_ETS_SDK || path.resolve(tsPath, '../../../..');
const source = path.resolve(__dirname, '../../deploy/harmonyos/bridge/QGCBleBridge.ets'), virtual = (source + '.ts').replaceAll('\\', '/');
const options = { strict: true, skipLibCheck: true, noEmit: true, target: ts.ScriptTarget.ES2020, module: ts.ModuleKind.CommonJS };
const host = ts.createCompilerHost(options), originalSource = host.getSourceFile.bind(host);
const kitPaths = new Map(), snippets = new Map([[virtual, fs.readFileSync(source, 'utf8') + '\ndeclare function canIUse(capability: string): boolean;']]);
for (const [name, code] of [
  ['AbilityKit', 'export { default as abilityAccessCtrl, PermissionRequestResult } from "@ohos.abilityAccessCtrl"; export {default as common} from "@ohos.app.ability.common";'],
  ['BasicServicesKit', 'export {BusinessError} from "@ohos.base";'],
  ['ConnectivityKit', 'export {default as ble} from "@ohos.bluetooth.ble"; export {default as access} from "@ohos.bluetooth.access"; export {default as constant} from "@ohos.bluetooth.constant";']
]) {
  const file = path.resolve(__dirname, `virtual-${name}.d.ts`).replaceAll('\\', '/'); kitPaths.set(`@kit.${name}`, file); snippets.set(file, code);
}
host.getSourceFile = (file, version, onError, again) => snippets.has(file.replaceAll('\\', '/')) ? ts.createSourceFile(file, snippets.get(file.replaceAll('\\', '/')), version, true) : originalSource(file, version, onError, again);
host.resolveModuleNames = (names, containing) => names.map(name => kitPaths.has(name) ? { resolvedFileName: kitPaths.get(name) } : name.startsWith('@ohos.') ? { resolvedFileName: path.join(sdk, 'api', name + '.d.ts') } : ts.resolveModuleName(name, containing, options, host).resolvedModule);
const diagnostics = ts.getPreEmitDiagnostics(ts.createProgram([virtual], options, host)), errors = diagnostics.filter(item => item.category === ts.DiagnosticCategory.Error);
if (errors.length) console.error(ts.formatDiagnosticsWithColorAndContext(errors, { getCanonicalFileName: file => file, getCurrentDirectory: () => process.cwd(), getNewLine: () => '\n' }));
for (const warning of diagnostics.filter(item => item.category !== ts.DiagnosticCategory.Error)) {
  const location = warning.file && warning.start !== undefined ? warning.file.getLineAndCharacterOfPosition(warning.start) : null;
  console.log(`SDK warning ${warning.code}${location ? ` at ${location.line + 1}:${location.character + 1}` : ''}: ${ts.flattenDiagnosticMessageText(warning.messageText, '\n')}`);
}
console.log(`BLE SDK declarations: ${errors.length} errors, ${diagnostics.length - errors.length} warnings`); process.exitCode = errors.length ? 1 : 0;
