import {
  INITIAL_SPEED,
  type CheckpointState,
  type ProcessSummary,
  type SpeedAppliedMessage,
  type StatusMessage,
  type TargetInspectionMessage,
  type TargetRuntime,
  type TimeBackend,
  type EffectState,
} from '../bridge/contracts';

export type ControllerStatus =
  | 'booting'
  | 'ready'
  | 'disconnected'
  | 'error';
export type TargetStatus =
  | 'none'
  | 'selected'
  | 'checking'
  | 'ready'
  | 'elevationRequired'
  | 'attaching'
  | 'hooked'
  | 'exited'
  | 'error';
export type ApplyStatus =
  | 'unknown'
  | 'synced'
  | 'unverified'
  | 'inFlight'
  | 'stalled'
  | 'degraded'
  | 'failed'
  | 'rejected';

const emptyCheckpoint: CheckpointState = { available: false, hasSnapshot: false, operation: 0, error: null };

export interface ControllerState {
  readonly checkpoint: CheckpointState;
  readonly controller: ControllerStatus;
  readonly target: TargetStatus;
  readonly apply: ApplyStatus;
  readonly desiredSpeed: number;
  readonly appliedSpeed: number | null;
  readonly desiredPaused: boolean;
  readonly appliedPaused: boolean | null;
  readonly targetRuntime: TargetRuntime;
  readonly timeBackend: TimeBackend;
  readonly effectState: EffectState | null;
  readonly controllerSessionId: string | null;
  readonly targetGeneration: string;
  readonly selectedProcess: ProcessSummary | null;
  readonly processes: readonly ProcessSummary[];
  readonly lastAcceptedCommandSequence: number;
  readonly lastClockRevision: number;
  readonly lastErrorCode: string | null;
  readonly lastError: string | null;
  readonly notice: string | null;
  readonly speedCheckpoint: number | null;
}

export type ControllerAction =
  | { readonly type: 'checkpointReceived'; readonly checkpoint: CheckpointState }
  | { readonly type: 'checkpointRequested'; readonly operation: number }
  | { readonly type: 'bridgeReady' }
  | { readonly type: 'bridgeUnavailable' }
  | {
      readonly type: 'setDesiredSpeed';
      readonly speed: number;
      readonly inFlight: boolean;
    }
  | { readonly type: 'setDesiredPaused'; readonly paused: boolean }
  | { readonly type: 'processesReceived'; readonly processes: readonly ProcessSummary[] }
  | {
      readonly type: 'targetRequested';
      readonly process: ProcessSummary;
      readonly targetGeneration: string;
    }
  | {
      readonly type: 'statusReceived';
      readonly message: StatusMessage;
    }
  | {
      readonly type: 'targetInspection';
      readonly message: TargetInspectionMessage;
      readonly errorText: string | null;
    }
  | {
      readonly type: 'speedApplied';
      readonly message: SpeedAppliedMessage;
    }
  | { readonly type: 'targetExited'; readonly targetGeneration: string }
  | {
      readonly type: 'commandRejected';
      readonly errorCode: string;
      readonly error: string;
    }
  | { readonly type: 'elevationCancelled' }
  | { readonly type: 'shortcutToggleSpeed'; readonly targetSpeed: number }
  | { readonly type: 'markStalled' }
  | { readonly type: 'clearError' };

export function createInitialControllerState(
  initialTargetGeneration: string,
): ControllerState {
  return {
    checkpoint: emptyCheckpoint,
    controller: 'booting',
    target: 'none',
    apply: 'unknown',
    desiredSpeed: INITIAL_SPEED,
    appliedSpeed: null,
    desiredPaused: false,
    appliedPaused: null,
    targetRuntime: 'generic',
    timeBackend: 'win32Clock',
    effectState: null,
    controllerSessionId: null,
    targetGeneration: initialTargetGeneration,
    selectedProcess: null,
    processes: [],
    lastAcceptedCommandSequence: 0,
    lastClockRevision: 0,
    lastErrorCode: null,
    lastError: null,
    notice: null,
    speedCheckpoint: null,
  };
}

function sessionMatches(
  state: ControllerState,
  controllerSessionId: string,
): boolean {
  return (
    state.controllerSessionId === null ||
    state.controllerSessionId === controllerSessionId
  );
}

function applyStatus(
  effectState: EffectState,
  valuesMatch: boolean,
): ApplyStatus {
  if (!valuesMatch) {
    return 'inFlight';
  }
  switch (effectState) {
    case 'applied':
      return 'synced';
    case 'unverified':
      return 'unverified';
    case 'pending':
      return 'inFlight';
    case 'degraded':
      return 'degraded';
    case 'failed':
      return 'failed';
  }
}

export function controllerReducer(
  state: ControllerState,
  action: ControllerAction,
): ControllerState {
  switch (action.type) {
    case 'checkpointReceived': return { ...state, checkpoint: action.checkpoint };
    case 'checkpointRequested': return { ...state, checkpoint: { ...state.checkpoint, operation: action.operation, error: null } };
    case 'bridgeReady':
      return {
        ...state,
        controller: 'ready',
        lastErrorCode: null,
        lastError: null,
      };
    case 'bridgeUnavailable':
      return {
        ...state,
        checkpoint: emptyCheckpoint,
        controller: 'disconnected',
        lastErrorCode: 'BRIDGE_UNAVAILABLE',
        lastError: '请通过 TimeTransle Controller 打开此界面。',
      };
    case 'setDesiredSpeed':
      return {
        ...state,
        desiredSpeed: action.speed,
        apply: action.inFlight ? 'inFlight' : state.apply,
        speedCheckpoint: null,
      };
    case 'setDesiredPaused':
      return {
        ...state,
        desiredPaused: action.paused,
        apply: state.target === 'hooked' ? 'inFlight' : state.apply,
      };
    case 'processesReceived':
      return {
        ...state,
        processes: action.processes,
        controller: 'ready',
      };
    case 'targetRequested':
      return {
        ...state,
        checkpoint: emptyCheckpoint,
        target: 'selected',
        apply: 'unknown',
        appliedSpeed: null,
        appliedPaused: null,
        targetRuntime: 'generic',
        timeBackend: 'win32Clock',
        effectState: null,
        selectedProcess: action.process,
        targetGeneration: action.targetGeneration,
        lastErrorCode: null,
        lastError: null,
        notice: null,
        speedCheckpoint: null,
      };
    case 'statusReceived': {
      const { message } = action;
      if (
        !sessionMatches(state, message.controllerSessionId) ||
        (state.selectedProcess !== null &&
          message.targetGeneration !== state.targetGeneration)
      ) {
        return state;
      }
      const applied = message.payload.applied;
      return {
        ...state,
        checkpoint: message.payload.status === 'hookedSynced' ? state.checkpoint : emptyCheckpoint,
        controller: 'ready',
        controllerSessionId: message.controllerSessionId,
        target:
          message.payload.status === 'hookedSynced'
            ? 'hooked'
            : message.payload.status === 'checking'
              ? 'checking'
            : message.payload.status === 'ready'
              ? 'ready'
            : message.payload.status === 'elevationRequired'
              ? 'elevationRequired'
            : message.payload.status === 'attaching' ||
                message.payload.status === 'hookedSyncing'
              ? 'attaching'
            : message.payload.target
              ? 'selected'
              : 'none',
        apply: applied
          ? applyStatus(
              applied.effectState,
              applied.configuredSpeed === state.desiredSpeed &&
                applied.paused === state.desiredPaused,
            )
          : 'unknown',
        appliedSpeed: applied?.configuredSpeed ?? null,
        appliedPaused: applied?.paused ?? null,
        targetRuntime: applied?.targetRuntime ?? state.targetRuntime,
        timeBackend: applied?.timeBackend ?? state.timeBackend,
        effectState: applied?.effectState ?? null,
        lastAcceptedCommandSequence:
          applied?.commandSequence ?? state.lastAcceptedCommandSequence,
        lastClockRevision:
          applied?.clockRevision ?? state.lastClockRevision,
        speedCheckpoint:
          message.payload.status === 'hookedSynced'
            ? state.speedCheckpoint
            : null,
      };
    }
    case 'targetInspection': {
      const { message } = action;
      if (
        !sessionMatches(state, message.controllerSessionId) ||
        message.targetGeneration !== state.targetGeneration ||
        state.selectedProcess === null ||
        state.selectedProcess.processId !==
          message.payload.process.processId ||
        state.selectedProcess.creationTime !==
          message.payload.process.creationTime
      ) {
        return state;
      }
      if (message.payload.result === 'ready') {
        return {
          ...state,
          target: 'ready',
          controllerSessionId: message.controllerSessionId,
          lastErrorCode: null,
          lastError: null,
          notice: null,
        };
      }
      if (message.payload.result === 'elevationRequired') {
        return {
          ...state,
          target: 'elevationRequired',
          controllerSessionId: message.controllerSessionId,
          lastErrorCode: message.payload.error,
          lastError: null,
          notice: null,
        };
      }
      return {
        ...state,
        target: 'error',
        controllerSessionId: message.controllerSessionId,
        lastErrorCode: message.payload.error,
        lastError: action.errorText,
        notice: null,
        speedCheckpoint: null,
      };
    }
    case 'speedApplied': {
      const { message } = action;
      if (
        !sessionMatches(state, message.controllerSessionId) ||
        message.targetGeneration !== state.targetGeneration ||
        !(
          (message.commandSequence > state.lastAcceptedCommandSequence &&
            message.clockRevision > state.lastClockRevision) ||
          (message.commandSequence === state.lastAcceptedCommandSequence &&
            message.clockRevision === state.lastClockRevision &&
            message.payload.effectState !== state.effectState)
        )
      ) {
        return state;
      }
      return {
        ...state,
        controllerSessionId: message.controllerSessionId,
        target: 'hooked',
        appliedSpeed: message.payload.configuredSpeed,
        appliedPaused: message.payload.paused,
        targetRuntime: message.payload.targetRuntime,
        timeBackend: message.payload.timeBackend,
        effectState: message.payload.effectState,
        lastAcceptedCommandSequence: message.commandSequence,
        lastClockRevision: message.clockRevision,
        apply: applyStatus(
          message.payload.effectState,
          message.payload.configuredSpeed === state.desiredSpeed &&
            message.payload.paused === state.desiredPaused,
        ),
        lastErrorCode: null,
        lastError: null,
        notice: null,
      };
    }
    case 'targetExited':
      if (action.targetGeneration !== state.targetGeneration) {
        return state;
      }
      return {
        ...state,
        checkpoint: emptyCheckpoint,
        target: 'exited',
        apply: 'unknown',
        appliedSpeed: null,
        appliedPaused: null,
        targetRuntime: 'generic',
        timeBackend: 'win32Clock',
        effectState: null,
        lastErrorCode: state.lastErrorCode ?? 'TARGET_EXITED',
        lastError: state.lastError ?? '目标程序已经退出。',
        notice: null,
        speedCheckpoint: null,
      };
    case 'commandRejected':
      return {
        ...state,
        apply: 'rejected',
        lastErrorCode: action.errorCode,
        lastError: action.error,
        notice: null,
      };
    case 'elevationCancelled':
      return {
        ...state,
        target: 'elevationRequired',
        notice: '已取消管理员权限重启，当前程序仍保持未连接。',
      };
    case 'shortcutToggleSpeed': {
      if (state.target !== 'hooked') {
        return state;
      }
      if (Math.abs(state.desiredSpeed - action.targetSpeed) < 0.005) {
        if (state.speedCheckpoint === null) {
          return state;
        }
        return {
          ...state,
          desiredSpeed: state.speedCheckpoint,
          desiredPaused: false,
          apply: 'inFlight',
          speedCheckpoint: null,
        };
      }
      return {
        ...state,
        desiredSpeed: action.targetSpeed,
        desiredPaused: false,
        apply: 'inFlight',
        speedCheckpoint: state.desiredSpeed,
      };
    }
    case 'markStalled':
      if (
        state.target !== 'hooked' ||
        state.effectState !== 'pending' ||
        state.apply !== 'inFlight'
      ) {
        return state;
      }
      return {
        ...state,
        apply: 'stalled',
      };
    case 'clearError':
      return {
        ...state,
        lastErrorCode: null,
        lastError: null,
        notice: null,
      };
  }
}
