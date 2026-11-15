import { describe, expect, it } from 'vitest';

import {
  CONNECTOR_ANGLES_DEGREES,
  DIAL_CENTER,
  HOUR_LINKAGE_RATIO,
  HOUR_REFERENCE_ANGLE_DEGREES,
  MINUTE_REFERENCE_ANGLE_DEGREES,
  createConnectorPaths,
  describeClockwiseArc,
  describeRadialConnector,
  linkedHourAngle,
  pointOnDial,
} from './geometry';

describe('clock dial geometry', () => {
  it.each([
    [0, DIAL_CENTER, DIAL_CENTER - 100],
    [90, DIAL_CENTER + 100, DIAL_CENTER],
    [180, DIAL_CENTER, DIAL_CENTER + 100],
    [270, DIAL_CENTER - 100, DIAL_CENTER],
  ])('maps %d° clockwise from twelve o’clock', (degrees, expectedX, expectedY) => {
    const point = pointOnDial(100, degrees);

    expect(point.x).toBeCloseTo(expectedX, 8);
    expect(point.y).toBeCloseTo(expectedY, 8);
  });

  it('builds four radial bridges between the two complete outer rings', () => {
    expect(CONNECTOR_ANGLES_DEGREES).toEqual([0, 90, 180, 270]);
    expect(createConnectorPaths()).toEqual([
      'M 256 102 L 256 75',
      'M 410 256 L 437 256',
      'M 256 410 L 256 437',
      'M 102 256 L 75 256',
    ]);
  });

  it('links the independent hour hand at one twelfth of the minute angle', () => {
    expect(MINUTE_REFERENCE_ANGLE_DEGREES).toBe(0);
    expect(HOUR_REFERENCE_ANGLE_DEGREES).toBe(90);
    expect(HOUR_LINKAGE_RATIO).toBe(1 / 12);
    expect(linkedHourAngle(0)).toBe(90);
    expect(linkedHourAngle(135)).toBe(101.25);
    expect(linkedHourAngle(-135)).toBe(78.75);
    expect(() => linkedHourAngle(Number.NaN)).toThrow(RangeError);
  });

  it('rejects invalid connector geometry', () => {
    expect(() => describeRadialConnector(0, 0, 179)).toThrow(RangeError);
    expect(() => describeRadialConnector(0, 180, 179)).toThrow(RangeError);
    expect(() => describeRadialConnector(Number.NaN)).toThrow(RangeError);
  });

  it('builds the clockwise 270° interactive arc without the bottom dead zone', () => {
    expect(describeClockwiseArc(174, -135, 135)).toBe(
      'M 132.963 379.037 A 174 174 0 1 1 379.037 379.037',
    );
    expect(() => describeClockwiseArc(0, -135, 135)).toThrow(RangeError);
  });
});
