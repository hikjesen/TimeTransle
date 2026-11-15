import {
  MAXIMUM_SPEED,
  MINIMUM_SPEED,
} from '../../bridge/contracts';

export const DEGREES_PER_OCTAVE = 45;
export const MINIMUM_ANGLE = -180;
export const MAXIMUM_ANGLE = 135;
export const SNAP_ENTER_THRESHOLD_DEGREES = 3.5;
export const SNAP_EXIT_THRESHOLD_DEGREES = 6;
export const SNAP_SPEEDS = [0.1, 0.2, 0.25, 0.4, 0.5, 1, 2, 4] as const;
export const TICK_SPEEDS = [
  0.0625,
  0.1,
  0.125,
  0.2,
  0.25,
  0.4,
  0.5,
  0.75,
  1,
  1.5,
  2,
  4,
  8,
] as const;

export interface SnapResolution {
  angle: number;
  speed: number;
  heldSpeed: number | null;
}

export function clampAngle(angle: number): number {
  if (!Number.isFinite(angle)) {
    throw new RangeError('Angle must be finite.');
  }
  return Math.min(MAXIMUM_ANGLE, Math.max(MINIMUM_ANGLE, angle));
}

export function clampSpeed(speed: number): number {
  if (!Number.isFinite(speed)) {
    throw new RangeError('Speed must be finite.');
  }
  return Math.min(MAXIMUM_SPEED, Math.max(MINIMUM_SPEED, speed));
}

// Drag continuity guard. A raw pointer angle is only meaningful relative to the
// previous one: atan2 flips between +180 and -180 at the bottom, so grabbing the
// hand there yields a grabOffset a full turn away, and a fast flick can land on
// the far side of the dead wedge. Taking the shorter arc to the previous angle
// resolves both, and the clamp then pins anything inside the wedge to the
// endpoint the pointer left, so 0.06× can never flip to 8× across the bottom.
export function resolveDragAngle(angle: number, previousAngle: number): number {
  if (!Number.isFinite(angle)) {
    throw new RangeError('Angle must be finite.');
  }
  const shortestDelta =
    ((((angle - clampAngle(previousAngle)) % 360) + 540) % 360) - 180;
  return clampAngle(clampAngle(previousAngle) + shortestDelta);
}

export function angleToSpeed(angle: number): number {
  return 2 ** (clampAngle(angle) / DEGREES_PER_OCTAVE);
}

// speedToAngle clamps the angle as well as the speed: MINIMUM_SPEED (0.05×) sits
// 4.32 octaves below 1×, which is past the reachable arc, so it pins to MINIMUM_ANGLE.
export function speedToAngle(speed: number): number {
  return clampAngle(DEGREES_PER_OCTAVE * Math.log2(clampSpeed(speed)));
}

export function nearestSnapSpeed(
  angle: number,
  threshold = SNAP_ENTER_THRESHOLD_DEGREES,
): number | null {
  if (!Number.isFinite(threshold) || threshold < 0) {
    throw new RangeError('Snap threshold must be finite and nonnegative.');
  }

  let nearest: number | null = null;
  let nearestDistance = Number.POSITIVE_INFINITY;
  for (const speed of SNAP_SPEEDS) {
    const distance = Math.abs(speedToAngle(speed) - angle);
    if (distance < nearestDistance) {
      nearestDistance = distance;
      nearest = speed;
    }
  }
  return nearestDistance <= threshold ? nearest : null;
}

// Detent state machine for live snapping while dragging. `heldSpeed` is the snap
// carried over from the previous sample; the wider exit threshold gives the
// hysteresis that stops the hand chattering on a detent boundary.
export function resolveSnap(
  angle: number,
  heldSpeed: number | null,
): SnapResolution {
  const bounded = clampAngle(angle);

  if (heldSpeed !== null && (SNAP_SPEEDS as readonly number[]).includes(heldSpeed)) {
    const heldAngle = speedToAngle(heldSpeed);
    if (Math.abs(bounded - heldAngle) <= SNAP_EXIT_THRESHOLD_DEGREES) {
      return { angle: heldAngle, speed: heldSpeed, heldSpeed };
    }
  }

  const entered = nearestSnapSpeed(bounded);
  if (entered !== null) {
    return { angle: speedToAngle(entered), speed: entered, heldSpeed: entered };
  }

  return { angle: bounded, speed: angleToSpeed(bounded), heldSpeed: null };
}

export function formatSpeed(speed: number): string {
  return clampSpeed(speed).toFixed(2);
}
