'use strict';

const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const tsPath = process.env.QGC_TYPESCRIPT || 'D:/Progrem/Huawei/DevEco Studio/sdk/default/openharmony/ets/build-tools/ets-loader/node_modules/typescript';
const ts = require(tsPath);
const sourcePath = process.env.QGC_BRIDGE_SOURCE ? path.resolve(process.env.QGC_BRIDGE_SOURCE) :
  path.resolve(__dirname, '../../deploy/harmonyos/bridge/QGCSerialBridge.ets');
const bridgeRoot = path.resolve(__dirname, '../../deploy/harmonyos/bridge');
if (process.argv.includes('--sdk-types')) {
  const virtual = (sourcePath + '.ts').replaceAll('\\', '/');
  const kit = path.resolve(__dirname, 'virtual-kit.d.ts').replaceAll('\\', '/');
  const sdk = process.env.QGC_ETS_SDK || path.resolve(tsPath, '../../../..');
  const options = { strict: true, skipLibCheck: true, noEmit: true, target: ts.ScriptTarget.ES2020, module: ts.ModuleKind.CommonJS };
  const host = ts.createCompilerHost(options), getSource = host.getSourceFile.bind(host);
  const snippets = new Map([[virtual, fs.readFileSync(sourcePath, 'utf8') + '\ndeclare function canIUse(capability: string): boolean;'],
    [kit, 'export {BusinessError} from "@ohos.base"; export {default as serialManager} from "@ohos.usbManager.serial"; export {default as usbManager} from "@ohos.usbManager";']]);
  for (const name of fs.readdirSync(bridgeRoot).filter(name => /^QGC(?:UsbSerialTypes|Cp210xDriver|FtdiDriver|Ch34xDriver|CdcDescriptors)\.ets$/.test(name))) {
    snippets.set(path.join(bridgeRoot, name + '.ts').replaceAll('\\', '/'), fs.readFileSync(path.join(bridgeRoot, name), 'utf8'));
  }
  host.getSourceFile = (file, version, onError, again) => snippets.has(file.replaceAll('\\', '/')) ?
    ts.createSourceFile(file, snippets.get(file.replaceAll('\\', '/')), version, true) : getSource(file, version, onError, again);
  host.resolveModuleNames = (names, containing) => names.map(name => name === '@kit.BasicServicesKit' ? { resolvedFileName: kit } :
    name.startsWith('@ohos.') ? { resolvedFileName: path.join(sdk, 'api', name + '.d.ts') } :
    name.startsWith('./QGC') ? { resolvedFileName: path.join(bridgeRoot, name + '.ets.ts').replaceAll('\\', '/') } :
      ts.resolveModuleName(name, containing, options, host).resolvedModule);
  const diagnostics = ts.getPreEmitDiagnostics(ts.createProgram([virtual], options, host));
  const errors = diagnostics.filter(diagnostic => diagnostic.category === ts.DiagnosticCategory.Error);
  if (errors.length) console.error(ts.formatDiagnosticsWithColorAndContext(errors, {
    getCanonicalFileName: file => file, getCurrentDirectory: () => process.cwd(), getNewLine: () => '\n'
  }));
  console.log(`SDK declaration type check: ${errors.length} errors, ${diagnostics.length - errors.length} warnings`);
  process.exit(errors.length ? 1 : 0);
}
const compiled = ts.transpileModule(fs.readFileSync(sourcePath, 'utf8'), {
  compilerOptions: { module: ts.ModuleKind.CommonJS, target: ts.ScriptTarget.ES2020 },
  fileName: 'QGCSerialBridge.ts', reportDiagnostics: true
});
assert.equal((compiled.diagnostics || []).filter(d => d.category === ts.DiagnosticCategory.Error).length, 0);

function deferred() {
  let resolve, reject;
  const promise = new Promise((yes, no) => { resolve = yes; reject = no; });
  return { promise, resolve, reject };
}
const tick = () => new Promise(resolve => setImmediate(resolve));
function endpoint(address, interfaceId = 7) {
  return { address, attributes: 2, interval: 0, maxPacketSize: 64, direction: address & 128, number: address & 15, type: 2, interfaceId };
}
function iface(id, clazz, subClass, endpoints = [], alternateSetting = 0) {
  return { id, clazz, subClass, endpoints, alternateSetting, protocol: 0, name: '' };
}
function cdc(name = 'private-usb-name', options = {}) {
  return {
    busNum: 1, devAddress: 2, serial: 'private-serial', name, vendorId: 9900, productId: 50,
    manufacturerName: 'PX4', productName: 'PX4 FMU V5 BL', version: '1', clazz: 0, subClass: 0, protocol: 0,
    configs: [{ id: 1, attributes: 0, maxPower: 100, name: '', isRemoteWakeup: false, isSelfPowered: false,
      interfaces: [iface(4, 2, 2), iface(7, 10, 0, [endpoint(0x83), endpoint(0x02)])] }], ...options
  };
}
function setup(options = {}) {
  const state = { devices: [cdc()], nativePorts: [], serialCapability: false, rawCapability: true, rights: true,
    calls: [], controls: [], transfers: [], canceled: [], logs: [], warnings: [], ...options };
  const usb = {
    getDevices: () => { if (state.usbError) throw state.usbError; return state.devices; },
    hasRight: () => state.rights,
    requestRight: name => { state.calls.push(['request', name]); return state.grant ? state.grant.promise : Promise.resolve(state.rights); },
    connectDevice: device => { const pipe = { busNum: device.busNum, devAddress: device.devAddress }; state.calls.push(['connect', device, pipe]); return pipe; },
    getRawDescriptor: pipe => {
      state.calls.push(['descriptor', pipe]);
      return state.rawDescriptor ? (typeof state.rawDescriptor === 'function' ? state.rawDescriptor(pipe) : state.rawDescriptor) :
        new Uint8Array([18, 1, 0, 2, 0, 0, 0, 64, 0, 0, 0, 0, 0, 6, 0, 0, 0, 1]);
    },
    claimInterface: (pipe, intf, force) => { state.calls.push(['claim', intf.id, pipe, force]); return state.claimResult || 0; },
    releaseInterface: (pipe, intf) => { state.calls.push(['release', intf.id, pipe]); return 0; },
    closePipe: pipe => { state.calls.push(['close', pipe]); return 0; },
    setConfiguration: (pipe, config) => { state.calls.push(['config', config.id]); return 0; },
    setInterface: (pipe, intf) => { state.calls.push(['alternate', intf.id, intf.alternateSetting]); return 0; },
    usbControlTransfer: (pipe, params, timeout) => {
      state.controls.push({ pipe, ...params, data: Array.from(params.data), timeout });
      return state.controlHook ? state.controlHook(params) : Promise.resolve(params.wLength);
    },
    UsbEndpointTransferType: { TRANSFER_TYPE_BULK: 2 },
    UsbTransferStatus: { TRANSFER_COMPLETED: 0, TRANSFER_ERROR: 1, TRANSFER_TIMED_OUT: 2, TRANSFER_CANCELED: 3, TRANSFER_STALL: 4, TRANSFER_NO_DEVICE: 5, TRANSFER_OVERFLOW: 6 },
    usbSubmitTransfer: transfer => { state.transfers.push(transfer); },
    usbCancelTransfer: transfer => { state.canceled.push(transfer); }
  };
  const serial = {
    getPortList: () => { if (state.serialError) throw state.serialError; return state.nativePorts; },
    hasSerialRight: () => true,
    open: id => state.calls.push(['native-open', id]),
    close: id => state.calls.push(['native-close', id]),
    setAttribute: (id, attrs) => { state.attrs = attrs; state.calls.push(['native-attrs', id]); },
    getAttribute: () => state.attrs,
    read: async (id, buffer) => { buffer.set([11, 12]); return 2; },
    write: async (id, buffer) => buffer.length
  };
  const module = { exports: {} };
  const modules = new Map();
  const requireModule = id => {
    if (id === '@kit.BasicServicesKit') return { serialManager: serial, usbManager: usb };
    assert.match(id, /^\.\/QGC[A-Za-z0-9]+$/);
    if (modules.has(id)) return modules.get(id).exports;
    const file = path.join(bridgeRoot, id + '.ets');
    const loaded = { exports: {} };
    modules.set(id, loaded);
    const code = ts.transpileModule(fs.readFileSync(file, 'utf8'), {
      compilerOptions: { module: ts.ModuleKind.CommonJS, target: ts.ScriptTarget.ES2020 }, fileName: file + '.ts'
    });
    vm.runInNewContext(code.outputText, { ...context, exports: loaded.exports, module: loaded }, { filename: file });
    return loaded.exports;
  };
  const context = { exports: module.exports, module, require: requireModule,
    Uint8Array, Map, Set, Error, Promise, console: { info: s => state.logs.push(s), warn: s => { state.logs.push(s); state.warnings.push(s); }, error: s => state.logs.push(s) },
  canIUse: capability => capability.endsWith('.Serial') ? state.serialCapability : state.rawCapability };
  vm.runInNewContext(compiled.outputText, context, { filename: sourcePath });
  const bridge = module.exports.getQGCSerialBridge();
  return { bridge, state, usb };
}
function complete(transfer, status = 0, bytes = [], actualLength = bytes.length, error = null) {
  transfer.buffer.set(bytes);
  transfer.callback(error, { status, actualLength, isoPacketDescs: [] });
}
async function openCdc(env, session = 1, args = {}) {
  const ports = env.bridge.getPorts();
  assert.equal(ports.length, 1);
  await env.bridge.open(session, `harmony-serial:${ports[0].portId}`, args.baud || 115200,
    args.dataBits || 8, args.parity || 0, args.stopBits || 0, args.autoConnect || false);
  return ports[0];
}
const tests = [];
const test = (name, run) => tests.push({ name, run });

for (const baud of [110, 300, 600, 1200, 2400, 4800, 9600, 14400, 19200, 38400,
  56000, 57600, 115200, 128000, 230400, 256000, 460800, 500000, 921600]) {
  test(`official v5.0.8 baud ${baud} reaches CDC line coding unchanged`, async () => {
    const env = setup();
    await openCdc(env, 1, { baud });
    const lineCoding = () => env.state.controls.filter(control => control.bRequest === 0x20).at(-1);
    assert.equal(Buffer.from(lineCoding().data).readUInt32LE(), baud);
    await env.bridge.setBaud(1, baud);
    assert.equal(Buffer.from(lineCoding().data).readUInt32LE(), baud);
    env.bridge.close(1); await tick();
  });
}

function vendorDevice(vendorId, productId, interfaces = [iface(0, 255, 0, [endpoint(0x81, 0), endpoint(2, 0)])]) {
  const device = cdc('vendor-usb', { vendorId, productId, manufacturerName: 'USB serial', productName: 'USB UART' });
  device.configs[0].interfaces = interfaces;
  return device;
}
function rawConfiguration(id, descriptors) {
  const body = descriptors.flat();
  const total = 9 + body.length;
  return [9, 2, total & 255, total >> 8, descriptors.filter(d => d[1] === 4 && d[3] === 0).length,
    id, 0, 0x80, 50, ...body];
}
function rawInterface(id, clazz, subClass, endpoints = []) {
  return [9, 4, id, 0, endpoints.length, clazz, subClass, 0, 0,
    ...endpoints.flatMap(address => [7, 5, address, 2, 64, 0, 0])];
}
function rawDevice(configurations, version = 0x0600) {
  return new Uint8Array([18, 1, 0, 2, 0, 0, 0, 64, 0, 0, 0, 0, version & 255, version >> 8,
    0, 0, 0, configurations.length, ...configurations.flat()]);
}
function multiCdc(options = {}) {
  const device = cdc();
  device.configs[0].interfaces = [iface(8, 2, 2), iface(9, 10, 0, [endpoint(0x84, 9), endpoint(5, 9)]),
    iface(4, 2, 2), iface(7, 10, 0, [endpoint(0x83), endpoint(2)])];
  const raw = rawDevice([rawConfiguration(1, [rawInterface(8, 2, 2), [5, 0x24, 6, 8, 9],
    rawInterface(9, 10, 0, [0x84, 5]), rawInterface(4, 2, 2), [5, 0x24, 6, 4, 7],
    rawInterface(7, 10, 0, [0x83, 2])])]);
  return setup({ devices: [device], rawDescriptor: raw, ...options });
}

for (const [name, vendorId, productId, requestType] of [
  ['CP210x', 0x10c4, 0xea60, 0x41], ['CH34x', 0x1a86, 0x7523, 0x40], ['FTDI', 0x0403, 0x6001, 0x40]
]) {
  test(`${name} raw backend enumerates without authorization and opens its vendor protocol`, async () => {
    const env = setup({ devices: [vendorDevice(vendorId, productId)] });
    const ports = env.bridge.getPorts();
    assert.equal(ports.length, 1);
    assert.equal(env.state.calls.length, 0);
    await env.bridge.open(1, `harmony-serial:${ports[0].portId}`, 115200, 8, 0, 0, false);
    assert.ok(env.state.controls.some(control => control.bmRequestType === requestType));
    assert.equal(env.state.calls.filter(call => call[0] === 'claim').length, 1, 'single-interface devices must be claimed once');
    const writing = env.bridge.write(1, new Uint8Array([0xfe, 1, 2]));
    complete(env.state.transfers.at(-1), 0, [], 3);
    assert.equal(await writing, 3);
    await env.bridge.setBaud(1, 57600);
    env.bridge.close(1); await tick();
    assert.equal(env.state.calls.filter(call => call[0] === 'release').length, 1);
    assert.equal(env.state.calls.filter(call => call[0] === 'close').length, 1);
  });
}

test('FTDI bridge strips USB status bytes before delivering MAVLink and preserves plain TX', async () => {
  const env = setup({ devices: [vendorDevice(0x0403, 0x6001)] });
  await openCdc(env);
  const reading = env.bridge.read(1);
  complete(env.state.transfers.at(-1), 0, [0x01, 0x60, 0xfd, 2, 3]);
  assert.deepEqual(Array.from(await reading), [0xfd, 2, 3]);
  const writing = env.bridge.write(1, new Uint8Array([0xfd, 7, 8]));
  assert.deepEqual(Array.from(env.state.transfers.at(-1).buffer), [0xfd, 7, 8]);
  complete(env.state.transfers.at(-1), 0, [], 3); await writing;
  env.bridge.close(1); await tick();
});

test('multi CDC candidates are discovered without reading protected descriptors and resolve per control', async () => {
  const env = multiCdc();
  const ports = env.bridge.getPorts();
  assert.equal(ports.length, 2);
  assert.equal(env.state.calls.length, 0);
  const names = ports.map(port => `harmony-serial:${port.portId}`);
  await env.bridge.open(1, names[0], 115200, 8, 0, 0, false);
  await env.bridge.open(2, names[1], 115200, 8, 0, 0, false);
  assert.deepEqual(new Set(env.state.controls.filter(c => c.bRequest === 0x20).map(c => c.wIndex)), new Set([4, 8]));
  assert.equal(new Set(env.state.calls.filter(c => c[0] === 'claim').map(c => c[1])).size, 4);
  env.bridge.close(1); await tick();
  const reading = env.bridge.read(2);
  complete(env.state.transfers.at(-1), 0, [0xfd, 9]);
  assert.deepEqual(Array.from(await reading), [0xfd, 9]);
  env.bridge.close(2); await tick();
});

test('closing one CDC port blocks only its own replacement and preserves another port', async () => {
  const env = multiCdc();
  const ports = env.bridge.getPorts(); assert.equal(ports.length, 2);
  const name = index => `harmony-serial:${ports[index].portId}`;
  await env.bridge.open(1, name(0), 115200, 8, 0, 0, false);
  const pending = env.bridge.read(1);
  const pendingResult = assert.rejects(pending);
  const transfer = env.state.transfers.at(-1);
  env.bridge.close(1); await pendingResult;
  await assert.rejects(env.bridge.open(3, name(0), 115200, 8, 0, 0, false));
  await env.bridge.open(2, name(1), 115200, 8, 0, 0, false);
  complete(transfer, 3); await tick();
  await env.bridge.open(3, name(0), 115200, 8, 0, 0, false);
  env.bridge.close(2); env.bridge.close(3); await tick();
});

test('multiport shares one device pipe until the last port and pending transfer are released', async () => {
  const env = multiCdc();
  let deviceClosed = false;
  const close = env.usb.closePipe;
  env.usb.closePipe = pipe => { deviceClosed = true; return close(pipe); };
  env.usb.usbSubmitTransfer = transfer => {
    if (deviceClosed) throw { code: 14400008, message: 'shared device pipe was closed' };
    env.state.transfers.push(transfer);
  };
  const ports = env.bridge.getPorts();
  await env.bridge.open(1, `harmony-serial:${ports[0].portId}`, 115200, 8, 0, 0, false);
  await env.bridge.open(2, `harmony-serial:${ports[1].portId}`, 115200, 8, 0, 0, false);
  assert.equal(env.state.calls.filter(call => call[0] === 'connect').length, 1);
  env.bridge.close(1); await tick();
  assert.equal(env.state.calls.filter(call => call[0] === 'close').length, 0);
  const reading = env.bridge.read(2);
  const rejected = assert.rejects(reading);
  const transfer = env.state.transfers.at(-1);
  env.bridge.close(2); await rejected;
  assert.equal(deviceClosed, false, 'the last pending USB callback still owns the pipe');
  complete(transfer, 3); await tick();
  assert.equal(env.state.calls.filter(call => call[0] === 'close').length, 1);
});

test('multi CDC rejects missing or conflicting associations before claiming interfaces', async () => {
  for (const descriptors of [new Uint8Array(0), rawDevice([rawConfiguration(1, [rawInterface(4, 2, 2),
    [5, 0x24, 6, 4, 9], rawInterface(8, 2, 2), [5, 0x24, 6, 8, 9], rawInterface(9, 10, 0)])])]) {
    const env = multiCdc({ rawDescriptor: descriptors });
    const ports = env.bridge.getPorts(); assert.equal(ports.length, 2);
    await assert.rejects(env.bridge.open(1, `harmony-serial:${ports[0].portId}`, 115200, 8, 0, 0, false));
    await tick();
    assert.equal(env.state.calls.filter(call => call[0] === 'claim').length, 0);
    assert.equal(env.state.calls.filter(call => call[0] === 'close').length, 1);
  }
});

test('raw port identity prevents a second open after a native ID collision renumbers discovery', async () => {
  const env = setup({ serialCapability: true });
  const original = await openCdc(env);
  env.state.nativePorts = [{ portId: original.portId, deviceName: 'another-device' }];
  const renumbered = env.bridge.getPorts().find(port => port.deviceName === 'private-usb-name');
  assert.notEqual(renumbered.portId, original.portId);
  await assert.rejects(env.bridge.open(2, `harmony-serial:${renumbered.portId}`, 115200, 8, 0, 0, false));
  assert.equal(env.state.calls.filter(call => call[0] === 'claim').length, 2);
  env.bridge.close(1); await tick();
});

test('native takeover waits for pending raw callbacks to release the same physical device', async () => {
  const env = setup({ serialCapability: true }); await openCdc(env);
  const reading = env.bridge.read(1);
  const rejected = assert.rejects(reading);
  const transfer = env.state.transfers.at(-1);
  env.bridge.close(1); await rejected;
  env.state.nativePorts = [{ portId: 42, deviceName: 'private-usb-name' }];
  await assert.rejects(env.bridge.open(2, 'harmony-serial:42', 115200, 8, 0, 0, false));
  complete(transfer, 3); await tick();
  await env.bridge.open(2, 'harmony-serial:42', 115200, 8, 0, 0, false);
  env.bridge.close(2);
  assert.equal(env.bridge.usbConnections.size, 0);
});

test('vendor permission denial never connects claims or sends chip requests', async () => {
  const env = setup({ devices: [vendorDevice(0x10c4, 0xea60)], rights: false });
  const port = env.bridge.getPorts()[0];
  await assert.rejects(env.bridge.open(1, `harmony-serial:${port.portId}`, 115200, 8, 0, 0, false));
  assert.equal(env.state.controls.length, 0);
  assert.equal(env.state.calls.filter(call => call[0] === 'connect').length, 0);
});

test('closing a vendor initialization waits for its control and shuts down the original session', async () => {
  const gate = deferred();
  const env = setup({ devices: [vendorDevice(0x10c4, 0xea60)], controlHook: params =>
    params.bRequest === 0 && params.wValue === 1 ? gate.promise : Promise.resolve(params.wLength) });
  const port = env.bridge.getPorts()[0];
  const opening = env.bridge.open(1, `harmony-serial:${port.portId}`, 115200, 8, 0, 0, false);
  const rejected = assert.rejects(opening);
  await tick(); env.bridge.close(1);
  assert.equal(env.state.calls.filter(call => call[0] === 'close').length, 0);
  gate.resolve(0); await rejected; await tick();
  assert.equal(env.state.controls.filter(control => control.bRequest === 0x1e).length, 0);
  assert.equal(env.state.calls.filter(call => call[0] === 'close').length, 1);
  assert.equal(env.bridge.usbConnections.size, 0);
});

test('short vendor control responses fail before further configuration and release the device', async () => {
  const env = setup({ devices: [vendorDevice(0x10c4, 0xea60)], controlHook: () => Promise.resolve(0) });
  await assert.rejects(openCdc(env), error => error.code === 14400012); await tick();
  assert.equal(env.state.controls.filter(control => control.bRequest === 3).length, 0);
  assert.equal(env.state.calls.filter(call => call[0] === 'close').length, 1);
  assert.equal(env.bridge.usbConnections.size, 0);
});

test('unsupported vendor framing releases the pipe without sending initialization or shutdown commands', async () => {
  const env = setup({ devices: [vendorDevice(0x0403, 0x6001)] });
  await assert.rejects(openCdc(env, 1, { dataBits: 5 }), error => error.code === 401); await tick();
  assert.equal(env.state.controls.length, 0);
  assert.equal(env.state.calls.filter(call => call[0] === 'close').length, 1);
});

test('FTDI dual UART ports retain channel indices and independently decoded reads', async () => {
  const device = vendorDevice(0x0403, 0x6010, [iface(0, 255, 0, [endpoint(0x81, 0), endpoint(2, 0)]),
    iface(1, 255, 0, [endpoint(0x83, 1), endpoint(4, 1)])]);
  const env = setup({ devices: [device], rawDescriptor: rawDevice([], 0x0700) });
  const ports = env.bridge.getPorts(); assert.equal(ports.length, 2);
  await env.bridge.open(1, `harmony-serial:${ports[0].portId}`, 115200, 8, 0, 0, false);
  await env.bridge.open(2, `harmony-serial:${ports[1].portId}`, 115200, 8, 0, 0, false);
  assert.deepEqual(new Set(env.state.controls.filter(control => control.bRequest === 1).map(control => control.wIndex)), new Set([1, 2]));
  const first = env.bridge.read(1), second = env.bridge.read(2);
  const [a, b] = env.state.transfers.slice(-2);
  assert.notEqual(a.endpoint, b.endpoint);
  complete(a, 0, [1, 0x60, 0xfd, 1]); complete(b, 0, [1, 0x60, 0xfe, 2]);
  assert.deepEqual(Array.from(await first), [0xfd, 1]);
  assert.deepEqual(Array.from(await second), [0xfe, 2]);
  env.bridge.close(1); env.bridge.close(2); await tick();
  assert.equal(env.state.calls.filter(call => call[0] === 'connect').length, 1);
  assert.equal(env.state.calls.filter(call => call[0] === 'close').length, 1);
});

test('unknown products on recognized USB vendors are not probed with vendor commands', () => {
  const env = setup({ serialCapability: true, devices: [vendorDevice(0x10c4, 1),
    vendorDevice(0x1a86, 1), vendorDevice(0x0403, 1)] });
  assert.equal(env.bridge.getPorts().length, 0);
  assert.equal(env.state.calls.length, 0);
  assert.equal(env.state.controls.length, 0);
});

test('parallel CDC ports share authorization and configuration with nonzero alternates', async () => {
  const env = multiCdc();
  const device = env.state.devices[0];
  device.configs.push({ ...device.configs[0], id: 7,
    interfaces: device.configs[0].interfaces.flatMap(intf => intf.clazz === 10 ?
      [{ ...intf, endpoints: [] }, { ...intf, alternateSetting: 1 }] : [intf]) });
  const a = rawInterface(7, 10, 0, [0x83, 2]); a[3] = 1;
  const b = rawInterface(9, 10, 0, [0x84, 5]); b[3] = 1;
  env.state.rawDescriptor = rawDevice([rawConfiguration(1, [rawInterface(4, 2, 2)]), rawConfiguration(7, [
    rawInterface(4, 2, 2), [5, 0x24, 6, 4, 7], rawInterface(7, 10, 0), a,
    rawInterface(8, 2, 2), [5, 0x24, 6, 8, 9], rawInterface(9, 10, 0), b])]);
  const grant = deferred(); env.state.rights = false; env.state.grant = grant;
  const ports = env.bridge.getPorts(); assert.equal(ports.length, 4);
  const first = env.bridge.open(1, `harmony-serial:${ports[2].portId}`, 115200, 8, 0, 0, false);
  const second = env.bridge.open(2, `harmony-serial:${ports[3].portId}`, 115200, 8, 0, 0, false);
  assert.equal(env.state.calls.filter(call => call[0] === 'connect' || call[0] === 'descriptor').length, 0);
  assert.equal(env.state.calls.filter(call => call[0] === 'request').length, 1);
  env.state.rights = true; grant.resolve(true); await Promise.all([first, second]);
  assert.deepEqual(env.state.calls.filter(call => call[0] === 'config').map(call => call[1]), [7]);
  assert.deepEqual(new Set(env.state.calls.filter(call => call[0] === 'alternate').map(call => `${call[1]}:${call[2]}`)), new Set(['7:1', '9:1']));
  await assert.rejects(env.bridge.open(3, `harmony-serial:${ports[0].portId}`, 115200, 8, 0, 0, false));
  env.bridge.close(1); env.bridge.close(2); await tick();
  assert.equal(env.state.calls.filter(call => call[0] === 'close').length, 1);
});

test('denied USB authorization suppresses automatic prompts for all ports until manual retry', async () => {
  const env = multiCdc({ rights: false });
  const ports = env.bridge.getPorts();
  const name = index => `harmony-serial:${ports[index].portId}`;
  await assert.rejects(env.bridge.open(1, name(0), 115200, 8, 0, 0, true));
  await assert.rejects(env.bridge.open(2, name(1), 115200, 8, 0, 0, true));
  assert.equal(env.state.calls.filter(call => call[0] === 'request').length, 1);
  env.state.rights = true;
  await env.bridge.open(2, name(1), 115200, 8, 0, 0, false);
  await env.bridge.open(1, name(0), 115200, 8, 0, 0, true);
  env.bridge.close(1); env.bridge.close(2); await tick();
});

test('serial capability 801 falls back to descriptor-qualified CDC without permission or connection', () => {
  const { bridge, state } = setup();
  const ports = bridge.getPorts();
  assert.equal(ports.length, 1);
  assert.equal(ports[0].hasUsbIdentity, true);
  assert.equal(ports[0].vendorId, 9900);
  assert.equal(ports[0].productId, 50);
  assert.equal(ports[0].manufacturer, 'PX4');
  assert.equal(ports[0].productName, 'PX4 FMU V5 BL');
  assert.match(bridge.getDiscoveryState(), /serialErrorCode=801/);
  assert.match(bridge.getDiscoveryState(), /\bnativePortCount=unavailable\b/);
  assert.match(bridge.getDiscoveryState(), /\bportCount=1\b/);
  assert.match(bridge.getDiscoveryState(), /cdcPortCount=1/);
  assert.equal(state.calls.length, 0);
  assert.ok(!state.logs.join('\n').includes('private-usb-name'));
  assert.ok(!state.logs.join('\n').includes('private-serial'));
});

test('existing single CDC port IDs survive the vendor and multiport extension', () => {
  const { bridge } = setup();
  assert.equal(bridge.getPorts()[0].portId, 2076020451);
});

test('unsupported native serial with no CDC candidate is a successful empty USB discovery', () => {
  const device = cdc(); device.configs[0].interfaces[0].clazz = 255;
  const env = setup({ devices: [device] });
  assert.equal(env.bridge.getPorts().length, 0);
  assert.equal(env.bridge.getPorts().length, 0);
  assert.match(env.bridge.getDiscoveryState(), /\bnativePortCount=unavailable\b/);
  assert.match(env.bridge.getDiscoveryState(), /\bportCount=0\b/);
  assert.match(env.bridge.getDiscoveryState(), /serialErrorCode=801/);
  assert.equal(env.state.logs.length, 1);
  assert.equal(env.state.warnings.length, 0);
});

test('no attached USB device does not throw on repeated discovery or block later attachment', () => {
  const env = setup({ devices: [] });
  for (let i = 0; i < 10; i++) assert.equal(env.bridge.getPorts().length, 0);
  assert.equal(env.state.logs.length, 1);
  assert.equal(env.state.warnings.length, 0);
  env.state.devices = [cdc()];
  assert.equal(env.bridge.getPorts().length, 1);
});

test('an unstarted USB service stays observable without a repeated NAPI exception', () => {
  const env = setup({ usbError: { code: 88080389, message: 'USB service is not started' } });
  for (let i = 0; i < 10; i++) assert.equal(env.bridge.getPorts().length, 0);
  assert.match(env.bridge.getDiscoveryState(), /rawUsbErrorCode=88080389/);
  assert.equal(env.state.warnings.length, 1);
  env.state.usbError = null;
  assert.equal(env.bridge.getPorts().length, 1);
});

test('USB permission and unexpected service failures retain their actual error codes', () => {
  for (const code of [201, 14400001]) {
    const env = setup({ usbError: { code, message: 'USB failure' } });
    assert.throws(() => env.bridge.getPorts(), error => error.code === code);
    assert.match(env.bridge.getDiscoveryState(), new RegExp(`rawUsbErrorCode=${code}`));
  }
});

test('unavailable USB and unexpected native serial failures remain errors', () => {
  const unavailable = setup({ rawCapability: false, devices: [] });
  assert.throws(() => unavailable.bridge.getPorts(), error => error.code === 801);
  const failed = setup({ serialCapability: true, devices: [], serialError: { code: 201, message: 'denied' } });
  assert.throws(() => failed.bridge.getPorts(), error => error.code === 201);
});

test('native getPortList error 801 also permits CDC fallback', () => {
  const env = setup({ serialCapability: true, serialError: { code: 801, message: 'unsupported' } });
  assert.equal(env.bridge.getPorts().length, 1);
  assert.match(env.bridge.getDiscoveryState(), /serialErrorCode=801/);
});

test('native serial port wins for the same device and retains native I/O', async () => {
  const env = setup({ serialCapability: true, nativePorts: [{ portId: 42, deviceName: 'private-usb-name' }] });
  const port = await openCdc(env);
  assert.equal(port.portId, 42);
  assert.deepEqual(Array.from(await env.bridge.read(1)), [11, 12]);
  assert.equal(await env.bridge.write(1, new Uint8Array([1, 2, 3])), 3);
  await env.bridge.setBaud(1, 57600);
  assert.equal(env.state.attrs.baudRate, 57600);
  env.bridge.close(1);
  assert.equal(env.state.calls.filter(c => c[0] === 'connect').length, 0);
  assert.equal(env.state.calls.filter(c => c[0] === 'native-close').length, 1);
});

test('two native port IDs on the same device retain independent native sessions', async () => {
  const env = setup({ serialCapability: true, nativePorts: [{ portId: 42, deviceName: 'private-usb-name' }, { portId: 43, deviceName: 'private-usb-name' }] });
  await env.bridge.open(1, 'harmony-serial:42', 115200, 8, 0, 0, false);
  await env.bridge.open(2, 'harmony-serial:43', 115200, 8, 0, 0, false);
  env.bridge.close(1); env.bridge.close(2);
  assert.equal(env.state.calls.filter(c => c[0] === 'native-close').length, 2);
});

test('unknown bulk devices are skipped and multiple CDC candidates defer descriptor pairing', () => {
  const bulk = cdc('bulk'); bulk.configs[0].interfaces[0].clazz = 255;
  const ambiguous = cdc('ambiguous'); ambiguous.configs[0].interfaces.push(iface(8, 2, 2), iface(9, 10, 0, [endpoint(0x84, 9), endpoint(5, 9)]));
  const { bridge, state } = setup({ serialCapability: true, devices: [bulk, ambiguous] });
  assert.equal(bridge.getPorts().length, 2);
  assert.match(bridge.getDiscoveryState(), /skip=not-cdc/);
  assert.match(bridge.getDiscoveryState(), /association=descriptor-pending/);
  assert.equal(state.calls.length, 0);
});

test('unpaired ACM and NCM composite candidates cannot open without Union evidence', async () => {
  const composite = cdc();
  composite.configs[0].interfaces = [iface(4, 2, 2), iface(7, 10, 0), iface(8, 2, 13), iface(9, 10, 0, [endpoint(0x84, 9), endpoint(5, 9)])];
  const env = setup({ serialCapability: true, devices: [composite] });
  const ports = env.bridge.getPorts();
  assert.equal(ports.length, 1);
  assert.match(env.bridge.getDiscoveryState(), /association=descriptor-pending/);
  await assert.rejects(env.bridge.open(1, `harmony-serial:${ports[0].portId}`, 115200, 8, 0, 0, false));
  await tick();
  assert.equal(env.state.calls.filter(call => call[0] === 'claim').length, 0);
});

test('IDs are Int32, stable across reorder/unplug, refreshed descriptors, and avoid native IDs', () => {
  const env = setup({ serialCapability: true, devices: [cdc('a'), cdc('b')] });
  const first = env.bridge.getPorts();
  assert.equal(new Set(first.map(p => p.portId)).size, 2);
  first.forEach(p => assert.ok(Number.isInteger(p.portId) && p.portId >= 0 && p.portId <= 2147483647));
  env.state.devices.reverse();
  assert.deepEqual(new Map(env.bridge.getPorts().map(p => [p.deviceName, p.portId])), new Map(first.map(p => [p.deviceName, p.portId])));
  env.state.devices = [];
  env.bridge.getPorts();
  env.state.devices = [cdc('a', { productName: 'new descriptor' })];
  let next = env.bridge.getPorts();
  assert.equal(next[0].portId, first.find(p => p.deviceName === 'a').portId);
  assert.equal(next[0].productName, 'new descriptor');
  env.state.nativePorts = [{ portId: next[0].portId, deviceName: 'different' }];
  next = env.bridge.getPorts();
  assert.equal(new Set(next.map(p => p.portId)).size, 2);
});

test('denied authorization suppresses auto-connect but manual reconnect can request again', async () => {
  const env = setup({ rights: false });
  const id = env.bridge.getPorts()[0].portId;
  const open = auto => env.bridge.open(1, `harmony-serial:${id}`, 115200, 8, 0, 0, auto);
  await assert.rejects(open(false), error => error.code === 31400002);
  await assert.rejects(open(true), error => error.code === 31400002);
  assert.equal(env.state.calls.filter(c => c[0] === 'request').length, 1);
  env.state.rights = true;
  await open(false);
  env.bridge.close(1);
});

test('ID hash collision only reallocates the CDC port conflicting with a native ID', () => {
  const env = setup({ serialCapability: true, devices: [cdc('Aa'), cdc('BB')] });
  const before = env.bridge.getPorts();
  const id = name => before.find(p => p.deviceName === name).portId;
  env.state.nativePorts = [{ portId: id('Aa'), deviceName: 'native-other' }];
  const after = env.bridge.getPorts();
  assert.equal(after.find(p => p.deviceName === 'BB').portId, id('BB'));
  assert.notEqual(after.find(p => p.deviceName === 'Aa').portId, id('Aa'));
});

test('CDC ID cache is bounded across repeated names while retaining active and closing keys', async () => {
  const env = setup(); await openCdc(env);
  const reading = env.bridge.read(1); const transfer = env.state.transfers.at(-1);
  env.bridge.close(1); await assert.rejects(reading);
  for (let index = 0; index < 80; index++) {
    env.state.devices = [cdc(`connection-${index}`)]; env.bridge.getPorts();
    assert.ok(env.bridge.usbIds.size <= 2);
  }
  complete(transfer, 3); await tick();
  env.bridge.getPorts(); assert.equal(env.bridge.usbIds.size, 1);
});

test('close during permission wait rejects a late grant without opening resources', async () => {
  const grant = deferred();
  const env = setup({ rights: false, grant });
  const port = env.bridge.getPorts()[0];
  const promise = env.bridge.open(1, `harmony-serial:${port.portId}`, 115200, 8, 0, 0, false);
  env.bridge.close(1);
  grant.resolve(true);
  await assert.rejects(promise, error => error.code === 31400007);
  assert.equal(env.state.calls.filter(c => c[0] === 'connect').length, 0);
});

test('CDC open configures exact control interface and line coding; baud retains framing', async () => {
  const env = setup();
  await openCdc(env, 1, { baud: 0x12345678, dataBits: 7, parity: 2, stopBits: 1 });
  const line = env.state.controls.find(c => c.bRequest === 0x20);
  assert.equal(line.bmRequestType, 0x21); assert.equal(line.wIndex, 4);
  assert.deepEqual(line.data, [0x78, 0x56, 0x34, 0x12, 2, 2, 7]);
  assert.equal(env.state.controls.find(c => c.bRequest === 0x22).wValue, 3);
  await env.bridge.setBaud(1, 57600);
  assert.deepEqual(env.state.controls.filter(c => c.bRequest === 0x20).at(-1).data, [0, 225, 0, 0, 2, 2, 7]);
  env.bridge.close(1); await tick();
  assert.equal(env.state.controls.filter(c => c.bRequest === 0x22).at(-1).wValue, 0);
  assert.equal(env.state.calls.filter(c => c[0] === 'release').length, 2);
  assert.equal(env.state.calls.filter(c => c[0] === 'close').length, 1);
});

test('invalid framing is rejected before connecting and alternate settings are applied when selected', async () => {
  const env = setup();
  await assert.rejects(openCdc(env, 1, { dataBits: 9 }), error => error.code === 401);
  assert.equal(env.state.calls.filter(c => c[0] === 'connect').length, 0);
  env.state.devices[0].configs[0].interfaces[1].alternateSetting = 1;
  await openCdc(env);
  assert.deepEqual(env.state.calls.find(c => c[0] === 'alternate'), ['alternate', 7, 1]);
  env.bridge.close(1); await tick();
});

test('re-enumeration opens fresh endpoint descriptors under the same stable ID', async () => {
  const env = setup(); const id = env.bridge.getPorts()[0].portId;
  env.state.devices = []; assert.equal(env.bridge.getPorts().length, 0);
  const fresh = cdc(); fresh.configs[0].interfaces[1].endpoints = [endpoint(0x85), endpoint(6)];
  env.state.devices = [fresh]; assert.equal(env.bridge.getPorts()[0].portId, id);
  await openCdc(env);
  const reading = env.bridge.read(1); assert.equal(env.state.transfers.at(-1).endpoint, 0x85);
  complete(env.state.transfers.at(-1), 0, [1]); await reading;
  env.bridge.close(1); await tick();
});

test('close during awaited control configuration cleans old resources exactly once', async () => {
  const control = deferred();
  const env = setup({ controlHook: params => params.bRequest === 0x20 ? control.promise : Promise.resolve(params.wLength) });
  const port = env.bridge.getPorts()[0];
  const promise = env.bridge.open(1, `harmony-serial:${port.portId}`, 115200, 8, 0, 0, false);
  await tick(); env.bridge.close(1);
  await assert.rejects(env.bridge.open(1, `harmony-serial:${port.portId}`, 115200, 8, 0, 0, false), error => error.code === 31400004);
  control.resolve(7);
  await assert.rejects(promise, error => error.code === 31400007);
  await tick(); env.bridge.close(1);
  assert.equal(env.state.calls.filter(c => c[0] === 'release').length, 2);
  assert.equal(env.state.calls.filter(c => c[0] === 'close').length, 1);
  assert.equal(env.state.controls.filter(c => c.bRequest === 0x22 && c.wValue === 3).length, 0);
});

test('close while DTR is awaited deasserts after completion before allowing a new open', async () => {
  const control = deferred();
  const env = setup({ controlHook: params => params.bRequest === 0x22 && params.wValue === 3 ? control.promise : Promise.resolve(params.wLength) });
  const port = env.bridge.getPorts()[0];
  const pending = env.bridge.open(1, `harmony-serial:${port.portId}`, 115200, 8, 0, 0, false);
  await tick(); env.bridge.close(1); control.resolve(0);
  await assert.rejects(pending, error => error.code === 31400007); await tick();
  assert.equal(env.state.controls.filter(c => c.bRequest === 0x22 && c.wValue === 0).length, 1);
  assert.equal(env.state.calls.filter(c => c[0] === 'close').length, 1);
});

test('short read/write retain actualLength and transfer API uses bounded buffers and deadlines', async () => {
  const env = setup(); await openCdc(env);
  const reading = env.bridge.read(1);
  const read = env.state.transfers.at(-1);
  assert.equal(read.endpoint, 0x83); assert.equal(read.length, 8192); assert.equal(read.flags, 0);
  assert.equal(read.type, 2); assert.equal(read.isoPacketCount, 0); assert.ok(read.timeout >= 100 && read.timeout <= 250);
  assert.ok(read.userData instanceof Uint8Array);
  complete(read, 0, [1, 2, 3]); assert.deepEqual(Array.from(await reading), [1, 2, 3]);
  const writing = env.bridge.write(1, new Uint8Array([4, 5, 6]));
  const write = env.state.transfers.at(-1);
  assert.equal(write.endpoint, 2); assert.equal(write.timeout, 1000); assert.equal(write.length, 3);
  complete(write, 0, [], 2); assert.equal(await writing, 2);
  env.bridge.close(1); await tick();
});

test('read timeout preserves bytes or is idle; disconnect/stall and invalid lengths reject', async () => {
  const env = setup(); await openCdc(env);
  let pending = env.bridge.read(1); complete(env.state.transfers.at(-1), 2); assert.equal((await pending).length, 0);
  pending = env.bridge.read(1); complete(env.state.transfers.at(-1), 2, [6, 7]); assert.deepEqual(Array.from(await pending), [6, 7]);
  for (const [status, actualLength] of [[5, 0], [4, 0], [0, 8193], [0, -1]]) {
    pending = env.bridge.read(1); complete(env.state.transfers.at(-1), status, [], actualLength);
    await assert.rejects(pending);
  }
  pending = env.bridge.write(1, new Uint8Array([1])); complete(env.state.transfers.at(-1), 2); await assert.rejects(pending);
  pending = env.bridge.write(1, new Uint8Array([1, 2])); complete(env.state.transfers.at(-1), 2, [], 1); assert.equal(await pending, 1);
  env.bridge.close(1); await tick();
});

test('close cancels original pending transfers and late callbacks cannot affect a replacement session', async () => {
  const env = setup(); await openCdc(env);
  const reading = env.bridge.read(1); const oldRead = env.state.transfers.at(-1);
  const writing = env.bridge.write(1, new Uint8Array([9])); const oldWrite = env.state.transfers.at(-1);
  env.bridge.close(1);
  await assert.rejects(reading, error => error.code === 31400007);
  await assert.rejects(writing, error => error.code === 31400007);
  assert.equal(env.state.canceled[0], oldRead); assert.equal(env.state.canceled[1], oldWrite);
  await tick();
  assert.equal(env.state.calls.filter(c => c[0] === 'release' || c[0] === 'close').length, 0);
  await assert.rejects(openCdc(env), error => error.code === 31400004);
  complete(oldRead, 0, [99]); await tick();
  assert.equal(env.state.calls.filter(c => c[0] === 'release' || c[0] === 'close').length, 0);
  complete(oldWrite, 0, [], 1); await tick(); await openCdc(env);
  complete(oldRead, 0, [99]); complete(oldWrite, 0, [], 1);
  const fresh = env.bridge.read(1); complete(env.state.transfers.at(-1), 0, [5]); assert.deepEqual(Array.from(await fresh), [5]);
  env.bridge.close(1); await tick();
});

test('same-direction concurrent transfer is rejected; independent read and write work', async () => {
  const env = setup(); await openCdc(env);
  const first = env.bridge.read(1);
  await assert.rejects(env.bridge.read(1));
  const writing = env.bridge.write(1, new Uint8Array([1]));
  complete(env.state.transfers.at(-1), 0, [], 1); assert.equal(await writing, 1);
  complete(env.state.transfers[0], 0); await first;
  env.bridge.close(1); await tick();
});

test('partial claim failure closes the pipe and frees only acquired interfaces', async () => {
  const env = setup(); let count = 0;
  env.usb.claimInterface = (pipe, intf) => { env.state.calls.push(['claim', intf.id]); return ++count === 2 ? -1 : 0; };
  await assert.rejects(openCdc(env));
  await tick();
  assert.equal(env.state.calls.filter(c => c[0] === 'release').length, 1);
  assert.equal(env.state.calls.filter(c => c[0] === 'close').length, 1);
  assert.match(env.bridge.getDiscoveryState(), /lastFailure=\[backend=cdc state=failed stage=claim-data code=14400007\]/);
});

test('failed controls and callback BusinessError reject and retain diagnostic failure stage', async () => {
  const env = setup({ controlHook: () => Promise.reject({ code: 14400008, message: 'gone' }) });
  await assert.rejects(openCdc(env), error => error.code === 14400008); await tick();
  assert.equal(env.state.calls.filter(c => c[0] === 'close').length, 1);
  assert.match(env.bridge.getDiscoveryState(), /stage=line-coding code=14400008/);
  env.state.controlHook = undefined; await openCdc(env);
  const reading = env.bridge.read(1);
  complete(env.state.transfers.at(-1), 0, [], 0, { code: 14400012, message: 'io' });
  await assert.rejects(reading, error => error.code === 14400012);
  env.bridge.close(1); await tick(); env.bridge.getPorts();
  assert.match(env.bridge.getDiscoveryState(), /lastFailure=\[backend=cdc state=failed stage=read code=14400012\]/);
});

test('DTR force-low is applied before CDC becomes ready', async () => {
  const env = setup();
  const port = env.bridge.getPorts()[0];
  await env.bridge.open(1, `harmony-serial:${port.portId}`, 115200, 8, 0, 0, false, true);
  assert.equal(env.state.controls.filter(c => c.bRequest === 0x22).at(-1).wValue, 2);
  assert.equal(env.state.controls.some(c => c.bRequest === 0x22 && (c.wValue & 1) !== 0), false);
  env.bridge.close(1); await tick();
});
test('native serial rejects DTR force-low without opening the port', async () => {
  const env = setup({ devices: [], nativePorts: [{portId: 42, deviceName: 'native'}], serialCapability: true });
  await assert.rejects(env.bridge.open(1, 'harmony-serial:42', 115200, 8, 0, 0, false, true),
    error => error.code === 801 && error.message.includes('DTR'));
  assert.equal(env.state.calls.some(c => c[0] === 'native-open'), false);
});

test('USB serial identity survives normal to bootloader enumeration without I/O', () => {
  const env = setup({ devices: [cdc('application-path', { serial: 'board-A', productId: 17 })] });
  const application = env.bridge.getPorts()[0];
  assert.equal(application.serialNumber, 'board-A');
  env.state.devices = [cdc('bootloader-path', { serial: 'board-A', productId: 18 })];
  const bootloader = env.bridge.getPorts()[0];
  assert.equal(bootloader.serialNumber, application.serialNumber);
  assert.notEqual(bootloader.portId, application.portId);
  assert.equal(env.state.calls.length, 0);
  assert.ok(!env.state.logs.join('\n').includes('board-A'));
});
test('native serial receives the matched USB identity and missing identities remain empty', () => {
  const env = setup({ serialCapability: true, nativePorts: [{ portId: 42, deviceName: 'private-usb-name' }] });
  assert.equal(env.bridge.getPorts()[0].serialNumber, 'private-serial');
  env.state.devices[0].serial = '  ';
  assert.equal(env.bridge.getPorts()[0].serialNumber, '');
  env.state.devices = [];
  assert.equal(env.bridge.getPorts()[0].serialNumber, '');
  assert.equal(env.state.calls.length, 0);
});

(async () => {
  let failures = 0;
  const filter = process.env.QGC_TEST_FILTER;
  for (const { name, run } of tests.filter(t => !filter || t.name.includes(filter))) {
    try { await run(); console.log(`PASS ${name}`); }
    catch (error) { failures++; console.error(`FAIL ${name}\n${error.stack}`); }
  }
  console.log(`${tests.filter(t => !filter || t.name.includes(filter)).length - failures} passed, ${failures} failed`);
  process.exitCode = failures ? 1 : 0;
})();
