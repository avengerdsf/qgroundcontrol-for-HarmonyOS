'use strict';

const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const ts = require(process.env.QGC_TYPESCRIPT ||
  'D:/Progrem/Huawei/DevEco Studio/sdk/default/openharmony/ets/build-tools/ets-loader/node_modules/typescript');
const sourcePath = process.env.QGC_SPP_SOURCE ? path.resolve(process.env.QGC_SPP_SOURCE) :
  path.resolve(__dirname, '../../deploy/harmonyos/bridge/QGCBluetoothBridge.ets');
const compiled = ts.transpileModule(fs.readFileSync(sourcePath, 'utf8'), {
  compilerOptions: { module: ts.ModuleKind.CommonJS, target: ts.ScriptTarget.ES2020 },
  fileName: sourcePath + '.ts', reportDiagnostics: true
});
assert.equal((compiled.diagnostics || []).filter(item => item.category === ts.DiagnosticCategory.Error).length, 0);

function deferred() {
  let resolve, reject;
  const promise = new Promise((yes, no) => { resolve = yes; reject = no; });
  return { promise, resolve, reject };
}
const failure = (code, message = 'mock native failure') => Object.assign(new Error(message), { code });
const tick = () => new Promise(resolve => setImmediate(resolve));
const plain = value => JSON.parse(JSON.stringify(value));

async function bounded(promise, milliseconds = 75) {
  let timer;
  try {
    return await Promise.race([promise, new Promise((_, reject) => {
      timer = setTimeout(() => reject(new Error('SPP operation did not settle within the cancellation budget')), milliseconds);
    })]);
  } finally {
    clearTimeout(timer);
  }
}

function setup(options = {}) {
  const state = { capable: true, granted: true, enabled: true, discovering: false,
    paired: [], names: new Map(), listeners: new Set(), calls: [], connects: [], reads: [], writes: [],
    closes: [], logs: [], timers: new Map(), nextTimer: 0,
    autoConnect: true, nextSocket: 0, settleReadOnClose: true, ...options };
  const permission = {
    GrantStatus: { PERMISSION_GRANTED: 0 },
    createAtManager: () => ({ requestPermissionsFromUser: (context, permissions) => {
      state.calls.push(['permission', context, [...permissions]]);
      if (state.permissionError) return Promise.reject(state.permissionError);
      return state.permission ? state.permission.promise : Promise.resolve({ authResults: [state.granted ? 0 : -1] });
    } })
  };
  const connection = {
    getPairedDevices: () => {
      if (state.pairedError) throw state.pairedError;
      return [...state.paired];
    },
    getRemoteDeviceName: address => {
      if (state.nameError) throw state.nameError;
      return state.names.get(address) || '';
    },
    on: (event, callback) => {
      assert.equal(event, 'bluetoothDeviceFind');
      state.calls.push(['on', callback]);
      if (state.onError) throw state.onError;
      state.listeners.add(callback);
    },
    off: (event, callback) => {
      assert.equal(event, 'bluetoothDeviceFind');
      assert.equal(typeof callback, 'function', 'A session must unsubscribe only its own listener');
      state.calls.push(['off', callback]);
      if (state.offError) throw state.offError;
      state.listeners.delete(callback);
    },
    isBluetoothDiscovering: () => {
      if (state.discoveryStateError) throw state.discoveryStateError;
      return state.discovering;
    },
    startBluetoothDiscovery: () => {
      state.calls.push(['startDiscovery']);
      if (state.startError) throw state.startError;
      state.discovering = true;
    },
    stopBluetoothDiscovery: () => {
      state.calls.push(['stopDiscovery']);
      if (state.stopError) throw state.stopError;
      state.discovering = false;
    }
  };
  const socket = {
    SppType: { SPP_RFCOMM: 0 },
    sppConnect: (address, config, callback) => {
      if (state.connectThrow) throw state.connectThrow;
      state.connects.push({ address, config: plain(config), callback });
      if (state.autoConnect) queueMicrotask(() => callback(state.connectError, state.nextSocket++));
    },
    sppReadAsync: socketId => {
      if (state.readThrow) throw state.readThrow;
      const result = deferred();
      state.reads.push({ socketId, ...result });
      return result.promise;
    },
    sppWriteAsync: (socketId, data) => {
      if (state.writeThrow) throw state.writeThrow;
      const result = deferred();
      state.writes.push({ socketId, bytes: [...new Uint8Array(data)], ...result });
      if (state.writeError) result.reject(state.writeError);
      else if (!state.blockWrites) result.resolve();
      return result.promise;
    },
    sppCloseClientSocket: socketId => {
      state.closes.push(socketId);
      if (state.closeError) throw state.closeError;
      if (state.settleReadOnClose) {
        state.reads.filter(read => read.socketId === socketId).forEach(read => read.reject(failure(2901054, 'Socket closed')));
      }
    }
  };
  const module = { exports: {} };
  vm.runInNewContext(compiled.outputText, {
    exports: module.exports, module, Uint8Array, ArrayBuffer, Map, Set, Promise, Error,
    setTimeout: (callback, milliseconds) => {
      const id = ++state.nextTimer;
      state.timers.set(id, { callback, milliseconds });
      return id;
    },
    clearTimeout: id => state.timers.delete(id),
    console: { warn: text => state.logs.push(text), error: text => state.logs.push(text) },
    canIUse: capability => {
      assert.equal(capability, 'SystemCapability.Communication.Bluetooth.Core');
      return state.capable;
    },
    require: id => {
      if (id === '@kit.AbilityKit') return { abilityAccessCtrl: permission };
      if (id === '@kit.BasicServicesKit') return {};
      if (id === '@kit.ConnectivityKit') return { connection, socket,
        access: { BluetoothState: { STATE_ON: 2 }, getState: () => state.enabled ? 2 : 0 } };
      throw new Error('Unexpected SPP dependency: ' + id);
    }
  }, { filename: sourcePath });
  state.emitDevices = addresses => [...state.listeners].forEach(callback => callback(addresses));
  state.count = name => state.calls.filter(call => call[0] === name).length;
  state.expireTimers = () => {
    for (const [id, timer] of [...state.timers]) {
      state.timers.delete(id);
      timer.callback();
    }
  };
  return { bridge: module.exports.getQGCBluetoothBridge(), state, socket, context: { testContext: true } };
}

module.exports = { setup, deferred, failure, tick, bounded, plain };
