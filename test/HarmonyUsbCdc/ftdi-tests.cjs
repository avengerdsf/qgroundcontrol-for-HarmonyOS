'use strict';

const assert = require('node:assert/strict');
const { loadDriver } = require('./driver-harness.cjs');
const { isFtdiDevice, createFtdiDriver } = loadDriver('QGCFtdiDriver');
const line = (changes = {}) => ({ baud: 115200, dataBits: 8, parity: 0, stopBits: 0, ...changes });
function control(failRequest) {
  const calls = [];
  return { calls, async transfer(requestType, request, value, index, data) {
    calls.push({ requestType, request, value, index, data: Array.from(data) });
    if (request === failRequest) throw Object.assign(new Error('mock control failure'), { code: 14400008 });
    return data.length;
  } };
}
const tests = [];
const test = (name, run) => tests.push([name, run]);

test('known FTDI IDs only', () => {
  for (const pid of [0x6001, 0x6010, 0x6011, 0x6014, 0x6015]) assert.equal(isFtdiDevice(0x0403, pid), true);
  for (const [vid, pid] of [[0x0403, 0xffff], [0x10c4, 0x6001], [0, 0], [0x0403, -1]]) assert.equal(isFtdiDevice(vid, pid), false);
});
test('factory rejects unknown product and invalid interface/revision fields', () => {
  for (const iface of [-1, 255, 0.5, NaN]) assert.throws(() => createFtdiDriver(iface, 0x0600, 0x6001), error => error.code === 401);
  for (const bcd of [-1, 0x10000, 0.5, NaN]) assert.throws(() => createFtdiDriver(0, bcd, 0x6001), error => error.code === 401);
  assert.throws(() => createFtdiDriver(0, 0x0600, 0x8372), error => error.code === 401);
});
test('initialization resets UART, sets modem and no flow, then baud and frame', async () => {
  const driver = createFtdiDriver(0, 0x0600, 0x6001), io = control(); await driver.initialize(io, line());
  assert.equal(driver.kind, 'ftdi');
  assert.deepEqual(io.calls.map(call => [call.requestType, call.request, call.value, call.index, call.data]),
    [[0x40, 0, 0, 1, []], [0x40, 1, 0x303, 1, []], [0x40, 2, 0, 1, []], [0x40, 3, 26, 0, []], [0x40, 4, 8, 1, []]]);
});
test('baud golden vectors include fractional and 2M/3M special divisors', async () => {
  const driver = createFtdiDriver(0, 0x0600, 0x6001);
  for (const [baud, value, index] of [[9600, 0x4138, 0], [57600, 0xc034, 0], [115200, 26, 0], [921600, 0x8003, 0],
    [1000000, 3, 0], [1500000, 2, 0], [2000000, 1, 0], [3000000, 0, 0], [184, 0x3fb0, 1]]) {
    const io = control(); await driver.configure(io, line({ baud }));
    assert.deepEqual(io.calls[0], { requestType: 0x40, request: 3, value, index, data: [] }, `baud=${baud}`);
  }
});
test('FTDI H and multiple channels encode the channel in baud index', async () => {
  for (const [interfaceNumber, bcd, pid] of [[0, 0x0700, 0x6010], [1, 0x0500, 0x6010], [3, 0x0800, 0x6011], [0, 0x0900, 0x6014]]) {
    const driver = createFtdiDriver(interfaceNumber, bcd, pid), io = control();
    await driver.configure(io, line({ baud: 184 }));
    assert.equal(io.calls[0].value, 0x3fb0); assert.equal(io.calls[0].index, 0x100 | (interfaceNumber + 1));
    assert.equal(io.calls[1].index, interfaceNumber + 1);
  }
  const x = createFtdiDriver(0, 0x1000, 0x6015), io = control(); await x.configure(io, line({ baud: 184 }));
  assert.equal(io.calls[0].index, 1);
  for (const bcd of [0x0700, 0x0800, 0x0900]) {
    const revision = createFtdiDriver(0, bcd, 0x6001), current = control();
    await revision.configure(current, line({ baud: 184 })); assert.equal(current.calls[0].index, 0x101);
  }
});
test('baud tolerance uses the pinned 3.1 percent cutoff without silently accepting unsupported rates', async () => {
  const driver = createFtdiDriver(0, 0x0600, 0x6001), io = control();
  await driver.configure(io, line({ baud: 3090000 })); assert.equal(io.calls[0].value, 0);
  await assert.rejects(driver.configure(control(), line({ baud: 3100000 })), error => error.code === 401);
});
test('FTDI framing supports 7/8bits, parity0..4 and C++ two-stop encoding', async () => {
  const driver = createFtdiDriver(1, 0x0700, 0x6010);
  for (const dataBits of [7, 8]) for (let parity = 0; parity <= 4; parity++) for (const stopBits of [0, 1]) {
    const io = control(); await driver.configure(io, line({ dataBits, parity, stopBits }));
    assert.equal(io.calls[1].request, 4); assert.equal(io.calls[1].value, dataBits | (parity << 8) | (stopBits ? 0x1000 : 0));
  }
});
test('invalid or unrepresentable baud/frame is rejected before reset', async () => {
  const driver = createFtdiDriver(0, 0x0600, 0x6001);
  for (const changes of [{ baud: 0 }, { baud: -1 }, { baud: 1.5 }, { baud: NaN }, { baud: Infinity }, { baud: 183 },
    { baud: 4000000 }, { baud: 2500000 }, { baud: 1800000 }, { dataBits: 5 }, { dataBits: 6 }, { dataBits: 9 },
    { parity: 5 }, { parity: 0.5 }, { stopBits: 2 }]) {
    const io = control(); await assert.rejects(driver.initialize(io, line(changes)), error => error.code === 401); assert.equal(io.calls.length, 0);
  }
});
test('line control uses channel and modem independent masks', async () => {
  const driver = createFtdiDriver(2, 0x0800, 0x6011), io = control();
  for (const [dtr, rts] of [[false, false], [true, false], [false, true], [true, true]]) await driver.setLines(io, dtr, rts);
  assert.deepEqual(io.calls.map(call => [call.requestType, call.request, call.value, call.index]),
    [[0x40, 1, 0x300, 3], [0x40, 1, 0x301, 3], [0x40, 1, 0x302, 3], [0x40, 1, 0x303, 3]]);
});
test('short reads strip only each USB packet status header', () => {
  const driver = createFtdiDriver(0, 0x0600, 0x6001);
  assert.deepEqual(Array.from(driver.decodeRead(new Uint8Array([0x10, 0x60, 0xfd, 1, 2]), 64)), [0xfd, 1, 2]);
  assert.equal(driver.decodeRead(new Uint8Array([0x10, 0x60]), 64).length, 0);
  assert.equal(driver.decodeRead(new Uint8Array(0), 64).length, 0);
  const storage = new Uint8Array([99, 99, 0x10, 0x60, 0xfd, 2, 3, 99]);
  assert.deepEqual(Array.from(driver.decodeRead(new Uint8Array(storage.buffer, 2, 5), 64)), [0xfd, 2, 3]);
});
test('multiple 64 and 512 byte packets retain payload and short final packet', () => {
  const driver = createFtdiDriver(0, 0x0600, 0x6001);
  for (const packetSize of [64, 512]) {
    const wire = new Uint8Array(packetSize * 2 + 5), expected = [];
    for (let offset = 0; offset < wire.length; offset += packetSize) {
      wire[offset] = 0x10; wire[offset + 1] = 0x60;
      for (let index = offset + 2; index < Math.min(offset + packetSize, wire.length); index++) { wire[index] = index & 255; expected.push(index & 255); }
    }
    const original = Array.from(wire); assert.deepEqual(Array.from(driver.decodeRead(wire, packetSize)), expected);
    assert.deepEqual(Array.from(wire), original, 'decoder must not mutate pending transfer data');
  }
});
test('status-only packets and malformed trailing one-byte packet are handled', () => {
  const driver = createFtdiDriver(0, 0x0600, 0x6001);
  const wire = new Uint8Array(66); wire[64] = 0x10; wire[65] = 0x60;
  assert.equal(driver.decodeRead(wire, 64).length, 62);
  for (const [buffer, packetSize] of [[new Uint8Array(1), 64], [new Uint8Array(65), 64], [new Uint8Array(513), 512], [new Uint8Array(2), 0], [new Uint8Array(2), 1], [new Uint8Array(2), 2.5]]) {
    assert.throws(() => driver.decodeRead(buffer, packetSize), error => error.code === 14400012);
  }
});
test('failed baud request stops before frame request and shutdown drops modem best effort', async () => {
  const driver = createFtdiDriver(0, 0x0600, 0x6001), io = control(3);
  await assert.rejects(driver.configure(io, line()), error => error.code === 14400008); assert.equal(io.calls.length, 1);
  const close = control(1); await driver.shutdown(close);
  assert.deepEqual(close.calls.map(call => [call.request, call.value, call.index]), [[1, 0x300, 1]]);
});
test('every initialization stage stops after the rejected request', async () => {
  for (const [request, expectedCalls] of [[0, 1], [1, 2], [2, 3], [3, 4], [4, 5]]) {
    const driver = createFtdiDriver(0, 0x0600, 0x6001), io = control(request);
    await assert.rejects(driver.initialize(io, line()), error => error.code === 14400008);
    assert.equal(io.calls.length, expectedCalls);
  }
});

test('initialization honors DTR low while keeping RTS high', async () => {
  const driver = createFtdiDriver(0, 0x0600, 0x6001), io = control();
  await driver.initialize(io, line(), false);
  assert.deepEqual(io.calls.filter(call => call.request === 1).map(call => call.value), [0x0302]);
});

(async () => {
  let failed = 0;
  for (const [name, run] of tests) { try { await run(); console.log(`PASS ${name}`); } catch (error) { failed++; console.error(`FAIL ${name}\n${error.stack}`); } }
  console.log(`${tests.length - failed} passed, ${failed} failed`); process.exitCode = failed ? 1 : 0;
})();
