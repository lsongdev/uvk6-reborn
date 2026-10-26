import { TimeoutError } from './serial.js';

const KEY = Uint8Array.from([0x16, 0x6c, 0x14, 0xe6, 0x2e, 0x91, 0x0d, 0x40, 0x21, 0x35, 0xd5, 0x40, 0x13, 0x03, 0xe9, 0x80]);
export const BOARDS = {
  uvk6: { origin: 0, size: 60 * 1024, prefix: '3.' },
  uvk1: { origin: 0x08002800, size: 118 * 1024, prefix: '7.' },
};
export const sleep = ms => new Promise(resolve => setTimeout(resolve, ms));
const view = bytes => new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
const u16 = (bytes, offset = 0) => view(bytes).getUint16(offset, true);
const u32 = (bytes, offset = 0) => view(bytes).getUint32(offset, true);
const text = bytes => new TextDecoder().decode(bytes).split('\0')[0];

export function crc16(bytes) {
  let crc = 0;
  for (const byte of bytes) {
    crc ^= byte << 8;
    for (let bit = 0; bit < 8; bit++) crc = ((crc << 1) ^ (crc & 0x8000 ? 0x1021 : 0)) & 0xffff;
  }
  return crc;
}

export function packet(payload, obfuscated = true) {
  const frame = new Uint8Array(payload.length + 8);
  frame.set([0xab, 0xcd]);
  view(frame).setUint16(2, payload.length, true);
  frame.set(payload, 4);
  view(frame).setUint16(4 + payload.length, crc16(payload), true);
  if (obfuscated) for (let i = 0; i < payload.length + 2; i++) frame[4 + i] ^= KEY[i % 16];
  frame.set([0xdc, 0xba], frame.length - 2);
  return frame;
}

export class FrameParser {
  constructor() { this.buffer = new Uint8Array(); }
  push(chunk) {
    const bytes = new Uint8Array(this.buffer.length + chunk.length);
    bytes.set(this.buffer);
    bytes.set(chunk, this.buffer.length);
    this.buffer = bytes;
  }
  next(obfuscated = true) {
    while (this.buffer.length >= 4) {
      const bytes = this.buffer;
      if (bytes[0] !== 0xab || bytes[1] !== 0xcd || u16(bytes, 2) > 512) {
        this.buffer = bytes.slice(1);
        continue;
      }
      const size = u16(bytes, 2);
      if (bytes.length < size + 8) return null;
      if (bytes[size + 6] !== 0xdc || bytes[size + 7] !== 0xba) {
        this.buffer = bytes.slice(1);
        continue;
      }
      this.buffer = bytes.slice(size + 8);
      const body = bytes.slice(4, size + 6);
      if (obfuscated) for (let i = 0; i < body.length; i++) body[i] ^= KEY[i % 16];
      const payload = body.slice(0, size);
      const crc = u16(body, size);
      // Bootloaders use a literal or encoded FFFF; application replies encode FFFF.
      if (crc === crc16(payload) || crc === 0xffff || (bytes[size + 4] === 0xff && bytes[size + 5] === 0xff)) return payload;
    }
    return null;
  }
}

export class RadioLink {
  constructor(transport, obfuscated = true) {
    this.transport = transport;
    this.obfuscated = obfuscated;
    this.parser = new FrameParser();
  }
  send(payload) { return this.transport.write(packet(payload, this.obfuscated)); }
  async receive(deadline) {
    while (performance.now() < deadline) {
      const payload = this.parser.next(this.obfuscated);
      if (payload) return payload;
      this.parser.push(await this.transport.read(deadline));
    }
    throw new TimeoutError('等待设备应答超时。');
  }
  async until(match, timeout = 3000) {
    const deadline = performance.now() + timeout;
    while (true) {
      const reply = await this.receive(deadline);
      if (match(reply)) return reply;
    }
  }
}

export function validateImage(image, board) {
  const target = BOARDS[board];
  if (!target || image.length < 8 || image.length > target.size) throw new Error('固件长度超出所选机型的应用分区。');
  const stack = u32(image), reset = u32(image, 4), address = reset & ~1;
  if (stack < 0x20000000 || stack > 0x20004000 || stack % 4 || !(reset & 1) || address < target.origin + 8 || address >= target.origin + image.length)
    throw new Error('固件向量表不匹配。请选择对应机型的原始 .bin，不能使用 .packed.bin。');
}

const hello = bytes => bytes.length >= 36 && u32(bytes) === 0x00200518;
export async function flashFirmware(link, image, board, { log = () => {}, progress = () => {}, delay = sleep } = {}) {
  validateImage(image, board);
  log('等待 Bootloader：按住 PTT 开机（最多 45 秒）…');
  const greeting = await link.until(hello, 45000);
  const version = text(greeting.slice(20, 36));
  if (!version.startsWith(BOARDS[board].prefix)) throw new Error(`Bootloader ${version} 与 ${board.toUpperCase()} 不匹配，未写入任何数据。`);
  log(`Bootloader ${version}，开始刷写 ${image.length} 字节。`);
  if (board === 'uvk1') {
    const handshake = new Uint8Array(8);
    handshake.set([0x30, 0x05, 0x04, 0x00]);
    handshake.set(new TextEncoder().encode(version.slice(0, 4)), 4);
    for (let i = 0; i < 3; i++) {
      if (i) await link.until(hello);
      await link.send(handshake);
    }
  } else {
    const handshake = new Uint8Array(20);
    handshake.set([0x30, 0x05, 0x10, 0x00]);
    handshake.set(new TextEncoder().encode('*.01.23'), 4);
    await link.send(handshake);
  }
  await delay(150);
  const pages = Math.ceil(image.length / 256);
  for (let page = 0; page < pages; page++) {
    const offset = page * 256;
    const chunk = image.slice(offset, offset + 256);
    const payload = new Uint8Array(272);
    payload.set([0x19, 0x05, 0x0c, 0x01, 0x8a, 0x8d, 0x9f, 0x1d]);
    view(payload).setUint16(8, page, true);
    view(payload).setUint16(10, pages, true);
    if (board === 'uvk6') view(payload).setUint16(12, chunk.length, true);
    payload.set(chunk, 16);
    for (let attempt = 0; attempt < 5; attempt++) {
      await link.send(payload);
      try {
        await link.until(reply => reply.length >= 12 && u32(reply) === 0x0008051a && u32(reply, 4) === 0x1d9f8d8a && u16(reply, 8) === page && u16(reply, 10) === 0);
        break;
      } catch (error) {
        if (!(error instanceof TimeoutError)) throw error;
        if (attempt === 4) throw new Error(`第 ${page + 1} 块连续 5 次未确认。请重新进入 Bootloader，完整重刷。`);
        log(`重试第 ${page + 1} 块（${attempt + 1}/4）…`);
        await delay(200);
      }
    }
    progress(Math.min(offset + 256, image.length), image.length);
    await delay(100);
  }
  log('全部数据块已确认。请关闭电源，再正常开机，然后连接设备。');
}

function command(id, size, stamp) {
  const bytes = new Uint8Array(size);
  view(bytes).setUint16(0, id, true);
  view(bytes).setUint16(2, size - 4, true);
  if (size === 8) view(bytes).setUint32(4, stamp, true);
  return bytes;
}

export class Radio {
  constructor(link) {
    this.link = link;
    this.link.obfuscated = false;
    this.stamp = Math.floor(Date.now() / 1000) >>> 0;
    this.retriesLeft = 6;
  }
  async request(payload, id, match = () => true, timeout = 2000) {
    await this.link.send(payload);
    return this.link.until(reply => reply.length >= 4 && u16(reply) === id && match(reply), timeout);
  }
  async connect() {
    const reply = await this.request(command(0x0514, 8, this.stamp), 0x0515);
    if (reply.length < 20) throw new Error('无效的固件版本应答。');
    this.version = text(reply.slice(4, 20));
    // Only Reborn's current read/diagnostic protocol is supported here.
    if (this.version !== 's0v4') throw new Error(`固件协议 ${this.version} 不受支持，请先刷入 UVK6 Reborn。`);
    const info = await this.request(command(0x0572, 8, this.stamp), 0x0573);
    if (info.length !== 32) throw new Error('无效的存储信息应答。');
    this.capacity = u32(info, 4);
    if (![524288, 2097152].includes(this.capacity)) throw new Error(`设备报告了未支持的存储容量：${this.capacity}。`);
    this.storage = text(info.slice(8, 32));
    return { version: this.version, capacity: this.capacity, storage: this.storage };
  }
  async diagnostics() {
    const reply = await this.request(command(0x0574, 8, this.stamp), 0x0575);
    if (reply.length !== 32) throw new Error('无效的诊断应答。');
    return {
      loops: u32(reply, 4), loopMaxMs: u16(reply, 8), keyEvents: u16(reply, 10),
      squelchChanges: u16(reply, 12), lastKey: reply[14], lastKeyState: reply[15],
      frequency: u32(reply, 16), device: reply[20], open: reply[21],
      broadcast: reply[22], si: reply[23], rssi: u16(reply, 24), noise: u16(reply, 26),
      glitch: u16(reply, 28), status: u16(reply, 30),
    };
  }
  async registers() {
    const reply = await this.request(command(0x0576, 8, this.stamp), 0x0577);
    if (reply.length < 8) throw new Error('无效的 RF 应答。');
    const chip = u16(reply, 4), count = u16(reply, 6);
    if (![4819, 4829].includes(chip) || !count || reply.length !== 8 + count * 4) throw new Error('RF 寄存器长度或芯片不匹配。');
    const values = [];
    for (let i = 0; i < count; i++) values.push([u16(reply, 8 + i * 4), u16(reply, 10 + i * 4)]);
    if (new Set(values.map(([reg]) => reg)).size !== count || values.some(([reg]) => reg >= 128)) throw new Error('无效的 RF 寄存器地址。');
    return { chip, values };
  }
  async backup(address, size, { progress = () => {}, signal, delay = sleep } = {}) {
    if (!Number.isInteger(address) || !Number.isInteger(size) || address < 0 || size <= 0 || address + size > this.capacity) throw new Error('备份范围超出设备存储。');
    const output = new Uint8Array(size);
    for (let offset = 0; offset < size; offset += 128) {
      signal?.throwIfAborted();
      const at = address + offset, count = Math.min(128, size - offset);
      const payload = command(0x051b, 16);
      view(payload).setUint32(4, at, true);
      payload[8] = count;
      view(payload).setUint32(12, this.stamp, true);
      let reply;
      for (let attempt = 0; attempt < 3; attempt++) {
        signal?.throwIfAborted();
        await delay(20);
        try {
          reply = await this.request(payload, 0x051c, bytes => bytes.length === 12 + count && u32(bytes, 4) === at && bytes[8] === count, 1000);
          break;
        } catch (error) {
          if (!(error instanceof TimeoutError) || attempt === 2 || this.retriesLeft === 0) throw error;
          this.retriesLeft--;
          await delay(250);
        }
      }
      output.set(reply.slice(12), offset);
      progress(Math.min(offset + count, size), size);
    }
    signal?.throwIfAborted();
    return output;
  }
  reset() { return this.link.send(command(0x05dd, 4)); }
}
