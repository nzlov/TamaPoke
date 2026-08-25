export class SerialClient {
  constructor(errorForLine = null, receiveError = null) {
    this.errorForLine = errorForLine;
    this.receiveError = receiveError;
    this.port = null;
    this.reader = null;
    this.writer = null;
    this.lines = [];
    this.waiters = [];
    this.encoder = new TextEncoder();
    this.decoder = new TextDecoder();
    this.lineBytes = [];
    this.binary = null;
  }

  get connected() { return Boolean(this.writer); }

  async connect() {
    if (this.connected) return;
    this.port = await navigator.serial.requestPort();
    await this.port.open({ baudRate: 115200 });
    this.reader = this.port.readable.getReader();
    this.writer = this.port.writable.getWriter();
    this.lines = [];
    this.pump(this.reader);
  }

  receive(line) {
    const waiter = this.waiters.shift();
    if (!waiter) { this.lines.push(line); return; }
    clearTimeout(waiter.timer);
    waiter.resolve(line);
  }

  consume(chunk) {
    let offset = 0;
    while (offset < chunk.length) {
      if (this.binary) {
        const transfer = this.binary;
        const count = Math.min(chunk.length - offset, transfer.data.length - transfer.offset);
        transfer.data.set(chunk.subarray(offset, offset + count), transfer.offset);
        transfer.offset += count;
        offset += count;
        transfer.progress?.(transfer.offset, transfer.data.length);
        if (transfer.offset === transfer.data.length) {
          this.binary = null;
          transfer.resolve(transfer.data);
        }
        continue;
      }
      const byte = chunk[offset++];
      if (byte === 10) {
        const line = this.decoder.decode(new Uint8Array(this.lineBytes)).trim();
        this.lineBytes = [];
        this.receive(line);
      } else {
        this.lineBytes.push(byte);
      }
    }
  }

  expectBinary(size, progress) {
    if (this.binary) throw new Error('another binary transfer is active');
    return new Promise((resolve, reject) => {
      this.binary = { data: new Uint8Array(size), offset: 0, progress, resolve, reject };
    });
  }

  cancelBinary(error) {
    if (!this.binary) return;
    const transfer = this.binary;
    this.binary = null;
    transfer.reject(error);
  }

  async pump(activeReader) {
    try {
      while (true) {
        const { value, done } = await activeReader.read();
        if (done) break;
        this.consume(value);
      }
    } catch (error) {
      this.receiveError?.(error);
    } finally {
      this.cancelBinary(new Error('board disconnected during file transfer'));
      for (const waiter of this.waiters.splice(0)) {
        clearTimeout(waiter.timer);
        waiter.resolve(null);
      }
    }
  }

  async readLine(timeoutMs = 6000) {
    if (this.lines.length) return this.lines.shift();
    if (timeoutMs <= 0) return null;
    return new Promise(resolve => {
      const waiter = { resolve, timer: 0 };
      waiter.timer = setTimeout(() => {
        const index = this.waiters.indexOf(waiter);
        if (index >= 0) this.waiters.splice(index, 1);
        resolve(null);
      }, timeoutMs);
      this.waiters.push(waiter);
    });
  }

  boardError(line) {
    if (!(line === 'ERR' || line.startsWith('ERR '))) return null;
    return this.errorForLine ? this.errorForLine(line) : new Error(line.slice(3).trim() || 'board error');
  }

  async waitFor(token, timeoutMs = 6000, action = token) {
    const deadline = Date.now() + timeoutMs;
    while (Date.now() < deadline) {
      const line = await this.readLine(deadline - Date.now());
      if (line === null) break;
      if (line === token) return;
      const error = this.boardError(line);
      if (error) throw error;
    }
    throw new Error(`board timed out while ${action}`);
  }

  async sendFile(path, data, progress = null) {
    if (!this.writer) throw new Error('connect the board first');
    await this.writer.write(this.encoder.encode(`PUT ${path} ${data.length}\n`));
    await this.waitFor('OK', 6000, `accepting ${path}`);
    for (let offset = 0; offset < data.length; offset += 2048) {
      await this.writer.write(data.slice(offset, offset + 2048));
      await this.waitFor('#', 6000, `writing ${path} at byte ${offset}`);
      progress?.(Math.min(offset + 2048, data.length));
    }
    await this.waitFor('DONE', 120000, `validating ${path}`);
  }

  async readFile(path, progress = null) {
    if (!this.writer) throw new Error('connect the board first');
    await this.writer.write(this.encoder.encode(`GET ${path}\n`));
    const deadline = Date.now() + 10000;
    let size = 0;
    while (Date.now() < deadline) {
      const line = await this.readLine(deadline - Date.now());
      if (line === null) break;
      const error = this.boardError(line);
      if (error) throw error;
      const header = line.match(/^FILE (\d+)$/);
      if (!header) continue;
      size = Number(header[1]);
      if (!Number.isSafeInteger(size) || size <= 0 || size > 256 * 1024 * 1024)
        throw new Error('board returned an invalid file size');
      break;
    }
    if (!size) throw new Error('board did not start the file transfer');

    const binary = this.expectBinary(size, progress);
    await this.writer.write(this.encoder.encode('OK\n'));
    let timer = 0;
    try {
      const data = await Promise.race([
        binary,
        new Promise((_, reject) => {
          timer = setTimeout(() => reject(new Error('board timed out while sending the file')),
            Math.max(120000, size));
        }),
      ]);
      await this.waitFor('DONE', 120000, `finishing ${path}`);
      return data;
    } catch (error) {
      this.cancelBinary(error);
      throw error;
    } finally {
      clearTimeout(timer);
    }
  }

  async command(command, timeoutMs = 10000) {
    if (!this.writer) throw new Error('connect the board first');
    await this.writer.write(this.encoder.encode(`${command}\n`));
    const lines = [], deadline = Date.now() + timeoutMs;
    while (Date.now() < deadline) {
      const line = await this.readLine(deadline - Date.now());
      if (line === null) break;
      if (line === 'DONE') return lines;
      const error = this.boardError(line);
      if (error) throw error;
      lines.push(line);
    }
    throw new Error('board response timed out');
  }
}
