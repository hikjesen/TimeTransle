import { describe, expect, it } from 'vitest';

import {
  controllerReducer,
  createInitialControllerState,
} from './controllerState';

describe('controller reducer authority rules', () => {
  it('keeps desired state separate from applied state', () => {
    const initial = createInitialControllerState('target-1');
    const desired = controllerReducer(initial, {
      type: 'setDesiredSpeed',
      speed: 0.5,
      inFlight: true,
    });

    expect(desired.desiredSpeed).toBe(0.5);
    expect(desired.appliedSpeed).toBeNull();
    expect(desired.apply).toBe('inFlight');
  });

  it('ignores stale revisions and the wrong target generation', () => {
    const initial = {
      ...createInitialControllerState('target-1'),
      controllerSessionId: 'controller-1',
      lastAcceptedCommandSequence: 7,
      lastClockRevision: 5,
    };
    const stale = controllerReducer(initial, {
      type: 'speedApplied',
      message: {
        v: 1,
        type: 'speedApplied',
        controllerSessionId: 'controller-1',
        uiEpoch: 'ui-1',
        uiSequence: 8,
        targetGeneration: 'target-1',
        commandSequence: 7,
        clockRevision: 6,
        payload: {
          configuredSpeed: 2,
          effectiveSpeed: 2,
          paused: false,
          targetRuntime: 'unityMono',
          timeBackend: 'unityMono',
          effectState: 'applied',
        },
      },
    });
    const wrongTarget = controllerReducer(initial, {
      type: 'targetExited',
      targetGeneration: 'target-old',
    });

    expect(stale).toBe(initial);
    expect(wrongTarget).toBe(initial);
  });

  it('tracks Unity pending, stalled, and applied states for one revision', () => {
    const initial = {
      ...createInitialControllerState('target-1'),
      controllerSessionId: 'controller-1',
      target: 'hooked' as const,
      desiredSpeed: 2,
    };
    const pending = controllerReducer(initial, {
      type: 'speedApplied',
      message: {
        v: 1,
        type: 'speedApplied',
        controllerSessionId: 'controller-1',
        uiEpoch: 'ui-1',
        uiSequence: 1,
        targetGeneration: 'target-1',
        commandSequence: 1,
        clockRevision: 1,
        payload: {
          configuredSpeed: 2,
          effectiveSpeed: 2,
          paused: false,
          targetRuntime: 'unityMono',
          timeBackend: 'unityMono',
          effectState: 'pending',
        },
      },
    });
    const stalled = controllerReducer(pending, { type: 'markStalled' });
    const applied = controllerReducer(stalled, {
      type: 'speedApplied',
      message: {
        v: 1,
        type: 'speedApplied',
        controllerSessionId: 'controller-1',
        uiEpoch: 'ui-1',
        uiSequence: 1,
        targetGeneration: 'target-1',
        commandSequence: 1,
        clockRevision: 1,
        payload: {
          configuredSpeed: 2,
          effectiveSpeed: 2,
          paused: false,
          targetRuntime: 'unityMono',
          timeBackend: 'unityMono',
          effectState: 'applied',
        },
      },
    });

    expect(pending.apply).toBe('inFlight');
    expect(stalled.apply).toBe('stalled');
    expect(applied.apply).toBe('synced');
  });

  it('marks a successful inspection ready for automatic connection', () => {
    const selected = controllerReducer(
      createInitialControllerState('initial-target'),
      {
        type: 'targetRequested',
        targetGeneration: 'target-1',
        process: {
          processId: 42,
          creationTime: '123456',
          architecture: 'x64',
          name: 'OrdinaryProgram.exe',
          icon: '',
        },
      },
    );
    const ready = controllerReducer(selected, {
      type: 'targetInspection',
      errorText: null,
      message: {
        v: 1,
        type: 'targetInspection',
        controllerSessionId: 'controller-1',
        uiEpoch: 'ui-1',
        uiSequence: 2,
        targetGeneration: 'target-1',
        payload: {
          result: 'ready',
          error: null,
          process: selected.selectedProcess!,
        },
      },
    });

    expect(selected.target).toBe('selected');
    expect(ready.target).toBe('ready');
    expect(ready.lastErrorCode).toBeNull();
  });

  it('offers elevation only for the integrity mismatch result', () => {
    const selected = {
      ...createInitialControllerState('target-1'),
      selectedProcess: {
        processId: 42,
        creationTime: '123456',
        architecture: 'x64' as const,
        name: 'ElevatedProgram.exe',
        icon: '',
      },
    };
    const elevation = controllerReducer(selected, {
      type: 'targetInspection',
      errorText: null,
      message: {
        v: 1,
        type: 'targetInspection',
        controllerSessionId: 'controller-1',
        uiEpoch: 'ui-1',
        uiSequence: 2,
        targetGeneration: 'target-1',
        payload: {
          result: 'elevationRequired',
          error: 'INTEGRITY_MISMATCH',
          process: selected.selectedProcess,
        },
      },
    });

    expect(elevation.target).toBe('elevationRequired');
    expect(elevation.lastErrorCode).toBe('INTEGRITY_MISMATCH');
  });

  it('toggles to a special speed and restores the re-recorded checkpoint', () => {
    const hooked = {
      ...createInitialControllerState('target-1'),
      target: 'hooked' as const,
      desiredSpeed: 2,
      desiredPaused: false,
      appliedSpeed: 2,
      appliedPaused: false,
    };

    const atPointFour = controllerReducer(hooked, {
      type: 'shortcutToggleSpeed',
      targetSpeed: 0.4,
    });
    expect(atPointFour.desiredSpeed).toBe(0.4);
    expect(atPointFour.desiredPaused).toBe(false);
    expect(atPointFour.speedCheckpoint).toBe(2);
    expect(atPointFour.apply).toBe('inFlight');

    const restored = controllerReducer(atPointFour, {
      type: 'shortcutToggleSpeed',
      targetSpeed: 0.4,
    });
    expect(restored.desiredSpeed).toBe(2);
    expect(restored.speedCheckpoint).toBeNull();
    expect(restored.apply).toBe('inFlight');

    const reactivated = controllerReducer(restored, {
      type: 'shortcutToggleSpeed',
      targetSpeed: 0.4,
    });
    expect(reactivated.desiredSpeed).toBe(0.4);
    expect(reactivated.speedCheckpoint).toBe(2);
  });

  it('discards the previous checkpoint when a different special speed is activated', () => {
    const hooked = {
      ...createInitialControllerState('target-1'),
      target: 'hooked' as const,
      desiredSpeed: 1.5,
      desiredPaused: false,
    };

    const atTwoPointFive = controllerReducer(hooked, {
      type: 'shortcutToggleSpeed',
      targetSpeed: 2.5,
    });
    expect(atTwoPointFive.desiredSpeed).toBe(2.5);
    expect(atTwoPointFive.speedCheckpoint).toBe(1.5);

    const atOne = controllerReducer(atTwoPointFive, {
      type: 'shortcutToggleSpeed',
      targetSpeed: 1,
    });
    expect(atOne.desiredSpeed).toBe(1);
    expect(atOne.speedCheckpoint).toBe(2.5);

    const restored = controllerReducer(atOne, {
      type: 'shortcutToggleSpeed',
      targetSpeed: 1,
    });
    expect(restored.desiredSpeed).toBe(2.5);
    expect(restored.speedCheckpoint).toBeNull();
  });

  it('unpauses when a shortcut activates or restores a speed', () => {
    const paused = {
      ...createInitialControllerState('target-1'),
      target: 'hooked' as const,
      desiredSpeed: 2,
      desiredPaused: true,
    };
    const activated = controllerReducer(paused, {
      type: 'shortcutToggleSpeed',
      targetSpeed: 1,
    });
    expect(activated.desiredPaused).toBe(false);
    expect(activated.speedCheckpoint).toBe(2);

    const pausedAtOne = { ...activated, desiredSpeed: 1, desiredPaused: true };
    const restored = controllerReducer(pausedAtOne, {
      type: 'shortcutToggleSpeed',
      targetSpeed: 1,
    });
    expect(restored.desiredPaused).toBe(false);
    expect(restored.desiredSpeed).toBe(2);
  });

  it('no-ops at a natural special speed and while no target is hooked', () => {
    const hookedAtPointFour = {
      ...createInitialControllerState('target-1'),
      target: 'hooked' as const,
      desiredSpeed: 0.4,
      desiredPaused: false,
    };
    expect(
      controllerReducer(hookedAtPointFour, {
        type: 'shortcutToggleSpeed',
        targetSpeed: 0.4,
      }),
    ).toBe(hookedAtPointFour);

    const idle = createInitialControllerState('target-1');
    expect(
      controllerReducer(idle, { type: 'shortcutToggleSpeed', targetSpeed: 1 }),
    ).toBe(idle);
  });

  it('cancels the checkpoint on manual speed input', () => {
    const temporarilySpecial = {
      ...createInitialControllerState('target-1'),
      target: 'hooked' as const,
      desiredSpeed: 2.5,
      desiredPaused: false,
      speedCheckpoint: 1.5,
    };
    const manuallyChanged = controllerReducer(temporarilySpecial, {
      type: 'setDesiredSpeed',
      speed: 4,
      inFlight: true,
    });
    expect(manuallyChanged.speedCheckpoint).toBeNull();
  });

  it('clears the shortcut checkpoint when the hook disconnects', () => {
    const temporarilySpecial = {
      ...createInitialControllerState('target-1'),
      target: 'hooked' as const,
      controllerSessionId: 'controller-1',
      desiredSpeed: 1,
      speedCheckpoint: 2,
    };
    const disconnected = controllerReducer(temporarilySpecial, {
      type: 'statusReceived',
      message: {
        v: 1,
        type: 'status',
        controllerSessionId: 'controller-1',
        uiEpoch: 'ui-1',
        uiSequence: 4,
        targetGeneration: 'target-1',
        payload: {
          status: 'hookUnknown',
          target: {
            processId: 42,
            creationTime: '123456',
          },
          applied: null,
        },
      },
    });

    expect(disconnected.speedCheckpoint).toBeNull();
  });
});
