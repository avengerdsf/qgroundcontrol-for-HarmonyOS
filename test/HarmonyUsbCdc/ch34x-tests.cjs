'use strict';

const assert = require('node:assert/strict');
const { loadDriver } = require('./driver-harness.cjs');
const { createCh34xDriver, isCh34xDevice } = loadDriver('QGCCh34xDriver');
const tests = [];
const test = (name, run) => tests.push({ name, run });
const line = (overrides = {}) => ({ baud: 115200, dataBits: 8, parity: 0, stopBits: 0, ...overrides });
const driver = product => createCh34xDriver(0, 0x0254, product || 0x7523);

function control(options = {}) {
  const calls = [];
  return { calls, async transfer(type, request, value, index, data) {
    const position = calls.length;
    calls.push([type, request, value, index, data.length, Array.from(data)]);
    if (position === options.failAt) throw options.error;
    if ((type & 0x80) !== 0) {
      const response = options.responses && options.responses[position] ||
        (request === 0x5f ? [0x27, 0] : value === 0x2518 ? [0x56, 0] : [0xff, 0xee]);
      data.set(response);
    }
    return data.length;
  } };
}
const commands = io => io.calls.map(call => call.slice(0, 5));

test('only the upstream CH340 and CH341A USB identities are selected', () => {
  assert.equal(isCh34xDevice(0x1a86, 0x7523), true);
  assert.equal(isCh34xDevice(0x1a86, 0x5523), true);
  for (const [vendor, product] of [[0x1234, 0x7523], [0x1a86, 0x7522], [0x1a86, 0x55d4], [0x1a86, 0x9102]]) {
    assert.equal(isCh34xDevice(vendor, product), false);
  }
});

test('CH340 open emits the upstream initialization and desired 115200 8N1 register sequence', async () => {
  const io = control();
  await driver().initialize(io, line());
  assert.deepEqual(commands(io), [
    [0xc0, 0x5f, 0, 0, 2],
    [0x40, 0xa1, 0, 0, 0],
    [0x40, 0x9a, 0x1312, 0xb282, 0], [0x40, 0x9a, 0x0f2c, 0x000c, 0],
    [0xc0, 0x95, 0x2518, 0, 2],
    [0x40, 0x9a, 0x2518, 0x00c3, 0],
    [0xc0, 0x95, 0x0706, 0, 2],
    [0x40, 0xa1, 0x501f, 0xd90a, 0],
    [0x40, 0x9a, 0x1312, 0xb282, 0], [0x40, 0x9a, 0x0f2c, 0x000c, 0],
    [0x40, 0xa4, 0xffff, 0, 0],
    [0xc0, 0x95, 0x0706, 0, 2],
    [0x40, 0x9a, 0x1312, 0xb282, 0], [0x40, 0x9a, 0x0f2c, 0x000c, 0],
    [0x40, 0x9a, 0x1312, 0xcc83, 0], [0x40, 0x9a, 0x0f2c, 0x0008, 0],
    [0x40, 0x9a, 0x2518, 0x00c3, 0],
    [0x40, 0xa4, 0xff9f, 0, 0]
  ]);
});

test('CH341A baud registers keep the send-immediately bit and the dedicated 921600 divisor', async () => {
  for (const [baud, first, second] of [
    [46, 0x0180, 0x00cf], [300, 0xd980, 0x0006], [9600, 0xb282, 0x000c], [57600, 0x9883, 0x0010],
    [115200, 0xcc83, 0x0008], [921600, 0xf387, 0x0000],
    [1000000, 0xfa83, 0x0004], [1228800, 0xfb83, 0x0021]
  ]) {
    const io = control();
    await driver(0x5523).configure(io, line({ baud }));
    assert.deepEqual(commands(io), [
      [0x40, 0x9a, 0x1312, first, 0], [0x40, 0x9a, 0x0f2c, second, 0],
      [0x40, 0x9a, 0x2518, 0x00c3, 0]
    ], `${baud} baud`);
  }
});

test('CH34x line coding preserves TX/RX and encodes each parity, width and stop choice', async () => {
  for (const [attributes, lcr] of [
    [{ dataBits: 5 }, 0xc0], [{ dataBits: 6 }, 0xc1], [{ dataBits: 7 }, 0xc2],
    [{ parity: 1 }, 0xcb], [{ parity: 2 }, 0xdb], [{ parity: 3 }, 0xeb],
    [{ parity: 4 }, 0xfb], [{ stopBits: 1 }, 0xc7], [{ dataBits: 7, parity: 2, stopBits: 1 }, 0xde]
  ]) {
    const io = control();
    await driver().configure(io, line(attributes));
    assert.deepEqual(commands(io)[2], [0x40, 0x9a, 0x2518, lcr, 0]);
  }
});

test('invalid or unrepresentable line coding is rejected before any USB request', async () => {
  for (const attributes of [
    { baud: 0 }, { baud: -1 }, { baud: 1 }, { baud: 45 }, { baud: 9600.5 },
    { baud: NaN }, { baud: Infinity }, { baud: 0xffffffff }, { baud: 0x100000000 },
    { dataBits: 4 }, { dataBits: 9 }, { dataBits: 7.5 },
    { parity: -1 }, { parity: 5 }, { parity: 1.5 }, { stopBits: 2 }, { stopBits: 1.5 }
  ]) {
    for (const operation of ['initialize', 'configure']) {
      const io = control();
      await assert.rejects(driver()[operation](io, line(attributes)), error => error.code === 401);
      assert.equal(io.calls.length, 0, JSON.stringify(attributes));
    }
  }
});

test('DTR and RTS are inverted 16-bit masks and shutdown deasserts both', async () => {
  const device = driver(), io = control();
  for (const [dtr, rts] of [[false, false], [true, false], [false, true], [true, true]]) {
    await device.setLines(io, dtr, rts);
  }
  await device.shutdown(io);
  assert.deepEqual(commands(io), [
    [0x40, 0xa4, 0xffff, 0, 0], [0x40, 0xa4, 0xffdf, 0, 0],
    [0x40, 0xa4, 0xffbf, 0, 0], [0x40, 0xa4, 0xff9f, 0, 0],
    [0x40, 0xa4, 0xffff, 0, 0]
  ]);
});

test('unexpected fixed init bytes stop before later initialization writes', async () => {
  for (const [position, response] of [[0, [0x27, 1]], [4, [0x56, 1]]]) {
    const io = control({ responses: { [position]: response } });
    await assert.rejects(driver().initialize(io, line()), error => error.code === 14400012);
    assert.equal(io.calls.length, position + 1);
  }
});

test('device version and modem-status bytes vary without rejecting valid CH34x firmware', async () => {
  const io = control({ responses: { 0: [0x30, 0], 4: [0xc3, 0], 6: [0x9f, 0xec], 11: [0xf0, 0xee] } });
  await driver().initialize(io, line());
  assert.equal(io.calls.length, 18);
});

test('IN transport errors abort initialization and preserve the original failure', async () => {
  for (const position of [0, 4, 6, 11]) {
    const error = new Error('control read failed'); error.code = 14400012;
    const io = control({ failAt: position, error });
    await assert.rejects(driver().initialize(io, line()), failure => failure === error);
    assert.equal(io.calls.length, position + 1);
  }
});

test('a rejected initialization write prevents subsequent register or line-state commands', async () => {
  for (const position of [1, 2, 3, 5, 7, 8, 9, 10, 12, 13, 14, 15, 16, 17]) {
    const error = new Error('control write failed'); error.code = 14400012;
    const io = control({ failAt: position, error });
    await assert.rejects(driver().initialize(io, line()), failure => failure === error);
    assert.equal(io.calls.length, position + 1);
  }
});

test('CH34x serial payload is delivered unchanged across USB packet boundaries', () => {
  const payload = new Uint8Array(130);
  payload.set([0xfe, 0x55, 0x03, 0x7e]); payload.set([0xaa, 0xbb, 0xcc], 63);
  assert.deepEqual(Array.from(driver().decodeRead(payload, 64)), Array.from(payload));
  assert.equal(driver().decodeRead(new Uint8Array(0), 64).length, 0);
});

(async () => {
  let failures = 0;
  for (const { name, run } of tests) {
    try { await run(); console.log(`PASS ${name}`); }
    catch (error) { failures++; console.error(`FAIL ${name}\n${error.stack}`); }
  }
  console.log(`CH34x driver: ${tests.length - failures}/${tests.length} tests passed`);
  process.exitCode = failures ? 1 : 0;
})();
