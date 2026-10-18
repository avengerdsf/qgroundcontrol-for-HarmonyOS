// Protocol adaptation from usb-serial-for-android v3.8.1, MIT license.
// Copyright 2011-2013 Google Inc.; Copyright 2013 Mike Wakerly.
// See the USB serial third-party license distributed with this project.
import { UsbSerialControl, UsbSerialDriver, UsbSerialDriverError, UsbSerialLine } from './QGCUsbSerialTypes';

class Cp210xDriver implements UsbSerialDriver {
  readonly kind: string = 'cp210x';
  private interfaceNumber: number;
  private restricted: boolean;

  constructor(interfaceNumber: number, productId: number) {
    this.interfaceNumber = interfaceNumber;
    this.restricted = productId === 0xea70 && interfaceNumber === 1;
  }

  private validate(line: UsbSerialLine): void {
    if (!Number.isInteger(line.baud) || line.baud <= 0 || line.baud > 0xffffffff ||
        !Number.isInteger(line.dataBits) || line.dataBits < 5 || line.dataBits > 8 ||
        !Number.isInteger(line.parity) || line.parity < 0 || line.parity > 4 ||
        (line.stopBits !== 0 && line.stopBits !== 1)) {
      throw new UsbSerialDriverError(401, 'Unsupported CP210x line coding');
    }
    if (this.restricted && (line.dataBits !== 8 || line.parity > 2 || line.stopBits !== 0)) {
      throw new UsbSerialDriverError(401, 'CP2105 second port supports 8 data bits, one stop bit and none/odd/even parity');
    }
  }

  private async command(control: UsbSerialControl, request: number, value: number,
      data: Uint8Array = new Uint8Array(0)): Promise<void> {
    await control.transfer(0x41, request, value, this.interfaceNumber, data);
  }

  async initialize(control: UsbSerialControl, line: UsbSerialLine, dtr: boolean = true): Promise<void> {
    this.validate(line);
    await this.command(control, 0, 1);
    await this.setLines(control, dtr, true);
    // No hardware/software flow control; retain manual DTR/RTS control.
    const flow: Uint8Array = new Uint8Array(16);
    flow[0] = 1;
    flow[4] = 0x40;
    await this.command(control, 0x13, 0, flow);
    await this.configure(control, line);
  }

  async configure(control: UsbSerialControl, line: UsbSerialLine): Promise<void> {
    this.validate(line);
    const baud: Uint8Array = new Uint8Array([
      line.baud & 0xff, (line.baud >>> 8) & 0xff, (line.baud >>> 16) & 0xff, (line.baud >>> 24) & 0xff
    ]);
    await this.command(control, 0x1e, 0, baud);
    await this.command(control, 3, (line.dataBits << 8) | (line.parity << 4) | (line.stopBits === 1 ? 2 : 0));
  }

  async setLines(control: UsbSerialControl, dtr: boolean, rts: boolean): Promise<void> {
    await this.command(control, 7, 0x300 | (dtr ? 1 : 0) | (rts ? 2 : 0));
  }

  async shutdown(control: UsbSerialControl): Promise<void> {
    try { await this.setLines(control, false, false); } catch (_) {}
    try { await this.command(control, 0, 0); } catch (_) {}
  }
  decodeRead(buffer: Uint8Array, _maxPacketSize: number): Uint8Array { return buffer; }
}

export function isCp210xDevice(vendorId: number, productId: number): boolean {
  return vendorId === 0x10c4 && (productId === 0xea60 || productId === 0xea70 || productId === 0xea71);
}

export function createCp210xDriver(interfaceNumber: number, _bcdDevice: number, productId: number): UsbSerialDriver {
  if (!Number.isInteger(interfaceNumber) || interfaceNumber < 0 || interfaceNumber > 255 ||
      !isCp210xDevice(0x10c4, productId)) {
    throw new UsbSerialDriverError(401, 'Unsupported CP210x interface or product');
  }
  return new Cp210xDriver(interfaceNumber, productId);
}
