'use strict';

const assert = require('node:assert/strict');
const { loadDriver } = require('./driver-harness.cjs');
const { isCp210xDevice, createCp210xDriver } = loadDriver('QGCCp210xDriver');
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

test('known CP210x IDs only', () => {
  for (const pid of [0xea60, 0xea70, 0xea71]) assert.equal(isCp210xDevice(0x10c4, pid), true);
  for (const [vid, pid] of [[0x10c4, 0xffff], [0x0403, 0xea60], [0, 0], [0x10c4, -1]]) assert.equal(isCp210xDevice(vid, pid), false);
});
test('factory rejects invalid interfaces and products without USB I/O', () => {
  for (const iface of [-1, 256, 0.5, NaN]) assert.throws(() => createCp210xDriver(iface, 0x0100, 0xea60), error => error.code === 401);
  assert.throws(() => createCp210xDriver(0, 0x0100, 0xffff), error => error.code === 401);
});
test('initialization enables UART and manual DTR/RTS with no flow control at actual interface', async () => {
  const driver = createCp210xDriver(2, 0x0100, 0xea71), io = control();
  await driver.initialize(io, line());
  assert.equal(driver.kind, 'cp210x');
  assert.deepEqual(io.calls, [
    { requestType: 0x41, request: 0, value: 1, index: 2, data: [] },
    { requestType: 0x41, request: 7, value: 0x303, index: 2, data: [] },
    { requestType: 0x41, request: 0x13, value: 0, index: 2, data: [1, 0, 0, 0, 0x40, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0] },
    { requestType: 0x41, request: 0x1e, value: 0, index: 2, data: [0, 0xc2, 1, 0] },
    { requestType: 0x41, request: 3, value: 0x0800, index: 2, data: [] }
  ]);
});
test('configuration encodes baud and all supported frame fields', async () => {
  const driver = createCp210xDriver(0, 0x0100, 0xea60), io = control();
  await driver.configure(io, line({ baud: 0x12345678, dataBits: 7, parity: 4, stopBits: 1 }));
  assert.deepEqual(io.calls.map(call => [call.request, call.value, call.index, call.data]), [
    [0x1e, 0, 0, [0x78, 0x56, 0x34, 0x12]], [3, 0x0742, 0, []]
  ]);
  for (let dataBits = 5; dataBits <= 8; dataBits++) {
    for (let parity = 0; parity <= 4; parity++) {
      const current = control(); await driver.configure(current, line({ dataBits, parity }));
      assert.equal(current.calls[1].value, (dataBits << 8) | (parity << 4));
    }
  }
});
test('line toggles retain independent modem masks', async () => {
  const driver = createCp210xDriver(3, 0x0100, 0xea71), io = control();
  for (const [dtr, rts] of [[false, false], [true, false], [false, true], [true, true]]) await driver.setLines(io, dtr, rts);
  assert.deepEqual(io.calls.map(call => [call.requestType, call.request, call.value, call.index]),
    [[0x41, 7, 0x300, 3], [0x41, 7, 0x301, 3], [0x41, 7, 0x302, 3], [0x41, 7, 0x303, 3]]);
});
test('CP2105 restricted second port rejects unsupported framing before USB I/O', async () => {
  const driver = createCp210xDriver(1, 0x0100, 0xea70);
  for (const changes of [{ dataBits: 5 }, { dataBits: 6 }, { dataBits: 7 }, { parity: 3 }, { parity: 4 }, { stopBits: 1 }]) {
    const io = control(); await assert.rejects(driver.initialize(io, line(changes)), error => error.code === 401);
    assert.equal(io.calls.length, 0);
  }
  for (const parity of [0, 1, 2]) { const io = control(); await driver.configure(io, line({ parity })); assert.equal(io.calls.length, 2); }
  const first = createCp210xDriver(0, 0x0100, 0xea70), io = control();
  await first.configure(io, line({ dataBits: 7, parity: 4, stopBits: 1 })); assert.equal(io.calls[1].value, 0x0742);
});
test('invalid numeric line values are rejected before enable', async () => {
  const driver = createCp210xDriver(0, 0x0100, 0xea60);
  for (const changes of [{ baud: 0 }, { baud: -1 }, { baud: 1.5 }, { baud: Infinity }, { baud: NaN }, { baud: 0x100000000 },
    { dataBits: 4 }, { dataBits: 9 }, { dataBits: 7.5 }, { parity: -1 }, { parity: 5 }, { parity: 1.5 }, { stopBits: 2 }]) {
    const io = control(); await assert.rejects(driver.initialize(io, line(changes)), error => error.code === 401); assert.equal(io.calls.length, 0);
  }
});
test('control errors stop configuration and preserve original failure', async () => {
  const driver = createCp210xDriver(0, 0x0100, 0xea60), io = control(0x1e);
  await assert.rejects(driver.configure(io, line()), error => error.code === 14400008);
  assert.equal(io.calls.length, 1);
});
test('every initialization stage stops on its original control error', async () => {
  for (const [request, expectedCalls] of [[0, 1], [7, 2], [0x13, 3], [0x1e, 4], [3, 5]]) {
    const driver = createCp210xDriver(0, 0x0100, 0xea60), io = control(request);
    await assert.rejects(driver.initialize(io, line()), error => error.code === 14400008);
    assert.equal(io.calls.length, expectedCalls);
  }
});
test('shutdown attempts modem drop and UART disable despite USB error', async () => {
  const driver = createCp210xDriver(1, 0x0100, 0xea70), io = control(7);
  await driver.shutdown(io);
  assert.deepEqual(io.calls.map(call => [call.request, call.value, call.index]), [[7, 0x300, 1], [0, 0, 1]]);
});
test('CP210x receives bytes without filtering', () => {
  const driver = createCp210xDriver(0, 0x0100, 0xea60), bytes = new Uint8Array([0, 1, 0xfd, 4]);
  assert.deepEqual(Array.from(driver.decodeRead(bytes, 64)), [0, 1, 0xfd, 4]);
});

test('initialization honors DTR low while keeping RTS high', async () => {
  const driver = createCp210xDriver(0, 0x0100, 0xea60), io = control();
  await driver.initialize(io, line(), false);
  assert.deepEqual(io.calls.filter(call => call.request === 7).map(call => call.value), [0x302]);
});

(async () => {
  let failed = 0;
  for (const [name, run] of tests) { try { await run(); console.log(`PASS ${name}`); } catch (error) { failed++; console.error(`FAIL ${name}\n${error.stack}`); } }
  console.log(`${tests.length - failed} passed, ${failed} failed`); process.exitCode = failed ? 1 : 0;
})();
