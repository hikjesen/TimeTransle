import { describe, expect, it } from 'vitest';

import {
  createCommand,
  createSetSpeedCommand,
  INITIAL_SPEED,
  isValidSpeed,
  MAXIMUM_SPEED,
  MINIMUM_SPEED,
} from './contracts';

describe('WebView V1 speed contract', () => {
  it.each([MINIMUM_SPEED, 0.5, INITIAL_SPEED, 2, MAXIMUM_SPEED])(
    'accepts supported speed %s',
    (speed) => {
      expect(isValidSpeed(speed)).toBe(true);
    },
  );

  it.each([0, -1, 8.000001, Number.NaN, Number.POSITIVE_INFINITY, '1'])(
    'rejects unsupported speed %s',
    (speed) => {
      expect(isValidSpeed(speed)).toBe(false);
    },
  );

  it('creates a versioned command without privileged native details', () => {
    expect(createSetSpeedCommand('ui-epoch', 7, 'target-generation', 0.5)).toEqual({
      v: 1,
      type: 'setSpeed',
      uiEpoch: 'ui-epoch',
      uiSequence: 7,
      targetGeneration: 'target-generation',
      payload: {
        speed: 0.5,
      },
    });
  });

  it('rejects invalid sequence and identity fields', () => {
    expect(() => createSetSpeedCommand('ui', 0, 'target', 1)).toThrow(RangeError);
    expect(() => createSetSpeedCommand('', 1, 'target', 1)).toThrow(TypeError);
  });

  it('keeps privileged details out of every command payload', () => {
    expect(() =>
      createCommand('listProcesses', 'ui', 1, 'target', {
        dllPath: 'C:\\bad.dll',
      } as never),
    ).toThrow(TypeError);
    expect(() =>
      createCommand('selectTarget', 'ui', 2, 'target', {
        processId: 42,
        creationTime: '123',
      }),
    ).not.toThrow();
  });
});
