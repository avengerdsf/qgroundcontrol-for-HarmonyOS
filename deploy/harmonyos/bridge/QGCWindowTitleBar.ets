import { window } from '@kit.ArkUI';
import { common } from '@kit.AbilityKit';
import { preferences } from '@kit.ArkData';
import { fileIo } from '@kit.CoreFileKit';
import { hilog } from '@kit.PerformanceAnalysisKit';
import { BusinessError } from '@kit.BasicServicesKit';
import deviceInfo from '@ohos.deviceInfo';

export const QGC_WINDOW_CLOSE_REQUEST: string = 'qgc.window.close.request';

export class QGCTitleBarState {
  visible: boolean = false;
  focused: boolean = true;
  topmost: boolean = false;
  busy: boolean = false;
  controlsInset: number = 0;
  customControls: boolean = false;
}

export class QGCTitleBarController {
  private mainWindow: window.Window | null = null;
  private store: preferences.Preferences | null = null;
  private context: common.Context | null = null;
  private state: QGCTitleBarState = new QGCTitleBarState();
  private changed: (state: QGCTitleBarState) => void = (_state: QGCTitleBarState): void => {};
  private generation: number = 0;
  private ready: boolean = false;
  private disabled: boolean = false;
  private refreshing: boolean = false;
  private decorHidden: boolean = false;
  private statusListening: boolean = false;
  private rectListening: boolean = false;
  private eventListening: boolean = false;
  private foregroundPending: boolean = false;
  private hoverTarget: boolean | null = null;
  private hoverRevision: number = 0;
  private hoverRestoreNeeded: boolean = false;
  private pending: Promise<void> = Promise.resolve();
  private readonly statusChanged = (status: window.WindowStatusType): void => {
    this.refresh(status === window.WindowStatusType.FULL_SCREEN);
  };
  private readonly rectChanged = (_rect: window.TitleButtonRect): void => this.refresh();
  private readonly windowEvent = (event: window.WindowEventType): void => {
    this.retryNativeHoverRestore();
    if (event === window.WindowEventType.WINDOW_ACTIVE || event === window.WindowEventType.WINDOW_INACTIVE) {
      this.state.focused = event === window.WindowEventType.WINDOW_ACTIVE;
      this.publish();
    } else if (event === window.WindowEventType.WINDOW_SHOWN) {
      this.foreground();
    }
  };

  async start(mainWindow: window.Window, context: common.Context,
      changed: (state: QGCTitleBarState) => void): Promise<void> {
    this.destroy();
    this.changed = changed;
    if (deviceInfo.deviceType === 'phone') {
      this.publish();
      return;
    }
    const generation: number = this.generation;
    try {
      // The native page verifies current-window ownership against its UIAbility WindowStage.
      const properties: window.WindowProperties = mainWindow.getWindowProperties();
      hilog.info(0x5147, 'QGCWindowTitleBar', 'Entry id=%{public}d type=%{public}d status=%{public}d',
        properties.id, properties.type, mainWindow.getWindowStatus());
      this.mainWindow = mainWindow;
      this.context = context;
      this.disabled = false;
      mainWindow.on('windowStatusDidChange', this.statusChanged);
      this.statusListening = true;
      mainWindow.on('windowTitleButtonRectChange', this.rectChanged);
      this.rectListening = true;
      mainWindow.on('windowEvent', this.windowEvent);
      this.eventListening = true;
      this.state.focused = mainWindow.isFocused();
      const store: preferences.Preferences = await preferences.getPreferences(context, { name: 'qgc_window' });
      if (generation !== this.generation) {
        return;
      }
      const value: boolean = await store.has('topmost') ?
        (await store.get('topmost', false)) === true : await this.readLegacyPreference(context);
      if (generation !== this.generation) {
        return;
      }
      this.store = store;
      await this.pending;
      if (generation !== this.generation) {
        return;
      }
      await mainWindow.setWindowTopmost(value);
      if (generation !== this.generation) {
        return;
      }
      this.state.topmost = value;
      if (!await store.has('topmost')) {
        await store.put('topmost', value);
        await store.flush();
      }
      if (generation !== this.generation) {
        return;
      }
      this.ready = true;
      this.refresh();
    } catch (caught) {
      if (generation === this.generation) {
        this.fallback(caught as Error);
      }
    }
  }

  toggle(): Promise<void> {
    if (!this.ready || this.disabled || this.state.busy || !this.mainWindow || !this.store) {
      return this.pending;
    }
    const mainWindow: window.Window = this.mainWindow;
    const store: preferences.Preferences = this.store;
    const generation: number = this.generation;
    const previous: boolean = this.state.topmost;
    this.state.busy = true;
    this.publish();
    this.pending = this.pending.then(async (): Promise<void> => {
      if (generation !== this.generation) {
        return;
      }
      try {
        await mainWindow.setWindowTopmost(!previous);
        if (generation !== this.generation) {
          return;
        }
        try {
          await store.put('topmost', !previous);
          await store.flush();
        } catch (caught) {
          await mainWindow.setWindowTopmost(previous);
          await store.put('topmost', previous);
          await store.flush();
          throw caught as Error;
        }
        if (generation === this.generation) {
          this.state.topmost = !previous;
        }
      } catch (caught) {
        if (generation === this.generation) {
          this.fallback(caught as Error);
        }
      } finally {
        if (generation === this.generation) {
          this.state.busy = false;
          this.publish();
        }
      }
    });
    return this.pending;
  }

  foreground(): void {
    this.refresh();
    if (!this.ready || this.disabled || this.state.busy || this.foregroundPending || !this.mainWindow) {
      return;
    }
    const mainWindow: window.Window = this.mainWindow;
    const generation: number = this.generation;
    this.foregroundPending = true;
    this.pending = this.pending.then(async (): Promise<void> => {
      try {
        if (generation !== this.generation || !this.ready || this.state.busy) {
          return;
        }
        await mainWindow.setWindowTopmost(this.state.topmost);
      } catch (caught) {
        if (generation === this.generation) {
          this.fallback(caught as Error);
        }
      } finally {
        if (generation === this.generation) {
          this.foregroundPending = false;
        }
      }
    });
  }

  layoutChanged(): void {
    this.refresh();
  }

  move(): void {
    if (this.ready && this.state.visible && !this.state.customControls && this.mainWindow) {
      this.mainWindow.startMoving().catch((error: Error): void => this.log(error));
    }
  }

  maximize(): void {
    if (this.ready && this.state.visible && this.mainWindow) {
      if (this.state.customControls) {
        this.restore();
      } else {
        // Existing QGCWindowPolicy handles the completed MAXIMIZE -> immersive transition.
        this.mainWindow.maximize().catch((error: Error): void => this.log(error));
      }
    }
  }

  async restore(): Promise<void> {
    if (this.ready && !this.disabled && this.state.customControls && this.mainWindow) {
      try { await this.mainWindow.recover(); } catch (caught) { this.log(caught as Error); }
    }
  }

  async minimize(): Promise<void> {
    if (this.ready && !this.disabled && this.state.customControls && this.mainWindow) {
      try { await this.mainWindow.minimize(); } catch (caught) { this.log(caught as Error); }
    }
  }

  close(): void {
    if (this.ready && !this.disabled && this.state.customControls && this.context) {
      try { this.context.eventHub.emit(QGC_WINDOW_CLOSE_REQUEST); } catch (caught) { this.log(caught as Error); }
    }
  }

  destroy(): void {
    ++this.generation;
    ++this.hoverRevision;
    const mainWindow: window.Window | null = this.mainWindow;
    const restoreHover: boolean = this.hoverTarget !== null;
    this.hoverTarget = null;
    this.hoverRestoreNeeded = false;
    if (mainWindow && restoreHover) {
      this.pending = this.pending.then(async (): Promise<void> => {
        try { await mainWindow.setTitleAndDockHoverShown(true, true); } catch (caught) { this.log(caught as Error); }
      });
    }
    this.mainWindow = null;
    this.store = null;
    this.context = null;
    this.ready = false;
    this.foregroundPending = false;
    if (mainWindow) {
      if (this.statusListening) {
        try { mainWindow.off('windowStatusDidChange', this.statusChanged); } catch (caught) { this.log(caught as Error); }
      }
      if (this.rectListening) {
        try { mainWindow.off('windowTitleButtonRectChange', this.rectChanged); } catch (caught) { this.log(caught as Error); }
      }
      if (this.eventListening) {
        try { mainWindow.off('windowEvent', this.windowEvent); } catch (caught) { this.log(caught as Error); }
      }
      try {
        mainWindow.setWindowDecorVisible(true);
        this.decorHidden = false;
      } catch (caught) {
        this.log(caught as Error);
      }
    }
    this.statusListening = false;
    this.rectListening = false;
    this.eventListening = false;
    this.state = new QGCTitleBarState();
    this.publish();
    this.changed = (_state: QGCTitleBarState): void => {};
  }

  private refresh(forceTitleHover: boolean = false): void {
    this.retryNativeHoverRestore();
    if (!this.mainWindow || !this.ready || this.disabled || this.refreshing) {
      return;
    }
    this.refreshing = true;
    try {
      const status: window.WindowStatusType = this.mainWindow.getWindowStatus();
      if (status === window.WindowStatusType.FULL_SCREEN) {
        this.state.visible = true;
        this.state.customControls = true;
        this.state.controlsInset = 20;
        this.setDecorHidden(false);
        this.publish();
        this.requestTitleHover(true, forceTitleHover || this.hoverTarget === null);
        return;
      }
      if (status !== window.WindowStatusType.FLOATING && status !== window.WindowStatusType.MAXIMIZE) {
        this.state.visible = false;
        this.state.customControls = false;
        this.setDecorHidden(false);
        this.publish();
        this.requestTitleHover(true);
        return;
      }
      const rect: window.TitleButtonRect = this.mainWindow.getTitleButtonRect();
      // All TitleButtonRect values are vp; right is the inset from the window's right edge.
      this.state.controlsInset = Math.max(0, rect.right) + Math.max(0, rect.width);
      this.state.visible = rect.width > 0 && rect.height > 0;
      this.state.customControls = false;
      if (status === window.WindowStatusType.MAXIMIZE && !this.state.visible) {
        this.state.visible = true;
        this.state.customControls = true;
        this.state.controlsInset = 20;
      }
      this.setDecorHidden(this.state.visible);
      this.publish();
      this.requestTitleHover(true);
    } catch (caught) {
      const error: BusinessError = caught as BusinessError;
      if (error.code === 1300002) {
        this.log(error);
        this.state.visible = false;
        try { this.setDecorHidden(false); } catch (restoreError) { this.log(restoreError as Error); }
        this.publish();
        this.requestTitleHover(true);
      } else {
        this.fallback(error);
      }
    } finally {
      this.refreshing = false;
    }
  }

  private retryNativeHoverRestore(): void {
    if (this.hoverRestoreNeeded) {
      this.requestTitleHover(true, true);
    }
  }

  private requestTitleHover(show: boolean, force: boolean = false): void {
    if (!this.mainWindow || (!force && ((show && this.hoverTarget === null) || this.hoverTarget === show))) {
      return;
    }
    const mainWindow: window.Window = this.mainWindow;
    const generation: number = this.generation;
    const revision: number = ++this.hoverRevision;
    this.hoverTarget = show;
    this.hoverRestoreNeeded = false;
    this.pending = this.pending.then(async (): Promise<void> => {
      if (generation !== this.generation || revision !== this.hoverRevision) {
        return;
      }
      try {
        await mainWindow.setTitleAndDockHoverShown(show, true);
      } catch (caught) {
        if (generation === this.generation && revision === this.hoverRevision) {
          if (show) {
            this.log(caught as Error);
            this.hoverRestoreNeeded = (caught as BusinessError).code === 1300002;
          } else {
            this.fallback(caught as Error);
          }
        }
      }
    });
  }

  private setDecorHidden(hidden: boolean): void {
    if (!this.mainWindow) {
      return;
    }
    const changed: boolean = hidden !== this.decorHidden;
    this.decorHidden = hidden;
    // In immersive mode the getter reports transient edge-title visibility.
    // Check its actual value only when hiding the floating window's title.
    if (hidden ? this.mainWindow.getWindowDecorVisible() : changed) {
      // Hiding the ContainerModal title preserves its native three-button area.
      // Full-screen mode uses the original system title when revealed from the top edge.
      this.mainWindow.setWindowDecorVisible(!hidden);
    }
  }

  private async readLegacyPreference(context: common.Context): Promise<boolean> {
    try {
      const ini: string = await fileIo.readText(`${context.getApplicationContext().preferencesDir}/QGroundControl/QGroundControl.ini`);
      const lines: string[] = ini.split(/\r?\n/);
      let generalSettings: boolean = false;
      for (const line of lines) {
        const trimmed: string = line.trim();
        if (trimmed.startsWith('[')) {
          generalSettings = trimmed === '[General]';
        } else if (generalSettings && /^windowTopmost\s*=/.test(trimmed)) {
          return trimmed.substring(trimmed.indexOf('=') + 1).trim() === 'true';
        }
      }
    } catch (_caught) {
      // No legacy settings file on a fresh installation.
    }
    return false;
  }

  private fallback(error: Error): void {
    this.log(error);
    this.disabled = true;
    this.state.visible = false;
    if (this.mainWindow) {
      try { this.mainWindow.setWindowDecorVisible(true); } catch (caught) { this.log(caught as Error); }
      this.decorHidden = false;
    }
    this.publish();
    this.requestTitleHover(true);
  }

  private publish(): void {
    const state: QGCTitleBarState = new QGCTitleBarState();
    state.visible = this.state.visible;
    state.focused = this.state.focused;
    state.topmost = this.state.topmost;
    state.busy = this.state.busy;
    state.controlsInset = this.state.controlsInset;
    state.customControls = this.state.customControls;
    this.changed(state);
  }

  private log(error: Error): void {
    hilog.warn(0x5147, 'QGCWindowTitleBar', 'Window title bar failed code=%{public}d: %{public}s',
      (error as BusinessError).code ?? 0, error.message);
  }
}

@Component
struct QGCWindowTitleButton {
  @Prop symbol: Resource = $r('sys.symbol.pin');
  @Prop label: string = '';
  @Prop focused: boolean = true;
  @Prop buttonEnabled: boolean = true;
  action: () => void = (): void => {};
  @State private hovered: boolean = false;
  @State private pressed: boolean = false;

  build() {
    Button({ type: ButtonType.Normal, stateEffect: false }) {
      SymbolGlyph(this.symbol)
        .fontSize(20)
        .fontColor([$r('sys.color.icon_primary')])
        .opacity(this.focused ? 1 : 0.4)
    }
    .width(28)
    .height(28)
    .borderRadius(4)
    .backgroundColor(this.pressed ? $r('sys.color.interactive_click') :
      (this.hovered ? $r('sys.color.interactive_hover') : Color.Transparent))
    .enabled(this.buttonEnabled)
    .onHover((hovered: boolean): void => { this.hovered = hovered; })
    .onTouch((event: TouchEvent): void => { this.pressed = event.type === TouchType.Down; })
    .accessibilityText(this.label)
    .onClick((): void => { this.action(); })
  }
}

@Component
export struct QGCWindowTitleBar {
  @Prop state: QGCTitleBarState = new QGCTitleBarState();
  controller: QGCTitleBarController = new QGCTitleBarController();

  build() {
    Row() {
      Row({ space: 12 }) {
        Image($r('app.media.android_512x512'))
          .width(20)
          .height(20)
        Text($r('app.string.QAbility_label'))
          .fontSize(16)
          .fontWeight(FontWeight.Medium)
          .fontColor($r('sys.color.ohos_id_color_primary'))
          .maxLines(1)
          .textOverflow({ overflow: TextOverflow.Ellipsis })
      }
      .padding({ left: 20 })
      .height('100%')
      .layoutWeight(1)
      .opacity(this.state.focused ? 1 : 0.4)
      .gesture(GestureGroup(GestureMode.Exclusive,
        TapGesture({ count: 2 }).onAction((): void => this.controller.maximize()),
        PanGesture().onActionStart((): void => this.controller.move())
      ))
      QGCWindowTitleButton({
        symbol: this.state.topmost ? $r('sys.symbol.pin_fill') : $r('sys.symbol.pin'),
        label: this.state.topmost ? 'Cancel window topmost' : 'Keep window on top',
        focused: this.state.focused,
        buttonEnabled: !this.state.busy,
        action: (): void => { this.controller.toggle(); }
      })
      .margin({ right: 12 })
      if (this.state.customControls) {
        QGCWindowTitleButton({ symbol: $r('sys.symbol.rectangle_on_rectangle'), label: 'Restore window',
          focused: this.state.focused, action: (): void => { this.controller.restore(); } })
          .margin({ left: 12, right: 12 })
        QGCWindowTitleButton({ symbol: $r('sys.symbol.minus'), label: 'Minimize window',
          focused: this.state.focused, action: (): void => { this.controller.minimize(); } })
          .margin({ right: 12 })
        QGCWindowTitleButton({ symbol: $r('sys.symbol.xmark'), label: 'Close window',
          focused: this.state.focused, action: (): void => { this.controller.close(); } })
      }
    }
    .padding({ right: this.state.controlsInset })
    .width('100%')
    // Share the 37vp Qt 6.12 standard caption height across windowed and full-screen modes.
    .height(this.state.visible ? 37 : 0)
    .visibility(this.state.visible ? Visibility.Visible : Visibility.None)
    .backgroundColor(this.state.focused ? $r('sys.color.container_modal_background') :
      $r('sys.color.container_modal_unfocus_background'))
  }
}
