// Byte transport only. Radio framing and commands live in protocol.js.
export class TimeoutError extends Error {}

export class SerialTransport {
  static async request() {
    if (!globalThis.isSecureContext || !navigator.serial)
      throw new Error('请使用桌面版 Chrome / Edge，通过 HTTPS 或 localhost 打开页面。');
    // Keep the permission picker directly in the user's click gesture.
    const port = await navigator.serial.requestPort();
    const transport = new SerialTransport(port);
    await transport.open();
    return transport;
  }

  constructor(port) {
    this.port = port;
    this.queue = [];
    this.queuedBytes = 0;
    this.error = null;
    this.pending = null;
  }

  async open() {
    await this.port.open({ baudRate: 38400, dataBits: 8, stopBits: 1, parity: 'none', flowControl: 'none', bufferSize: 4096 });
    this.reader = this.port.readable.getReader();
    this.writer = this.port.writable.getWriter();
    this.pump = this.readLoop();
  }

  fail(error) {
    this.error = error;
    this.queue = [];
    this.queuedBytes = 0;
    this.pending?.reject(error);
    this.pending = null;
  }

  async readLoop() {
    try {
      while (true) {
        const { value, done } = await this.reader.read();
        if (done) break;
        if (!value?.length) continue;
        if (this.pending) {
          this.pending.resolve(value);
          this.pending = null;
        } else {
          this.queuedBytes += value.length;
          if (this.queuedBytes > 65536) throw new Error('串口接收缓冲区已满，请重新连接。');
          this.queue.push(value);
        }
      }
      this.fail(new Error('串口已断开，请重新连接。'));
    } catch (error) {
      this.fail(error);
    } finally {
      this.reader.releaseLock();
    }
  }

  read(deadline) {
    if (this.error) return Promise.reject(this.error);
    if (this.queue.length) {
      const chunk = this.queue.shift();
      this.queuedBytes -= chunk.length;
      return Promise.resolve(chunk);
    }
    if (this.pending) return Promise.reject(new Error('串口正在读取。'));
    return new Promise((resolve, reject) => {
      const timer = setTimeout(() => {
        this.pending = null;
        reject(new TimeoutError('等待设备应答超时。'));
      }, Math.max(0, deadline - performance.now()));
      this.pending = {
        resolve: value => { clearTimeout(timer); resolve(value); },
        reject: error => { clearTimeout(timer); reject(error); },
      };
    });
  }

  async write(data) {
    if (this.error) throw this.error;
    let timer;
    try {
      await Promise.race([
        this.writer.write(data),
        new Promise((_, reject) => { timer = setTimeout(() => reject(new TimeoutError('串口写入超时。')), 3000); }),
      ]);
    } finally {
      clearTimeout(timer);
    }
  }

  async close() {
    this.fail(new Error('连接已关闭。'));
    await this.reader?.cancel().catch(() => {});
    await this.pump;
    if (this.writer) {
      await this.writer.abort().catch(() => {});
      this.writer.releaseLock();
      this.writer = null;
    }
    await this.port.close();
  }
}
