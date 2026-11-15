import {
  createCommand,
  parseNativeMessage,
  type NativeMessage,
  type UiCommand,
  type UiCommandType,
} from './contracts';

interface WebViewMessageEvent extends Event {
  readonly data: unknown;
}

interface WebViewHost {
  postMessage(message: UiCommand): void;
  addEventListener(
    type: 'message',
    listener: (event: WebViewMessageEvent) => void,
  ): void;
  removeEventListener(
    type: 'message',
    listener: (event: WebViewMessageEvent) => void,
  ): void;
}

declare global {
  interface Window {
    chrome?: {
      webview?: WebViewHost;
    };
  }
}

function createIdentifier(prefix: string): string {
  const random = globalThis.crypto?.randomUUID?.();
  if (random) {
    return `${prefix}-${random}`.slice(0, 64);
  }
  const fallback = `${Date.now().toString(36)}-${Math.random()
    .toString(36)
    .slice(2)}`;
  return `${prefix}-${fallback}`.slice(0, 64);
}

export class NativeBridge {
  readonly uiEpoch = createIdentifier('ui');
  readonly initialTargetGeneration = createIdentifier('target');

  private sequence = 0;
  private readonly host = window.chrome?.webview;

  get available(): boolean {
    return this.host !== undefined;
  }

  createTargetGeneration(): string {
    return createIdentifier('target');
  }

  send(
    type: UiCommandType,
    targetGeneration: string,
    payload: UiCommand['payload'] = {},
  ): UiCommand | null {
    if (!this.host) {
      return null;
    }
    this.sequence += 1;
    const command = createCommand(
      type,
      this.uiEpoch,
      this.sequence,
      targetGeneration,
      payload,
    );
    this.host.postMessage(command);
    return command;
  }

  subscribe(listener: (message: NativeMessage) => void): () => void {
    if (!this.host) {
      return () => undefined;
    }
    const handler = (event: WebViewMessageEvent) => {
      const message = parseNativeMessage(event.data);
      if (message) {
        listener(message);
      }
    };
    this.host.addEventListener('message', handler);
    return () => {
      this.host?.removeEventListener('message', handler);
    };
  }
}
