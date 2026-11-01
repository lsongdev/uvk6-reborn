import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { Radio } from '../protocol.js';
import { TimeoutError } from '../serial.js';
import { encodeChannel, decodeChannel, newChannel, encodeName, importChannels, sameChannel, TONES } from '../channels.js';
import { CTCSS, DCS } from '../channel-data.js';
const dv = b => new DataView(b.buffer, b.byteOffset, b.length);
function reply(id, number, status, channel) {
  const b = new Uint8Array(channel ? 49 : 8);
  dv(b).setUint16(0, id, true); dv(b).setUint16(2, b.length - 4, true); dv(b).setUint16(4, number, true); b[6] = status;
  if (channel) b.set(encodeChannel(channel), 8);
  return b;
}
class Fake {
  constructor(replies) { this.replies = replies; this.sent = []; }
  async send(bytes) { this.sent.push(bytes); }
  async until(match) { while (this.replies.length) { const next = this.replies.shift(); if (next instanceof Error) throw next; if (match(next)) return next; } throw new TimeoutError('timeout'); }
}
const c = { ...newChannel(), name: '中国电台', rxTone: 0x1375, txTone: 0x2013, txEnabled: true, power: 2, shift: 3, tail: true, split: 435000000 };
test('41-byte canonical layout, GB2312 names and exact tone wire values round-trip', () => {
  const b = encodeChannel(c); assert.equal(b.length, 41);
  assert.equal(Buffer.from(b.slice(0, 8)).toString('hex'), 'd6d0b9fab5e7cca8');
  assert.equal(dv(b).getUint32(16, true), 145500000); assert.equal(b[30], 61);
  assert.equal(dv(b).getUint16(28, true), 0x1375); assert.equal(dv(b).getUint16(39, true), 0x2013);
  assert.deepEqual(decodeChannel(b), c); assert.equal(encodeName('中'.repeat(8)).length, 16);
  assert.throws(() => encodeName('中'.repeat(9))); assert.throws(() => encodeName('📻'));
  const full = { ...c, name: '1234567890123456' }; assert.deepEqual(decodeChannel(encodeChannel(full)), full);
});
test('tone lists exactly match firmware and every supported tone round-trips', () => {
  const source = readFileSync(new URL('../../src/core/dcs.c', import.meta.url), 'utf8');
  for (const [name, list] of [['CTCSS', CTCSS], ['DCS', DCS]]) {
    const block = source.match(new RegExp(`${name}_Options\\[\\d+\\] = \\{([\\s\\S]*?)\\};`))[1];
    assert.deepEqual(list, block.match(/0x[0-9A-Fa-f]+|\d+/g).map(Number));
  }
  for (const [tone] of TONES) assert.equal(decodeChannel(encodeChannel({ ...c, rxTone: tone, txTone: tone })).rxTone, tone);
});
test('invalid values are rejected before narrowing, including reserved flags and malformed names', () => {
  for (const change of [{ frequency: 149999 }, { frequency: 1340000001 }, { step: 0 }, { step: 2 ** 32 }, { bandwidth: 65536 }, { power: 3 }, { squelch: 11 }, { mode: 5 }, { offset: 1 }, { offset: 100000010 }, { split: 17000000 }, { split: 1300000010 }, { rxTone: 0x1376 }, { txTone: 0x4000 }, { mode: 1 }, { txEnabled: 'yes' }]) assert.throws(() => encodeChannel({ ...c, ...change }));
  const flags = encodeChannel(c); flags[30] |= 0x80; assert.throws(() => decodeChannel(flags));
  const padding = encodeChannel(c); padding[0] = 0; assert.throws(() => decodeChannel(padding));
  const broken = encodeChannel(c); broken[15] = 0xd6; assert.throws(() => decodeChannel(broken));
});
test('JSON import validates entire file and preserves explicit deletions without duplicate targets', () => {
  const file = channels => JSON.stringify({ format: 'reborn-channels-v1', channels });
  assert.deepEqual(importChannels(file([{ number: 1, channel: c }, { number: 200, channel: null }])), [{ number: 1, channel: c }, { number: 200, channel: null }]);
  for (const entries of [[{ number: 0, channel: c }], [{ number: 1, channel: c }, { number: 1, channel: null }], [{ number: 1, channel: { ...c, step: 0 } }]]) assert.throws(() => importChannels(file(entries)));
  assert.throws(() => importChannels('{}'));
});
test('read matches channel number and validates response sizes and status', async () => {
  const link = new Fake([reply(0x581, 2, 0), reply(0x581, 1, 1, c), reply(0x581, 200, 0)]), radio = new Radio(link);
  assert.deepEqual(await radio.readChannel(1), c); assert.equal(await radio.readChannel(200), null);
  assert.equal(link.sent[0].length, 12); assert.equal(dv(link.sent[0]).getUint16(2, true), 8); assert.equal(dv(link.sent[0]).getUint32(8, true), radio.stamp);
  for (const bad of [reply(0x581, 1, 2), reply(0x581, 1, 1)]) await assert.rejects(new Radio(new Fake([bad])).readChannel(1));
});
test('save checks device baseline, writes normalized record, and verifies by reading back', async () => {
  const link = new Fake([reply(0x581, 1, 0), reply(0x583, 2, 0), reply(0x583, 1, 0), reply(0x581, 1, 1, c)]), radio = new Radio(link);
  assert.deepEqual(await radio.writeChannel(1, c, null), c);
  const write = link.sent[1]; assert.equal(write.length, 53); assert.equal(dv(write).getUint16(2, true), 49); assert.equal(write[6], 1); assert.equal(write[7], 0); assert.equal(dv(write).getUint32(8, true), radio.stamp); assert.deepEqual(write.slice(12), encodeChannel(c));
});
test('deletion is acknowledged and verified empty', async () => {
  const link = new Fake([reply(0x581, 200, 1, c), reply(0x583, 200, 0), reply(0x581, 200, 0)]);
  assert.equal(await new Radio(link).writeChannel(200, null, c), null);
  assert.equal(link.sent[1][6], 0); assert.ok(link.sent[1].slice(12).every(n => n === 0));
});
test('conflicts, missing baselines and invalid requests never send write commands', async () => {
  const link = new Fake([reply(0x581, 1, 1, c)]), radio = new Radio(link);
  await assert.rejects(radio.writeChannel(1, c, null), /改变/);
  await assert.rejects(radio.writeChannel(1, c)); await assert.rejects(radio.writeChannel(201, c, null));
  await assert.rejects(radio.writeChannel(1, { ...c, power: 3 }, null));
  assert.equal(link.sent.length, 1); assert.equal(dv(link.sent[0]).getUint16(0, true), 0x580);
});
test('every refusal, timeout and readback mismatch stops without retrying writes', async () => {
  for (const status of [1, 2, 3, 4, 255]) {
    const link = new Fake([reply(0x581, 1, 0), reply(0x583, 1, status)]);
    await assert.rejects(new Radio(link).writeChannel(1, c, null)); assert.equal(link.sent.length, 2);
  }
  const timed = new Fake([reply(0x581, 1, 0), new TimeoutError('uncertain')]);
  await assert.rejects(new Radio(timed).writeChannel(1, c, null)); assert.equal(timed.sent.length, 2);
  const mismatch = new Fake([reply(0x581, 1, 0), reply(0x583, 1, 0), reply(0x581, 1, 0)]);
  await assert.rejects(new Radio(mismatch).writeChannel(1, c, null), /不一致/); assert.equal(mismatch.sent.length, 3);
  assert.equal(sameChannel(null, null), true);
});
