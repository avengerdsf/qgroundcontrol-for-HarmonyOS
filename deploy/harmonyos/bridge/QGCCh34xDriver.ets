/*
 * CH34x protocol adapted from usb-serial-for-android v3.8.1
 * Copyright 2014 Andreas Butti. MIT License; see USB_SERIAL_LICENSE.txt.
 * https://github.com/mik3y/usb-serial-for-android/blob/v3.8.1/usbSerialForAndroid/src/main/java/com/hoho/android/usbserial/driver/Ch34xSerialDriver.java
 */
import { UsbSerialControl, UsbSerialDriver, UsbSerialDriverError, UsbSerialLine } from './QGCUsbSerialTypes';

class Ch34xDriver implements UsbSerialDriver {
  readonly kind: string = 'ch34x';

  private baudRegisters(baud: number): number[] {
    if (!Number.isInteger(baud) || baud <= 0 || baud > 0xffffffff) {
      throw new UsbSerialDriverError(401, 'Invalid CH34x baud rate');
    }
    let factor: number;
    let divisor: number;
    if (baud === 921600) {
      factor = 0xf300;
      divisor = 7;
    } else {
      factor = Math.floor(1532620800 / baud);
      divisor = 3;
      if (factor < 1) {
        throw new UsbSerialDriverError(401, 'Unsupported CH34x baud rate');
      }
      while (factor > 0xfff0 && divisor > 0) {
        factor >>= 3;
        divisor--;
      }
      if (factor > 0xfff0) {
        throw new UsbSerialDriverError(401, 'Unsupported CH34x baud rate');
      }
      factor = 0x10000 - factor;
    }
    // CH341A otherwise holds outgoing bytes until its buffer fills.
    return [(factor & 0xff00) | divisor | 0x80, factor & 0xff];
  }

  private lineControl(line: UsbSerialLine): number {
    if (!Number.isInteger(line.dataBits) || line.dataBits < 5 || line.dataBits > 8 ||
      !Number.isInteger(line.parity) || line.parity < 0 || line.parity > 4 ||
      (line.stopBits !== 0 && line.stopBits !== 1)) {
      throw new UsbSerialDriverError(401, 'Unsupported CH34x line coding');
    }
    const parity: number[] = [0, 0x08, 0x18, 0x28, 0x38];
    return 0xc0 | (line.dataBits - 5) | parity[line.parity] | (line.stopBits === 1 ? 0x04 : 0);
  }

  private async out(control: UsbSerialControl, request: number, value: number, index: number): Promise<void> {
    await control.transfer(0x40, request, value, index, new Uint8Array(0));
  }

  private async check(control: UsbSerialControl, request: number, value: number, zeroSecond: boolean): Promise<void> {
    const data: Uint8Array = new Uint8Array(2);
    await control.transfer(0xc0, request, value, 0, data);
    // Firmware versions vary in byte zero; only byte one is fixed for these registers.
    if (zeroSecond && data[1] !== 0) {
      throw new UsbSerialDriverError(14400012, 'Unexpected CH34x initialization response');
    }
  }

  private async baud(control: UsbSerialControl, registers: number[]): Promise<void> {
    await this.out(control, 0x9a, 0x1312, registers[0]);
    await this.out(control, 0x9a, 0x0f2c, registers[1]);
  }

  async initialize(control: UsbSerialControl, line: UsbSerialLine, dtr: boolean = true): Promise<void> {
    const registers: number[] = this.baudRegisters(line.baud);
    const lcr: number = this.lineControl(line);
    const defaults: number[] = [0xb282, 0x000c];
    await this.check(control, 0x5f, 0, true);
    await this.out(control, 0xa1, 0, 0);
    await this.baud(control, defaults);
    await this.check(control, 0x95, 0x2518, true);
    await this.out(control, 0x9a, 0x2518, 0xc3);
    await this.check(control, 0x95, 0x0706, false);
    await this.out(control, 0xa1, 0x501f, 0xd90a);
    await this.baud(control, defaults);
    await this.setLines(control, false, false);
    await this.check(control, 0x95, 0x0706, false);
    await this.baud(control, defaults);
    await this.baud(control, registers);
    await this.out(control, 0x9a, 0x2518, lcr);
    await this.setLines(control, dtr, true);
  }

  async configure(control: UsbSerialControl, line: UsbSerialLine): Promise<void> {
    const registers: number[] = this.baudRegisters(line.baud);
    const lcr: number = this.lineControl(line);
    await this.baud(control, registers);
    await this.out(control, 0x9a, 0x2518, lcr);
  }

  async setLines(control: UsbSerialControl, dtr: boolean, rts: boolean): Promise<void> {
    await this.out(control, 0xa4, ~((dtr ? 0x20 : 0) | (rts ? 0x40 : 0)) & 0xffff, 0);
  }

  async shutdown(control: UsbSerialControl): Promise<void> {
    await this.setLines(control, false, false);
  }
  decodeRead(buffer: Uint8Array, _maxPacketSize: number): Uint8Array { return buffer; }
}

export function isCh34xDevice(vendorId: number, productId: number): boolean {
  return vendorId === 0x1a86 && (productId === 0x7523 || productId === 0x5523);
}
export function createCh34xDriver(_interfaceNumber: number, _bcdDevice: number, _productId: number): UsbSerialDriver {
  return new Ch34xDriver();
}
