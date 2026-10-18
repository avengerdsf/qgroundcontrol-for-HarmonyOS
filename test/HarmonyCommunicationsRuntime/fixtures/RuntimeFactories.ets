import { abilityAccessCtrl, common, PermissionRequestResult } from '@kit.AbilityKit';
import { BusinessError } from '@kit.BasicServicesKit';
import { access } from '@kit.ConnectivityKit';
import { getQGCBluetoothBridge as getProductionSpp } from './QGCBluetoothBridge';
import { getQGCBleBridge as getProductionBle } from './QGCBleBridge';

class RuntimeError extends Error implements BusinessError {
  code: number;
  constructor(code: number, message: string) { super(message); this.code = code; }
}

interface RuntimeDevice { address: string; name: string; }
interface RuntimeScan { devices: RuntimeDevice[]; scanning: boolean; }
interface RuntimeBleOptions { serviceUuid: string; writeUuid: string; notifyUuid: string; }
interface BridgeApi {
  isSupported(): boolean;
  startScan(session: number, context: common.UIAbilityContext): Promise<RuntimeScan>;
  scan(session: number): RuntimeScan;
  stopScan(session: number): void;
  open(session: number, address: string, context: common.UIAbilityContext, options?: RuntimeBleOptions): Promise<void>;
  read(session: number): Promise<Uint8Array>;
  write(session: number, data: Uint8Array): Promise<number>;
  close(session: number): void;
}
interface RuntimeRequest {
  transport: string; operation: string; session: number; address: string; contextToken: number;
  serviceUuid: string; writeUuid: string; notifyUuid: string; data: number[];
}
class PendingSession {
  closed: boolean = false;
  openResolve?: () => void;
  openReject?: (error: RuntimeError) => void;
  readResolve?: (data: Uint8Array) => void;
  readReject?: (error: RuntimeError) => void;
}

class RuntimeControl {
  mode: string = 'production';
  scenario: string = 'normal';
  records: RuntimeRequest[] = [];
  bridges: RuntimeBridge[] = [];

  configure(mode: string, scenario: string): void {
    if (mode !== 'controlled' && mode !== 'production') { throw new RuntimeError(401, 'Invalid runtime mode'); }
    this.bridges.forEach((bridge: RuntimeBridge) => bridge.reset());
    this.records = [];
    this.mode = mode;
    this.scenario = scenario;
  }
  requestsJson(): string { return JSON.stringify(this.records); }
  releaseOpen(session: number): boolean {
    return this.bridges.some((bridge: RuntimeBridge) => bridge.releaseOpen(session));
  }
  deliverRead(session: number, data: Uint8Array): boolean {
    return this.bridges.some((bridge: RuntimeBridge) => bridge.deliverRead(session, data));
  }
  checkPermission(context: common.UIAbilityContext): number {
    return abilityAccessCtrl.createAtManager().checkAccessTokenSync(
      context.applicationInfo.accessTokenId, 'ohos.permission.ACCESS_BLUETOOTH');
  }
  async requestPermission(context: common.UIAbilityContext): Promise<string> {
    const result: PermissionRequestResult = await abilityAccessCtrl.createAtManager().requestPermissionsFromUser(
      context, ['ohos.permission.ACCESS_BLUETOOTH']);
    return JSON.stringify({ authResults: result.authResults,
      dialogReported: result.dialogShownResults !== undefined && result.dialogShownResults.length === result.authResults.length,
      dialogShown: result.dialogShownResults === undefined || result.dialogShownResults.length === 0 ? false : result.dialogShownResults[0] });
  }
  capabilitySnapshot(context: common.UIAbilityContext): string {
    let state: number = -1;
    let stateError: number = 0;
    let permission: number = -2;
    let permissionError: number = 0;
    try { state = access.getState(); } catch (error) { stateError = (error as BusinessError).code; }
    try { permission = this.checkPermission(context); } catch (error) { permissionError = (error as BusinessError).code; }
    return JSON.stringify({ capability: canIUse('SystemCapability.Communication.Bluetooth.Core'),
      state: state, stateError: stateError, permission: permission, permissionError: permissionError,
      contextToken: context.applicationInfo.accessTokenId, bundle: context.applicationInfo.name });
  }
}

const control: RuntimeControl = new RuntimeControl();

class RuntimeBridge implements BridgeApi {
  private transport: string;
  private production: BridgeApi;
  private sessions: Map<number, PendingSession> = new Map<number, PendingSession>();
  private scans: Set<number> = new Set<number>();

  constructor(transport: string, production: object) {
    this.transport = transport;
    this.production = production as BridgeApi;
  }
  private note(operation: string, session: number): RuntimeRequest {
    const record: RuntimeRequest = { transport: this.transport, operation: operation, session: session,
      address: '', contextToken: 0, serviceUuid: '', writeUuid: '', notifyUuid: '', data: [] };
    control.records.push(record);
    return record;
  }
  isSupported(): boolean { return control.mode === 'controlled' ? true : this.production.isSupported(); }
  async startScan(session: number, context: common.UIAbilityContext): Promise<RuntimeScan> {
    if (control.mode === 'production') { return this.production.startScan(session, context); }
    this.note('startScan', session).contextToken = context.applicationInfo.accessTokenId;
    this.scans.add(session);
    return { devices: [{ address: '12:34:56:78:90:AB', name: `runtime-${this.transport}` }], scanning: true };
  }
  scan(session: number): RuntimeScan {
    if (control.mode === 'production') { return this.production.scan(session); }
    this.note('scan', session);
    return { devices: [], scanning: this.scans.has(session) };
  }
  stopScan(session: number): void {
    if (control.mode === 'production') { this.production.stopScan(session); return; }
    this.note('stopScan', session);
    this.scans.delete(session);
  }
  async open(session: number, address: string, context: common.UIAbilityContext, options?: RuntimeBleOptions): Promise<void> {
    if (control.mode === 'production') { return this.production.open(session, address, context, options); }
    const record: RuntimeRequest = this.note('open', session);
    record.address = address;
    record.contextToken = context.applicationInfo.accessTokenId;
    if (options !== undefined) {
      record.serviceUuid = options.serviceUuid;
      record.writeUuid = options.writeUuid;
      record.notifyUuid = options.notifyUuid;
    }
    if (control.scenario === 'openError') { throw new RuntimeError(2900099, 'runtime-native-open-error'); }
    const pending: PendingSession = new PendingSession();
    this.sessions.set(session, pending);
    if (control.scenario === 'lateOpen') {
      await new Promise<void>((resolve, reject) => { pending.openResolve = resolve; pending.openReject = reject; });
    }
  }
  read(session: number): Promise<Uint8Array> {
    if (control.mode === 'production') { return this.production.read(session); }
    this.note('read', session);
    const pending: PendingSession | undefined = this.sessions.get(session);
    if (pending === undefined || pending.closed) { return Promise.reject(new RuntimeError(2901054, 'runtime-read-closed')); }
    return new Promise<Uint8Array>((resolve, reject) => { pending.readResolve = resolve; pending.readReject = reject; });
  }
  async write(session: number, data: Uint8Array): Promise<number> {
    if (control.mode === 'production') { return this.production.write(session, data); }
    const record: RuntimeRequest = this.note('write', session);
    for (let i: number = 0; i < data.length; ++i) { record.data.push(data[i]); }
    return data.length;
  }
  close(session: number): void {
    if (control.mode === 'production') { this.production.close(session); return; }
    this.note('close', session);
    const pending: PendingSession | undefined = this.sessions.get(session);
    if (pending !== undefined) {
      pending.closed = true;
      if (control.scenario !== 'lateRead' && pending.readReject !== undefined) {
        pending.readReject(new RuntimeError(2901054, 'runtime-read-closed'));
        pending.readResolve = undefined;
        pending.readReject = undefined;
      }
    }
  }
  releaseOpen(session: number): boolean {
    const pending: PendingSession | undefined = this.sessions.get(session);
    if (pending === undefined || pending.openResolve === undefined) { return false; }
    pending.openResolve();
    pending.openResolve = undefined;
    pending.openReject = undefined;
    return true;
  }
  deliverRead(session: number, data: Uint8Array): boolean {
    const pending: PendingSession | undefined = this.sessions.get(session);
    if (pending === undefined || pending.readResolve === undefined) { return false; }
    pending.readResolve(data.slice());
    pending.readResolve = undefined;
    pending.readReject = undefined;
    return true;
  }
  reset(): void {
    this.sessions.forEach((pending: PendingSession) => {
      if (pending.openReject !== undefined) { pending.openReject(new RuntimeError(2901054, 'runtime-reset')); }
      if (pending.readReject !== undefined) { pending.readReject(new RuntimeError(2901054, 'runtime-reset')); }
    });
    this.sessions.clear();
    this.scans.clear();
  }
}

const spp: RuntimeBridge = new RuntimeBridge('spp', getProductionSpp());
const ble: RuntimeBridge = new RuntimeBridge('ble', getProductionBle());
control.bridges.push(spp);
control.bridges.push(ble);

export function getQGCBluetoothBridge(): object { return spp; }
export function getQGCBleBridge(): object { return ble; }
export function getRuntimeControl(): object { return control; }
