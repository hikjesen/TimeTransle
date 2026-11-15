import { expect, it } from 'vitest';
import { createCommand, parseNativeMessage } from './contracts';
import { controllerReducer, createInitialControllerState } from '../state/controllerState';

it('shares empty checkpoint commands and parses checkpoint status and shortcuts', () => {
  for (const type of ['launchProgram', 'saveCheckpoint', 'loadCheckpoint'] as const)
    expect(createCommand(type, 'ui-test', 1, 'target-test').payload).toEqual({});
  const message = {v: 1, controllerSessionId: 'controller-test', uiEpoch: 'ui-test', uiSequence: 2,
    targetGeneration: 'target-test', type: 'checkpointStatus',
    payload: {available: true, hasSnapshot: true, operation: 0, error: null}};
  expect(parseNativeMessage(message)).toEqual(message);
  expect(parseNativeMessage({...message, payload: {...message.payload, operation: 3}})).toBeNull();
  expect(parseNativeMessage({v: 1, controllerSessionId: 'controller-test', type: 'shortcutInvoked',
    payload: {action: 'loadCheckpoint'}})?.type).toBe('shortcutInvoked');
});

it('keeps a slot across operations and clears it when switching or losing a target', () => {
  const state = controllerReducer(createInitialControllerState('target-test'), {
    type: 'checkpointReceived', checkpoint: {available: true, hasSnapshot: true, operation: 0, error: null}});
  const busy = controllerReducer(state, {type: 'checkpointRequested', operation: 2});
  expect(busy.checkpoint).toEqual({...state.checkpoint, operation: 2});
  const failed = controllerReducer(busy, {type: 'checkpointReceived', checkpoint: {...state.checkpoint, error: 'CHECKPOINT_RESTORE_FAILED'}});
  expect(failed.checkpoint.hasSnapshot).toBe(true);
  expect(failed.checkpoint.operation).toBe(0);
  expect(controllerReducer(state, {type: 'bridgeUnavailable'}).checkpoint.hasSnapshot).toBe(false);
  expect(controllerReducer(state, {type: 'targetRequested', targetGeneration: 'target-next', process: {
    processId: 12, creationTime: '1', architecture: 'x86', name: 'fixture.exe', icon: ''}}).checkpoint.hasSnapshot).toBe(false);
});
