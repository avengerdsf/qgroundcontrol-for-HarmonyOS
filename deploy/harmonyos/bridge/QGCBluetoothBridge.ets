import { abilityAccessCtrl, common, PermissionRequestResult } from '@kit.AbilityKit';
import { BusinessError } from '@kit.BasicServicesKit';
import { access, connection, socket } from '@kit.ConnectivityKit';

class BluetoothBridgeError extends Error implements BusinessError {
  code: number;

  constructor(code: number, message: string) {
    super(message);
    this.code = code;
  }
}

interface BluetoothDevice {
  address: string;
  name: string;
}

interface DiscoverySnapshot {
  devices: BluetoothDevice[];
  scanning: boolean;
}

class DiscoverySession {
  devices: Map<string, BluetoothDevice> = new Map<string, BluetoothDevice>();
  listening: boolean = false;
  callback: (addresses: string[]) => void;

  constructor(callback: (addresses: string[]) => void) {
    this.callback = callback;
  }
}

class BluetoothSession {
  socketId: number = -1;
  reading: boolean = false;
  pending: Set<(error: BluetoothBridgeError) => void> = new Set<(error: BluetoothBridgeError) => void>();
}

class QGCBluetoothBridge {
  private sessions: Map<number, BluetoothSession> = new Map<number, BluetoothSession>();
  private discoveries: Map<number, DiscoverySession> = new Map<number, DiscoverySession>();
  private discoveryOwned: boolean = false;

  isSupported(): boolean {
    return canIUse('SystemCapability.Communication.Bluetooth.Core') &&
      typeof socket.sppConnect === 'function' && typeof socket.sppReadAsync === 'function' &&
      typeof socket.sppWriteAsync === 'function' && typeof socket.sppCloseClientSocket === 'function';
  }

  private async authorize(context: common.UIAbilityContext): Promise<void> {
    if (!this.isSupported()) {
      throw new BluetoothBridgeError(801, 'Bluetooth capability is unavailable on this device');
    }
    const manager: abilityAccessCtrl.AtManager = abilityAccessCtrl.createAtManager();
    const result: PermissionRequestResult =
      await manager.requestPermissionsFromUser(context, ['ohos.permission.ACCESS_BLUETOOTH']);
    if (result.authResults.length !== 1 || result.authResults[0] !== abilityAccessCtrl.GrantStatus.PERMISSION_GRANTED) {
      throw new BluetoothBridgeError(201, 'Bluetooth access was denied');
    }
    if (access.getState() !== access.BluetoothState.STATE_ON) {
      throw new BluetoothBridgeError(2900003, 'Bluetooth is disabled; enable it in system settings');
    }
  }

  async startScan(sessionId: number, context: common.UIAbilityContext): Promise<DiscoverySnapshot> {
    if (this.discoveries.has(sessionId)) {
      throw new BluetoothBridgeError(2900099, 'Bluetooth discovery session is already in use');
    }
    const discovery: DiscoverySession = new DiscoverySession((addresses: string[]) => {
      if (this.discoveries.get(sessionId) !== discovery) {
        return;
      }
      addresses.forEach((address: string) => {
        let name: string = address;
        try {
          name = connection.getRemoteDeviceName(address);
        } catch (error) {
          const bluetoothError: BusinessError = error as BusinessError;
          console.warn(`QGC Bluetooth name lookup failed (${bluetoothError.code}): ${bluetoothError.message}`);
        }
        discovery.devices.set(address, { address: address, name: name.length > 0 ? name : address });
      });
    });
    this.discoveries.set(sessionId, discovery);
    try {
      await this.authorize(context);
      if (this.discoveries.get(sessionId) !== discovery) {
        throw new BluetoothBridgeError(2900099, 'Bluetooth discovery was canceled');
      }
      connection.on('bluetoothDeviceFind', discovery.callback);
      discovery.listening = true;
      discovery.callback(connection.getPairedDevices());
      if (!connection.isBluetoothDiscovering()) {
        connection.startBluetoothDiscovery();
        this.discoveryOwned = true;
      }
      return this.scan(sessionId);
    } catch (error) {
      if (this.discoveries.get(sessionId) === discovery) {
        this.stopScan(sessionId);
      }
      const bluetoothError: BusinessError = error as BusinessError;
      throw new BluetoothBridgeError(bluetoothError.code, bluetoothError.message);
    }
  }

  scan(sessionId: number): DiscoverySnapshot {
    const discovery: DiscoverySession | undefined = this.discoveries.get(sessionId);
    if (discovery === undefined || !discovery.listening) {
      throw new BluetoothBridgeError(2900099, 'Bluetooth discovery is stopped');
    }
    try {
      const devices: BluetoothDevice[] = [];
      discovery.devices.forEach((device: BluetoothDevice) => devices.push(device));
      discovery.devices.clear();
      const scanning: boolean = connection.isBluetoothDiscovering();
      if (!scanning) {
        this.stopScan(sessionId);
      }
      return { devices: devices, scanning: scanning };
    } catch (error) {
      this.stopScan(sessionId);
      const bluetoothError: BusinessError = error as BusinessError;
      throw new BluetoothBridgeError(bluetoothError.code, bluetoothError.message);
    }
  }

  stopScan(sessionId: number): void {
    const discovery: DiscoverySession | undefined = this.discoveries.get(sessionId);
    this.discoveries.delete(sessionId);
    if (discovery === undefined || !discovery.listening) {
      return;
    }
    try {
      connection.off('bluetoothDeviceFind', discovery.callback);
    } catch (error) {
      const bluetoothError: BusinessError = error as BusinessError;
      console.error(`QGC Bluetooth discovery unsubscribe failed (${bluetoothError.code}): ${bluetoothError.message}`);
    }
    const active: boolean = Array.from(this.discoveries.values()).some((other: DiscoverySession) => other.listening);
    if (!active && this.discoveryOwned) {
      this.discoveryOwned = false;
      try {
        if (connection.isBluetoothDiscovering()) {
          connection.stopBluetoothDiscovery();
        }
      } catch (error) {
        const bluetoothError: BusinessError = error as BusinessError;
        console.error(`QGC Bluetooth discovery stop failed (${bluetoothError.code}): ${bluetoothError.message}`);
      }
    }
  }

  async open(sessionId: number, address: string, context: common.UIAbilityContext): Promise<void> {
    if (this.sessions.has(sessionId)) {
      throw new BluetoothBridgeError(2900099, 'Bluetooth socket session is already in use');
    }
    const session: BluetoothSession = new BluetoothSession();
    this.sessions.set(sessionId, session);
    try {
      await this.waitForSession(sessionId, session, () => this.authorize(context));
      if (this.sessions.get(sessionId) !== session) {
        throw new BluetoothBridgeError(2900099, 'Bluetooth connection was canceled');
      }
      const options: socket.SppOptions = {
        uuid: '00001101-0000-1000-8000-00805F9B34FB',
        secure: true,
        type: socket.SppType.SPP_RFCOMM
      };
      await this.waitForSession(sessionId, session, () => new Promise<void>((resolve, reject) => {
        socket.sppConnect(address, options, (error: BusinessError, socketId: number) => {
          if (error) {
            reject(new BluetoothBridgeError(error.code, error.message));
            return;
          }
          if (socketId < 0) {
            reject(new BluetoothBridgeError(2900099, 'Bluetooth returned an invalid socket'));
            return;
          }
          if (this.sessions.get(sessionId) !== session) {
            this.closeSocket(socketId);
            reject(new BluetoothBridgeError(2900099, 'Bluetooth connection was canceled'));
            return;
          }
          session.socketId = socketId;
          resolve();
        });
      }), 30000);
    } catch (error) {
      if (this.sessions.get(sessionId) === session) {
        this.close(sessionId);
      }
      const bluetoothError: BusinessError = error as BusinessError;
      throw new BluetoothBridgeError(bluetoothError.code, bluetoothError.message);
    }
  }

  private requireSession(sessionId: number): BluetoothSession {
    const session: BluetoothSession | undefined = this.sessions.get(sessionId);
    if (session === undefined || session.socketId < 0) {
      throw new BluetoothBridgeError(2901054, 'Bluetooth socket is closed');
    }
    return session;
  }

  private waitForSession<T>(sessionId: number, session: BluetoothSession,
    operation: () => Promise<T>, timeout: number = 0): Promise<T> {
    return new Promise<T>((resolve, reject) => {
      let timer: number = -1;
      const cancel: (error: BluetoothBridgeError) => void = (error: BluetoothBridgeError) => {
        if (!session.pending.delete(cancel)) {
          return;
        }
        if (timer >= 0) {
          clearTimeout(timer);
        }
        reject(error);
      };
      const fail: (error: BusinessError) => void = (error: BusinessError) => {
        if (!session.pending.has(cancel)) {
          return;
        }
        cancel(new BluetoothBridgeError(error.code, error.message));
        if (this.sessions.get(sessionId) === session) {
          this.close(sessionId);
        }
      };
      session.pending.add(cancel);
      if (timeout > 0) {
        timer = setTimeout(() => fail(new BluetoothBridgeError(2901054, 'Bluetooth operation timed out')), timeout);
      }
      try {
        operation().then((value: T) => {
          if (!session.pending.delete(cancel)) {
            return;
          }
          if (timer >= 0) {
            clearTimeout(timer);
          }
          resolve(value);
        }, (error: BusinessError) => fail(error));
      } catch (error) {
        fail(error as BusinessError);
      }
    });
  }

  async read(sessionId: number): Promise<Uint8Array> {
    const session: BluetoothSession = this.requireSession(sessionId);
    if (session.reading) {
      throw new BluetoothBridgeError(2900099, 'A Bluetooth read is already pending');
    }
    session.reading = true;
    try {
      const data: ArrayBuffer = await this.waitForSession(sessionId, session, () => socket.sppReadAsync(session.socketId));
      if (this.sessions.get(sessionId) !== session) {
        throw new BluetoothBridgeError(2901054, 'Bluetooth read was canceled');
      }
      if (data.byteLength > 8192) {
        this.close(sessionId);
        throw new BluetoothBridgeError(2901054, 'Bluetooth read exceeded the receive buffer');
      }
      return new Uint8Array(data);
    } finally {
      session.reading = false;
    }
  }

  async write(sessionId: number, data: Uint8Array): Promise<number> {
    const session: BluetoothSession = this.requireSession(sessionId);
    if (data.length === 0 || data.length > 4096) {
      throw new BluetoothBridgeError(401, 'Bluetooth write length must be between 1 and 4096 bytes');
    }
    await this.waitForSession(sessionId, session, () => socket.sppWriteAsync(session.socketId, data.slice().buffer), 5000);
    if (this.sessions.get(sessionId) !== session) {
      throw new BluetoothBridgeError(2901054, 'Bluetooth write was canceled');
    }
    return data.length;
  }

  close(sessionId: number): void {
    const session: BluetoothSession | undefined = this.sessions.get(sessionId);
    this.sessions.delete(sessionId);
    if (session !== undefined) {
      const error: BluetoothBridgeError = new BluetoothBridgeError(2901054, 'Bluetooth session was closed');
      session.pending.forEach((cancel: (error: BluetoothBridgeError) => void) => cancel(error));
      session.pending.clear();
      if (session.socketId >= 0) {
        this.closeSocket(session.socketId);
      }
    }
  }

  private closeSocket(socketId: number): void {
    try {
      socket.sppCloseClientSocket(socketId);
    } catch (error) {
      const bluetoothError: BusinessError = error as BusinessError;
      console.error(`QGC Bluetooth close failed (${bluetoothError.code}): ${bluetoothError.message}`);
    }
  }
}

const bridge: QGCBluetoothBridge = new QGCBluetoothBridge();

export function getQGCBluetoothBridge(): object {
  return bridge;
}
