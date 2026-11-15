import { describe, expect, it } from 'vitest';

import {
  DEGREES_PER_OCTAVE,
  MAXIMUM_ANGLE,
  MINIMUM_ANGLE,
  SNAP_ENTER_THRESHOLD_DEGREES,
  SNAP_EXIT_THRESHOLD_DEGREES,
  SNAP_SPEEDS,
  TICK_SPEEDS,
  angleToSpeed,
  nearestSnapSpeed,
  resolveDragAngle,
  resolveSnap,
  speedToAngle,
} from './speedMapping';

describe('uniform logarithmic speed mapping', () => {
  it.each([
    [0.0625, MINIMUM_ANGLE],
    [1, 0],
    [8, MAXIMUM_ANGLE],
  ])('maps %s× to %s°', (speed, angle) => {
    expect(speedToAngle(speed)).toBeCloseTo(angle, 10);
    expect(angleToSpeed(angle)).toBeCloseTo(speed, 10);
  });

  it.each([
    [-180, 0.0625],
    [-135, 0.125],
    [-90, 0.25],
    [-45, 0.5],
    [0, 1],
    [45, 2],
    [90, 4],
    [135, 8],
  ])('places the %s° octave checkpoint at %s×', (angle, speed) => {
    expect(angleToSpeed(angle)).toBeCloseTo(speed, 10);
    expect(speedToAngle(speed)).toBeCloseTo(angle, 10);
  });

  // Regression lock for the slope discontinuity the old segmented mapping had at
  // 1×: doubling the speed must always cost exactly one octave of arc.
  it.each([0.0625, 0.1, 0.25, 0.4, 0.5, 1, 2, 4])(
    'spends exactly one octave of arc doubling %s×',
    (speed) => {
      expect(speedToAngle(speed * 2) - speedToAngle(speed)).toBeCloseTo(
        DEGREES_PER_OCTAVE,
        10,
      );
    },
  );

  it.each(TICK_SPEEDS)('round-trips the %s× tick', (speed) => {
    expect(angleToSpeed(speedToAngle(speed))).toBeCloseTo(speed, 10);
  });

  // MINIMUM_SPEED sits 4.32 octaves below 1×, past the reachable arc, so the dial
  // pins it to the bottom endpoint; only the precise-input dialog can reach 0.05×.
  it('pins the contract minimum to the bottom endpoint', () => {
    expect(speedToAngle(0.05)).toBe(MINIMUM_ANGLE);
    expect(speedToAngle(0.01)).toBe(MINIMUM_ANGLE);
  });

  it('clamps beyond both endpoints', () => {
    expect(angleToSpeed(MINIMUM_ANGLE - 30)).toBeCloseTo(0.0625, 10);
    expect(angleToSpeed(MAXIMUM_ANGLE + 30)).toBeCloseTo(8, 10);
    expect(speedToAngle(64)).toBe(MAXIMUM_ANGLE);
  });

  it('rejects non-finite input', () => {
    expect(() => speedToAngle(Number.NaN)).toThrow(RangeError);
    expect(() => angleToSpeed(Number.POSITIVE_INFINITY)).toThrow(RangeError);
  });
});

describe('snap point geometry', () => {
  it.each([
    [0.1, -149.48676426993129],
    [0.2, -104.4867642699313],
    [0.25, -90],
    [0.4, -59.4867642699313],
    [0.5, -45],
    [1, 0],
    [2, 45],
    [4, 90],
  ])('places the %s× snap point at %s°', (speed, angle) => {
    expect(speedToAngle(speed)).toBeCloseTo(angle, 10);
  });

  it('keeps every snap point inside the reachable arc', () => {
    for (const speed of SNAP_SPEEDS) {
      const angle = speedToAngle(speed);
      expect(angle).toBeGreaterThan(MINIMUM_ANGLE);
      expect(angle).toBeLessThan(MAXIMUM_ANGLE);
      expect(angleToSpeed(angle)).toBeCloseTo(speed, 10);
    }
  });

  // The hysteresis only reads as a detent if no two capture zones overlap: a
  // point held to 6° must never sit inside its neighbour's zone. Adding a snap
  // point closer than 12° to an existing one would silently ruin the feel.
  it('keeps adjacent capture zones disjoint', () => {
    const gaps: number[] = [];
    let previous: number | null = null;
    for (const angle of SNAP_SPEEDS.map(speedToAngle)) {
      if (previous !== null) {
        gaps.push(angle - previous);
      }
      previous = angle;
    }
    expect(Math.min(...gaps)).toBeGreaterThan(2 * SNAP_EXIT_THRESHOLD_DEGREES);
  });

  it('lists snap points in ascending order', () => {
    const angles = SNAP_SPEEDS.map(speedToAngle);
    expect([...angles].sort((left, right) => left - right)).toEqual(angles);
  });

  it('snaps only when a snap point is close enough', () => {
    expect(nearestSnapSpeed(speedToAngle(2) + 3)).toBe(2);
    expect(nearestSnapSpeed(speedToAngle(2) + 4)).toBeNull();
  });

  it('rejects a negative or non-finite threshold', () => {
    expect(() => nearestSnapSpeed(0, -1)).toThrow(RangeError);
    expect(() => nearestSnapSpeed(0, Number.NaN)).toThrow(RangeError);
  });
});

describe('resolveSnap detent hysteresis', () => {
  it('enters a detent within the enter threshold', () => {
    const angle = speedToAngle(0.5) + SNAP_ENTER_THRESHOLD_DEGREES - 0.1;
    const resolved = resolveSnap(angle, null);
    expect(resolved.speed).toBe(0.5);
    expect(resolved.angle).toBeCloseTo(speedToAngle(0.5), 10);
    expect(resolved.heldSpeed).toBe(0.5);
  });

  it('stays free just outside the enter threshold', () => {
    const angle = speedToAngle(0.5) + SNAP_ENTER_THRESHOLD_DEGREES + 0.1;
    const resolved = resolveSnap(angle, null);
    expect(resolved.heldSpeed).toBeNull();
    expect(resolved.angle).toBeCloseTo(angle, 10);
    expect(resolved.speed).toBeCloseTo(angleToSpeed(angle), 10);
  });

  // The whole point of the wider exit threshold: an angle that could not have
  // entered the detent still holds it once entered.
  it('holds a detent between the enter and exit thresholds', () => {
    const angle = speedToAngle(2) + SNAP_EXIT_THRESHOLD_DEGREES - 0.1;
    expect(nearestSnapSpeed(angle)).toBeNull();
    const resolved = resolveSnap(angle, 2);
    expect(resolved.speed).toBe(2);
    expect(resolved.angle).toBeCloseTo(speedToAngle(2), 10);
    expect(resolved.heldSpeed).toBe(2);
  });

  it('releases a detent past the exit threshold', () => {
    const angle = speedToAngle(2) + SNAP_EXIT_THRESHOLD_DEGREES + 0.1;
    const resolved = resolveSnap(angle, 2);
    expect(resolved.heldSpeed).toBeNull();
    expect(resolved.angle).toBeCloseTo(angle, 10);
  });

  it('hands a released detent straight to a neighbour in range', () => {
    // 0.2× and 0.25× are the tightest pair; leaving 0.2× toward 0.25× must land
    // on 0.25× rather than falling back to a free angle.
    const angle = speedToAngle(0.25) - 1;
    const resolved = resolveSnap(angle, 0.2);
    expect(resolved.speed).toBe(0.25);
    expect(resolved.heldSpeed).toBe(0.25);
  });

  it('ignores a held speed that is not a snap point', () => {
    const angle = speedToAngle(3);
    const resolved = resolveSnap(angle, 3);
    expect(resolved.heldSpeed).toBeNull();
    expect(resolved.angle).toBeCloseTo(angle, 10);
  });

  it('clamps a detent resolution to the reachable arc', () => {
    expect(resolveSnap(MINIMUM_ANGLE - 20, null).angle).toBe(MINIMUM_ANGLE);
    expect(resolveSnap(MAXIMUM_ANGLE + 20, null).angle).toBe(MAXIMUM_ANGLE);
  });
});

describe('resolveDragAngle bottom guard', () => {
  it('passes a sample through inside the reachable arc', () => {
    expect(resolveDragAngle(-60, -70)).toBeCloseTo(-60, 10);
    expect(resolveDragAngle(120, 100)).toBeCloseTo(120, 10);
  });

  it('allows a fast sweep that stays inside the arc', () => {
    // 110° in one frame is a legitimate flick, not a bottom crossing.
    expect(resolveDragAngle(-60, -170)).toBeCloseTo(-60, 10);
  });

  it('pins a sample that arrived through the dead wedge', () => {
    expect(resolveDragAngle(120, -170)).toBe(MINIMUM_ANGLE);
    expect(resolveDragAngle(-170, 120)).toBe(MAXIMUM_ANGLE);
  });

  it('pins a sample inside the dead wedge to the side it came from', () => {
    expect(resolveDragAngle(160, MINIMUM_ANGLE)).toBe(MINIMUM_ANGLE);
    expect(resolveDragAngle(160, MAXIMUM_ANGLE)).toBe(MAXIMUM_ANGLE);
    expect(resolveDragAngle(-179, 130)).toBe(MAXIMUM_ANGLE);
  });

  // atan2 returns (-180, +180], so grabbing the hand a hair right of straight
  // down yields a grabOffset a full turn away. Without the shorter-arc wrap the
  // hand would freeze at the bottom endpoint for the rest of the drag.
  it('survives a grab offset wound a full turn away', () => {
    const grabOffset = MINIMUM_ANGLE - 178.57;
    expect(resolveDragAngle(178.57 + grabOffset, MINIMUM_ANGLE)).toBe(
      MINIMUM_ANGLE,
    );
    expect(resolveDragAngle(-179 + grabOffset, MINIMUM_ANGLE)).toBeCloseTo(
      -177.57,
      10,
    );
  });

  it('leaves the bottom endpoint as soon as the pointer returns to the arc', () => {
    expect(resolveDragAngle(-170, MINIMUM_ANGLE)).toBeCloseTo(-170, 10);
  });

  it('rejects non-finite input', () => {
    expect(() => resolveDragAngle(Number.NaN, 0)).toThrow(RangeError);
    expect(() => resolveDragAngle(0, Number.NaN)).toThrow(RangeError);
  });
});
