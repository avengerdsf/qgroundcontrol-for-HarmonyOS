'use strict';

const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const assert = require('node:assert/strict');
const ts = require(process.env.QGC_TYPESCRIPT ||
  'D:/Progrem/Huawei/DevEco Studio/sdk/default/openharmony/ets/build-tools/ets-loader/node_modules/typescript');
const sourcePath = path.resolve(__dirname, '../../deploy/harmonyos/bridge/QGCBleBridge.ets');
const source = ts.transpileModule(fs.readFileSync(sourcePath, 'utf8'), {
  compilerOptions: { module: ts.ModuleKind.CommonJS, target: ts.ScriptTarget.ES2020 },
  fileName: sourcePath + '.ts', reportDiagnostics: true
});
assert.equal((source.diagnostics || []).filter(item => item.category === ts.DiagnosticCategory.Error).length, 0);

const NUS = { serviceUuid: '6e400001-b5a3-f393-e0a9-e50e24dcca9e', writeUuid: '6e400002-b5a3-f393-e0a9-e50e24dcca9e', notifyUuid: '6e400003-b5a3-f393-e0a9-e50e24dcca9e' };
const HM10 = { serviceUuid: '0000ffe0-0000-1000-8000-00805f9b34fb', writeUuid: '0000ffe1-0000-1000-8000-00805f9b34fb', notifyUuid: '0000ffe1-0000-1000-8000-00805f9b34fb' };
const ADDRESS = '11:22:33:44:55:66';
const CCCD = '00002902-0000-1000-8000-00805f9b34fb';
function deferred() {
  let resolve, reject;
  const promise = new Promise((yes, no) => { resolve = yes; reject = no; });
  return { promise, resolve, reject };
}
async function flush() { for (let i = 0; i < 50; i++) await Promise.resolve(); }
function clock() {
  let now = 0, next = 1;
  const jobs = new Map();
  return {
    jobs, setTimeout(callback, delay = 0) { const id = next++; jobs.set(id, { callback, due: now + delay }); return id; },
    clearTimeout(id) { jobs.delete(id); },
    advance(milliseconds) {
      const target = now + milliseconds;
      while (true) {
        const ready = [...jobs].filter(([, job]) => job.due <= target).sort((a, b) => a[1].due - b[1].due || a[0] - b[0]);
        if (!ready.length) break;
        const [id, job] = ready[0]; jobs.delete(id); now = job.due; job.callback();
      }
      now = target;
    }
  };
}
function events() {
  const listeners = new Map(), removed = [];
  return {
    listeners, removed,
    on(type, callback) { if (!listeners.has(type)) listeners.set(type, new Set()); listeners.get(type).add(callback); },
    off(type, callback) { removed.push([type, callback]); if (listeners.has(type)) { listeners.get(type).delete(callback); if (!listeners.get(type).size) listeners.delete(type); } },
    emit(type, value) { for (const callback of [...(listeners.get(type) || [])]) callback(value); }
  };
}
function services(options = NUS, properties = {}) {
  const write = { serviceUuid: options.serviceUuid, characteristicUuid: options.writeUuid, characteristicValue: new ArrayBuffer(0), descriptors: [], properties: { write: true } };
  const notify = { serviceUuid: options.serviceUuid, characteristicUuid: options.notifyUuid, characteristicValue: new ArrayBuffer(0),
    descriptors: [{ serviceUuid: options.serviceUuid, characteristicUuid: options.notifyUuid, descriptorUuid: CCCD, descriptorValue: new ArrayBuffer(2) }], properties: { notify: true } };
  if (options.writeUuid === options.notifyUuid) { write.descriptors = notify.descriptors; write.properties = { write: true, notify: true, ...properties }; return [{ serviceUuid: options.serviceUuid, isPrimary: true, characteristics: [write] }]; }
  return [{ serviceUuid: options.serviceUuid, isPrimary: true, characteristics: [{ ...write, properties: { ...write.properties, ...(properties.write || {}) } }, { ...notify, properties: { ...notify.properties, ...(properties.notify || {}) } }] }];
}
function setup(options = {}) {
  const state = { supported: true, power: 2, grant: true, calls: [], clients: [], scanners: [], services: services(), autoMtu: 23, ...options };
  const time = clock(), accessEvents = events();
  const access = { ...accessEvents, BluetoothState: { STATE_ON: 2, STATE_BLE_ON: 5 }, getState: () => state.power };
  const kit = {
    access,
    constant: { ProfileConnectionState: { STATE_DISCONNECTED: 0, STATE_CONNECTING: 1, STATE_CONNECTED: 2, STATE_DISCONNECTING: 3 } },
    ble: {
      GattWriteType: { WRITE: 1, WRITE_NO_RESPONSE: 2 }, ScanReportType: { ON_FOUND: 1, ON_LOST: 2, ON_BATCH: 3 },
      createBleScanner() {
        const scanner = { ...events(), async startScan(filters) { state.calls.push(['scan-start', scanner, filters]); if (state.scanGate) await state.scanGate.promise; },
          async stopScan() { state.calls.push(['scan-stop', scanner]); } };
        state.scanners.push(scanner); return scanner;
      },
      createGattClientDevice(address) {
        const client = { ...events(), address,
          connect() { state.calls.push(['connect', client]); if (state.connectError) throw state.connectError; },
          disconnect() { state.calls.push(['disconnect', client]); }, close() { state.calls.push(['close', client]); },
          getServices() { state.calls.push(['services', client]); return state.servicesGate ? state.servicesGate.promise : Promise.resolve(state.services); },
          setBLEMtuSize(mtu) { state.calls.push(['mtu', client, mtu]); if (state.mtuError) throw state.mtuError; if (state.autoMtu) client.emit('BLEMtuChange', state.autoMtu); },
          setCharacteristicChangeNotification(characteristic, enabled) { state.calls.push(['notify', client, characteristic, enabled]); return state.subscribeGate ? state.subscribeGate.promise : state.subscribeError ? Promise.reject(state.subscribeError) : Promise.resolve(); },
          setCharacteristicChangeIndication(characteristic, enabled) { state.calls.push(['indicate', client, characteristic, enabled]); return state.subscribeGate ? state.subscribeGate.promise : state.subscribeError ? Promise.reject(state.subscribeError) : Promise.resolve(); },
          writeCharacteristicValue(characteristic, type) {
            const bytes = Array.from(new Uint8Array(characteristic.characteristicValue));
            state.calls.push(['write', client, characteristic, type, bytes]);
            return state.writeHook ? state.writeHook(bytes, type, client) : Promise.resolve();
          }
        };
        if (state.listenError) { const on = client.on; client.on = (type, callback) => { if (type === state.listenError) throw Object.assign(new Error('listener unavailable'), { code: 801 }); on(type, callback); }; }
        state.clients.push(client); return client;
      }
    }
  };
  const ability = { abilityAccessCtrl: { GrantStatus: { PERMISSION_GRANTED: 0 }, createAtManager: () => ({
    requestPermissionsFromUser(context, permissions) { state.calls.push(['permission', context, permissions]); return state.permissionGate ? state.permissionGate.promise : Promise.resolve({ authResults: [state.grant ? 0 : -1] }); }
  }) } };
  const output = { exports: {} };
  vm.runInNewContext(source.outputText, { module: output, exports: output.exports, Uint8Array, ArrayBuffer, Map, Set, Error, Promise,
    setTimeout: time.setTimeout, clearTimeout: time.clearTimeout, canIUse: () => state.supported,
    require: name => { if (name === '@kit.AbilityKit') return ability; if (name === '@kit.ConnectivityKit') return kit; if (name === '@kit.BasicServicesKit') return {}; throw new Error(`Unexpected module ${name}`); },
    console: { warn: (...args) => state.calls.push(['warn', ...args]), error: (...args) => state.calls.push(['error', ...args]), info: () => {} }
  }, { filename: sourcePath });
  const bridge = output.exports.getQGCBleBridge();
  return { state, bridge, clock: time, access, kit };
}
async function connected(env, sessionId = 1, address = ADDRESS, options = NUS) {
  const opening = env.bridge.open(sessionId, address, {}, options);
  await flush();
  const client = env.state.clients.at(-1); assert.ok(client, 'open must create a GATT client');
  client.emit('BLEConnectionStateChange', { deviceId: address, state: 2 });
  await flush();
  return { opening, client };
}
async function open(env, sessionId = 1, address = ADDRESS, options = NUS) {
  const result = await connected(env, sessionId, address, options);
  if (env.state.autoMtu === 0) { env.clock.advance(500); await flush(); }
  await result.opening; return result.client;
}
function notify(client, options, bytes) {
  client.emit('BLECharacteristicChange', { serviceUuid: options.serviceUuid, characteristicUuid: options.notifyUuid,
    characteristicValue: new Uint8Array(bytes).buffer, descriptors: [] });
}
module.exports = { setup, NUS, HM10, ADDRESS, CCCD, services, deferred, flush, connected, open, notify };
