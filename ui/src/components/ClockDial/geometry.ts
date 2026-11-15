export const DIAL_SIZE = 512;
export const DIAL_CENTER = DIAL_SIZE / 2;
export const OUTER_RING_RADIUS = 179;
export const MIDDLE_RING_RADIUS = 156;
export const INNER_RING_RADIUS = 110;
export const MINUTE_HAND_LENGTH = 107;
export const HOUR_HAND_LENGTH = 104;
export const MINUTE_REFERENCE_ANGLE_DEGREES = 0;
export const HOUR_REFERENCE_ANGLE_DEGREES = 90;
export const HOUR_LINKAGE_RATIO = 1 / 12;
export const CONNECTOR_INNER_RADIUS = 154;
export const CONNECTOR_OUTER_RADIUS = 181;
export const CONNECTOR_ANGLES_DEGREES = [0, 90, 180, 270] as const;

export interface DialPoint {
  x: number;
  y: number;
}

export function pointOnDial(radius: number, degrees: number): DialPoint {
  const radians = (degrees * Math.PI) / 180;

  return {
    x: DIAL_CENTER + radius * Math.sin(radians),
    y: DIAL_CENTER - radius * Math.cos(radians),
  };
}

export function linkedHourAngle(minuteAngle: number): number {
  if (!Number.isFinite(minuteAngle)) {
    throw new RangeError('Minute angle must be finite.');
  }

  return HOUR_REFERENCE_ANGLE_DEGREES + minuteAngle * HOUR_LINKAGE_RATIO;
}

function formatCoordinate(value: number): string {
  const rounded = Number(value.toFixed(3));
  return Object.is(rounded, -0) ? '0' : String(rounded);
}

export function describeRadialConnector(
  degrees: number,
  innerRadius = CONNECTOR_INNER_RADIUS,
  outerRadius = CONNECTOR_OUTER_RADIUS,
): string {
  if (
    !Number.isFinite(innerRadius) ||
    !Number.isFinite(outerRadius) ||
    innerRadius <= 0 ||
    outerRadius <= innerRadius
  ) {
    throw new RangeError(
      'Connector radii must be finite, positive, and ordered from inner to outer.',
    );
  }

  if (!Number.isFinite(degrees)) {
    throw new RangeError('Connector angle must be finite.');
  }

  const inner = pointOnDial(innerRadius, degrees);
  const outer = pointOnDial(outerRadius, degrees);

  return `M ${formatCoordinate(inner.x)} ${formatCoordinate(inner.y)} L ${formatCoordinate(outer.x)} ${formatCoordinate(outer.y)}`;
}

export function createConnectorPaths(): readonly string[] {
  return CONNECTOR_ANGLES_DEGREES.map((degrees) =>
    describeRadialConnector(degrees),
  );
}

export function describeClockwiseArc(
  radius: number,
  startDegrees: number,
  endDegrees: number,
): string {
  if (
    !Number.isFinite(radius) ||
    radius <= 0 ||
    !Number.isFinite(startDegrees) ||
    !Number.isFinite(endDegrees)
  ) {
    throw new RangeError('Arc geometry must use finite angles and a positive radius.');
  }

  const start = pointOnDial(radius, startDegrees);
  const end = pointOnDial(radius, endDegrees);
  const sweep = ((endDegrees - startDegrees) % 360 + 360) % 360;
  const largeArc = sweep > 180 ? 1 : 0;

  return [
    `M ${formatCoordinate(start.x)} ${formatCoordinate(start.y)}`,
    `A ${formatCoordinate(radius)} ${formatCoordinate(radius)} 0 ${largeArc} 1`,
    `${formatCoordinate(end.x)} ${formatCoordinate(end.y)}`,
  ].join(' ');
}
