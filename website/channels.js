import { GB2312, CTCSS, DCS } from './channel-data.js';
export const MODES = ['NFM', 'WFM', 'AM', 'LSB', 'USB'];
export const TONES = [[0, '关闭'], ...CTCSS.map(n => [0x1000 | n, `${n / 10} Hz`]),
  ...[2, 3].flatMap(type => DCS.map(n => [(type << 12) | n, `D${n.toString(8).padStart(3, '0')}${type === 2 ? 'N' : 'I'}`]))];
const validTones = new Set(TONES.map(([value]) => value));
const names = new Map();
for (let i = 0; i < GB2312.length; i++) if (GB2312[i] !== '\0') names.set(GB2312[i], [0xa1 + Math.floor(i / 94), 0xa1 + i % 94]);
export function encodeName(name) {
  if (typeof name !== 'string') throw new Error('频道名称必须是文字。');
  const bytes = [];
  for (const char of name) {
    const code = char.codePointAt(0);
    const pair = code >= 32 && code <= 126 ? [code] : names.get(char);
    if (!pair) throw new Error(`名称中的「${char}」无法用 GB2312 表示。`);
    bytes.push(...pair);
  }
  if (bytes.length > 16) throw new Error('名称最多 16 字节：汉字通常占 2 字节。');
  return Uint8Array.from(bytes);
}
function decodeName(bytes) {
  let result = '';
  for (let i = 0; i < bytes.length; i++) {
    const code = bytes[i];
    if (!code) {
      if (bytes.slice(i).some(n => n !== 0)) throw new Error('名称零填充无效。');
      break;
    }
    if (code >= 32 && code <= 126) result += String.fromCharCode(code);
    else {
      const low = bytes[++i];
      const char = code >= 0xa1 && code <= 0xf7 && low >= 0xa1 && low <= 0xfe ? GB2312[(code - 0xa1) * 94 + low - 0xa1] : null;
      if (!char || char === '\0') throw new Error('名称包含无效 GB2312 字节。');
      result += char;
    }
  }
  return result;
}
function integer(value, min, max, label) {
  if (!Number.isInteger(value) || value < min || value > max) throw new Error(`${label}必须为 ${min}–${max} 的整数。`);
}
export function validateChannel(c) {
  if (!c || typeof c !== 'object') throw new Error('无效频道。');
  encodeName(c.name);
  integer(c.frequency, 150000, 1340000000, 'RX 频率 (Hz)');
  integer(c.step, 1, 0xffffffff, '步进 (Hz)');
  integer(c.bandwidth, 0, 65535, '带宽 (Hz)');
  integer(c.mode, 0, 4, '模式'); integer(c.squelch, 0, 10, '静噪');
  if (c.mode === 1 && c.bandwidth !== 0) throw new Error('WFM 的带宽字段须设为 0。');
  integer(c.power, 0, 2, '功率'); integer(c.shift, 0, 3, '偏移方式');
  integer(c.offset, 0, 100000000, '偏移量 (Hz)');
  integer(c.split, 0, 1340000000, '异频 TX (Hz)');
  if (c.split && (c.split < 18000000 || c.split > 1300000000)) throw new Error('异频 TX 需为 0 或 18–1300 MHz。');
  if (c.offset % 10 || c.split % 10) throw new Error('偏移量和异频 TX 必须为 10 Hz 整数倍。');
  if (!validTones.has(c.rxTone) || !validTones.has(c.txTone)) throw new Error('亚音不在固件支持列表内。');
  if (c.rxTone && c.mode !== 0) throw new Error('仅 NFM 模式支持 RX 亚音。');
  if (typeof c.txEnabled !== 'boolean' || typeof c.tail !== 'boolean') throw new Error('TX 与尾音开关无效。');
  return c;
}
export function encodeChannel(channel) {
  const c = validateChannel(channel), bytes = new Uint8Array(41), v = new DataView(bytes.buffer);
  bytes.set(encodeName(c.name));
  v.setUint32(16, c.frequency, true); v.setUint32(20, c.step, true);
  v.setUint16(24, c.bandwidth, true); bytes[26] = c.mode; bytes[27] = c.squelch;
  v.setUint16(28, c.rxTone, true);
  bytes[30] = Number(c.txEnabled) | c.power << 1 | c.shift << 3 | Number(c.tail) << 5;
  v.setUint32(31, c.offset, true); v.setUint32(35, c.split, true); v.setUint16(39, c.txTone, true);
  return bytes;
}
export function decodeChannel(bytes) {
  if (bytes.length !== 41 || bytes[30] & 0xc0) throw new Error('频道记录长度或保留位无效。');
  const v = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength), flags = bytes[30];
  return validateChannel({ name: decodeName(bytes.slice(0, 16)), frequency: v.getUint32(16, true),
    step: v.getUint32(20, true), bandwidth: v.getUint16(24, true), mode: bytes[26], squelch: bytes[27],
    rxTone: v.getUint16(28, true), txEnabled: !!(flags & 1), power: flags >> 1 & 3, shift: flags >> 3 & 3,
    tail: !!(flags & 32), offset: v.getUint32(31, true), split: v.getUint32(35, true), txTone: v.getUint16(39, true) });
}
export const newChannel = () => ({ name: '', frequency: 145500000, step: 12500, bandwidth: 12000,
  mode: 0, squelch: 4, rxTone: 0, txEnabled: false, power: 0, shift: 0, tail: false, offset: 0, split: 0, txTone: 0 });
export function sameChannel(a, b) {
  if (a === null || b === null) return a === b;
  const left = encodeChannel(a), right = encodeChannel(b);
  return left.every((value, i) => value === right[i]);
}
export function importChannels(text) {
  const file = JSON.parse(text);
  if (file.format !== 'reborn-channels-v1' || !Array.isArray(file.channels) || file.channels.length > 200) throw new Error('请选择 Reborn 频道 JSON 文件。');
  const seen = new Set();
  return file.channels.map(({ number, channel }) => {
    integer(number, 1, 200, '频道号');
    if (seen.has(number)) throw new Error(`重复频道号 ${number}。`);
    seen.add(number);
    return { number, channel: channel === null ? null : decodeChannel(encodeChannel(channel)) };
  });
}
