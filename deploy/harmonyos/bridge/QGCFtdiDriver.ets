// Protocol adaptation from usb-serial-for-android v3.8.1, MIT license.
// Copyright 2011-2013 Google Inc.; Copyright 2013 Mike Wakerly;
// Copyright 2020 Kai Morich. See the distributed USB serial third-party license.
import { UsbSerialControl, UsbSerialDriver, UsbSerialDriverError, UsbSerialLine } from './QGCUsbSerialTypes';

class FtdiBaudRequest {
  value: number;
  index: number;

  constructor(value: number, index: number) {
    this.value = value;
    this.index = index;
  }
}

class FtdiDriver implements UsbSerialDriver {
  readonly kind: string = 'ftdi';
  private channel: number;
  private baudWithChannel: boolean;

  constructor(interfaceNumber: number, bcdDevice: number, productId: number) {
    this.channel = interfaceNumber + 1;
    const type: number = (bcdDevice >>> 8) & 0xff;
    this.baudWithChannel = type === 7 || type === 8 || type === 9 || productId === 0x6010 || productId === 0x6011;
  }

  private baudRequest(baud: number): FtdiBaudRequest {
    if (!Number.isInteger(baud) || baud <= 0 || baud > 3500000) {
      throw new UsbSerialDriverError(401, 'Unsupported FTDI baud rate');
    }
    let divisor: number;
    let fraction: number;
    let effective: number;
    if (baud >= 2500000) {
      divisor = 0;
      fraction = 0;
      effective = 3000000;
    } else if (baud >= 1750000) {
      divisor = 1;
      fraction = 0;
      effective = 2000000;
    } else {
      const eighths: number = Math.floor((Math.floor(48000000 / baud) + 1) / 2);
      fraction = eighths & 7;
      divisor = Math.floor(eighths / 8);
      if (divisor > 0x3fff) {
        throw new UsbSerialDriverError(401, 'FTDI baud rate is below the supported divisor range');
      }
      effective = Math.floor((Math.floor(48000000 / eighths) + 1) / 2);
    }
    if (Math.abs(1 - effective / baud) >= 0.031) {
      throw new UsbSerialDriverError(401, 'FTDI baud rate deviation exceeds the supported tolerance');
    }
    const fractions: number[] = [0, 3, 2, 4, 1, 5, 6, 7];
    const encoded: number = divisor | (fractions[fraction] << 14);
    const high: number = encoded >>> 16;
    return new FtdiBaudRequest(encoded & 0xffff, this.baudWithChannel ? (high << 8) | this.channel : high);
  }

  private frameValue(line: UsbSerialLine): number {
    if ((line.dataBits !== 7 && line.dataBits !== 8) || !Number.isInteger(line.parity) ||
        line.parity < 0 || line.parity > 4 || (line.stopBits !== 0 && line.stopBits !== 1)) {
      throw new UsbSerialDriverError(401, 'FTDI supports 7/8 data bits, one/two stop bits and parity 0..4');
    }
    return line.dataBits | (line.parity << 8) | (line.stopBits === 1 ? 0x1000 : 0);
  }

  private async command(control: UsbSerialControl, request: number, value: number, index: number): Promise<void> {
    await control.transfer(0x40, request, value, index, new Uint8Array(0));
  }

  async initialize(control: UsbSerialControl, line: UsbSerialLine, dtr: boolean = true): Promise<void> {
    this.baudRequest(line.baud);
    this.frameValue(line);
    await this.command(control, 0, 0, this.channel);
    await this.setLines(control, dtr, true);
    await this.command(control, 2, 0, this.channel);
    await this.configure(control, line);
  }

  async configure(control: UsbSerialControl, line: UsbSerialLine): Promise<void> {
    const baud: FtdiBaudRequest = this.baudRequest(line.baud);
    const frame: number = this.frameValue(line);
    await this.command(control, 3, baud.value, baud.index);
    await this.command(control, 4, frame, this.channel);
  }

  async setLines(control: UsbSerialControl, dtr: boolean, rts: boolean): Promise<void> {
    await this.command(control, 1, 0x300 | (dtr ? 1 : 0) | (rts ? 2 : 0), this.channel);
  }

  async shutdown(control: UsbSerialControl): Promise<void> {
    try { await this.setLines(control, false, false); } catch (_) {}
  }

  decodeRead(buffer: Uint8Array, maxPacketSize: number): Uint8Array {
    if (!Number.isInteger(maxPacketSize) || maxPacketSize < 2) {
      throw new UsbSerialDriverError(14400012, 'Invalid FTDI bulk packet size');
    }
    const payload: Uint8Array = new Uint8Array(buffer.length);
    let written: number = 0;
    for (let offset: number = 0; offset < buffer.length; offset += maxPacketSize) {
      const end: number = Math.min(offset + maxPacketSize, buffer.length);
      if (end - offset < 2) {
        throw new UsbSerialDriverError(14400012, 'FTDI packet is missing status bytes');
      }
      payload.set(buffer.subarray(offset + 2, end), written);
      written += end - offset - 2;
    }
    return payload.slice(0, written);
  }
}

export function isFtdiDevice(vendorId: number, productId: number): boolean {
  return vendorId === 0x0403 && (productId === 0x6001 || productId === 0x6010 || productId === 0x6011 ||
    productId === 0x6014 || productId === 0x6015);
}

export function createFtdiDriver(interfaceNumber: number, bcdDevice: number, productId: number): UsbSerialDriver {
  if (!Number.isInteger(interfaceNumber) || interfaceNumber < 0 || interfaceNumber > 254 ||
      !Number.isInteger(bcdDevice) || bcdDevice < 0 || bcdDevice > 0xffff || !isFtdiDevice(0x0403, productId)) {
    throw new UsbSerialDriverError(401, 'Unsupported FTDI interface, revision or product');
  }
  return new FtdiDriver(interfaceNumber, bcdDevice, productId);
}
