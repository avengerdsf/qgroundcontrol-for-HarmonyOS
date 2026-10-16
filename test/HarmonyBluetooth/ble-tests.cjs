'use strict';
const assert = require('node:assert/strict');
const { setup, NUS, HM10, ADDRESS, CCCD, services, deferred, flush, connected, open, notify } = require('./ble-harness.cjs');
const tests = [];
const test = (name, run) => tests.push([name, run]);

test('independent BLE scanner requires permission and deduplicates connectable devices', async () => {
  const env = setup(); assert.equal(env.bridge.isSupported(), true);
  const snapshot = await env.bridge.startScan(1, {}); assert.equal(snapshot.scanning, true);
  assert.deepEqual(Array.from(env.state.calls.find(call => call[0] === 'permission')[2]), ['ohos.permission.ACCESS_BLUETOOTH']);
  const scanner = env.state.scanners[0]; assert.ok(scanner);
  const found = { deviceId: ADDRESS, deviceName: 'UART', connectable: true, rssi: -40, data: new ArrayBuffer(0) };
  scanner.emit('BLEDeviceFind', { reportType: 1, scanResult: [found, found, { ...found, deviceId: '22:33:44:55:66:77', connectable: false }] });
  assert.deepEqual(JSON.parse(JSON.stringify(env.bridge.scan(1))), { devices: [{ address: ADDRESS, name: 'UART' }], scanning: true });
  scanner.emit('BLEDeviceFind', { reportType: 1, scanResult: [found] }); assert.equal(env.bridge.scan(1).devices.length, 0);
  scanner.emit('BLEDeviceFind', { reportType: 1, scanResult: [{ ...found, deviceName: '' }] });
  assert.equal(env.bridge.scan(1).devices.length, 0, 'an unnamed advertisement must not erase the known device name');
  env.bridge.stopScan(1); await flush(); assert.equal(scanner.listeners.size, 0); assert.equal(env.access.listeners.size, 0);
});

test('open waits for connected event, exact service/characteristics and successful subscription', async () => {
  const subscribeGate = deferred(), env = setup({ subscribeGate }); let settled = false;
  const opening = env.bridge.open(1, ADDRESS, {}, NUS).then(() => { settled = true; });
  await flush(); assert.equal(settled, false); assert.equal(env.state.calls.filter(call => call[0] === 'services').length, 0);
  const client = env.state.clients[0]; assert.ok(client);
  client.emit('BLEConnectionStateChange', { deviceId: ADDRESS, state: 2 }); await flush();
  assert.equal(settled, false); assert.equal(env.state.calls.filter(call => call[0] === 'notify').length, 1);
  subscribeGate.resolve(); await opening;
  assert.equal(await env.bridge.write(1, new Uint8Array([0xfd, 1, 2])), 3);
  assert.deepEqual(env.state.calls.filter(call => call[0] === 'write').map(call => [call[3], call[4]]), [[1, [0xfd, 1, 2]]]);
  env.bridge.close(1); assert.equal(client.listeners.size, 0);
});

test('default MTU fragments writes into 20 bytes and copies notifications into the byte stream', async () => {
  const env = setup(), client = await open(env), bytes = Uint8Array.from({ length: 45 }, (_, index) => index);
  assert.equal(await env.bridge.write(1, bytes), 45);
  const writes = env.state.calls.filter(call => call[0] === 'write');
  assert.deepEqual(writes.map(call => call[4].length), [20, 20, 5]); assert.deepEqual(writes.flatMap(call => call[4]), Array.from(bytes));
  const reading = env.bridge.read(1); notify(client, NUS, [0xfd, 5, 6]); assert.deepEqual(Array.from(await reading), [0xfd, 5, 6]);
  env.bridge.close(1);
});

test('HM10 same characteristic and custom UUIDs use only configured GATT objects', async () => {
  const custom = { serviceUuid: '12345678-1111-2222-3333-444455556666', writeUuid: '12345679-1111-2222-3333-444455556666', notifyUuid: '1234567a-1111-2222-3333-444455556666' };
  for (const options of [HM10, custom]) {
    const env = setup({ services: services(options) }), client = await open(env, 1, ADDRESS, options);
    await env.bridge.write(1, new Uint8Array([1, 2]));
    const sent = env.state.calls.find(call => call[0] === 'write');
    assert.equal(sent[2].serviceUuid, options.serviceUuid); assert.equal(sent[2].characteristicUuid, options.writeUuid);
    notify(client, options, [3, 4]); assert.deepEqual(Array.from(await env.bridge.read(1)), [3, 4]);
    env.bridge.close(1);
  }
});

test('short and uppercase standard UUIDs match canonical discovered characteristics', async () => {
  const env = setup({ services: services(HM10) });
  const client = await open(env, 1, ADDRESS.toLowerCase(), { serviceUuid: 'FFE0', writeUuid: '0000FFE1', notifyUuid: 'FFE1' });
  notify(client, HM10, [5]); assert.deepEqual(Array.from(await env.bridge.read(1)), [5]); env.bridge.close(1);
});

test('write response is preferred while notify or indicate subscription follows properties', async () => {
  const env = setup({ services: services(NUS, { write: { write: true, writeNoResponse: true }, notify: { notify: false, indicate: true } }) });
  await open(env); assert.equal(env.state.calls.filter(call => call[0] === 'indicate').length, 1);
  assert.equal(env.state.calls.filter(call => call[0] === 'notify').length, 0);
  await env.bridge.write(1, new Uint8Array([1])); assert.equal(env.state.calls.find(call => call[0] === 'write')[3], 1); env.bridge.close(1);
  const noResponse = setup({ services: services(NUS, { write: { write: false, writeNoResponse: true } }) });
  await open(noResponse); await noResponse.bridge.write(1, new Uint8Array([1]));
  assert.equal(noResponse.state.calls.find(call => call[0] === 'write')[3], 2); noResponse.bridge.close(1);
});

test('wrong service, wrong characteristic, absent properties and CCCD are rejected before subscription or write', async () => {
  const variants = [];
  variants.push([]); variants.push([...services(), ...services()]);
  const wrongWrite = services(); wrongWrite[0].characteristics[0].characteristicUuid = HM10.writeUuid; variants.push(wrongWrite);
  const notWritable = services(); notWritable[0].characteristics[0].properties = { read: true }; variants.push(notWritable);
  const noProperties = services(); delete noProperties[0].characteristics[1].properties; variants.push(noProperties);
  const noCccd = services(); noCccd[0].characteristics[1].descriptors = []; variants.push(noCccd);
  const wrongCccd = services(); wrongCccd[0].characteristics[1].descriptors[0].characteristicUuid = NUS.writeUuid; variants.push(wrongCccd);
  const duplicate = services(); duplicate[0].characteristics.push(duplicate[0].characteristics[0]); variants.push(duplicate);
  for (const discovered of variants) {
    const env = setup({ services: discovered });
    const pending = env.bridge.open(1, ADDRESS, {}, NUS), rejected = assert.rejects(pending, error => error.code === 401);
    await flush(); env.state.clients[0].emit('BLEConnectionStateChange', { deviceId: ADDRESS, state: 2 }); await rejected;
    assert.equal(env.state.calls.filter(call => ['write', 'notify', 'indicate'].includes(call[0])).length, 0);
    assert.equal(env.state.clients[0].listeners.size, 0); assert.equal(env.clock.jobs.size, 0);
  }
});

test('invalid options and unsupported capability never create a native client', async () => {
  const env = setup();
  for (const [address, options] of [['', NUS], [ADDRESS, { ...NUS, writeUuid: 'invalid' }], [ADDRESS, null]]) {
    await assert.rejects(env.bridge.open(1, address, {}, options), error => error.code === 401);
  }
  assert.equal(env.state.calls.length, 0);
  const unsupported = setup({ supported: false }); assert.equal(unsupported.bridge.isSupported(), false);
  await assert.rejects(unsupported.bridge.open(1, ADDRESS, {}, NUS), error => error.code === 801);
  assert.equal(unsupported.state.clients.length, 0); assert.equal(unsupported.state.calls.length, 0);
});

test('subscription failure prevents ready and closes GATT with precise error', async () => {
  const env = setup({ subscribeError: { code: 2900099, message: 'CCCD rejected' } });
  const pending = env.bridge.open(1, ADDRESS, {}, NUS), rejected = assert.rejects(pending, /CCCD rejected/);
  await flush(); env.state.clients[0].emit('BLEConnectionStateChange', { deviceId: ADDRESS, state: 2 }); await rejected;
  assert.equal(env.state.calls.filter(call => call[0] === 'close').length, 1);
  await assert.rejects(env.bridge.write(1, new Uint8Array([1]))); assert.equal(env.state.calls.filter(call => call[0] === 'write').length, 0);
});

test('permission denied, Bluetooth off and BLE-only state obey platform state', async () => {
  for (const [options, code] of [[{ grant: false }, 201], [{ power: 0 }, 2900003]]) {
    const env = setup(options); await assert.rejects(env.bridge.startScan(1, {}), error => error.code === code);
    await assert.rejects(env.bridge.open(1, ADDRESS, {}, NUS), error => error.code === code);
    assert.equal(env.state.clients.length + env.state.scanners.length, 0);
  }
  const bleOnly = setup({ power: 5 }); await open(bleOnly); bleOnly.bridge.close(1);
});

test('negotiated MTU controls fragment boundaries and invalid MTU events are ignored', async () => {
  const env = setup({ autoMtu: 100 }), client = await open(env);
  for (const mtu of [0, 22, 518, 25.5]) client.emit('BLEMtuChange', mtu);
  await env.bridge.write(1, new Uint8Array(200));
  assert.deepEqual(env.state.calls.filter(call => call[0] === 'write').map(call => call[4].length), [97, 97, 6]); env.bridge.close(1);
});

test('ATT characteristic values never exceed 512 bytes even with MTU517', async () => {
  const env = setup({ autoMtu: 517 }); await open(env);
  assert.equal(await env.bridge.write(1, new Uint8Array(4096)), 4096);
  const sent = env.state.calls.filter(call => call[0] === 'write');
  assert.deepEqual(sent.map(call => call[4].length), new Array(8).fill(512)); env.bridge.close(1);
});

test('unsupported or silent MTU requests fall back to 23 with a bounded virtual deadline', async () => {
  for (const options of [{ mtuError: { code: 801, message: 'unavailable' } }, { autoMtu: 0 }, { listenError: 'BLEMtuChange' }]) {
    const env = setup(options); await open(env); await env.bridge.write(1, new Uint8Array(21));
    assert.deepEqual(env.state.calls.filter(call => call[0] === 'write').map(call => call[4].length), [20, 1]);
    env.bridge.close(1); assert.equal(env.clock.jobs.size, 0);
  }
});

test('notifications filter service/characteristic and copy native buffers into an ordered bounded-read FIFO', async () => {
  const env = setup(), client = await open(env), source = new Uint8Array([1, 2, 3]);
  notify(client, { ...NUS, serviceUuid: HM10.serviceUuid }, [99]);
  notify(client, { ...NUS, notifyUuid: NUS.writeUuid }, [98]);
  client.emit('BLECharacteristicChange', { serviceUuid: NUS.serviceUuid, characteristicUuid: NUS.notifyUuid, characteristicValue: source.buffer, descriptors: [] });
  source.fill(55); notify(client, NUS, [4, 5]);
  assert.deepEqual(Array.from(await env.bridge.read(1)), [1, 2, 3, 4, 5]);
  const bytes = Uint8Array.from({ length: 10240 }, (_, index) => index & 255);
  for (let offset = 0; offset < bytes.length; offset += 512) notify(client, NUS, bytes.slice(offset, offset + 512));
  const first = await env.bridge.read(1), second = await env.bridge.read(1);
  assert.equal(first.length, 8192); assert.equal(second.length, 2048);
  assert.deepEqual([...first, ...second], Array.from(bytes)); env.bridge.close(1);
});

test('64KiB overflow closes the session explicitly and never silently drops MAVLink bytes', async () => {
  const env = setup(), client = await open(env);
  for (let index = 0; index < 128; index++) notify(client, NUS, new Uint8Array(512));
  notify(client, NUS, [1]);
  await assert.rejects(env.bridge.read(1), /overflow/);
  await assert.rejects(env.bridge.write(1, new Uint8Array([1])), /overflow/);
  assert.equal(client.listeners.size, 0); assert.equal(env.state.calls.filter(call => call[0] === 'close').length, 1);
  env.bridge.close(1); assert.equal(env.state.calls.filter(call => call[0] === 'close').length, 1);
});

test('read waits for data, yields bounded idle, rejects concurrent reads, and preserves timeout-boundary arrival', async () => {
  const env = setup(), client = await open(env);
  let reading = env.bridge.read(1); await assert.rejects(env.bridge.read(1), error => error.code === 2900011);
  env.clock.advance(200); assert.equal((await reading).length, 0);
  reading = env.bridge.read(1); env.clock.advance(200); notify(client, NUS, [7]); assert.deepEqual(Array.from(await reading), [7]);
  reading = env.bridge.read(1); notify(client, NUS, [8, 9]); assert.deepEqual(Array.from(await reading), [8, 9]); env.bridge.close(1);
});

test('concurrent writes serialize every GATT request and copy queued application buffers', async () => {
  const gate = deferred(), env = setup(); let calls = 0;
  env.state.writeHook = () => ++calls === 1 ? gate.promise : Promise.resolve(); await open(env);
  const first = env.bridge.write(1, new Uint8Array(25).fill(1)), bytes = new Uint8Array([2, 3]), second = env.bridge.write(1, bytes);
  bytes.fill(99); await flush(); assert.equal(env.state.calls.filter(call => call[0] === 'write').length, 1);
  gate.resolve(); assert.equal(await first, 25); assert.equal(await second, 2);
  assert.deepEqual(env.state.calls.filter(call => call[0] === 'write').map(call => call[4]), [new Array(20).fill(1), new Array(5).fill(1), [2, 3]]);
  env.bridge.close(1);
});

test('invalid write lengths never send data and a rejected fragment fails the stream', async () => {
  const env = setup(); await open(env);
  for (const length of [0, 4097]) await assert.rejects(env.bridge.write(1, new Uint8Array(length)), error => error.code === 401);
  assert.equal(env.state.calls.filter(call => call[0] === 'write').length, 0);
  env.state.writeHook = () => Promise.reject({ code: 2900099, message: 'write rejected' });
  await assert.rejects(env.bridge.write(1, new Uint8Array(50)), /write rejected/);
  assert.equal(env.state.calls.filter(call => call[0] === 'write').length, 1);
  assert.equal(env.state.calls.filter(call => call[0] === 'close').length, 1);
});

test('close rejects pending read and all queued writes before native callbacks arrive', async () => {
  const gate = deferred(), env = setup({ writeHook: () => gate.promise }), client = await open(env);
  const reading = env.bridge.read(1), first = env.bridge.write(1, new Uint8Array(30)), second = env.bridge.write(1, new Uint8Array([1]));
  const rejected = [assert.rejects(reading, error => error.code === 2901003), assert.rejects(first, error => error.code === 2901003), assert.rejects(second, error => error.code === 2901003)];
  await flush(); env.bridge.close(1); await Promise.all(rejected); assert.equal(client.listeners.size, 0); assert.equal(env.clock.jobs.size, 0);
  gate.resolve(); await flush(); assert.equal(env.state.calls.filter(call => call[0] === 'write').length, 1);
});

test('disconnect and Bluetooth power loss reject pending operations with explicit error', async () => {
  for (const power of [false, true]) {
    const env = setup(), client = await open(env), reading = env.bridge.read(1), rejected = assert.rejects(reading, error => error.code === (power ? 2900003 : 2901003));
    if (power) env.access.emit('stateChange', 3); else client.emit('BLEConnectionStateChange', { deviceId: ADDRESS, state: 0 });
    await rejected; assert.equal(client.listeners.size, 0); assert.equal(env.access.listeners.size, 0); env.bridge.close(1);
  }
});

test('same device cannot open concurrently and connection events from another device are ignored', async () => {
  const env = setup(), pending = env.bridge.open(1, ADDRESS, {}, NUS); await flush(); const client = env.state.clients[0];
  client.emit('BLEConnectionStateChange', { deviceId: '22:33:44:55:66:77', state: 2 }); await flush();
  assert.equal(env.state.calls.filter(call => call[0] === 'services').length, 0);
  await assert.rejects(env.bridge.open(2, ADDRESS, {}, NUS), error => error.code === 2900011);
  client.emit('BLEConnectionStateChange', { deviceId: ADDRESS, state: 2 }); await pending; env.bridge.close(1);
});

test('replacing a connecting session closes its client and ignores late old callbacks', async () => {
  const env = setup(), first = env.bridge.open(1, ADDRESS, {}, NUS), firstRejected = assert.rejects(first);
  await flush(); const old = env.state.clients[0], staleConnect = [...old.listeners.get('BLEConnectionStateChange')][0], staleNotify = [...old.listeners.get('BLECharacteristicChange')][0];
  const replacement = env.bridge.open(1, ADDRESS, {}, NUS); await firstRejected; await flush(); const fresh = env.state.clients[1];
  staleConnect({ deviceId: ADDRESS, state: 2 }); staleNotify({ serviceUuid: NUS.serviceUuid, characteristicUuid: NUS.notifyUuid, characteristicValue: new Uint8Array([99]).buffer, descriptors: [] });
  assert.equal(env.state.calls.filter(call => call[0] === 'services').length, 0);
  fresh.emit('BLEConnectionStateChange', { deviceId: ADDRESS, state: 2 }); await replacement;
  notify(fresh, NUS, [5]); assert.deepEqual(Array.from(await env.bridge.read(1)), [5]); assert.equal(old.listeners.size, 0); env.bridge.close(1);
});

test('closing pending permission rejects immediately and a late grant creates no native resources', async () => {
  const permissionGate = deferred(), env = setup({ permissionGate }), pending = env.bridge.open(1, ADDRESS, {}, NUS), rejected = assert.rejects(pending);
  env.bridge.close(1); await rejected; permissionGate.resolve({ authResults: [0] }); await flush(); assert.equal(env.state.clients.length, 0);
  const scanGate = deferred(), scan = setup({ permissionGate: scanGate }), scanning = scan.bridge.startScan(1, {}), stopped = assert.rejects(scanning);
  scan.bridge.stopScan(1); await stopped; scanGate.resolve({ authResults: [0] }); await flush(); assert.equal(scan.state.scanners.length, 0);
});

test('connection, discovery, subscription and write stalls expire on virtual clocks and clean resources', async () => {
  const connection = setup(), pending = connection.bridge.open(1, ADDRESS, {}, NUS), rejected = assert.rejects(pending, /connection timed out/);
  await flush(); connection.clock.advance(10000); await rejected; assert.equal(connection.state.clients[0].listeners.size, 0); assert.equal(connection.clock.jobs.size, 0);
  for (const [option, stage] of [['servicesGate', 'service discovery'], ['subscribeGate', 'subscription']]) {
    const gate = deferred(), env = setup({ [option]: gate }), pending = env.bridge.open(1, ADDRESS, {}, NUS), rejected = assert.rejects(pending, new RegExp(stage + ' timed out'));
    await flush(); env.state.clients[0].emit('BLEConnectionStateChange', { deviceId: ADDRESS, state: 2 }); await flush(); env.clock.advance(8000); await rejected;
    const before = env.state.calls.length; gate.resolve(option === 'servicesGate' ? services() : undefined); await flush(); assert.equal(env.state.calls.length, before);
    assert.equal(env.state.clients[0].listeners.size, 0); assert.equal(env.clock.jobs.size, 0);
  }
  const write = setup({ writeHook: () => new Promise(() => {}) }); await open(write);
  const writing = write.bridge.write(1, new Uint8Array([1])), expired = assert.rejects(writing, /characteristic write timed out/);
  await flush(); write.clock.advance(5000); await expired; assert.equal(write.state.clients[0].listeners.size, 0); assert.equal(write.clock.jobs.size, 0);
});

test('GATT service changes stop the old byte stream instead of using stale attributes', async () => {
  const env = setup(), client = await open(env), reading = env.bridge.read(1), rejected = assert.rejects(reading, /services changed/);
  client.emit('serviceChange'); await rejected; assert.equal(client.listeners.size, 0); env.bridge.close(1);
});

test('scan stop, power loss and replacement clean only their own scanner including late start', async () => {
  const env = setup(); await env.bridge.startScan(1, {}); await env.bridge.startScan(2, {});
  env.bridge.stopScan(1); await flush(); assert.equal(env.state.scanners[0].listeners.size, 0); assert.equal(env.state.scanners[1].listeners.size, 1);
  env.access.emit('stateChange', 0); assert.throws(() => env.bridge.scan(2), error => error.code === 2900003); assert.equal(env.access.listeners.size, 0); env.bridge.stopScan(2);
  const gate = deferred(), delayed = setup({ scanGate: gate }), pending = delayed.bridge.startScan(1, {}), rejected = assert.rejects(pending);
  await flush(); const old = delayed.state.scanners[0]; delayed.bridge.stopScan(1); await rejected;
  gate.resolve(); await flush(); assert.equal(old.listeners.size, 0); assert.ok(delayed.state.calls.filter(call => call[0] === 'scan-stop').length >= 2);
});

test('close during discovery subscription or MTU cannot revive an old session after replacement', async () => {
  for (const phase of ['servicesGate', 'subscribeGate', 'mtu']) {
    const gate = deferred(), env = setup(phase === 'mtu' ? { autoMtu: 0 } : { [phase]: gate });
    const oldOpening = env.bridge.open(1, ADDRESS, {}, NUS), oldRejected = assert.rejects(oldOpening);
    await flush(); const old = env.state.clients[0]; old.emit('BLEConnectionStateChange', { deviceId: ADDRESS, state: 2 }); await flush();
    const oldMtu = [...old.listeners.get('BLEMtuChange')][0];
    env.bridge.close(1); await oldRejected;
    env.state[phase] = undefined; env.state.autoMtu = 23;
    const fresh = await open(env); const count = env.state.calls.length;
    gate.resolve(phase === 'servicesGate' ? services() : undefined); oldMtu(100); await flush();
    assert.equal(env.state.calls.length, count); assert.equal(old.listeners.size, 0);
    await env.bridge.write(1, new Uint8Array(21));
    assert.deepEqual(env.state.calls.filter(call => call[0] === 'write').map(call => call[4].length), [20, 1]);
    notify(fresh, NUS, [8]); assert.deepEqual(Array.from(await env.bridge.read(1)), [8]); env.bridge.close(1);
  }
});

test('overflow cancels an outstanding native write and sends no remaining fragments', async () => {
  const gate = deferred(), env = setup({ writeHook: () => gate.promise }), client = await open(env);
  const pending = env.bridge.write(1, new Uint8Array(45)), rejected = assert.rejects(pending, /overflow/); await flush();
  for (let index = 0; index < 128; index++) notify(client, NUS, new Uint8Array(512)); notify(client, NUS, [1]);
  await rejected; gate.resolve(); await flush();
  assert.equal(env.state.calls.filter(call => call[0] === 'write').length, 1); assert.equal(client.listeners.size, 0); env.bridge.close(1);
});

test('maximum write preserves all 4096 bytes over MTU20 fragments', async () => {
  const env = setup(); await open(env); const data = Uint8Array.from({ length: 4096 }, (_, index) => index & 255);
  assert.equal(await env.bridge.write(1, data), 4096);
  const sent = env.state.calls.filter(call => call[0] === 'write'); assert.equal(sent.length, 205);
  assert.deepEqual(sent.flatMap(call => call[4]), Array.from(data)); env.bridge.close(1);
});

test('scan start and connect API failures stop ownership without leaking listeners', async () => {
  const gate = deferred(), env = setup({ scanGate: gate }), starting = env.bridge.startScan(1, {}), rejected = assert.rejects(starting, /scan start timed out/);
  await flush(); env.clock.advance(8000); await rejected; assert.equal(env.state.scanners[0].listeners.size, 0); assert.equal(env.access.listeners.size, 0);
  gate.resolve(); await flush(); assert.ok(env.state.calls.filter(call => call[0] === 'scan-stop').length >= 2);
  for (const options of [{ connectError: { code: 2900099, message: 'connect failure' } }, { listenError: 'BLECharacteristicChange' }]) {
    const bad = setup(options); await assert.rejects(bad.bridge.open(1, ADDRESS, {}, NUS));
    assert.equal(bad.state.clients[0].listeners.size, 0); assert.equal(bad.state.calls.filter(call => call[0] === 'close').length, 1);
    assert.equal(bad.access.listeners.size, 0); assert.equal(bad.clock.jobs.size, 0);
  }
});

test('scan naturally stops at 30 seconds without affecting an active GATT session', async () => {
  const env = setup(); await env.bridge.startScan(1, {}); const scanner = env.state.scanners[0];
  const client = await open(env, 2); env.clock.advance(30000); await flush();
  assert.equal(scanner.listeners.size, 0); assert.equal(env.bridge.scan(1).scanning, false);
  assert.equal(env.access.listeners.size, 1, 'only the GATT owner must retain its power listener');
  notify(client, NUS, [1, 2]); assert.deepEqual(Array.from(await env.bridge.read(2)), [1, 2]);
  assert.equal(await env.bridge.write(2, new Uint8Array([3])), 1); env.bridge.close(2);
});

test('scan discovery memory is bounded when advertisers continuously rotate addresses', async () => {
  const env = setup(); await env.bridge.startScan(1, {}); const scanner = env.state.scanners[0];
  scanner.emit('BLEDeviceFind', { reportType: 1, scanResult: Array.from({ length: 600 }, (_, index) => ({
    deviceId: '00:11:22:33:' + (index >> 8).toString(16).padStart(2, '0') + ':' + (index & 255).toString(16).padStart(2, '0'),
    deviceName: 'UART-' + index, connectable: true, rssi: -40, data: new ArrayBuffer(0)
  })) });
  const snapshot = env.bridge.scan(1); assert.equal(snapshot.scanning, false); assert.equal(snapshot.devices.length, 512);
  assert.equal(scanner.listeners.size, 0); assert.equal(env.access.listeners.size, 0); assert.equal(env.clock.jobs.size, 0);
});

(async () => {
  let failed = 0;
  for (const [name, run] of tests) { try { await run(); console.log(`PASS ${name}`); } catch (error) { failed++; console.error(`FAIL ${name}\n${error.stack}`); } }
  console.log(`${tests.length - failed} passed, ${failed} failed`); process.exitCode = failed ? 1 : 0;
})();
