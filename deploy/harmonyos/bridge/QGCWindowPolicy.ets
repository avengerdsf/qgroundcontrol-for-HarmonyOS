import deviceInfo from '@ohos.deviceInfo';
import hilog from '@ohos.hilog';
import window from '@ohos.window';
import { BusinessError } from '@ohos.base';
import common from '@ohos.app.ability.common';
import fileIo from '@ohos.file.fs';

enum WindowPolicyTraceAction {
  ColdEntry, StatusChange, StatusDidChange, RectChange, Refresh, Apply, Restore, Ready
}

class WindowPolicySession {
  mainWindow: window.Window;
  active: boolean = true;
  target: boolean = false;
  changed: boolean = false;
  baselineLayout: boolean = false;
  revision: number = 0;
  lastStatus: window.WindowStatusType;
  callback: (status: window.WindowStatusType) => void = (_status: window.WindowStatusType): void => {};
  statusCallback: (status: window.WindowStatusType) => void = (_status: window.WindowStatusType): void => {};
  rectCallback: (options: window.RectChangeOptions) => void = (_options: window.RectChangeOptions): void => {};
  statusListening: boolean = false;
  rectListening: boolean = false;
  lastTrace: string = '';

  constructor(mainWindow: window.Window) {
    this.mainWindow = mainWindow;
    this.lastStatus = mainWindow.getWindowStatus();
  }
}

export class QGCWindowPolicy {
  private session: WindowPolicySession | null = null;
  private generation: number = 0;
  private pending: Promise<void> = Promise.resolve();
  private phoneWindow: window.Window | null = null;
  private phoneForeground: boolean = true;
  private phoneLifetime: number = 0;
  private deviceTypeLogged: boolean = false;

  async prepare(stage: window.WindowStage, onReady: () => void, context?: common.Context): Promise<boolean> {
    const generation: number = ++this.generation;
    this.detach(true);
    if (!this.deviceTypeLogged) {
      hilog.info(0x5147, 'QGCWindowPolicy', 'deviceType=%{public}s', deviceInfo.deviceType);
      this.deviceTypeLogged = true;
    }
    if (deviceInfo.deviceType === 'phone') {
      try {
        const mainWindow: window.Window = await stage.getMainWindow();
        if (generation !== this.generation) {
          return false;
        }
        this.phoneWindow = mainWindow;
        await this.enqueue(async (): Promise<void> => {
          if (mainWindow === this.phoneWindow) {
            await mainWindow.setPreferredOrientation(window.Orientation.AUTO_ROTATION_LANDSCAPE);
          }
        });
        if (generation !== this.generation) {
          return false;
        }
        this.updatePhoneScreen();
      } catch (caught) {
        this.log(caught as BusinessError);
      }
      if (generation !== this.generation) {
        return false;
      }
      onReady();
      return true;
    }
    if (deviceInfo.deviceType !== 'tablet' && deviceInfo.deviceType !== '2in1') {
      onReady();
      return true;
    }
    try {
      const mainWindow: window.Window = await stage.getMainWindow();
      await this.pending;
      if (generation !== this.generation) {
        return false;
      }
      if (context) {
        const visibility: number = await this.readStartupVisibility(context);
        if (generation !== this.generation) {
          return false;
        }
        await this.enqueue(async (): Promise<void> => {
          if (generation !== this.generation) {
            return;
          }
          const status: window.WindowStatusType = mainWindow.getWindowStatus();
          if (visibility === 4) {
            await mainWindow.maximize(window.MaximizePresentation.EXIT_IMMERSIVE);
          } else if (visibility === 2 && (status === window.WindowStatusType.FULL_SCREEN ||
              status === window.WindowStatusType.MAXIMIZE || status === window.WindowStatusType.SPLIT_SCREEN)) {
            await mainWindow.recover();
          }
        });
        if (generation !== this.generation) {
          return false;
        }
      }
      if (deviceInfo.deviceType === 'tablet') {
        const session: WindowPolicySession = new WindowPolicySession(mainWindow);
        this.session = session;
        session.callback = (status: window.WindowStatusType): void => {
          this.statusDidChange(session, status);
        };
        mainWindow.on('windowStatusDidChange', session.callback);
        session.statusCallback = (status: window.WindowStatusType): void => {
          this.trace(session, WindowPolicyTraceAction.StatusChange, status);
        };
        session.rectCallback = (options: window.RectChangeOptions): void => {
          if (options.reason === window.RectChangeReason.MAXIMIZE || options.reason === window.RectChangeReason.RECOVER) {
            this.trace(session, WindowPolicyTraceAction.RectChange, options.reason, options.rect);
          }
        };
        try {
          mainWindow.on('windowStatusChange', session.statusCallback);
          session.statusListening = true;
        } catch (caught) {
          this.log(caught as BusinessError);
        }
        try {
          mainWindow.on('windowRectChange', session.rectCallback);
          session.rectListening = true;
        } catch (caught) {
          this.log(caught as BusinessError);
        }
        let preparation: Promise<void> = this.refresh(session);
        while (generation === this.generation) {
          await preparation;
          if (preparation === this.pending) {
            break;
          }
          preparation = this.pending;
        }
      }
    } catch (caught) {
      if (generation === this.generation) {
        this.detach(false);
      }
      this.log(caught as BusinessError);
    }
    if (generation !== this.generation) {
      return false;
    }
    if (this.session) {
      this.trace(this.session, WindowPolicyTraceAction.Ready);
    }
    onReady();
    return true;
  }

  private async readStartupVisibility(context: common.Context): Promise<number> {
    const fallback: number = deviceInfo.deviceType === 'tablet' ? 4 : 2;
    try {
      const ini: string = await fileIo.readText(`${context.getApplicationContext().preferencesDir}/QGroundControl/QGroundControl.ini`);
      let inSection: boolean = false;
      let width: number = 0;
      let height: number = 0;
      let visibility: number = 2;
      for (const line of ini.split(/\r?\n/)) {
        const item: string = line.trim();
        if (item.startsWith('[')) {
          inSection = item === '[MainWindowState]';
          continue;
        }
        if (!inSection || item.startsWith(';') || item.startsWith('#')) {
          continue;
        }
        const separator: number = item.indexOf('=');
        if (separator < 0) {
          continue;
        }
        const key: string = item.substring(0, separator).trim();
        const value: number = Number(item.substring(separator + 1).trim());
        if (key === 'width') {
          width = value;
        } else if (key === 'height') {
          height = value;
        } else if (key === 'visibility') {
          visibility = value;
        }
      }
      if (!Number.isFinite(width) || !Number.isFinite(height) || width <= 0 || height <= 0) {
        return fallback;
      }
      return visibility === 5 ? 4 : visibility === 2 || visibility === 4 ? visibility : 2;
    } catch (_) {
      return fallback;
    }
  }

  foreground(): void {
    this.phoneForeground = true;
    this.updatePhoneScreen();
    if (this.session) {
      this.refresh(this.session);
    }
  }

  background(): void {
    this.phoneForeground = false;
    this.updatePhoneScreen();
  }

  private updatePhoneScreen(): void {
    const mainWindow: window.Window | null = this.phoneWindow;
    if (!mainWindow) {
      return;
    }
    this.enqueue(async (): Promise<void> => {
      if (mainWindow !== this.phoneWindow) {
        return;
      }
      await mainWindow.setWindowKeepScreenOn(this.phoneForeground);
      if (mainWindow === this.phoneWindow) {
        hilog.info(0x5147, 'QGCWindowPolicy', 'Phone keepScreenOn=%{public}d',
          mainWindow.getWindowProperties().isKeepScreenOn ? 1 : 0);
      }
    });
  }

  destroy(): void {
    ++this.generation;
    ++this.phoneLifetime;
    this.detach(false);
  }

  private statusDidChange(session: WindowPolicySession, status: window.WindowStatusType): void {
    if (!session.active) {
      return;
    }
    const previous: window.WindowStatusType = session.lastStatus;
    session.lastStatus = status;
    this.trace(session, WindowPolicyTraceAction.StatusDidChange, status);
    if (status === window.WindowStatusType.MAXIMIZE && previous !== status) {
      this.enqueue(async (): Promise<void> => {
        if (session.active && session.mainWindow.getWindowStatus() === window.WindowStatusType.MAXIMIZE) {
          await session.mainWindow.maximize(window.MaximizePresentation.EXIT_IMMERSIVE);
        }
      });
    }
    this.request(session, status === window.WindowStatusType.FULL_SCREEN, previous !== status);
  }

  private refresh(session: WindowPolicySession): Promise<void> {
    this.trace(session, WindowPolicyTraceAction.Refresh);
    try {
      return this.request(session,
        session.mainWindow.getWindowStatus() === window.WindowStatusType.FULL_SCREEN, true);
    } catch (caught) {
      this.log(caught as BusinessError);
      return this.pending;
    }
  }

  private request(session: WindowPolicySession, target: boolean, force: boolean): Promise<void> {
    if (!session.active || (!force && session.target === target)) {
      return this.pending;
    }
    session.target = target;
    const revision: number = ++session.revision;
    return this.enqueue(async (): Promise<void> => {
      if (!session.active || revision !== session.revision) {
        return;
      }
      if (!session.target && session.mainWindow.getWindowStatus() !== window.WindowStatusType.MAXIMIZE) {
        await this.restore(session);
        return;
      }
      if (!session.target) {
        await session.mainWindow.setWindowSystemBarEnable(['status', 'navigation']);
      }
      if (!session.changed) {
        session.baselineLayout = session.mainWindow.getImmersiveModeEnabledState();
        session.changed = true;
      }
      this.trace(session, WindowPolicyTraceAction.Apply);
      await session.mainWindow.setWindowLayoutFullScreen(true);
      if (!session.active || revision !== session.revision ||
          !session.target || session.mainWindow.getWindowStatus() !== window.WindowStatusType.FULL_SCREEN) {
        return;
      }
      // Leave temporary edge-gesture system bars under the system's control.
      await session.mainWindow.setWindowSystemBarEnable([]);
    });
  }

  private async restore(session: WindowPolicySession): Promise<void> {
    if (!session.changed) {
      return;
    }
    this.trace(session, WindowPolicyTraceAction.Restore);
    let restored: boolean = true;
    try {
      await session.mainWindow.setWindowSystemBarEnable(['status', 'navigation']);
    } catch (caught) {
      restored = false;
      this.log(caught as BusinessError);
    }
    try {
      await session.mainWindow.setWindowLayoutFullScreen(session.baselineLayout);
    } catch (caught) {
      restored = false;
      this.log(caught as BusinessError);
    }
    session.changed = !restored;
  }

  private detach(restore: boolean): void {
    const phoneWindow: window.Window | null = this.phoneWindow;
    const phoneLifetime: number = this.phoneLifetime;
    this.phoneWindow = null;
    if (phoneWindow && restore) {
      this.enqueue(async (): Promise<void> => {
        if (phoneLifetime === this.phoneLifetime) {
          await phoneWindow.setWindowKeepScreenOn(false);
        }
      });
    }
    const session: WindowPolicySession | null = this.session;
    this.session = null;
    if (!session) {
      return;
    }
    session.active = false;
    try {
      session.mainWindow.off('windowStatusDidChange', session.callback);
    } catch (caught) {
      this.log(caught as BusinessError);
    }
    if (session.statusListening) {
      try {
        session.mainWindow.off('windowStatusChange', session.statusCallback);
      } catch (caught) {
        this.log(caught as BusinessError);
      }
    }
    if (session.rectListening) {
      try {
        session.mainWindow.off('windowRectChange', session.rectCallback);
      } catch (caught) {
        this.log(caught as BusinessError);
      }
    }
    if (restore) {
      this.enqueue(async (): Promise<void> => this.restore(session));
    }
  }

  private enqueue(action: () => Promise<void>): Promise<void> {
    this.pending = this.pending.then(action).catch((caught: BusinessError): void => {
      this.log(caught);
    });
    return this.pending;
  }

  private trace(session: WindowPolicySession, action: WindowPolicyTraceAction,
      event: number = -1, rect?: window.Rect): void {
    if (!session.active) {
      return;
    }
    try {
      const status: window.WindowStatusType = session.mainWindow.getWindowStatus();
      const configured: number = session.mainWindow.getImmersiveModeEnabledState() ? 1 : 0;
      const immersive: number = session.mainWindow.isImmersiveLayout() ? 1 : 0;
      const bounds: window.Rect = rect ?? session.mainWindow.getWindowProperties().windowRect;
      const key: string = `${action},${event},${status},${configured},${immersive},${bounds.left},${bounds.top},${bounds.width},${bounds.height}`;
      if (session.lastTrace === key) {
        return;
      }
      session.lastTrace = key;
      hilog.info(0x5147, 'QGCWindowPolicy',
        'action=%{public}d event=%{public}d status=%{public}d configured=%{public}d immersive=%{public}d x=%{public}d y=%{public}d w=%{public}d h=%{public}d',
        action, event, status, configured, immersive, bounds.left, bounds.top, bounds.width, bounds.height);
    } catch (caught) {
      const error: BusinessError = caught as BusinessError;
      // Layout diagnostics are unavailable until the native content is loaded.
      if (error.code !== 1300002) {
        this.log(error);
      }
    }
  }

  private log(error: BusinessError): void {
    hilog.warn(0x5147, 'QGCWindowPolicy', 'Window policy failed: %{public}s', error.message);
  }
}
