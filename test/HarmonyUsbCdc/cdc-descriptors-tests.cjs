const assert = require('node:assert/strict');
const test = require('node:test');
const { loadDriver } = require('./driver-harness.cjs');
const { parseCdcPairs } = loadDriver('QGCCdcDescriptors');

function iface(id, clazz, subClass = 0, alternate = 0) {
  return [9, 4, id, alternate, 0, clazz, subClass, 0, 0];
}
const acm = (id, alternate = 0) => iface(id, 2, 2, alternate);
const data = (id, alternate = 0) => iface(id, 10, 0, alternate);
const union = (master, ...slaves) => [4 + slaves.length, 0x24, 6, master, ...slaves];
const iad = (first, count, clazz = 2, subClass = 2) => [8, 0x0b, first, count, clazz, subClass, 0, 0];

function config(id, ...descriptors) {
  const body = descriptors.flat();
  const length = body.length + 9;
  const count = new Set(descriptors.filter(d => d[1] === 4).map(d => d[2])).size;
  return [9, 2, length & 255, length >> 8, count, id, 0, 0x80, 50, ...body];
}

function raw(...configs) {
  return new Uint8Array([18, 1, 0, 2, 0xef, 2, 1, 64, 0, 0, 0, 0, 0, 1, 0, 0, 0, configs.length,
    ...configs.flat()]);
}

function pairs(bytes, id = 1) {
  return JSON.parse(JSON.stringify(parseCdcPairs(bytes, id))).sort((a, b) => a.controlId - b.controlId);
}

const pair = (controlId, dataId, configId = 1) => ({ controlId, dataId, configId });
function invalid(bytes, id = 1) {
  assert.throws(() => parseCdcPairs(bytes, id), error => error.code === 401);
}

test('Union pairs two reordered CDC functions with nonadjacent interface IDs', () => {
  assert.deepEqual(pairs(raw(config(1, data(7), acm(4), union(4, 9), acm(1), union(1, 7), data(9)))),
    [pair(1, 7), pair(4, 9)]);
});

test('Union pairs three CDC functions', () => {
  assert.deepEqual(pairs(raw(config(1, acm(2), union(2, 3), data(3), acm(4), union(4, 5), data(5),
    acm(6), union(6, 7), data(7)))), [pair(2, 3), pair(4, 5), pair(6, 7)]);
});

test('IAD supplies pairing when Union is absent', () => {
  assert.deepEqual(pairs(raw(config(1, iad(9, 2), data(10), acm(9), iad(2, 2), data(3), acm(2)))),
    [pair(2, 3), pair(9, 10)]);
});

test('matching Union and IAD evidence produces one pair', () => {
  assert.deepEqual(pairs(raw(config(1, iad(4, 2), acm(4), union(4, 5), data(5)))), [pair(4, 5)]);
});

test('configuration values are not assumed to be one', () => {
  assert.deepEqual(pairs(raw(config(7, acm(3), union(3, 8), data(8))), 7), [pair(3, 8, 7)]);
});

test('only the selected configuration contributes interface relationships', () => {
  const bytes = raw(config(1, acm(0), union(0, 1), data(1)), config(8, data(6), acm(0), union(0, 6)));
  assert.deepEqual(pairs(bytes, 8), [pair(0, 6, 8)]);
  assert.deepEqual(pairs(bytes, 1), [pair(0, 1)]);
});

test('alternate settings share a single logical interface ID', () => {
  assert.deepEqual(pairs(raw(config(1, acm(0), union(0, 2), acm(0, 1), union(0, 2), data(2), data(2, 1)))),
    [pair(0, 2)]);
});

test('NCM Union and IAD do not become ACM ports', () => {
  assert.deepEqual(pairs(raw(config(1, iad(0, 2, 2, 13), iface(0, 2, 13), union(0, 1), data(1),
    iad(4, 2), acm(4), union(4, 5), data(5)))), [pair(4, 5)]);
});

test('identical duplicate Union descriptors are deduplicated', () => {
  assert.deepEqual(pairs(raw(config(1, acm(1), union(1, 8), union(1, 8), data(8)))), [pair(1, 8)]);
});

test('identical duplicate IAD descriptors are deduplicated', () => {
  assert.deepEqual(pairs(raw(config(1, iad(2, 2), iad(2, 2), acm(2), data(3)))), [pair(2, 3)]);
});

test('unrelated endpoint and class descriptors do not change pairs', () => {
  assert.deepEqual(pairs(raw(config(1, acm(0), [5, 0x24, 0, 0x10, 1], union(0, 4),
    [7, 5, 0x81, 3, 16, 0, 16], data(4), [7, 5, 2, 2, 64, 0, 0]))), [pair(0, 4)]);
});

test('multiple unassociated CDC interfaces are never paired by ordinal', () => {
  assert.deepEqual(pairs(raw(config(1, acm(0), data(1), acm(2), data(3)))), []);
});

test('a unique CDC pair without descriptor evidence remains a caller fallback', () => {
  assert.deepEqual(pairs(raw(config(1, acm(0), data(1)))), []);
});

test('non-ACM IAD evidence alone does not pair ports', () => {
  assert.deepEqual(pairs(raw(config(1, iad(0, 2, 2, 13), iface(0, 2, 13), data(1)))), []);
});

const invalidCases = [
  ['selected configuration is absent', () => raw(config(2, acm(0), union(0, 1), data(1)))],
  ['conflicting Union descriptors', () => raw(config(1, acm(0), union(0, 1), union(0, 2), data(1), data(2)))],
  ['Union master differs from its current ACM interface', () => raw(config(1, acm(0), union(2, 1), data(1), acm(2)))],
  ['Union slave is a control interface', () => raw(config(1, acm(0), union(0, 2), acm(2)))],
  ['Union slave is absent', () => raw(config(1, acm(0), union(0, 7), data(1)))],
  ['Union has more than one slave', () => raw(config(1, acm(0), union(0, 1, 3), data(1), data(3)))],
  ['two controls own the same data interface', () => raw(config(1, acm(0), union(0, 3), acm(2), union(2, 3), data(3)))],
  ['Union and IAD disagree', () => raw(config(1, iad(0, 2), acm(0), union(0, 3), data(1), data(3)))],
  ['IAD contains multiple data interfaces', () => raw(config(1, iad(0, 3), acm(0), data(1), data(2)))],
  ['IAD contains multiple ACM controls', () => raw(config(1, iad(0, 3), acm(0), acm(1), data(2)))],
  ['IAD range overlaps another function', () => raw(config(1, iad(0, 2), iad(1, 2), acm(0), data(1), acm(2)))],
  ['IAD range is empty', () => raw(config(1, iad(0, 0), acm(0), data(1)))],
  ['IAD range exceeds the interface number space', () => raw(config(1, iad(255, 2), acm(255), data(0)))],
  ['IAD range contains an undeclared interface', () => raw(config(1, iad(0, 3), acm(0), data(2)))],
  ['ACM Union conflicts with its non-ACM IAD', () => raw(config(1, iad(0, 2, 2, 13), acm(0), union(0, 1), data(1)))],
  ['alternate settings disagree about interface class', () => raw(config(1, acm(0), union(0, 1), data(1), acm(1, 1)))],
  ['zero descriptor length', () => raw(config(1, [0, 4]))],
  ['one-byte descriptor length', () => raw(config(1, [1, 4]))],
  ['truncated descriptor header', () => raw(config(1, [7]))],
  ['descriptor extends beyond configuration boundary', () => raw(config(1, [12, 4, 0, 0, 0, 2, 2, 0, 0]), config(2))],
  ['truncated Union functional descriptor', () => raw(config(1, acm(0), [4, 0x24, 6, 0], data(1)))],
  ['truncated IAD descriptor', () => raw(config(1, [7, 0x0b, 0, 2, 2, 2, 0], acm(0), data(1)))],
  ['truncated interface descriptor', () => raw(config(1, [8, 4, 0, 0, 0, 2, 2, 0]))],
  ['duplicate selected configuration values', () => raw(config(1), config(1))],
  ['raw data is empty', () => new Uint8Array(0)],
  ['raw header is truncated', () => new Uint8Array([18])],
  ['configuration header is truncated', () => new Uint8Array([9, 2, 9, 0, 0, 1, 0, 0])],
  ['configuration total length is smaller than its header', () => new Uint8Array([9, 2, 8, 0, 0, 1, 0, 0x80, 50])],
  ['configuration total length exceeds available data', () => new Uint8Array([9, 2, 10, 0, 0, 1, 0, 0x80, 50])],
  ['configuration boundary cannot contain another configuration', () => raw(config(1, config(2)))],
];

for (const [name, bytes] of invalidCases) {
  test('rejects ' + name, () => invalid(bytes()));
}

test('a short total length cannot move interface descriptors outside their configuration', () => {
  const bytes = config(1, acm(0), union(0, 1), data(1));
  bytes[2] = 9;
  invalid(new Uint8Array(bytes));
});

test('configuration IDs must be valid USB configuration values', () => {
  for (const id of [0, -1, 256, 1.5, NaN]) invalid(raw(config(1)), id);
});

test('a Union descriptor cannot be attached to a CDC data interface', () => {
  invalid(raw(config(1, acm(0), data(1), union(0, 1))));
});

test('duplicate IAD ranges with different function protocols conflict', () => {
  const conflicting = iad(0, 2);
  conflicting[6] = 1;
  invalid(raw(config(1, iad(0, 2), conflicting, acm(0), data(1))));
});

test('configuration total length is decoded as a 16-bit little-endian value', () => {
  const padding = new Array(253).fill(0);
  padding[0] = 253;
  padding[1] = 0x30;
  assert.deepEqual(pairs(raw(config(1, padding, acm(0), union(0, 4), data(4)))), [pair(0, 4)]);
});
