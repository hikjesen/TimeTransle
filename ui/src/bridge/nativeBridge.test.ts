import { describe, expect, it } from 'vitest';

import { parseNativeMessage } from './contracts';

describe('native bridge response validation', () => {
  it('accepts a bounded process list without privileged details', () => {
    expect(
      parseNativeMessage({
        v: 1,
        type: 'processList',
        controllerSessionId: 'controller-1',
        uiEpoch: 'ui-1',
        uiSequence: 2,
        targetGeneration: 'target-1',
        payload: {
          processes: [
            {
              processId: 42,
              creationTime: '123456',
              architecture: 'x64',
              name: 'game.exe',
              icon: 'data:image/png;base64,iVBORw0KGgo=',
            },
          ],
        },
      }),
    ).not.toBeNull();
  });

  it('rejects unknown response types and invalid applied speeds', () => {
    expect(
      parseNativeMessage({
        v: 1,
        type: 'remoteHandle',
        controllerSessionId: 'controller-1',
      }),
    ).toBeNull();
    expect(
      parseNativeMessage({
        v: 1,
        type: 'speedApplied',
        controllerSessionId: 'controller-1',
        uiEpoch: 'ui-1',
        uiSequence: 2,
        targetGeneration: 'target-1',
        commandSequence: 3,
        clockRevision: 4,
        payload: {
          configuredSpeed: Number.NaN,
          effectiveSpeed: 1,
          paused: false,
          targetRuntime: 'unityMono',
          timeBackend: 'unityMono',
          effectState: 'applied',
        },
      }),
    ).toBeNull();
  });

  it('accepts a paused Unity state with zero effective speed', () => {
    expect(
      parseNativeMessage({
        v: 1,
        type: 'speedApplied',
        controllerSessionId: 'controller-1',
        uiEpoch: 'ui-1',
        uiSequence: 2,
        targetGeneration: 'target-1',
        commandSequence: 3,
        clockRevision: 4,
        payload: {
          configuredSpeed: 2,
          effectiveSpeed: 0,
          paused: true,
          targetRuntime: 'unityMono',
          timeBackend: 'unityMono',
          effectState: 'applied',
        },
      }),
    ).not.toBeNull();
  });

  it('accepts a minimal target inspection without module or path evidence', () => {
    const message = parseNativeMessage({
      v: 1,
      type: 'targetInspection',
      controllerSessionId: 'controller-1',
      uiEpoch: 'ui-1',
      uiSequence: 3,
      targetGeneration: 'target-1',
      payload: {
        result: 'ready',
        error: null,
        process: {
          processId: 42,
          creationTime: '123456',
          architecture: 'x64',
          name: 'AntiCheatFixture.exe',
          icon: '',
        },
      },
    });

    expect(message?.type).toBe('targetInspection');
    expect(
      parseNativeMessage({
        ...(message as object),
        payload: {
          ...(message as { payload: object }).payload,
          modules: ['AntiCheatFixture.dll'],
        },
      }),
    ).toBeNull();
  });

  it('accepts the speed toggle shortcut and its exact payload shape', () => {
    for (const targetSpeed of [1, 0.4, 2.5]) {
      expect(
        parseNativeMessage({
          v: 1,
          type: 'shortcutInvoked',
          controllerSessionId: 'controller-1',
          payload: { action: 'toggleSpeed', targetSpeed },
        }),
      ).not.toBeNull();
    }

    expect(
      parseNativeMessage({
        v: 1,
        type: 'shortcutInvoked',
        controllerSessionId: 'controller-1',
        payload: { action: 'toggleSpeed', targetSpeed: 3 },
      }),
    ).toBeNull();
    expect(
      parseNativeMessage({
        v: 1,
        type: 'shortcutInvoked',
        controllerSessionId: 'controller-1',
        payload: { action: 'setOne' },
      }),
    ).toBeNull();
    expect(
      parseNativeMessage({
        v: 1,
        type: 'shortcutInvoked',
        controllerSessionId: 'controller-1',
        payload: { action: 'toggleSpeed' },
      }),
    ).toBeNull();
    expect(
      parseNativeMessage({
        v: 1,
        type: 'shortcutInvoked',
        controllerSessionId: 'controller-1',
        payload: { action: 'toggleSpeed', targetSpeed: 1, extra: true },
      }),
    ).toBeNull();
  });

  it('rejects process summaries with a missing or malformed icon', () => {
    const base = {
      v: 1,
      type: 'processList',
      controllerSessionId: 'controller-1',
      uiEpoch: 'ui-1',
      uiSequence: 2,
      targetGeneration: 'target-1',
    };
    expect(
      parseNativeMessage({
        ...base,
        payload: {
          processes: [
            {
              processId: 42,
              creationTime: '123456',
              architecture: 'x64',
              name: 'game.exe',
            },
          ],
        },
      }),
    ).toBeNull();
    expect(
      parseNativeMessage({
        ...base,
        payload: {
          processes: [
            {
              processId: 42,
              creationTime: '123456',
              architecture: 'x64',
              name: 'game.exe',
              icon: 'https://app.timetransle.local/icon.png',
            },
          ],
        },
      }),
    ).toBeNull();
    expect(
      parseNativeMessage({
        ...base,
        payload: {
          processes: [
            {
              processId: 42,
              creationTime: '123456',
              architecture: 'x64',
              name: 'game.exe',
              icon: '',
            },
          ],
        },
      }),
    ).not.toBeNull();
  });
});
