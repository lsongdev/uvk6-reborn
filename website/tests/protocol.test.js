import { test } from 'node:test';
import assert from 'node:assert/strict';
import { FrameParser, RadioLink, Radio, BOARDS, crc16, packet, validateImage, flashFirmware } from '../protocol.js';
import { SerialTransport, TimeoutError } from '../serial.js';

const fromHex = hex => Uint8Array.from(Buffer.from(hex, 'hex'));
const u16 = (bytes, at) => new DataView(bytes.buffer, bytes.byteOffset).getUint16(at, true);
const u32 = (bytes, at) => new DataView(bytes.buffer, bytes.byteOffset).getUint32(at, true);
const put16 = (bytes, at, value) => new DataView(bytes.buffer).setUint16(at, value, true);
const put32 = (bytes, at, value) => new DataView(bytes.buffer).setUint32(at, value, true);
const noDelay = async () => {};
function image(board, size = 308) {
  const bytes = new Uint8Array(size);
  put32(bytes, 0, 0x20004000); put32(bytes, 4, BOARDS[board].origin + 9);
  return bytes;
}
function greeting(version) {
  const bytes = new Uint8Array(36);
  bytes.set(fromHex('18052000'));
  bytes.set(new TextEncoder().encode(version), 20);
  return bytes;
}
function ack(page, status = 0) {
  const bytes = new Uint8Array(12);
  bytes.set(fromHex('1a0508008a8d9f1d'));
  put16(bytes, 8, page); put16(bytes, 10, status);
  return bytes;
}
class FakeLink {
  constructor(replies) { this.replies = replies; this.sent = []; }
  async send(payload) { this.sent.push(payload.slice()); }
  async until(match) {
    while (this.replies.length) {
      const reply = this.replies.shift();
      if (reply instanceof Error) throw reply;
      if (match(reply)) return reply;
    }
    throw new TimeoutError('simulated timeout');
  }
}

test('CRC and real measured K6 bootloader frame, fragmented with leading noise', () => {
  assert.equal(crc16(new TextEncoder().encode('123456789')), 0x31c3);
  const frame = fromHex('abcd24000e6934e62f930944366682755d4416810a6c53e61dbf3d700f05e7402709e980166c14c6ffffdcba');
  const parser = new FrameParser();
  parser.push(fromHex('0001ab')); assert.equal(parser.next(), null);
  parser.push(frame.slice(0, 3)); assert.equal(parser.next(), null);
  parser.push(frame.slice(3));
  const reply = parser.next();
  assert.equal(u32(reply, 0), 0x00200518);
  assert.equal(new TextDecoder().decode(reply.slice(20, 27)), '3.00.02');
});

test('bad CRC, bad footer and excessive lengths are ignored; back-to-back packets work', () => {
  for (const obfuscated of [true, false]) {
    const bytes = fromHex('1a0508008a8d9f1d00000000');
    const corrupt = packet(bytes, obfuscated); corrupt[5] ^= 1;
    const footer = packet(bytes, obfuscated); footer[footer.length - 1] ^= 1;
    const parser = new FrameParser();
    parser.push(fromHex('abcdffff'));
    parser.push(corrupt); parser.push(footer);
    parser.push(packet(bytes, obfuscated)); parser.push(packet(bytes, obfuscated));
    assert.deepEqual(parser.next(obfuscated), bytes);
    assert.deepEqual(parser.next(obfuscated), bytes);
    assert.equal(parser.next(obfuscated), null);
  }
});

test('accept literal and encoded placeholder CRC in bootloader/application replies', () => {
  for (const obfuscated of [true, false]) {
    const payload = fromHex('73051c0000000800');
    const encoded = packet(payload, obfuscated);
    const crc = crc16(payload);
    encoded[payload.length + 4] ^= (crc & 255) ^ 255;
    encoded[payload.length + 5] ^= (crc >> 8) ^ 255;
    const parser = new FrameParser(); parser.push(encoded);
    assert.deepEqual(parser.next(obfuscated), payload);
    const literal = packet(payload, obfuscated);
    literal[payload.length + 4] = literal[payload.length + 5] = 255;
    parser.push(literal); assert.deepEqual(parser.next(obfuscated), payload);
  }
});

test('raw vector validation enforces board origin, Thumb bit, RAM, and partition size', () => {
  for (const board of Object.keys(BOARDS)) {
    validateImage(image(board), board);
    validateImage(image(board, BOARDS[board].size), board);
    for (const bad of [new Uint8Array(), new Uint8Array(300), image(board, BOARDS[board].size + 1), image(board === 'uvk6' ? 'uvk1' : 'uvk6')])
      assert.throws(() => validateImage(bad, board));
    for (const [offset, value] of [[0, 0x20004004], [0, 0x20003fff], [4, BOARDS[board].origin + 8], [4, BOARDS[board].origin + 309]]) {
      const bad = image(board); put32(bad, offset, value);
      assert.throws(() => validateImage(bad, board));
    }
  }
});

test('mismatched board or invalid image sends neither handshake nor write', async () => {
  for (const board of ['uvk6', 'uvk1']) {
    const link = new FakeLink([greeting(board === 'uvk6' ? '7.02.02' : '3.00.02')]);
    await assert.rejects(flashFirmware(link, image(board), board, { delay: noDelay }), /Bootloader/);
    assert.equal(link.sent.length, 0);
    await assert.rejects(flashFirmware(link, new Uint8Array(300), board));
    assert.equal(link.sent.length, 0);
  }
});

test('K6 short final page, identical timeout retry, stale/rejected ACKs and progress', async () => {
  const link = new FakeLink([greeting('3.00.02'), new TimeoutError(), ack(1), ack(0, 1), ack(0), ack(0), ack(1)]);
  const progress = [];
  await flashFirmware(link, image('uvk6', 300), 'uvk6', { delay: noDelay, progress: (...values) => progress.push(values) });
  assert.equal(Buffer.from(link.sent[0]).toString('hex'), '300510002a2e30312e3233000000000000000000');
  assert.deepEqual(link.sent[1], link.sent[2]);
  assert.equal(link.sent[3].length, 272);
  assert.equal(u16(link.sent[3], 8), 1);
  assert.equal(u16(link.sent[3], 10), 2);
  assert.equal(u16(link.sent[3], 12), 44);
  assert.ok(link.sent[3].slice(60).every(byte => byte === 0));
  assert.deepEqual(progress, [[256, 300], [300, 300]]);
});

test('K1 full 472-page image uses three version handshakes, 16-bit pages, zero reserves', async () => {
  const link = new FakeLink([...Array.from({ length: 3 }, () => greeting('7.02.02')), ...Array.from({ length: 472 }, (_, page) => ack(page))]);
  await flashFirmware(link, image('uvk1', 118 * 1024), 'uvk1', { delay: noDelay });
  assert.equal(link.sent.length, 475);
  for (const sent of link.sent.slice(0, 3)) assert.equal(Buffer.from(sent).toString('hex'), '30050400372e3032');
  for (const page of [0, 256, 471]) {
    assert.equal(u16(link.sent[3 + page], 8), page);
    assert.equal(u16(link.sent[3 + page], 10), 472);
    assert.deepEqual(link.sent[3 + page].slice(12, 16), new Uint8Array(4));
  }
});

test('failed page stops after five attempts without advancing', async () => {
  const link = new FakeLink([greeting('3.00.02')]);
  await assert.rejects(flashFirmware(link, image('uvk6'), 'uvk6', { delay: noDelay }), /5/);
  assert.equal(link.sent.length, 6);
  assert.ok(link.sent.slice(1).every(bytes => u16(bytes, 8) === 0));
});

test('RadioLink receives arbitrary serial chunks and filters a stale reply', async () => {
  const bytes = packet(ack(3));
  const chunks = [fromHex('ff00'), bytes.slice(0, 1), bytes.slice(1, 8), bytes.slice(8)];
  const writes = [];
  const link = new RadioLink({ read: async () => chunks.shift(), write: async data => writes.push(data) });
  await link.send(ack(1));
  assert.deepEqual(writes[0], packet(ack(1)));
  assert.deepEqual(await link.until(bytes => u16(bytes, 8) === 3), ack(3));
});

function versionReply(version = 's0v4') {
  const bytes = new Uint8Array(40); put16(bytes, 0, 0x0515);
  bytes.set(new TextEncoder().encode(version), 4);
  return bytes;
}
function storageReply(capacity = 524288) {
  const bytes = new Uint8Array(32); put16(bytes, 0, 0x0573); put32(bytes, 4, capacity);
  bytes.set(new TextEncoder().encode('EEPROM'), 8);
  return bytes;
}
function readReply(address, count) {
  const bytes = new Uint8Array(12 + count); put16(bytes, 0, 0x051c); put32(bytes, 4, address); bytes[8] = count;
  for (let i = 0; i < count; i++) bytes[12 + i] = (address + i) & 255;
  return bytes;
}

test('application establishes plain stamped session and reads physical capacity', async () => {
  for (const capacity of [524288, 2097152]) {
    const link = new FakeLink([versionReply(), storageReply(capacity)]);
    const radio = new Radio(link);
    assert.deepEqual(await radio.connect(), { version: 's0v4', capacity, storage: 'EEPROM' });
    assert.equal(link.obfuscated, false);
    assert.equal(u16(link.sent[0], 0), 0x0514);
    assert.equal(u32(link.sent[0], 4), u32(link.sent[1], 4));
  }
  for (const replies of [[versionReply('stock')], [versionReply(), storageReply(8192)]])
    await assert.rejects(new Radio(new FakeLink(replies)).connect());
});

test('diagnostics and RF whitelist parse byte offsets and reject malformed replies', async () => {
  const stats = new Uint8Array(32); put16(stats, 0, 0x0575);
  put32(stats, 4, 123456); put16(stats, 8, 42); put32(stats, 16, 145500000);
  stats[21] = 1; put16(stats, 24, 511); put16(stats, 30, 0x1234);
  const rf = new Uint8Array(16); put16(rf, 0, 0x0577); put16(rf, 4, 4829); put16(rf, 6, 2);
  put16(rf, 8, 0x30); put16(rf, 10, 0x1234); put16(rf, 12, 0x67); put16(rf, 14, 511);
  const radio = new Radio(new FakeLink([stats, rf]));
  const result = await radio.diagnostics();
  assert.equal(result.frequency, 145500000); assert.equal(result.rssi, 511);
  assert.equal(result.loops, 123456); assert.equal(result.loopMaxMs, 42); assert.equal(result.status, 0x1234);
  assert.deepEqual(await radio.registers(), { chip: 4829, values: [[0x30, 0x1234], [0x67, 511]] });
  put16(rf, 12, 0x30);
  await assert.rejects(new Radio(new FakeLink([rf])).registers());
});

test('backup matches address/count, retries idempotent reads, enforces bounds and cancellation', async () => {
  const link = new FakeLink([new TimeoutError(), readReply(128, 128), readReply(0, 128), readReply(0, 128), readReply(128, 12)]);
  const radio = new Radio(link); radio.capacity = 524288;
  const data = await radio.backup(0, 140, { delay: noDelay });
  assert.deepEqual(data, Uint8Array.from({ length: 140 }, (_, i) => i));
  assert.deepEqual(link.sent[0], link.sent[1]); assert.equal(u32(link.sent[2], 4), 128);
  assert.equal(link.sent[2][8], 12); assert.equal(radio.retriesLeft, 5);
  for (const [at, size] of [[-1, 1], [524288, 1], [0, 0], [0.5, 1]]) await assert.rejects(radio.backup(at, size));
  const abort = new AbortController(); abort.abort();
  await assert.rejects(radio.backup(0, 128, { signal: abort.signal, delay: noDelay }), { name: 'AbortError' });
});

test('backup never publishes a partial result and obeys a connection retry budget', async () => {
  const radio = new Radio(new FakeLink([readReply(0, 128)])); radio.capacity = 524288;
  await assert.rejects(radio.backup(0, 256, { delay: noDelay }), TimeoutError);
  assert.equal(radio.link.sent.length, 4);
  radio.retriesLeft = 0;
  await assert.rejects(radio.backup(0, 128, { delay: noDelay }), TimeoutError);
  assert.equal(radio.link.sent.length, 5);
});

test('serial transport times out a read without losing the following chunk and closes all locks', async () => {
  let controller; const writes = [];
  const port = {
    readable: new ReadableStream({ start(value) { controller = value; } }),
    writable: new WritableStream({ write(value) { writes.push(value); } }),
    async open(options) { assert.equal(options.baudRate, 38400); },
    async close() { assert.equal(this.readable.locked, false); assert.equal(this.writable.locked, false); },
  };
  const transport = new SerialTransport(port); await transport.open();
  await assert.rejects(transport.read(performance.now() + 1), TimeoutError);
  const pending = transport.read(performance.now() + 1000);
  controller.enqueue(fromHex('abcd'));
  assert.deepEqual(await pending, fromHex('abcd'));
  await transport.write(fromHex('0102')); assert.deepEqual(writes[0], fromHex('0102'));
  const next = transport.read(performance.now() + 1000);
  const failed = assert.rejects(next, /关闭/);
  await transport.close(); await failed;
});

test('physical unplug rejects a pending read and subsequent writes', async () => {
  let controller;
  const port = {
    readable: new ReadableStream({ start(value) { controller = value; } }),
    writable: new WritableStream(), async open() {}, async close() {},
  };
  const transport = new SerialTransport(port); await transport.open();
  const pending = assert.rejects(transport.read(performance.now() + 1000), /unplug/);
  controller.error(new Error('unplug')); await pending;
  await assert.rejects(transport.write(new Uint8Array(1)), /unplug/);
  await transport.close();
});
