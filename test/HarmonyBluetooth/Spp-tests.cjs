'use strict';

const assert = require('node:assert/strict');
const test = require('node:test');
const { setup, deferred, failure, tick, bounded, plain } = require('./spp-harness.cjs');
const address = '12:34:56:78:90:AB';
const otherAddress = '12:34:56:78:90:CD';
const code = expected => error => error.code === expected;

test('SPP capability, denied permission and disabled Bluetooth reject scan and open before native work', async () => {
  for (const [options, expected] of [[{ capable: false }, 801], [{ granted: false }, 201], [{ enabled: false }, 2900003]]) {
    const { bridge, state, context } = setup(options);
    await assert.rejects(bridge.startScan(1, context), code(expected));
    await assert.rejects(bridge.open(2, address, context), code(expected));
    assert.equal(state.listeners.size, 0);
    assert.equal(state.count('startDiscovery'), 0);
    assert.equal(state.connects.length, 0);
    assert.equal(state.count('permission'), options.capable === false ? 0 : 2);
    for (const call of state.calls.filter(call => call[0] === 'permission')) {
      assert.equal(call[1], context);
      assert.deepEqual(call[2], ['ohos.permission.ACCESS_BLUETOOTH']);
    }
  }
});

test('SPP permission service errors retain their code and create no discovery listeners', async () => {
  const { bridge, state, context } = setup({ permissionError: failure(12100001) });
  await assert.rejects(bridge.startScan(1, context), code(12100001));
  await assert.rejects(bridge.open(2, address, context), code(12100001));
  assert.equal(state.listeners.size, 0);
  assert.equal(state.connects.length, 0);
});

test('SPP discovery includes paired and active peers and keeps same-name addresses distinct', async () => {
  const { bridge, state, context } = setup({ paired: [address], names: new Map([[address, 'Radio'], [otherAddress, 'Radio']]) });
  const first = await bridge.startScan(1, context);
  assert.deepEqual(plain(first.devices), [{ address, name: 'Radio' }]);
  assert.equal(first.scanning, true);
  state.emitDevices([address, otherAddress]);
  assert.deepEqual(plain(bridge.scan(1).devices), [{ address, name: 'Radio' }, { address: otherAddress, name: 'Radio' }]);
  assert.deepEqual(plain(bridge.scan(1).devices), []);
  bridge.stopScan(1);
  assert.equal(state.listeners.size, 0);
});

test('SPP discovery falls back to the address if a peer name lookup fails', async () => {
  const { bridge, context } = setup({ paired: [address], nameError: failure(2900099) });
  assert.deepEqual(plain((await bridge.startScan(1, context)).devices), [{ address, name: address }]);
  bridge.stopScan(1);
});

test('SPP shared discovery owns one native scan and removes only each session listener', async () => {
  const { bridge, state, context } = setup();
  await bridge.startScan(1, context);
  await bridge.startScan(2, context);
  assert.equal(state.count('startDiscovery'), 1);
  assert.equal(state.listeners.size, 2);
  bridge.stopScan(1);
  assert.equal(state.count('stopDiscovery'), 0);
  assert.equal(state.listeners.size, 1);
  state.emitDevices([otherAddress]);
  assert.equal(bridge.scan(2).devices[0].address, otherAddress);
  bridge.stopScan(2);
  bridge.stopScan(2);
  assert.equal(state.listeners.size, 0);
  assert.equal(state.count('off'), 2);
  assert.equal(state.count('stopDiscovery'), 1);
});

test('SPP stopping an externally started discovery does not stop the external scan', async () => {
  const { bridge, state, context } = setup({ discovering: true });
  await bridge.startScan(1, context);
  bridge.stopScan(1);
  assert.equal(state.count('startDiscovery'), 0);
  assert.equal(state.count('stopDiscovery'), 0);
  assert.equal(state.discovering, true);
  assert.equal(state.listeners.size, 0);
});

test('SPP duplicate discovery session is rejected without replacing or leaking its listener', async () => {
  const { bridge, state, context } = setup();
  await bridge.startScan(1, context);
  try {
    await assert.rejects(bridge.startScan(1, context), code(2900099));
    assert.equal(state.listeners.size, 1);
  } finally {
    bridge.stopScan(1);
  }
  assert.equal(state.listeners.size, 0);
});

test('SPP discovery start, registration and paired enumeration failures clean their session', async () => {
  for (const errorProperty of ['startError', 'onError', 'pairedError']) {
    const { bridge, state, context } = setup({ [errorProperty]: failure(2900099) });
    await assert.rejects(bridge.startScan(1, context), code(2900099));
    assert.equal(state.listeners.size, 0, errorProperty);
    state[errorProperty] = undefined;
    await bridge.startScan(1, context);
    bridge.stopScan(1);
    assert.equal(state.listeners.size, 0);
  }
});

test('SPP stopping discovery during a pending permission cannot install a late listener', async () => {
  const permission = deferred();
  const { bridge, state, context } = setup({ permission });
  const pending = bridge.startScan(1, context);
  const rejected = assert.rejects(pending, code(2900099));
  bridge.stopScan(1);
  permission.resolve({ authResults: [0] });
  await rejected;
  assert.equal(state.count('on'), 0);
  assert.equal(state.count('startDiscovery'), 0);
});

test('SPP polling failure removes the discovery listener', async () => {
  const { bridge, state, context } = setup();
  await bridge.startScan(1, context);
  state.discoveryStateError = failure(2900001);
  assert.throws(() => bridge.scan(1), code(2900001));
  assert.equal(state.listeners.size, 0);
});

test('SPP listener removal errors still attempt to stop the owned native discovery', async () => {
  const { bridge, state, context } = setup();
  await bridge.startScan(1, context);
  state.offError = failure(2900099);
  bridge.stopScan(1);
  assert.equal(state.count('stopDiscovery'), 1);
  assert.equal(state.discovering, false);
  assert.equal(state.logs.length, 1);
});

test('SPP uses the standard serial UUID, secure RFCOMM and accepts socket zero', async () => {
  const { bridge, state, context } = setup();
  await bridge.open(1, address, context);
  assert.equal(state.connects[0].address, address);
  assert.deepEqual(state.connects[0].config, { uuid: '00001101-0000-1000-8000-00805F9B34FB', secure: true, type: 0 });
  bridge.close(1);
  bridge.close(1);
  assert.deepEqual(state.closes, [0]);
});

test('SPP callback and synchronous connection failures preserve the native error', async () => {
  for (const options of [{ connectError: failure(2900004) }, { connectThrow: failure(2900099) }]) {
    const { bridge, state, context } = setup(options);
    await assert.rejects(bridge.open(1, address, context), code((options.connectError || options.connectThrow).code));
    assert.deepEqual(state.closes, []);
    state.connectError = state.connectThrow = undefined;
    await bridge.open(1, address, context);
    bridge.close(1);
  }
});

test('SPP duplicate open rejects without overwriting an active socket', async () => {
  const { bridge, state, context } = setup();
  await bridge.open(1, address, context);
  try {
    await assert.rejects(bridge.open(1, otherAddress, context), code(2900099));
    assert.equal(state.connects.length, 1);
  } finally {
    bridge.close(1);
  }
  assert.deepEqual(state.closes, [0]);
});

test('SPP close cancels an open permission wait without waiting for the system dialog', async () => {
  const permission = deferred();
  const { bridge, state, context } = setup({ permission });
  const pending = bridge.open(1, address, context);
  const rejected = assert.rejects(bounded(pending), code(2901054));
  bridge.close(1);
  try {
    await rejected;
  } finally {
    permission.resolve({ authResults: [0] });
    await tick();
  }
  assert.equal(state.connects.length, 0);
});

test('SPP close cancels a pending connection and closes a socket returned late', async () => {
  const { bridge, state, context } = setup({ autoConnect: false });
  const pending = bridge.open(1, address, context);
  const rejected = assert.rejects(bounded(pending), code(2901054));
  await tick();
  bridge.close(1);
  try {
    await rejected;
  } finally {
    state.connects[0].callback(undefined, 31);
    await tick();
  }
  assert.deepEqual(state.closes, [31]);
});

test('SPP late old connection callback cannot close a replacement session', async () => {
  const { bridge, state, context } = setup({ autoConnect: false });
  const previous = bridge.open(1, address, context);
  const previousRejected = assert.rejects(bounded(previous), code(2901054));
  await tick();
  bridge.close(1);
  const current = bridge.open(1, otherAddress, context);
  await tick();
  state.connects[1].callback(undefined, 42);
  await current;
  state.connects[0].callback(undefined, 41);
  await previousRejected;
  assert.deepEqual(state.closes, [41]);
  assert.equal(await bridge.write(1, Uint8Array.of(7)), 1);
  bridge.close(1);
  assert.deepEqual(state.closes, [41, 42]);
});

test('SPP read transfers raw bytes and rejects reads after close', async () => {
  const { bridge, state, context } = setup();
  await bridge.open(1, address, context);
  const reading = bridge.read(1);
  state.reads[0].resolve(Uint8Array.of(0, 255, 3).buffer);
  assert.deepEqual([...(await reading)], [0, 255, 3]);
  bridge.close(1);
  await assert.rejects(bridge.read(1), code(2901054));
});

test('SPP overlapping reads reject before issuing a second unsupported native read', async () => {
  const { bridge, state, context } = setup();
  await bridge.open(1, address, context);
  const reading = bridge.read(1);
  try {
    await assert.rejects(bounded(bridge.read(1)), code(2900099));
    assert.equal(state.reads.length, 1);
  } finally {
    state.reads.forEach(read => read.resolve(Uint8Array.of(1).buffer));
    await reading;
    bridge.close(1);
  }
});

test('SPP receive buffer accepts 8192 bytes and closes on overflow', async () => {
  const { bridge, state, context } = setup();
  await bridge.open(1, address, context);
  const first = bridge.read(1);
  state.reads[0].resolve(new ArrayBuffer(8192));
  assert.equal((await first).length, 8192);
  const overflowing = bridge.read(1);
  const rejected = assert.rejects(overflowing, code(2901054));
  state.reads[1].resolve(new ArrayBuffer(8193));
  await rejected;
  assert.deepEqual(state.closes, [0]);
});

test('SPP close settles a blocked read even when native close fails or has no callback', async () => {
  for (const closeError of [undefined, failure(2900099)]) {
    const { bridge, state, context } = setup({ settleReadOnClose: false, closeError });
    await bridge.open(1, address, context);
    const rejected = assert.rejects(bounded(bridge.read(1)), code(2901054));
    bridge.close(1);
    try {
      await rejected;
    } finally {
      state.reads[0].resolve(Uint8Array.of(99).buffer);
      await tick();
    }
    assert.deepEqual(state.closes, [0]);
  }
});

test('SPP native disconnect rejects pending read and closes the bridge session', async () => {
  const { bridge, state, context } = setup();
  await bridge.open(1, address, context);
  const rejected = assert.rejects(bridge.read(1), code(2901054));
  state.reads[0].reject(failure(2901054, 'Remote socket disconnected'));
  await rejected;
  assert.deepEqual(state.closes, [0]);
  await assert.rejects(bridge.write(1, Uint8Array.of(1)), code(2901054));
});

test('SPP write validates byte bounds and copies only the submitted subarray', async () => {
  const { bridge, state, context } = setup();
  await bridge.open(1, address, context);
  await assert.rejects(bridge.write(1, new Uint8Array(0)), code(401));
  await assert.rejects(bridge.write(1, new Uint8Array(4097)), code(401));
  assert.equal(state.writes.length, 0);
  assert.equal(await bridge.write(1, new Uint8Array(4096)), 4096);
  const bytes = Uint8Array.of(99, 10, 20, 30, 88);
  assert.equal(await bridge.write(1, bytes.subarray(1, 4)), 3);
  assert.deepEqual(state.writes[1].bytes, [10, 20, 30]);
  bridge.close(1);
});

test('SPP native write errors close the session and reject a pending read', async () => {
  for (const errorProperty of ['writeError', 'writeThrow']) {
    const { bridge, state, context } = setup({ [errorProperty]: failure(2900099) });
    await bridge.open(1, address, context);
    const readRejected = assert.rejects(bounded(bridge.read(1)), code(2901054));
    await assert.rejects(bridge.write(1, Uint8Array.of(1)), code(2900099));
    try {
      await readRejected;
      assert.deepEqual(state.closes, [0]);
    } finally {
      bridge.close(1);
    }
  }
});

test('SPP close rejects a blocked write and ignores the old completion after session reuse', async () => {
  const { bridge, state, context } = setup({ blockWrites: true });
  await bridge.open(1, address, context);
  const oldWrite = assert.rejects(bounded(bridge.write(1, Uint8Array.of(1))), code(2901054));
  bridge.close(1);
  await bridge.open(1, otherAddress, context);
  try {
    await oldWrite;
  } finally {
    state.writes[0].resolve();
    await tick();
  }
  state.blockWrites = false;
  assert.equal(await bridge.write(1, Uint8Array.of(2)), 1);
  assert.equal(state.writes[1].socketId, 1);
  bridge.close(1);
  assert.deepEqual(state.closes, [0, 1]);
});

test('SPP missing connect callback has a bounded deadline and any late socket is closed', async () => {
  const { bridge, state, context } = setup({ autoConnect: false });
  const pending = bridge.open(1, address, context);
  const rejected = assert.rejects(bounded(pending), code(2901054));
  rejected.catch(() => {});
  await tick();
  try {
    assert.equal(state.timers.size, 1);
    assert.ok([...state.timers.values()].every(timer => timer.milliseconds > 0 && timer.milliseconds <= 30000));
    state.expireTimers();
    await rejected;
  } finally {
    state.connects[0].callback(undefined, 51);
    await tick();
    await rejected.catch(() => {});
  }
  assert.deepEqual(state.closes, [51]);
  assert.equal(state.timers.size, 0);
});

test('SPP stalled write reaches its deadline, closes native socket and rejects pending read', async () => {
  const { bridge, state, context } = setup({ blockWrites: true, settleReadOnClose: false });
  await bridge.open(1, address, context);
  const readRejected = assert.rejects(bounded(bridge.read(1)), code(2901054));
  const writeRejected = assert.rejects(bounded(bridge.write(1, Uint8Array.of(1))), code(2901054));
  readRejected.catch(() => {});
  writeRejected.catch(() => {});
  try {
    assert.equal(state.timers.size, 1, 'Only writes have a deadline; idle reads must not time out');
    assert.ok([...state.timers.values()].every(timer => timer.milliseconds > 0 && timer.milliseconds <= 5000));
    state.expireTimers();
    await writeRejected;
    await readRejected;
  } finally {
    bridge.close(1);
    state.reads[0].resolve(Uint8Array.of(1).buffer);
    state.writes[0].resolve();
    await Promise.all([readRejected.catch(() => {}), writeRejected.catch(() => {})]);
  }
  assert.deepEqual(state.closes, [0]);
  assert.equal(state.timers.size, 0);
});

test('SPP native discovery completion removes every shared-session listener', async () => {
  const { bridge, state, context } = setup();
  await bridge.startScan(1, context);
  await bridge.startScan(2, context);
  state.discovering = false;
  assert.equal(bridge.scan(1).scanning, false);
  assert.equal(bridge.scan(2).scanning, false);
  assert.equal(state.listeners.size, 0);
  assert.equal(state.count('off'), 2);
  assert.equal(state.count('stopDiscovery'), 0);
});

test('SPP synchronous native read error closes its socket session', async () => {
  const { bridge, state, context } = setup({ readThrow: failure(2901054) });
  await bridge.open(1, address, context);
  await assert.rejects(bridge.read(1), code(2901054));
  assert.deepEqual(state.closes, [0]);
  await assert.rejects(bridge.write(1, Uint8Array.of(1)), code(2901054));
});

test('SPP late read completion after session reuse cannot satisfy the replacement read', async () => {
  const { bridge, state, context } = setup({ settleReadOnClose: false });
  await bridge.open(1, address, context);
  const oldRead = assert.rejects(bounded(bridge.read(1)), code(2901054));
  bridge.close(1);
  await bridge.open(1, otherAddress, context);
  const current = bridge.read(1);
  let currentFinished = false;
  current.then(() => { currentFinished = true; }, () => {});
  state.reads[0].resolve(Uint8Array.of(99).buffer);
  try {
    await oldRead;
    await tick();
    assert.equal(currentFinished, false);
    state.reads[1].resolve(Uint8Array.of(1, 2).buffer);
    assert.deepEqual([...(await current)], [1, 2]);
  } finally {
    state.reads[1].resolve(Uint8Array.of(1, 2).buffer);
    await current.catch(() => {});
    bridge.close(1);
  }
  assert.deepEqual(state.closes, [0, 1]);
});

test('SPP capability rejects missing native socket APIs before authorization', async () => {
  const env = setup();
  delete env.socket.sppReadAsync;
  assert.equal(env.bridge.isSupported(), false);
  await assert.rejects(env.bridge.open(1, address, env.context), code(801));
  assert.equal(env.state.calls.length, 0);
});