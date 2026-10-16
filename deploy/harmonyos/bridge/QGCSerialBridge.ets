import { BusinessError, serialManager, usbManager } from '@kit.BasicServicesKit';
import { UsbSerialControl, UsbSerialDriver, UsbSerialLine } from './QGCUsbSerialTypes';
import { isCp210xDevice, createCp210xDriver } from './QGCCp210xDriver';
import { isCh34xDevice, createCh34xDriver } from './QGCCh34xDriver';
import { isFtdiDevice, createFtdiDriver } from './QGCFtdiDriver';
import { CdcInterfacePair, parseCdcPairs } from './QGCCdcDescriptors';

class SerialPortMetadata {
  portId: number;
  deviceName: string;
  hasUsbIdentity: boolean = false;
  vendorId: number = -1;
  productId: number = -1;
  manufacturer: string = '';
  productName: string = '';

  constructor(port: serialManager.SerialPort) {
    this.portId = port.portId;
    this.deviceName = port.deviceName;
  }
}

class SerialBridgeError extends Error implements BusinessError {
  code: number;

  constructor(code: number, message: string) {
    super(message);
    this.code = code;
  }
}

type UsbControlHandler = (requestType: number, request: number, value: number,
  index: number, data: Uint8Array) => Promise<number>;

class SessionUsbControl implements UsbSerialControl {
  private handler: UsbControlHandler;

  constructor(handler: UsbControlHandler) { this.handler = handler; }

  transfer(requestType: number, request: number, value: number, index: number, data: Uint8Array): Promise<number> {
    return this.handler(requestType, request, value, index, data);
  }
}

class SerialSession {
  portId: number;
  deviceName: string;
  opened: boolean = false;
  closed: boolean = false;
  cdc: UsbSerialPort | undefined;
  pipe: usbManager.USBDevicePipe | undefined;
  connection: UsbDeviceConnection | undefined;
  controlClaimed: boolean = false;
  dataClaimed: boolean = false;
  controlPending: number = 0;
  releasing: boolean = false;
  lineStateSet: boolean = false;
  baud: number = 115200;
  dataBits: number = 8;
  parity: number = 0;
  stopBits: number = 0;
  readTransfer: UsbPendingTransfer | undefined;
  writeTransfer: UsbPendingTransfer | undefined;
  stage: string = 'permission';

  constructor(portId: number, deviceName: string, cdc?: UsbSerialPort) {
    this.portId = portId;
    this.deviceName = deviceName;
    this.cdc = cdc;
  }
}

class UsbDeviceConnection {
  pipe: usbManager.USBDevicePipe;
  configId: number;
  users: Set<SerialSession> = new Set<SerialSession>();

  constructor(pipe: usbManager.USBDevicePipe, configId: number) {
    this.pipe = pipe;
    this.configId = configId;
  }
}

class UsbSerialPort {
  device: usbManager.USBDevice;
  config: usbManager.USBConfiguration;
  control: usbManager.USBInterface;
  data: usbManager.USBInterface | undefined;
  input: usbManager.USBEndpoint | undefined;
  output: usbManager.USBEndpoint | undefined;
  kind: string = 'cdc';
  driver: UsbSerialDriver | undefined;
  private identityDataId: number | undefined;

  constructor(device: usbManager.USBDevice, config: usbManager.USBConfiguration,
    control: usbManager.USBInterface, data?: usbManager.USBInterface,
    input?: usbManager.USBEndpoint, output?: usbManager.USBEndpoint) {
    this.device = device;
    this.config = config;
    this.control = control;
    this.data = data;
    this.identityDataId = data?.id;
    this.input = input;
    this.output = output;
  }

  key(): string {
    return `${this.device.name}|${this.device.vendorId}:${this.device.productId}|` +
      `${this.config.id}:${this.control.id}${this.identityDataId === undefined ? '' : `:${this.identityDataId}`}`;
  }
}

class UsbPendingTransfer {
  params: usbManager.UsbDataTransferParams;
  reject: (error: SerialBridgeError) => void;

  constructor(params: usbManager.UsbDataTransferParams, reject: (error: SerialBridgeError) => void) {
    this.params = params;
    this.reject = reject;
  }
}

class QGCSerialBridge {
  private sessions: Map<number, SerialSession> = new Map<number, SerialSession>();
  private deniedPorts: Set<string> = new Set<string>();
  private usbPorts: Map<number, UsbSerialPort> = new Map<number, UsbSerialPort>();
  private usbIds: Map<string, number> = new Map<string, number>();
  private closingUsbPorts: Map<string, SerialSession> = new Map<string, SerialSession>();
  private usbConnections: Map<string, UsbDeviceConnection> = new Map<string, UsbDeviceConnection>();
  private pendingUsbRights: Map<string, Promise<boolean>> = new Map<string, Promise<boolean>>();
  private lastDiscoveryState: string = '';
  private discoveryState: string = '';
  private sessionState: string = 'idle';
  private lastFailure: string = 'none';

  private logDiscovery(state: string, failed: boolean): void {
    if (state === this.lastDiscoveryState) {
      return;
    }
    this.lastDiscoveryState = state;
    if (failed) {
      console.warn(`QGC USB serial discovery: ${state}`);
    } else {
      console.info(`QGC USB serial discovery: ${state}`);
    }
  }

  getDiscoveryState(): string {
    return this.lastDiscoveryState;
  }

  private logSession(session: SerialSession, state: string, code: number = 0): void {
    this.sessionState = `backend=${session.cdc === undefined ? 'native' : session.cdc.kind} ` +
      `state=${state} stage=${session.stage} code=${code}`;
    if (code !== 0) {
      this.lastFailure = this.sessionState;
    }
    this.logDiscovery(`${this.discoveryState} session=[${this.sessionState}] lastFailure=[${this.lastFailure}]`, code !== 0);
  }

  private cdcId(key: string, used: Set<number>): number {
    let id: number | undefined = this.usbIds.get(key);
    if (id === undefined || used.has(id)) {
      id = 0x40000000;
      for (let index: number = 0; index < key.length; index++) {
        id = ((id * 31) + key.charCodeAt(index)) & 0x7fffffff;
      }
      while (used.has(id)) {
        id = (id + 1) & 0x7fffffff;
      }
      this.usbIds.set(key, id);
    }
    used.add(id);
    return id;
  }

  private bulkInterfaces(interfaces: usbManager.USBInterface[], clazz: number): usbManager.USBInterface[] {
    const result: usbManager.USBInterface[] = [];
    const ids: Set<number> = new Set<number>(interfaces.filter((iface: usbManager.USBInterface) =>
      iface.clazz === clazz).map((iface: usbManager.USBInterface) => iface.id));
    ids.forEach((id: number) => {
      const candidates: usbManager.USBInterface[] = interfaces.filter((iface: usbManager.USBInterface) =>
        iface.id === id && iface.clazz === clazz && iface.endpoints.filter((endpoint: usbManager.USBEndpoint) =>
          endpoint.type === 2 && (endpoint.address & 0x80) !== 0).length === 1 &&
        iface.endpoints.filter((endpoint: usbManager.USBEndpoint) =>
          endpoint.type === 2 && (endpoint.address & 0x80) === 0).length === 1);
      const defaults: usbManager.USBInterface[] = candidates.filter((iface: usbManager.USBInterface) => iface.alternateSetting === 0);
      if (defaults.length === 1) { result.push(defaults[0]); }
      else if (candidates.length === 1) { result.push(candidates[0]); }
    });
    return result;
  }

  private assignData(port: UsbSerialPort, data: usbManager.USBInterface): void {
    port.data = data;
    port.input = data.endpoints.find((endpoint: usbManager.USBEndpoint) =>
      endpoint.type === 2 && (endpoint.address & 0x80) !== 0);
    port.output = data.endpoints.find((endpoint: usbManager.USBEndpoint) =>
      endpoint.type === 2 && (endpoint.address & 0x80) === 0);
    if (port.input === undefined || port.output === undefined) {
      throw new SerialBridgeError(401, 'USB serial bulk endpoints are unavailable');
    }
  }

  private findCdc(device: usbManager.USBDevice, diagnostics: string[]): UsbSerialPort[] {
    const ports: UsbSerialPort[] = [];
    device.configs.forEach((config: usbManager.USBConfiguration) => {
      const label: string = `vid=${device.vendorId},pid=${device.productId},config=${config.id}`;
      const summary: string = config.interfaces.map((iface: usbManager.USBInterface) =>
        `id=${iface.id}/alt=${iface.alternateSetting}/class=${iface.clazz}/subclass=${iface.subClass}/ep=` +
        iface.endpoints.map((endpoint: usbManager.USBEndpoint) =>
          `${endpoint.address}:${endpoint.type}`).join(',')).join('|');
      const controls: usbManager.USBInterface[] = [];
      config.interfaces.filter((iface: usbManager.USBInterface) => iface.clazz === 2 && iface.subClass === 2)
        .forEach((iface: usbManager.USBInterface) => {
          const existing: number = controls.findIndex((candidate: usbManager.USBInterface) => candidate.id === iface.id);
          if (existing < 0) { controls.push(iface); }
          else if (iface.alternateSetting === 0) { controls[existing] = iface; }
        });
      const data: usbManager.USBInterface[] = this.bulkInterfaces(config.interfaces, 10);
      const dataIds: Set<number> = new Set<number>(config.interfaces.filter((iface: usbManager.USBInterface) =>
        iface.clazz === 10).map((iface: usbManager.USBInterface) => iface.id));
      const communicationIds: Set<number> = new Set<number>(config.interfaces.filter((iface: usbManager.USBInterface) =>
        iface.clazz === 2).map((iface: usbManager.USBInterface) => iface.id));
      if (controls.length > 0 && data.length > 0) {
        controls.forEach((control: usbManager.USBInterface) => {
          const unique: boolean = controls.length === 1 && communicationIds.size === 1 && dataIds.size === 1 &&
            data.length === 1 && control.id !== data[0].id;
          const port: UsbSerialPort = new UsbSerialPort(device, config, control, unique ? data[0] : undefined);
          if (unique) {
            this.assignData(port, data[0]);
          }
          ports.push(port);
        });
        diagnostics.push(`${label},candidate=${controls.length},association=${controls.length === 1 &&
          communicationIds.size === 1 && dataIds.size === 1 ? 'unique' : 'descriptor-pending'},interfaces=[${summary}]`);
        return;
      }
      let kind: string = '';
      if (isCp210xDevice(device.vendorId, device.productId)) { kind = 'cp210x'; }
      else if (isCh34xDevice(device.vendorId, device.productId)) { kind = 'ch34x'; }
      else if (isFtdiDevice(device.vendorId, device.productId)) { kind = 'ftdi'; }
      const vendorInterfaces: usbManager.USBInterface[] = kind === '' ? [] : this.bulkInterfaces(config.interfaces, 255);
      if (kind === 'ch34x' && vendorInterfaces.length !== 1) {
        diagnostics.push(`${label},skip=ambiguous,interfaces=[${summary}]`);
        return;
      }
      vendorInterfaces.forEach((iface: usbManager.USBInterface) => {
        const port: UsbSerialPort = new UsbSerialPort(device, config, iface);
        port.kind = kind;
        this.assignData(port, iface);
        ports.push(port);
      });
      diagnostics.push(`${label},${vendorInterfaces.length === 0 ? 'skip=not-cdc' : `driver=${kind}`},interfaces=[${summary}]`);
    });
    return ports;
  }

  getPorts(): SerialPortMetadata[] {
    const serialCapability: boolean = canIUse('SystemCapability.USB.USBManager.Serial');
    const rawUsbCapability: boolean = canIUse('SystemCapability.USB.USBManager');
    let ports: serialManager.SerialPort[] = [];
    let serialError: BusinessError | undefined = undefined;
    if (!serialCapability) {
      serialError = new SerialBridgeError(801, 'USB serial capability is unavailable on this device');
    } else {
      try {
        ports = serialManager.getPortList();
      } catch (error) {
        serialError = error as BusinessError;
      }
    }

    let devices: usbManager.USBDevice[] = [];
    let usbError: BusinessError | undefined = undefined;
    if (rawUsbCapability) {
      try {
        devices = usbManager.getDevices();
      } catch (error) {
        usbError = error as BusinessError;
      }
    }
    const usbIdentities: string[] = devices.map((device: usbManager.USBDevice) =>
      `vid=${device.vendorId},pid=${device.productId}`).sort();
    const discoveryState: string = `serialCapability=${serialCapability} rawUsbCapability=${rawUsbCapability} ` +
      `rawUsbCount=${rawUsbCapability && usbError === undefined ? devices.length : 'unavailable'} ` +
      `rawUsbErrorCode=${usbError === undefined ? 0 : usbError.code} usbDevices=[${usbIdentities.join(';')}] ` +
      `nativePortCount=${serialError === undefined ? ports.length : 'unavailable'} ` +
      `serialErrorCode=${serialError === undefined ? 0 : serialError.code}`;
    const identityMatches: string[] = [];
    const metadata: SerialPortMetadata[] = ports.map((port: serialManager.SerialPort) => {
      const metadata: SerialPortMetadata = new SerialPortMetadata(port);
      const matches: usbManager.USBDevice[] = devices.filter((device: usbManager.USBDevice) =>
        device.name === port.deviceName);
      if (matches.length === 1) {
        const device: usbManager.USBDevice = matches[0];
        if (device.vendorId > 0 && device.vendorId <= 65535 && device.productId >= 0 && device.productId <= 65535) {
          metadata.hasUsbIdentity = true;
          metadata.vendorId = device.vendorId;
          metadata.productId = device.productId;
          metadata.manufacturer = device.manufacturerName;
          metadata.productName = device.productName;
        }
      }
      identityMatches.push(`port=${port.portId},usbMatches=${matches.length},identity=` +
        `${metadata.hasUsbIdentity ? `${metadata.vendorId}:${metadata.productId}` : 'unavailable'}`);
      return metadata;
    });
    const diagnostics: string[] = [];
    const candidates: UsbSerialPort[] = [];
    devices.forEach((device: usbManager.USBDevice) => {
      if (!ports.some((port: serialManager.SerialPort) => port.deviceName === device.name)) {
        this.findCdc(device, diagnostics).forEach((port: UsbSerialPort) => candidates.push(port));
      }
    });
    const retainedKeys: Set<string> = new Set<string>(candidates.map((port: UsbSerialPort) => port.key()));
    this.sessions.forEach((session: SerialSession) => {
      if (session.cdc !== undefined) { retainedKeys.add(session.cdc.key()); }
    });
    this.closingUsbPorts.forEach((session: SerialSession) => {
      if (session.cdc !== undefined) { retainedKeys.add(session.cdc.key()); }
    });
    this.usbIds.forEach((_: number, key: string) => {
      if (!retainedKeys.has(key)) { this.usbIds.delete(key); }
    });
    const used: Set<number> = new Set<number>(ports.map((port: serialManager.SerialPort) => port.portId));
    this.usbPorts.clear();
    candidates.sort((left: UsbSerialPort, right: UsbSerialPort) => left.key().localeCompare(right.key()));
    // Reserve existing IDs before assigning new or native-conflicting candidates.
    const assigned: Map<string, number> = new Map<string, number>();
    candidates.forEach((port: UsbSerialPort) => {
      const id: number | undefined = this.usbIds.get(port.key());
      if (id !== undefined && !used.has(id)) {
        used.add(id);
        assigned.set(port.key(), id);
      }
    });
    candidates.forEach((port: UsbSerialPort) => {
      const existing: number | undefined = assigned.get(port.key());
      const id: number = existing === undefined ? this.cdcId(port.key(), used) : existing;
      this.usbPorts.set(id, port);
      const item: SerialPortMetadata = new SerialPortMetadata({ portId: id, deviceName: port.device.name });
      item.hasUsbIdentity = port.device.vendorId > 0 && port.device.vendorId <= 65535 &&
        port.device.productId >= 0 && port.device.productId <= 65535;
      item.vendorId = port.device.vendorId;
      item.productId = port.device.productId;
      item.manufacturer = port.device.manufacturerName;
      item.productName = port.device.productName;
      metadata.push(item);
    });
    const present: Set<string> = new Set<string>(metadata.map((port: SerialPortMetadata) => `harmony-serial:${port.portId}`));
    devices.forEach((device: usbManager.USBDevice) => present.add(`usb:${device.name}`));
    this.deniedPorts.forEach((name: string) => {
      if (!present.has(name)) {
        this.deniedPorts.delete(name);
      }
    });
    this.discoveryState = `${discoveryState} portCount=${metadata.length} identityMatches=[${identityMatches.sort().join(';')}] ` +
      `cdcPortCount=${candidates.filter((port: UsbSerialPort) => port.kind === 'cdc').length} ` +
      `usbPortCount=${this.usbPorts.size} cdc=[${diagnostics.sort().join(';')}]`;
    this.logDiscovery(`${this.discoveryState} session=[${this.sessionState}] lastFailure=[${this.lastFailure}]`, usbError !== undefined ||
      (serialError !== undefined && metadata.length === 0));
    if (serialError !== undefined && metadata.length === 0) {
      throw new SerialBridgeError(serialError.code, serialError.message);
    }
    return metadata;
  }

  async open(sessionId: number, portName: string, baud: number, dataBits: number,
    parity: number, stopBits: number, autoConnect: boolean): Promise<void> {
    const ports: SerialPortMetadata[] = this.getPorts();
    const port: SerialPortMetadata | undefined = ports.find((candidate: SerialPortMetadata) =>
      `harmony-serial:${candidate.portId}` === portName);
    if (port === undefined) {
      throw new SerialBridgeError(31400003, 'USB serial port is no longer present');
    }
    if (autoConnect && (this.deniedPorts.has(portName) || this.deniedPorts.has(`usb:${port.deviceName}`))) {
      throw new SerialBridgeError(31400002, 'USB serial access was denied; reconnect manually to request access');
    }
    if (this.sessions.has(sessionId)) {
      throw new SerialBridgeError(31400004, 'USB serial session is already in use');
    }
    const usbPort: UsbSerialPort | undefined = this.usbPorts.get(port.portId);
    if (usbPort !== undefined && this.closingUsbPorts.has(usbPort.key())) {
      throw new SerialBridgeError(31400004, 'USB CDC port is still closing');
    }
    // One physical port cannot be shared by two SerialLink sessions.
    this.sessions.forEach((existing: SerialSession) => {
      if (existing.portId === port.portId || (existing.deviceName === port.deviceName &&
        ((existing.cdc === undefined) !== (usbPort === undefined) ||
        (existing.cdc !== undefined && usbPort !== undefined &&
          (existing.cdc.control.id === usbPort.control.id || existing.cdc.config.id !== usbPort.config.id))))) {
        throw new SerialBridgeError(31400004, 'USB serial port is already in use');
      }
    });
    this.closingUsbPorts.forEach((closing: SerialSession) => {
      if (closing.deviceName === port.deviceName && closing.cdc !== undefined &&
          (usbPort === undefined || closing.cdc.config.id !== usbPort.config.id ||
            closing.cdc.control.id === usbPort.control.id)) {
        throw new SerialBridgeError(31400004, 'USB configuration is still in use');
      }
    });
    const session: SerialSession = new SerialSession(port.portId, port.deviceName, usbPort);
    this.sessions.set(sessionId, session);
    try {
      if (session.cdc !== undefined) {
        await this.openUsb(sessionId, session, baud, dataBits, parity, stopBits);
        this.ensureActive(sessionId, session);
        this.deniedPorts.delete(portName);
        this.deniedPorts.delete(`usb:${port.deviceName}`);
        this.logSession(session, 'open');
        return;
      }
      if (!serialManager.hasSerialRight(port.portId)) {
        const granted: boolean = await serialManager.requestSerialRight(port.portId);
        if (this.sessions.get(sessionId) !== session) {
          throw new SerialBridgeError(31400007, 'USB serial connection was canceled');
        }
        if (!granted) {
          this.deniedPorts.add(portName);
          throw new SerialBridgeError(31400002, 'USB serial access was denied');
        }
      }
      if (this.sessions.get(sessionId) !== session) {
        throw new SerialBridgeError(31400007, 'USB serial connection was canceled');
      }
      serialManager.open(port.portId);
      session.opened = true;
      const attribute: serialManager.SerialAttribute = {
        baudRate: baud as serialManager.BaudRates,
        dataBits: dataBits as serialManager.DataBits,
        parity: parity as serialManager.Parity,
        stopBits: stopBits as serialManager.StopBits
      };
      serialManager.setAttribute(port.portId, attribute);
      this.deniedPorts.delete(portName);
      this.deniedPorts.delete(`usb:${port.deviceName}`);
      this.logSession(session, 'open');
    } catch (error) {
      if (this.sessions.get(sessionId) === session) {
        this.close(sessionId);
      }
      this.releaseUsb(session);
      const serialError: BusinessError = error as BusinessError;
      this.logSession(session, 'failed', serialError.code);
      throw new SerialBridgeError(serialError.code, serialError.message);
    }
  }

  private ensureActive(sessionId: number, session: SerialSession): void {
    if (session.closed || this.sessions.get(sessionId) !== session) {
      throw new SerialBridgeError(31400007, 'USB serial operation was canceled');
    }
  }

  private validateLine(baud: number, dataBits: number, parity: number, stopBits: number): void {
    if (!Number.isInteger(baud) || baud <= 0 || baud > 0xffffffff || !Number.isInteger(dataBits) ||
      dataBits < 5 || dataBits > 8 || !Number.isInteger(parity) || parity < 0 || parity > 4 ||
      (stopBits !== 0 && stopBits !== 1)) {
      throw new SerialBridgeError(401, 'Unsupported USB CDC line coding');
    }
  }

  private async cdcControl(sessionId: number, session: SerialSession, request: number,
    value: number, buffer: Uint8Array): Promise<void> {
    const cdc: UsbSerialPort | undefined = session.cdc;
    if (cdc === undefined) {
      throw new SerialBridgeError(31400005, 'USB CDC port is closed');
    }
    await this.usbControl(sessionId, session, 0x21, request, value, cdc.control.id, buffer);
  }

  private async usbControl(sessionId: number, session: SerialSession, requestType: number,
    request: number, value: number, index: number, buffer: Uint8Array, closing: boolean = false): Promise<number> {
    const pipe: usbManager.USBDevicePipe | undefined = session.pipe;
    if (!closing) { this.ensureActive(sessionId, session); }
    if (pipe === undefined) {
      throw new SerialBridgeError(31400005, 'USB serial pipe is closed');
    }
    const params: usbManager.USBDeviceRequestParams = {
      bmRequestType: requestType, bRequest: request, wValue: value, wIndex: index,
      wLength: buffer.length, data: buffer
    };
    session.controlPending++;
    try {
      const size: number = await usbManager.usbControlTransfer(pipe, params, 1000);
      if (!closing) { this.ensureActive(sessionId, session); }
      if (size !== buffer.length) {
        throw new SerialBridgeError(14400012, 'USB serial control transfer returned an invalid length');
      }
      return size;
    } finally {
      session.controlPending--;
      if (session.closed) {
        this.releaseUsb(session);
      }
    }
  }

  private driverControl(sessionId: number, session: SerialSession, closing: boolean = false): UsbSerialControl {
    return new SessionUsbControl((requestType: number, request: number, value: number, index: number,
      data: Uint8Array): Promise<number> => {
        if (!closing) {
          this.ensureActive(sessionId, session);
          session.lineStateSet = true;
        }
        return this.usbControl(sessionId, session, requestType, request, value, index, data, closing);
    });
  }

  private line(session: SerialSession, baud: number): UsbSerialLine {
    const line: UsbSerialLine = { baud: baud, dataBits: session.dataBits, parity: session.parity, stopBits: session.stopBits };
    return line;
  }

  private async setCdcLine(sessionId: number, session: SerialSession, baud: number): Promise<void> {
    this.validateLine(baud, session.dataBits, session.parity, session.stopBits);
    const buffer: Uint8Array = new Uint8Array([
      baud & 0xff, (baud >>> 8) & 0xff, (baud >>> 16) & 0xff, (baud >>> 24) & 0xff,
      session.stopBits === 1 ? 2 : 0, session.parity, session.dataBits
    ]);
    await this.cdcControl(sessionId, session, 0x20, 0, buffer);
    this.ensureActive(sessionId, session);
    session.baud = baud;
  }

  private async openUsb(sessionId: number, session: SerialSession,
    baud: number, dataBits: number, parity: number, stopBits: number): Promise<void> {
    const cdc: UsbSerialPort = session.cdc as UsbSerialPort;
    this.validateLine(baud, dataBits, parity, stopBits);
    session.dataBits = dataBits;
    session.parity = parity;
    session.stopBits = stopBits;
    if (!usbManager.hasRight(cdc.device.name)) {
      let pending: Promise<boolean> | undefined = this.pendingUsbRights.get(cdc.device.name);
      if (pending === undefined) {
        pending = usbManager.requestRight(cdc.device.name).finally(() => {
          if (this.pendingUsbRights.get(cdc.device.name) === pending) {
            this.pendingUsbRights.delete(cdc.device.name);
          }
        });
        this.pendingUsbRights.set(cdc.device.name, pending);
      }
      const granted: boolean = await pending;
      this.ensureActive(sessionId, session);
      if (!granted) {
        this.deniedPorts.add(`usb:${cdc.device.name}`);
        throw new SerialBridgeError(31400002, 'USB serial access was denied');
      }
    }
    this.ensureActive(sessionId, session);
    session.stage = 'connect';
    let connection: UsbDeviceConnection | undefined = this.usbConnections.get(session.deviceName);
    const sharedConfiguration: boolean = connection !== undefined;
    if (connection === undefined) {
      connection = new UsbDeviceConnection(usbManager.connectDevice(cdc.device), cdc.config.id);
      this.usbConnections.set(session.deviceName, connection);
    } else if (connection.configId !== cdc.config.id) {
      throw new SerialBridgeError(31400004, 'USB configuration is already in use');
    }
    connection.users.add(session);
    session.connection = connection;
    session.pipe = connection.pipe;
    if (cdc.device.configs.length > 1 && !sharedConfiguration) {
      session.stage = 'configuration';
      if (usbManager.setConfiguration(session.pipe, cdc.config) !== 0) {
        throw new SerialBridgeError(14400012, 'USB CDC configuration failed');
      }
    }
    session.stage = 'descriptors';
    if (cdc.kind === 'cdc' && cdc.data === undefined) {
      const raw: Uint8Array = usbManager.getRawDescriptor(session.pipe);
      if (raw === undefined || raw.length === 0) {
        throw new SerialBridgeError(401, 'USB CDC association descriptors are unavailable');
      }
      const pairs: CdcInterfacePair[] = parseCdcPairs(raw, cdc.config.id);
      const pair: CdcInterfacePair | undefined = pairs.find((candidate: CdcInterfacePair) => candidate.controlId === cdc.control.id);
      const data: usbManager.USBInterface[] = this.bulkInterfaces(cdc.config.interfaces, 10);
      const matching: usbManager.USBInterface[] = pair === undefined ? [] :
        data.filter((iface: usbManager.USBInterface) => iface.id === pair.dataId);
      if (matching.length !== 1) {
        throw new SerialBridgeError(401, 'USB CDC control/data association is unavailable or ambiguous');
      }
      this.assignData(cdc, matching[0]);
    } else if (cdc.kind !== 'cdc') {
      let version: number = 0;
      if (cdc.kind === 'ftdi') {
        const raw: Uint8Array = usbManager.getRawDescriptor(session.pipe);
        if (raw === undefined || raw.length < 18 || raw[0] < 18 || raw[1] !== 1) {
          throw new SerialBridgeError(401, 'FTDI device revision descriptor is unavailable');
        }
        version = raw[12] | (raw[13] << 8);
      }
      if (cdc.kind === 'cp210x') {
        cdc.driver = createCp210xDriver(cdc.control.id, version, cdc.device.productId);
      } else if (cdc.kind === 'ch34x') {
        cdc.driver = createCh34xDriver(cdc.control.id, version, cdc.device.productId);
      } else {
        cdc.driver = createFtdiDriver(cdc.control.id, version, cdc.device.productId);
      }
    }
    if (cdc.data === undefined || cdc.input === undefined || cdc.output === undefined) {
      throw new SerialBridgeError(401, 'USB serial data interface is unavailable');
    }
    session.stage = 'claim-control';
    if (usbManager.claimInterface(session.pipe, cdc.control, true) !== 0) {
      throw new SerialBridgeError(14400007, 'USB CDC control interface is busy');
    }
    session.controlClaimed = true;
    session.stage = 'claim-data';
    if (cdc.control.id !== cdc.data.id && usbManager.claimInterface(session.pipe, cdc.data, true) !== 0) {
      throw new SerialBridgeError(14400007, 'USB CDC data interface is busy');
    }
    session.dataClaimed = cdc.control.id !== cdc.data.id;
    session.stage = 'alternate';
    if ((cdc.control.alternateSetting !== 0 && usbManager.setInterface(session.pipe, cdc.control) !== 0) ||
      (cdc.control.id !== cdc.data.id && cdc.data.alternateSetting !== 0 &&
        usbManager.setInterface(session.pipe, cdc.data) !== 0)) {
      throw new SerialBridgeError(14400012, 'USB CDC alternate setting failed');
    }
    if (cdc.driver !== undefined) {
      session.stage = `initialize-${cdc.kind}`;
      await cdc.driver.initialize(this.driverControl(sessionId, session), this.line(session, baud));
      session.baud = baud;
    } else {
      session.stage = 'line-coding';
      await this.setCdcLine(sessionId, session, baud);
      this.ensureActive(sessionId, session);
      session.stage = 'line-state';
      // Also deassert if close races the SET_CONTROL_LINE_STATE completion.
      session.lineStateSet = true;
      await this.cdcControl(sessionId, session, 0x22, 3, new Uint8Array(0));
    }
    this.ensureActive(sessionId, session);
    session.stage = 'ready';
    session.opened = true;
  }

  private releaseUsb(session: SerialSession): void {
    const cdc: UsbSerialPort | undefined = session.cdc;
    const pipe: usbManager.USBDevicePipe | undefined = session.pipe;
    if (cdc === undefined || pipe === undefined || session.releasing || session.controlPending !== 0 ||
      session.readTransfer !== undefined || session.writeTransfer !== undefined) {
      return;
    }
    session.releasing = true;
    if (session.lineStateSet) {
      session.lineStateSet = false;
      if (cdc.driver !== undefined) {
        const driver: UsbSerialDriver = cdc.driver;
        Promise.resolve().then(() => driver.shutdown(this.driverControl(-1, session, true)))
          .catch(() => {}).finally(() => this.freeUsb(session));
        return;
      }
      const params: usbManager.USBDeviceRequestParams = {
        bmRequestType: 0x21, bRequest: 0x22, wValue: 0, wIndex: cdc.control.id,
        wLength: 0, data: new Uint8Array(0)
      };
      try {
        usbManager.usbControlTransfer(pipe, params, 1000).catch(() => {}).finally(() => this.freeUsb(session));
        return;
      } catch (_) {
        // Device removal may make the best-effort line-state request fail synchronously.
      }
    }
    this.freeUsb(session);
  }

  private freeUsb(session: SerialSession): void {
    const cdc: UsbSerialPort | undefined = session.cdc;
    const pipe: usbManager.USBDevicePipe | undefined = session.pipe;
    if (cdc === undefined || pipe === undefined) {
      return;
    }
    session.pipe = undefined;
    if (session.dataClaimed && cdc.data !== undefined) {
      session.dataClaimed = false;
      try { usbManager.releaseInterface(pipe, cdc.data); } catch (_) {}
    }
    if (session.controlClaimed) {
      session.controlClaimed = false;
      try { usbManager.releaseInterface(pipe, cdc.control); } catch (_) {}
    }
    const connection: UsbDeviceConnection | undefined = session.connection;
    session.connection = undefined;
    if (connection !== undefined) {
      connection.users.delete(session);
      if (connection.users.size === 0) {
        try { usbManager.closePipe(connection.pipe); } catch (_) {}
        if (this.usbConnections.get(session.deviceName) === connection) {
          this.usbConnections.delete(session.deviceName);
        }
      }
    }
    if (this.closingUsbPorts.get(cdc.key()) === session) {
      this.closingUsbPorts.delete(cdc.key());
    }
  }

  private usbTransfer(sessionId: number, session: SerialSession, buffer: Uint8Array,
    reading: boolean): Promise<number> {
    const cdc: UsbSerialPort = session.cdc as UsbSerialPort;
    const pipe: usbManager.USBDevicePipe = session.pipe as usbManager.USBDevicePipe;
    if ((reading ? session.readTransfer : session.writeTransfer) !== undefined) {
      return Promise.reject(new SerialBridgeError(14400007, 'USB CDC transfer is already pending'));
    }
    const endpoint: usbManager.USBEndpoint | undefined = reading ? cdc.input : cdc.output;
    if (endpoint === undefined) {
      return Promise.reject(new SerialBridgeError(401, 'USB serial endpoint is unavailable'));
    }
    return new Promise<number>((resolve, reject) => {
      const params: usbManager.UsbDataTransferParams = {
        devPipe: pipe, flags: 0, endpoint: endpoint.address,
        type: usbManager.UsbEndpointTransferType.TRANSFER_TYPE_BULK,
        timeout: reading ? 100 : 1000, length: buffer.length, userData: new Uint8Array(0),
        buffer: buffer, isoPacketCount: 0,
        callback: (error: BusinessError, result: usbManager.SubmitTransferCallback): void => {
          if (reading) { session.readTransfer = undefined; } else { session.writeTransfer = undefined; }
          try {
            this.ensureActive(sessionId, session);
            if (error !== undefined && error !== null && error.code !== 0) {
              throw new SerialBridgeError(error.code, 'USB CDC transfer failed');
            }
            if (!Number.isInteger(result.actualLength) || result.actualLength < 0 || result.actualLength > buffer.length) {
              throw new SerialBridgeError(14400012, 'USB CDC transfer returned an invalid length');
            }
            if (result.status === usbManager.UsbTransferStatus.TRANSFER_TIMED_OUT && (reading || result.actualLength > 0)) {
              resolve(result.actualLength);
              return;
            }
            if (result.status !== usbManager.UsbTransferStatus.TRANSFER_COMPLETED) {
              throw new SerialBridgeError(result.status === usbManager.UsbTransferStatus.TRANSFER_NO_DEVICE ?
                14400008 : 14400012, `USB CDC transfer failed (status=${result.status})`);
            }
            resolve(result.actualLength);
          } catch (failure) {
            const usbError: BusinessError = failure as BusinessError;
            reject(new SerialBridgeError(usbError.code, usbError.message));
          } finally {
            if (session.closed) { this.releaseUsb(session); }
          }
        }
      };
      const pending: UsbPendingTransfer = new UsbPendingTransfer(params, reject);
      if (reading) { session.readTransfer = pending; } else { session.writeTransfer = pending; }
      try {
        usbManager.usbSubmitTransfer(params);
      } catch (failure) {
        if (reading) { session.readTransfer = undefined; } else { session.writeTransfer = undefined; }
        const usbError: BusinessError = failure as BusinessError;
        reject(new SerialBridgeError(usbError.code, usbError.message));
      }
    });
  }

  private requireSession(sessionId: number): SerialSession {
    const session: SerialSession | undefined = this.sessions.get(sessionId);
    if (session === undefined || session.closed || !session.opened) {
      throw new SerialBridgeError(31400005, 'USB serial port is closed');
    }
    return session;
  }

  async read(sessionId: number): Promise<Uint8Array> {
    const session: SerialSession = this.requireSession(sessionId);
    const buffer: Uint8Array = new Uint8Array(8192);
    try {
      const size: number = session.cdc === undefined ? await serialManager.read(session.portId, buffer, 100) :
        await this.usbTransfer(sessionId, session, buffer, true);
      if (size < 0 || size > buffer.length) {
        throw new SerialBridgeError(31400007, 'USB serial read returned an invalid length');
      }
      if (this.sessions.get(sessionId) !== session) {
        throw new SerialBridgeError(31400007, 'USB serial read was canceled');
      }
      const received: Uint8Array = buffer.slice(0, size);
      if (session.cdc !== undefined && session.cdc.driver !== undefined && session.cdc.input !== undefined) {
        return session.cdc.driver.decodeRead(received, session.cdc.input.maxPacketSize);
      }
      return received;
    } catch (error) {
      const serialError: BusinessError = error as BusinessError;
      if (serialError.code === 31400006 && this.sessions.get(sessionId) === session) {
        return new Uint8Array(0);
      }
      if (!session.closed) {
        session.stage = 'read';
        this.logSession(session, 'failed', serialError.code);
      }
      throw new SerialBridgeError(serialError.code, serialError.message);
    }
  }

  async write(sessionId: number, buffer: Uint8Array): Promise<number> {
    const session: SerialSession = this.requireSession(sessionId);
    if (buffer.length === 0 || buffer.length > 4096) {
      throw new SerialBridgeError(401, 'USB serial write length must be between 1 and 4096 bytes');
    }
    let size: number;
    try {
      size = session.cdc === undefined ? await serialManager.write(session.portId, buffer, 100) :
        await this.usbTransfer(sessionId, session, buffer.slice(), false);
    } catch (failure) {
      const error: BusinessError = failure as BusinessError;
      if (!session.closed) {
        session.stage = 'write';
        this.logSession(session, 'failed', error.code);
      }
      throw new SerialBridgeError(error.code, error.message);
    }
    if (this.sessions.get(sessionId) !== session) {
      throw new SerialBridgeError(31400007, 'USB serial write was canceled');
    }
    return size;
  }

  async setBaud(sessionId: number, baud: number): Promise<void> {
    const session: SerialSession = this.requireSession(sessionId);
    if (session.cdc !== undefined) {
      session.stage = 'set-baud';
      try {
        if (session.cdc.driver !== undefined) {
          this.validateLine(baud, session.dataBits, session.parity, session.stopBits);
          await session.cdc.driver.configure(this.driverControl(sessionId, session), this.line(session, baud));
          session.baud = baud;
        } else {
          await this.setCdcLine(sessionId, session, baud);
        }
        this.ensureActive(sessionId, session);
        session.stage = 'ready';
      } catch (failure) {
        const error: BusinessError = failure as BusinessError;
        this.logSession(session, 'failed', error.code);
        throw new SerialBridgeError(error.code, error.message);
      }
      return;
    }
    const current: serialManager.SerialAttribute = serialManager.getAttribute(session.portId);
    const attribute: serialManager.SerialAttribute = {
      baudRate: baud as serialManager.BaudRates,
      dataBits: current.dataBits,
      parity: current.parity,
      stopBits: current.stopBits
    };
    serialManager.setAttribute(session.portId, attribute);
    if (this.sessions.get(sessionId) !== session) {
      throw new SerialBridgeError(31400007, 'USB serial configuration was canceled');
    }
  }

  close(sessionId: number): void {
    const session: SerialSession | undefined = this.sessions.get(sessionId);
    this.sessions.delete(sessionId);
    const wasOpened: boolean = session !== undefined && session.opened;
    if (session !== undefined) {
      session.closed = true;
      session.opened = false;
      if (session.cdc !== undefined) {
        if (session.pipe !== undefined) {
          this.closingUsbPorts.set(session.cdc.key(), session);
        }
        [session.readTransfer, session.writeTransfer].forEach((pending: UsbPendingTransfer | undefined) => {
          if (pending !== undefined) {
            try { usbManager.usbCancelTransfer(pending.params); } catch (_) {}
            pending.reject(new SerialBridgeError(31400007, 'USB serial operation was canceled'));
          }
        });
        this.releaseUsb(session);
      }
      this.logSession(session, 'closed');
    }
    if (session !== undefined && session.cdc === undefined && wasOpened) {
      try {
        serialManager.close(session.portId);
      } catch (error) {
        const serialError: BusinessError = error as BusinessError;
        if (serialError.code !== 31400003 && serialError.code !== 31400005) {
          console.error(`QGC USB serial close failed (${serialError.code}): ${serialError.message}`);
        }
      }
    }
  }
}

const bridge: QGCSerialBridge = new QGCSerialBridge();

export function getQGCSerialBridge(): object {
  return bridge;
}
