export const WEBVIEW_PROTOCOL_VERSION = 1 as const;
export const MINIMUM_SPEED = 0.05;
export const INITIAL_SPEED = 1;
export const MAXIMUM_SPEED = 8;

export type TargetRuntime = 'generic' | 'unityMono';
export type TimeBackend = 'win32Clock' | 'unityMono';
export type EffectState =
  | 'unverified'
  | 'pending'
  | 'applied'
  | 'degraded'
  | 'failed';

export type UiCommandType =
  | 'launchProgram'
  | 'saveCheckpoint'
  | 'loadCheckpoint'
  | 'listProcesses'
  | 'selectTarget'
  | 'attachTarget'
  | 'setSpeed'
  | 'pause'
  | 'resume'
  | 'reset'
  | 'getStatus'
  | 'deactivate'
  | 'relaunchElevated'
  | 'openDiagnostics';

export interface ProcessSummary {
  readonly processId: number;
  readonly creationTime: string;
  readonly architecture: 'x86' | 'x64';
  readonly name: string;
  readonly icon: string;
}

export interface UiCommand {
  readonly v: typeof WEBVIEW_PROTOCOL_VERSION;
  readonly type: UiCommandType;
  readonly uiEpoch: string;
  readonly uiSequence: number;
  readonly targetGeneration: string;
  readonly payload:
    | Readonly<Record<string, never>>
    | {
        readonly processId: number;
        readonly creationTime: string;
      }
    | {
        readonly speed: number;
      };
}

interface NativeMessageBase {
  readonly v: typeof WEBVIEW_PROTOCOL_VERSION;
  readonly type: string;
  readonly controllerSessionId: string;
  readonly uiEpoch?: string;
  readonly uiSequence?: number;
  readonly targetGeneration?: string;
}

export interface ProcessListMessage extends NativeMessageBase {
  readonly type: 'processList';
  readonly uiEpoch: string;
  readonly uiSequence: number;
  readonly targetGeneration: string;
  readonly payload: {
    readonly processes: readonly ProcessSummary[];
  };
}

export interface StatusMessage extends NativeMessageBase {
  readonly type: 'status' | 'targetSelected';
  readonly uiEpoch: string;
  readonly uiSequence: number;
  readonly targetGeneration: string;
  readonly payload: {
    readonly status: string;
    readonly target: {
      readonly processId: number;
      readonly creationTime: string;
    } | null;
    readonly applied: {
      readonly configuredSpeed: number;
      readonly paused: boolean;
      readonly commandSequence: number;
      readonly clockRevision: number;
      readonly targetRuntime: TargetRuntime;
      readonly timeBackend: TimeBackend;
      readonly effectState: EffectState;
    } | null;
  };
}

export interface SpeedAppliedMessage extends NativeMessageBase {
  readonly type: 'speedApplied';
  readonly uiEpoch: string;
  readonly uiSequence: number;
  readonly targetGeneration: string;
  readonly commandSequence: number;
  readonly clockRevision: number;
  readonly payload: {
    readonly configuredSpeed: number;
    readonly paused: boolean;
    readonly effectiveSpeed: number;
    readonly targetRuntime: TargetRuntime;
    readonly timeBackend: TimeBackend;
    readonly effectState: EffectState;
  };
}

export interface TargetExitedMessage extends NativeMessageBase {
  readonly type: 'targetExited';
  readonly targetGeneration: string;
  readonly payload: Readonly<Record<string, never>>;
}

export interface TargetInspectionMessage extends NativeMessageBase {
  readonly type: 'targetInspection';
  readonly uiEpoch: string;
  readonly uiSequence: number;
  readonly targetGeneration: string;
  readonly payload: {
    readonly result: 'ready' | 'elevationRequired' | 'rejected';
    readonly error: string | null;
    readonly process: ProcessSummary;
  };
}

export interface ElevatedTargetReadyMessage extends NativeMessageBase {
  readonly type: 'elevatedTargetReady' | 'programLaunched';
  readonly uiEpoch: string;
  readonly uiSequence: number;
  readonly targetGeneration: string;
  readonly payload: {
    readonly process: ProcessSummary;
  };
}

export interface ElevationCancelledMessage extends NativeMessageBase {
  readonly type: 'elevationCancelled';
  readonly uiEpoch: string;
  readonly uiSequence: number;
  readonly targetGeneration: string;
  readonly payload: Readonly<Record<string, never>>;
}

export const SHORTCUT_TARGET_SPEEDS = [1, 0.4, 2.5] as const;

export function isShortcutTargetSpeed(value: unknown): value is number {
  return (
    typeof value === 'number' &&
    Number.isFinite(value) &&
    SHORTCUT_TARGET_SPEEDS.some((speed) => Math.abs(value - speed) < 0.005)
  );
}

export interface ShortcutInvokedMessage extends NativeMessageBase {
  readonly type: 'shortcutInvoked';
  readonly payload: {
    readonly action: 'toggleSpeed';
    readonly targetSpeed: number;
  } | { readonly action: 'saveCheckpoint' | 'loadCheckpoint' };
}

export interface CheckpointState {
  readonly available: boolean;
  readonly hasSnapshot: boolean;
  readonly operation: number;
  readonly error: string | null;
}
export interface CheckpointStatusMessage extends NativeMessageBase {
  readonly type: 'checkpointStatus';
  readonly targetGeneration: string;
  readonly payload: CheckpointState;
}

export interface CommandRejectedMessage extends NativeMessageBase {
  readonly type: 'commandRejected';
  readonly error: string;
  readonly uiEpoch?: string;
  readonly uiSequence?: number;
  readonly targetGeneration?: string;
}

export type NativeMessage =
  | CheckpointStatusMessage
  | ProcessListMessage
  | StatusMessage
  | SpeedAppliedMessage
  | TargetExitedMessage
  | TargetInspectionMessage
  | ElevatedTargetReadyMessage
  | ElevationCancelledMessage
  | ShortcutInvokedMessage
  | CommandRejectedMessage;

const identifierPattern = /^[A-Za-z0-9-]{1,64}$/;

function isRecord(value: unknown): value is Record<string, unknown> {
  return typeof value === 'object' && value !== null && !Array.isArray(value);
}

function hasExactKeys(
  value: Record<string, unknown>,
  keys: readonly string[],
): boolean {
  const actual = Object.keys(value);
  return (
    actual.length === keys.length &&
    keys.every((key) => Object.hasOwn(value, key))
  );
}

export function isIdentifier(value: unknown): value is string {
  return typeof value === 'string' && identifierPattern.test(value);
}

export function isPositiveSafeInteger(value: unknown): value is number {
  return Number.isSafeInteger(value) && Number(value) >= 1;
}

export function isValidSpeed(value: unknown): value is number {
  return (
    typeof value === 'number' &&
    Number.isFinite(value) &&
    value >= MINIMUM_SPEED &&
    value <= MAXIMUM_SPEED
  );
}

function isProcessIcon(value: unknown): value is string {
  return (
    typeof value === 'string' &&
    value.length <= 16_384 &&
    (value === '' ||
      /^data:image\/png;base64,[A-Za-z0-9+/]+={0,2}$/.test(value))
  );
}

function isProcessSummary(value: unknown): value is ProcessSummary {
  if (!isRecord(value)) {
    return false;
  }
  return (
    hasExactKeys(value, [
      'processId',
      'creationTime',
      'architecture',
      'name',
      'icon',
    ]) &&
    isPositiveSafeInteger(value.processId) &&
    value.processId <= 4_294_967_295 &&
    typeof value.creationTime === 'string' &&
    /^[0-9]{1,20}$/.test(value.creationTime) &&
    (value.architecture === 'x86' || value.architecture === 'x64') &&
    typeof value.name === 'string' &&
    value.name.length > 0 &&
    value.name.length <= 260 &&
    isProcessIcon(value.icon)
  );
}

function hasValidCommonResponse(value: Record<string, unknown>): boolean {
  return (
    value.v === WEBVIEW_PROTOCOL_VERSION &&
    typeof value.type === 'string' &&
    isIdentifier(value.controllerSessionId)
  );
}

function hasValidRequestIdentity(value: Record<string, unknown>): boolean {
  return (
    isIdentifier(value.uiEpoch) &&
    isPositiveSafeInteger(value.uiSequence) &&
    isIdentifier(value.targetGeneration)
  );
}

function isAppliedState(value: unknown): boolean {
  if (!isRecord(value)) {
    return false;
  }
  return (
    hasExactKeys(value, [
      'configuredSpeed',
      'paused',
      'commandSequence',
      'clockRevision',
      'targetRuntime',
      'timeBackend',
      'effectState',
    ]) &&
    isValidSpeed(value.configuredSpeed) &&
    typeof value.paused === 'boolean' &&
    Number.isSafeInteger(value.commandSequence) &&
    Number(value.commandSequence) >= 0 &&
    Number.isSafeInteger(value.clockRevision) &&
    Number(value.clockRevision) >= 0 &&
    isTargetRuntime(value.targetRuntime) &&
    isTimeBackend(value.timeBackend) &&
    isEffectState(value.effectState)
  );
}

function isTargetRuntime(value: unknown): value is TargetRuntime {
  return value === 'generic' || value === 'unityMono';
}

function isTimeBackend(value: unknown): value is TimeBackend {
  return value === 'win32Clock' || value === 'unityMono';
}

function isEffectState(value: unknown): value is EffectState {
  return (
    value === 'unverified' ||
    value === 'pending' ||
    value === 'applied' ||
    value === 'degraded' ||
    value === 'failed'
  );
}

export function parseNativeMessage(value: unknown): NativeMessage | null {
  if (!isRecord(value) || !hasValidCommonResponse(value)) {
    return null;
  }

  if (value.type === 'processList') {
    if (!hasValidRequestIdentity(value) || !isRecord(value.payload)) {
      return null;
    }
    const processes = value.payload.processes;
    if (
      !Array.isArray(processes) ||
      processes.length > 10_000 ||
      !processes.every(isProcessSummary)
    ) {
      return null;
    }
    return value as unknown as ProcessListMessage;
  }

  if (value.type === 'status' || value.type === 'targetSelected') {
    if (!hasValidRequestIdentity(value) || !isRecord(value.payload)) {
      return null;
    }
    const target = value.payload.target;
    const targetIsValid =
      target === null ||
      (isRecord(target) &&
        isPositiveSafeInteger(target.processId) &&
        target.processId <= 4_294_967_295 &&
        typeof target.creationTime === 'string' &&
        /^[0-9]{1,20}$/.test(target.creationTime));
    if (
      !hasExactKeys(value.payload, ['status', 'target', 'applied']) ||
      typeof value.payload.status !== 'string' ||
      !targetIsValid ||
      (value.payload.applied !== null &&
        !isAppliedState(value.payload.applied))
    ) {
      return null;
    }
    return value as unknown as StatusMessage;
  }

  if (value.type === 'speedApplied') {
    if (
      !hasValidRequestIdentity(value) ||
      !isPositiveSafeInteger(value.commandSequence) ||
      !isPositiveSafeInteger(value.clockRevision) ||
      !isRecord(value.payload) ||
      !hasExactKeys(value.payload, [
        'configuredSpeed',
        'paused',
        'effectiveSpeed',
        'targetRuntime',
        'timeBackend',
        'effectState',
      ]) ||
      !isValidSpeed(value.payload.configuredSpeed) ||
      typeof value.payload.paused !== 'boolean' ||
      typeof value.payload.effectiveSpeed !== 'number' ||
      !Number.isFinite(value.payload.effectiveSpeed) ||
      (value.payload.paused
        ? value.payload.effectiveSpeed !== 0
        : value.payload.effectiveSpeed !== value.payload.configuredSpeed) ||
      !isTargetRuntime(value.payload.targetRuntime) ||
      !isTimeBackend(value.payload.timeBackend) ||
      !isEffectState(value.payload.effectState)
    ) {
      return null;
    }
    return value as unknown as SpeedAppliedMessage;
  }

  if (value.type === 'targetExited') {
    if (
      !isIdentifier(value.targetGeneration) ||
      !isRecord(value.payload) ||
      Object.keys(value.payload).length !== 0
    ) {
      return null;
    }
    return value as unknown as TargetExitedMessage;
  }

  if (value.type === 'targetInspection') {
    if (
      !hasValidRequestIdentity(value) ||
      !isRecord(value.payload) ||
      !hasExactKeys(value.payload, ['result', 'error', 'process']) ||
      (value.payload.result !== 'ready' &&
        value.payload.result !== 'elevationRequired' &&
        value.payload.result !== 'rejected') ||
      (value.payload.error !== null &&
        (typeof value.payload.error !== 'string' ||
          value.payload.error.length > 128)) ||
      !isProcessSummary(value.payload.process)
    ) {
      return null;
    }
    return value as unknown as TargetInspectionMessage;
  }

  if (value.type === 'elevatedTargetReady' || value.type === 'programLaunched') {
    if (
      !hasValidRequestIdentity(value) ||
      !isRecord(value.payload) ||
      !hasExactKeys(value.payload, ['process']) ||
      !isProcessSummary(value.payload.process)
    ) {
      return null;
    }
    return value as unknown as ElevatedTargetReadyMessage;
  }

  if (value.type === 'elevationCancelled') {
    if (
      !hasValidRequestIdentity(value) ||
      !isRecord(value.payload) ||
      Object.keys(value.payload).length !== 0
    ) {
      return null;
    }
    return value as unknown as ElevationCancelledMessage;
  }

  if (value.type === 'checkpointStatus') {
    if (!hasValidRequestIdentity(value) || !isRecord(value.payload) ||
        !hasExactKeys(value.payload, ['available', 'hasSnapshot', 'operation', 'error']) ||
        typeof value.payload.available !== 'boolean' || typeof value.payload.hasSnapshot !== 'boolean' ||
        ![0, 1, 2].includes(Number(value.payload.operation)) ||
        (value.payload.error !== null && typeof value.payload.error !== 'string')) return null;
    return value as unknown as CheckpointStatusMessage;
  }
  if (value.type === 'shortcutInvoked') {
    if (isRecord(value.payload) && hasExactKeys(value.payload, ['action']) &&
        (value.payload.action === 'saveCheckpoint' || value.payload.action === 'loadCheckpoint'))
      return value as unknown as ShortcutInvokedMessage;
    if (
      !isRecord(value.payload) ||
      !hasExactKeys(value.payload, ['action', 'targetSpeed']) ||
      value.payload.action !== 'toggleSpeed' ||
      !isShortcutTargetSpeed(value.payload.targetSpeed)
    ) {
      return null;
    }
    return value as unknown as ShortcutInvokedMessage;
  }

  if (value.type === 'commandRejected') {
    if (
      typeof value.error !== 'string' ||
      value.error.length > 128 ||
      (value.uiEpoch !== undefined && !isIdentifier(value.uiEpoch)) ||
      (value.uiSequence !== undefined &&
        !isPositiveSafeInteger(value.uiSequence)) ||
      (value.targetGeneration !== undefined &&
        !isIdentifier(value.targetGeneration))
    ) {
      return null;
    }
    return value as unknown as CommandRejectedMessage;
  }

  return null;
}

export function createCommand(
  type: UiCommandType,
  uiEpoch: string,
  uiSequence: number,
  targetGeneration: string,
  payload: UiCommand['payload'] = {},
): UiCommand {
  if (!isPositiveSafeInteger(uiSequence)) {
    throw new RangeError('UI sequence must be a positive safe integer.');
  }
  if (!isIdentifier(uiEpoch) || !isIdentifier(targetGeneration)) {
    throw new TypeError('UI epoch and target generation must be valid identifiers.');
  }
  if (
    type === 'setSpeed' &&
    (Object.keys(payload).length !== 1 ||
      !('speed' in payload) ||
      !isValidSpeed(payload.speed))
  ) {
    throw new RangeError('Speed must be finite and within [0.05, 8.0].');
  }
  if (type === 'selectTarget') {
    if (
      Object.keys(payload).length !== 2 ||
      !('processId' in payload) ||
      !isPositiveSafeInteger(payload.processId) ||
      payload.processId > 4_294_967_295 ||
      !('creationTime' in payload) ||
      typeof payload.creationTime !== 'string' ||
      !/^[0-9]{1,20}$/.test(payload.creationTime)
    ) {
      throw new RangeError('Target identity is invalid.');
    }
  } else if (
    type !== 'setSpeed' &&
    Object.keys(payload).length !== 0
  ) {
    throw new TypeError('This command requires an empty payload.');
  }
  return {
    v: WEBVIEW_PROTOCOL_VERSION,
    type,
    uiEpoch,
    uiSequence,
    targetGeneration,
    payload,
  };
}

export function createSetSpeedCommand(
  uiEpoch: string,
  uiSequence: number,
  targetGeneration: string,
  speed: number,
): UiCommand {
  return createCommand(
    'setSpeed',
    uiEpoch,
    uiSequence,
    targetGeneration,
    { speed },
  );
}
