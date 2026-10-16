export interface UsbSerialLine {
  baud: number;
  dataBits: number;
  parity: number;
  stopBits: number;
}

export interface UsbSerialControl {
  transfer(requestType: number, request: number, value: number, index: number,
    data: Uint8Array): Promise<number>;
}

export interface UsbSerialDriver {
  readonly kind: string;
  initialize(control: UsbSerialControl, line: UsbSerialLine): Promise<void>;
  configure(control: UsbSerialControl, line: UsbSerialLine): Promise<void>;
  setLines(control: UsbSerialControl, dtr: boolean, rts: boolean): Promise<void>;
  shutdown(control: UsbSerialControl): Promise<void>;
  decodeRead(buffer: Uint8Array, maxPacketSize: number): Uint8Array;
}

export class UsbSerialDriverError extends Error {
  code: number;

  constructor(code: number, message: string) {
    super(message);
    this.code = code;
  }
}
