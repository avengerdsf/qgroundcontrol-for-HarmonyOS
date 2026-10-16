import { abilityAccessCtrl, common, PermissionRequestResult } from '@kit.AbilityKit';
import { BusinessError } from '@kit.BasicServicesKit';
import { access, ble, constant } from '@kit.ConnectivityKit';

export interface BleSerialOptions {
  serviceUuid: string;
  writeUuid: string;
  notifyUuid: string;
}

interface BleDevice { address: string; name: string; }
interface BleSnapshot { devices: BleDevice[]; scanning: boolean; }

class BleBridgeError extends Error implements BusinessError {
  code: number;
  constructor(code: number, message: string) { super(message); this.code = code; }
}

class BleTimeoutError extends BleBridgeError {
  constructor(stage: string) { super(2900099, `BLE ${stage} timed out`); }
}

class BlePending {
  timer: number = -1;
  done: boolean = false;
  reject: (error: BleBridgeError) => void;
  constructor(reject: (error: BleBridgeError) => void) { this.reject = reject; }
}

class BleOwner {
  closed: boolean = false;
  error: BleBridgeError = new BleBridgeError(2901003, 'BLE session is closed');
  pending: Set<BlePending> = new Set<BlePending>();
  cleanup: () => void = (): void => {};
  powerListening: boolean = false;
  powerCallback: (state: access.BluetoothState) => void = (_state: access.BluetoothState): void => {};
}

class BleDiscovery extends BleOwner {
  scanner: ble.BleScanner | undefined;
  scanning: boolean = false;
  finished: boolean = false;
  stopRequested: boolean = false;
  finishTimer: number = -1;
  known: Map<string, string> = new Map<string, string>();
  updates: Map<string, BleDevice> = new Map<string, BleDevice>();
  callback: (report: ble.ScanReport) => void = (_report: ble.ScanReport): void => {};
}

class BleSession extends BleOwner {
  address: string;
  options: BleSerialOptions;
  client: ble.GattClientDevice | undefined;
  connected: boolean = false;
  opened: boolean = false;
  mtu: number = 23;
  mtuListening: boolean = false;
  writeCharacteristic: ble.BLECharacteristic | undefined;
  notifyCharacteristic: ble.BLECharacteristic | undefined;
  writeType: ble.GattWriteType = ble.GattWriteType.WRITE;
  indicate: boolean = false;
  chunks: Uint8Array[] = [];
  queuedBytes: number = 0;
  reading: boolean = false;
  writeTail: Promise<void> = Promise.resolve();
  connectedReady: (() => void) | undefined;
  mtuReady: (() => void) | undefined;
  readReady: (() => void) | undefined;
  connectionCallback: (state: ble.BLEConnectionChangeState) => void = (_state: ble.BLEConnectionChangeState): void => {};
  notificationCallback: (value: ble.BLECharacteristic) => void = (_value: ble.BLECharacteristic): void => {};
  mtuCallback: (mtu: number) => void = (_mtu: number): void => {};
  serviceCallback: () => void = (): void => {};

  constructor(address: string, options: BleSerialOptions) { super(); this.address = address; this.options = options; }
}

class QGCBleBridge {
  private sessions: Map<number, BleSession> = new Map<number, BleSession>();
  private discoveries: Map<number, BleDiscovery> = new Map<number, BleDiscovery>();

  isSupported(): boolean { return canIUse('SystemCapability.Communication.Bluetooth.Core'); }

  private error(caught: BusinessError, fallback: string): BleBridgeError {
    if (caught instanceof BleBridgeError) { return caught; }
    if (caught === undefined || caught === null) { return new BleBridgeError(2900099, fallback); }
    return new BleBridgeError(typeof caught.code === 'number' ? caught.code : 2900099, caught.message || fallback);
  }

  private ensure(owner: BleOwner): void { if (owner.closed) { throw owner.error; } }

  private powered(): boolean {
    try {
      const state: access.BluetoothState = access.getState();
      return state === access.BluetoothState.STATE_ON || state === access.BluetoothState.STATE_BLE_ON;
    } catch (caught) { throw this.error(caught as BusinessError, 'BLE power state is unavailable'); }
  }

  private operation<T>(owner: BleOwner, start: () => Promise<T>, timeout: number, stage: string): Promise<T> {
    this.ensure(owner);
    return new Promise<T>((resolve, reject) => {
      const pending: BlePending = new BlePending((error: BleBridgeError): void => {
        if (pending.done) { return; }
        pending.done = true;
        if (pending.timer >= 0) { clearTimeout(pending.timer); }
        owner.pending.delete(pending);
        reject(error);
      });
      owner.pending.add(pending);
      if (timeout > 0) { pending.timer = setTimeout((): void => pending.reject(new BleTimeoutError(stage)), timeout); }
      try {
        start().then((result: T): void => {
          if (pending.done) { return; }
          if (owner.closed) { pending.reject(owner.error); return; }
          pending.done = true;
          if (pending.timer >= 0) { clearTimeout(pending.timer); }
          owner.pending.delete(pending);
          resolve(result);
        }, (caught: BusinessError): void => pending.reject(this.error(caught, `BLE ${stage} failed`)));
      } catch (caught) { pending.reject(this.error(caught as BusinessError, `BLE ${stage} failed`)); }
    });
  }

  private terminate(owner: BleOwner, error: BleBridgeError): void {
    if (owner.closed) { return; }
    owner.closed = true;
    owner.error = error;
    owner.pending.forEach((pending: BlePending) => pending.reject(error));
    owner.cleanup();
    if (owner.powerListening) {
      owner.powerListening = false;
      try { access.off('stateChange', owner.powerCallback); } catch (_) {}
    }
  }

  private async authorize(owner: BleOwner, context: common.UIAbilityContext): Promise<void> {
    if (!this.isSupported()) { throw new BleBridgeError(801, 'BLE capability is unavailable'); }
    const manager: abilityAccessCtrl.AtManager = abilityAccessCtrl.createAtManager();
    const result: PermissionRequestResult = await this.operation(owner,
      (): Promise<PermissionRequestResult> => manager.requestPermissionsFromUser(context, ['ohos.permission.ACCESS_BLUETOOTH']), 0, 'permission');
    this.ensure(owner);
    if (result.authResults.length !== 1 || result.authResults[0] !== abilityAccessCtrl.GrantStatus.PERMISSION_GRANTED) {
      throw new BleBridgeError(201, 'Bluetooth access was denied');
    }
    if (!this.powered()) { throw new BleBridgeError(2900003, 'Bluetooth is disabled'); }
    owner.powerCallback = (state: access.BluetoothState): void => {
      if (state !== access.BluetoothState.STATE_ON && state !== access.BluetoothState.STATE_BLE_ON) {
        this.terminate(owner, new BleBridgeError(2900003, 'Bluetooth was disabled'));
      }
    };
    owner.powerListening = true;
    try { access.on('stateChange', owner.powerCallback); }
    catch (caught) { throw this.error(caught as BusinessError, 'BLE power listener failed'); }
  }

  private stopScanner(discovery: BleDiscovery, force: boolean = false): void {
    discovery.scanning = false;
    if (discovery.finishTimer >= 0) { clearTimeout(discovery.finishTimer); discovery.finishTimer = -1; }
    const scanner: ble.BleScanner | undefined = discovery.scanner;
    if (scanner === undefined || (discovery.stopRequested && !force)) { return; }
    discovery.stopRequested = true;
    try { scanner.off('BLEDeviceFind', discovery.callback); } catch (_) {}
    try { scanner.stopScan().catch((): void => {}); } catch (_) {}
  }

  private finishScan(discovery: BleDiscovery): void {
    if (discovery.closed || discovery.finished) { return; }
    discovery.finished = true;
    this.stopScanner(discovery);
    discovery.known.clear();
    if (discovery.powerListening) {
      discovery.powerListening = false;
      try { access.off('stateChange', discovery.powerCallback); } catch (_) {}
    }
  }

  async startScan(sessionId: number, context: common.UIAbilityContext): Promise<BleSnapshot> {
    this.stopScan(sessionId);
    const discovery: BleDiscovery = new BleDiscovery();
    discovery.cleanup = (): void => {
      this.stopScanner(discovery);
      discovery.known.clear();
      discovery.updates.clear();
    };
    this.discoveries.set(sessionId, discovery);
    try {
      await this.authorize(discovery, context);
      this.ensure(discovery);
      const scanner: ble.BleScanner = ble.createBleScanner();
      discovery.scanner = scanner;
      discovery.callback = (report: ble.ScanReport): void => {
        if (discovery.closed || discovery.finished || this.discoveries.get(sessionId) !== discovery || report.reportType === ble.ScanReportType.ON_LOST) { return; }
        report.scanResult.forEach((result: ble.ScanResult) => {
          if (discovery.closed || discovery.finished || !result.connectable || result.deviceId.length === 0) { return; }
          const address: string = result.deviceId.toUpperCase();
          if (!discovery.known.has(address) && discovery.known.size >= 512) { this.finishScan(discovery); return; }
          const name: string = result.deviceName.length > 0 ? result.deviceName : (discovery.known.get(address) ?? address);
          if (discovery.known.get(address) !== name) {
            discovery.known.set(address, name);
            discovery.updates.set(address, { address: address, name: name });
          }
        });
      };
      scanner.on('BLEDeviceFind', discovery.callback);
      await this.operation(discovery, (): Promise<void> => {
        const started: Promise<void> = scanner.startScan([]);
        started.then((): void => { if (discovery.closed || discovery.finished) { this.stopScanner(discovery, true); } }, (): void => {});
        return started;
      }, 8000, 'scan start');
      this.ensure(discovery);
      discovery.scanning = !discovery.finished;
      if (discovery.scanning) { discovery.finishTimer = setTimeout((): void => this.finishScan(discovery), 30000); }
      return this.scan(sessionId);
    } catch (caught) {
      const error: BleBridgeError = this.error(caught as BusinessError, 'BLE scan failed');
      this.terminate(discovery, error);
      if (this.discoveries.get(sessionId) === discovery) { this.discoveries.delete(sessionId); }
      throw error;
    }
  }

  scan(sessionId: number): BleSnapshot {
    const discovery: BleDiscovery | undefined = this.discoveries.get(sessionId);
    if (discovery === undefined) { throw new BleBridgeError(2900099, 'BLE scan is stopped'); }
    this.ensure(discovery);
    const devices: BleDevice[] = [];
    discovery.updates.forEach((device: BleDevice) => devices.push(device));
    discovery.updates.clear();
    devices.sort((left: BleDevice, right: BleDevice) => left.address.localeCompare(right.address));
    const snapshot: BleSnapshot = { devices: devices, scanning: discovery.scanning };
    if (discovery.finished) {
      this.discoveries.delete(sessionId);
      this.terminate(discovery, new BleBridgeError(2900099, 'BLE scan finished'));
    }
    return snapshot;
  }

  stopScan(sessionId: number): void {
    const discovery: BleDiscovery | undefined = this.discoveries.get(sessionId);
    this.discoveries.delete(sessionId);
    if (discovery !== undefined) { this.terminate(discovery, new BleBridgeError(2900099, 'BLE scan was canceled')); }
  }

  private uuid(value: string): string {
    if (value === undefined || value === null) { throw new BleBridgeError(401, 'BLE UUID is required'); }
    const normalized: string = value.trim().toLowerCase();
    if (/^[0-9a-f]{4}$/.test(normalized)) { return `0000${normalized}-0000-1000-8000-00805f9b34fb`; }
    if (/^[0-9a-f]{8}$/.test(normalized)) { return `${normalized}-0000-1000-8000-00805f9b34fb`; }
    if (!/^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$/.test(normalized)) { throw new BleBridgeError(401, 'Invalid BLE UUID'); }
    return normalized;
  }

  private matches(value: string, expected: string): boolean { try { return this.uuid(value) === expected; } catch (_) { return false; } }

  private current(sessionId: number, session: BleSession, client: ble.GattClientDevice): boolean {
    return !session.closed && this.sessions.get(sessionId) === session && session.client === client;
  }

  private cleanupClient(session: BleSession): void {
    session.opened = false;
    session.connected = false;
    session.chunks = [];
    session.queuedBytes = 0;
    const client: ble.GattClientDevice | undefined = session.client;
    session.client = undefined;
    if (client === undefined) { return; }
    try { client.off('BLEConnectionStateChange', session.connectionCallback); } catch (_) {}
    try { client.off('BLECharacteristicChange', session.notificationCallback); } catch (_) {}
    try { client.off('BLEMtuChange', session.mtuCallback); } catch (_) {}
    try { client.off('serviceChange', session.serviceCallback); } catch (_) {}
    try { client.disconnect(); } catch (_) {}
    try { client.close(); } catch (_) {}
  }

  private receive(session: BleSession, value: ble.BLECharacteristic): void {
    if (!this.matches(value.serviceUuid, session.options.serviceUuid) || !this.matches(value.characteristicUuid, session.options.notifyUuid)) { return; }
    const length: number = value.characteristicValue.byteLength;
    if (length === 0) { return; }
    if (session.queuedBytes + length > 65536) { this.terminate(session, new BleBridgeError(2900099, 'BLE receive buffer overflow')); return; }
    session.chunks.push(new Uint8Array(value.characteristicValue).slice());
    session.queuedBytes += length;
    if (session.readReady !== undefined) { session.readReady(); }
  }

  private bind(sessionId: number, session: BleSession, client: ble.GattClientDevice): void {
    session.connectionCallback = (event: ble.BLEConnectionChangeState): void => {
      if (!this.current(sessionId, session, client) || event.deviceId.toUpperCase() !== session.address) { return; }
      if (event.state === constant.ProfileConnectionState.STATE_CONNECTED) {
        session.connected = true;
        if (session.connectedReady !== undefined) { session.connectedReady(); }
      } else if (event.state === constant.ProfileConnectionState.STATE_DISCONNECTED || event.state === constant.ProfileConnectionState.STATE_DISCONNECTING) {
        this.terminate(session, new BleBridgeError(2901003, 'BLE device disconnected'));
      }
    };
    session.notificationCallback = (value: ble.BLECharacteristic): void => {
      if (this.current(sessionId, session, client) && session.notifyCharacteristic !== undefined) { this.receive(session, value); }
    };
    session.mtuCallback = (mtu: number): void => {
      if (this.current(sessionId, session, client) && Number.isInteger(mtu) && mtu >= 23 && mtu <= 517) {
        session.mtu = mtu;
        if (session.mtuReady !== undefined) { session.mtuReady(); }
      }
    };
    session.serviceCallback = (): void => {
      if (this.current(sessionId, session, client)) { this.terminate(session, new BleBridgeError(2900099, 'BLE services changed; reconnect to discover them again')); }
    };
    try {
      client.on('BLEConnectionStateChange', session.connectionCallback);
      client.on('BLECharacteristicChange', session.notificationCallback);
    } catch (caught) { throw this.error(caught as BusinessError, 'BLE connection listeners failed'); }
    try { client.on('BLEMtuChange', session.mtuCallback); session.mtuListening = true; } catch (_) {}
    try { client.on('serviceChange', session.serviceCallback); } catch (_) {}
  }

  private select(session: BleSession, services: ble.GattService[]): void {
    const matching: ble.GattService[] = services.filter((service: ble.GattService) => this.matches(service.serviceUuid, session.options.serviceUuid));
    if (matching.length !== 1) { throw new BleBridgeError(401, 'BLE service UUID is absent or ambiguous'); }
    const writes: ble.BLECharacteristic[] = matching[0].characteristics.filter((characteristic: ble.BLECharacteristic) =>
      this.matches(characteristic.serviceUuid, session.options.serviceUuid) && this.matches(characteristic.characteristicUuid, session.options.writeUuid));
    const notifications: ble.BLECharacteristic[] = matching[0].characteristics.filter((characteristic: ble.BLECharacteristic) =>
      this.matches(characteristic.serviceUuid, session.options.serviceUuid) && this.matches(characteristic.characteristicUuid, session.options.notifyUuid));
    if (writes.length !== 1 || notifications.length !== 1) { throw new BleBridgeError(401, 'BLE write or notify UUID is absent or ambiguous'); }
    const writable: ble.GattProperties | undefined = writes[0].properties;
    const receivable: ble.GattProperties | undefined = notifications[0].properties;
    if (writable === undefined || (!writable.write && !writable.writeNoResponse)) { throw new BleBridgeError(401, 'BLE characteristic does not support write'); }
    if (receivable === undefined || (!receivable.notify && !receivable.indicate)) { throw new BleBridgeError(401, 'BLE characteristic does not support notify or indicate'); }
    if (!notifications[0].descriptors.some((descriptor: ble.BLEDescriptor) =>
      this.matches(descriptor.descriptorUuid, '00002902-0000-1000-8000-00805f9b34fb') &&
      this.matches(descriptor.serviceUuid, session.options.serviceUuid) && this.matches(descriptor.characteristicUuid, session.options.notifyUuid))) {
      throw new BleBridgeError(401, 'BLE notification characteristic has no matching CCCD');
    }
    session.writeCharacteristic = writes[0];
    session.notifyCharacteristic = notifications[0];
    session.writeType = writable.write ? ble.GattWriteType.WRITE : ble.GattWriteType.WRITE_NO_RESPONSE;
    session.indicate = !receivable.notify;
  }

  private async negotiateMtu(session: BleSession, client: ble.GattClientDevice): Promise<void> {
    if (!session.mtuListening) { return; }
    try {
      await this.operation<void>(session, (): Promise<void> => new Promise<void>((resolve) => {
        session.mtuReady = resolve;
        client.setBLEMtuSize(247);
      }), 300, 'MTU negotiation');
    } catch (caught) {
      this.ensure(session);
      const error: BleBridgeError = this.error(caught as BusinessError, 'BLE MTU negotiation failed');
      if (error.code === 201 || error.code === 2900003) { throw error; }
    } finally { session.mtuReady = undefined; }
  }

  async open(sessionId: number, address: string, context: common.UIAbilityContext, options: BleSerialOptions): Promise<void> {
    if (address === undefined || !/^([0-9a-f]{2}:){5}[0-9a-f]{2}$/i.test(address.trim()) || options === undefined || options === null) {
      throw new BleBridgeError(401, 'BLE address and serial UUIDs are required');
    }
    const normalized: BleSerialOptions = { serviceUuid: this.uuid(options.serviceUuid), writeUuid: this.uuid(options.writeUuid), notifyUuid: this.uuid(options.notifyUuid) };
    const deviceAddress: string = address.trim().toUpperCase();
    this.sessions.forEach((other: BleSession, id: number) => {
      if (id !== sessionId && !other.closed && other.address === deviceAddress) { throw new BleBridgeError(2900011, 'BLE device is already in use'); }
    });
    this.close(sessionId);
    const session: BleSession = new BleSession(deviceAddress, normalized);
    session.cleanup = (): void => this.cleanupClient(session);
    this.sessions.set(sessionId, session);
    try {
      await this.authorize(session, context);
      this.ensure(session);
      const client: ble.GattClientDevice = ble.createGattClientDevice(deviceAddress);
      session.client = client;
      const connected: Promise<void> = new Promise<void>((resolve) => { session.connectedReady = resolve; });
      this.bind(sessionId, session, client);
      await this.operation(session, (): Promise<void> => { if (!session.connected) { client.connect(); } return connected; }, 10000, 'connection');
      const services: ble.GattService[] = await this.operation(session, (): Promise<ble.GattService[]> => client.getServices(), 8000, 'service discovery');
      this.ensure(session);
      this.select(session, services);
      const notify: ble.BLECharacteristic = session.notifyCharacteristic as ble.BLECharacteristic;
      await this.operation(session, (): Promise<void> => session.indicate ? client.setCharacteristicChangeIndication(notify, true) :
        client.setCharacteristicChangeNotification(notify, true), 8000, 'subscription');
      await this.negotiateMtu(session, client);
      this.ensure(session);
      session.opened = true;
    } catch (caught) {
      const error: BleBridgeError = this.error(caught as BusinessError, 'BLE connection failed');
      this.terminate(session, error);
      if (this.sessions.get(sessionId) === session) { this.sessions.delete(sessionId); }
      throw error;
    } finally { session.connectedReady = undefined; }
  }

  private requireSession(sessionId: number): BleSession {
    const session: BleSession | undefined = this.sessions.get(sessionId);
    if (session === undefined) { throw new BleBridgeError(2901003, 'BLE session is closed'); }
    this.ensure(session);
    if (!session.opened) { throw new BleBridgeError(2901003, 'BLE session is not ready'); }
    return session;
  }

  private consume(session: BleSession): Uint8Array {
    const output: Uint8Array = new Uint8Array(Math.min(8192, session.queuedBytes));
    let position: number = 0;
    while (position < output.length) {
      const first: Uint8Array = session.chunks[0];
      const amount: number = Math.min(first.length, output.length - position);
      output.set(first.subarray(0, amount), position);
      position += amount;
      if (amount === first.length) { session.chunks.shift(); } else { session.chunks[0] = first.subarray(amount); }
    }
    session.queuedBytes -= output.length;
    return output;
  }

  async read(sessionId: number): Promise<Uint8Array> {
    const session: BleSession = this.requireSession(sessionId);
    if (session.reading) { throw new BleBridgeError(2900011, 'BLE read is already pending'); }
    if (session.queuedBytes > 0) { return this.consume(session); }
    session.reading = true;
    try {
      await this.operation<void>(session, (): Promise<void> => new Promise<void>((resolve) => { session.readReady = resolve; }), 200, 'read');
    } catch (caught) {
      this.ensure(session);
      if (!(caught instanceof BleTimeoutError)) { throw this.error(caught as BusinessError, 'BLE read failed'); }
    } finally { session.reading = false; session.readReady = undefined; }
    this.ensure(session);
    return this.consume(session);
  }

  async write(sessionId: number, data: Uint8Array): Promise<number> {
    const session: BleSession = this.requireSession(sessionId);
    if (data.length === 0 || data.length > 4096) { throw new BleBridgeError(401, 'BLE write length must be between 1 and 4096 bytes'); }
    const bytes: Uint8Array = data.slice();
    try {
      return await this.operation<number>(session, (): Promise<number> => {
        const task: Promise<number> = session.writeTail.then(async (): Promise<number> => {
          this.ensure(session);
          const client: ble.GattClientDevice = session.client as ble.GattClientDevice;
          const characteristic: ble.BLECharacteristic = session.writeCharacteristic as ble.BLECharacteristic;
          for (let offset: number = 0; offset < bytes.length;) {
            const amount: number = Math.min(session.mtu - 3, 512, bytes.length - offset);
            const value: ble.BLECharacteristic = {
              serviceUuid: characteristic.serviceUuid, characteristicUuid: characteristic.characteristicUuid,
              characteristicValue: bytes.slice(offset, offset + amount).buffer, descriptors: characteristic.descriptors,
              properties: characteristic.properties, characteristicValueHandle: characteristic.characteristicValueHandle
            };
            await this.operation(session, (): Promise<void> => client.writeCharacteristicValue(value, session.writeType), 5000, 'characteristic write');
            offset += amount;
          }
          this.ensure(session);
          return bytes.length;
        });
        session.writeTail = task.then((): void => {}, (): void => {});
        return task;
      }, 30000, 'write');
    } catch (caught) {
      const error: BleBridgeError = this.error(caught as BusinessError, 'BLE write failed');
      this.terminate(session, error);
      throw error;
    }
  }

  close(sessionId: number): void {
    const session: BleSession | undefined = this.sessions.get(sessionId);
    this.sessions.delete(sessionId);
    if (session !== undefined) { this.terminate(session, new BleBridgeError(2901003, 'BLE connection was closed')); }
  }
}

const bridge: QGCBleBridge = new QGCBleBridge();
export function getQGCBleBridge(): object { return bridge; }
