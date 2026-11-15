import {
  CONNECTOR_ANGLES_DEGREES,
  DIAL_CENTER,
  HOUR_HAND_LENGTH,
  INNER_RING_RADIUS,
  MINUTE_HAND_LENGTH,
  MIDDLE_RING_RADIUS,
  OUTER_RING_RADIUS,
  createConnectorPaths,
  linkedHourAngle,
} from './geometry';
import styles from './ClockDialArtwork.module.css';

const connectorPaths = createConnectorPaths();

interface ClockDialArtworkProps {
  minuteAngle: number;
  paint: string;
  includeHubVoid?: boolean;
  hubVoidFill?: string;
}

function rotation(angle: number): string {
  return `rotate(${String(angle)} ${String(DIAL_CENTER)} ${String(DIAL_CENTER)})`;
}

export function ClockDialArtwork({
  minuteAngle,
  paint,
  includeHubVoid = false,
  hubVoidFill = '#050302',
}: ClockDialArtworkProps) {
  const hourAngle = linkedHourAngle(minuteAngle);

  return (
    <>
      <circle
        className={styles.outerRing}
        data-ring="outer"
        cx={DIAL_CENTER}
        cy={DIAL_CENTER}
        r={OUTER_RING_RADIUS}
        stroke={paint}
      />
      <circle
        className={styles.middleRing}
        data-ring="middle"
        cx={DIAL_CENTER}
        cy={DIAL_CENTER}
        r={MIDDLE_RING_RADIUS}
        stroke={paint}
      />
      {connectorPaths.map((path, index) => (
        <path
          className={styles.connector}
          data-connector-angle={CONNECTOR_ANGLES_DEGREES[index]}
          d={path}
          key={`connector-${String(CONNECTOR_ANGLES_DEGREES[index])}`}
          stroke={paint}
        />
      ))}
      <circle
        className={styles.innerRing}
        cx={DIAL_CENTER}
        cy={DIAL_CENTER}
        r={INNER_RING_RADIUS}
        stroke={paint}
      />

      <g
        className={styles.minuteHand}
        data-minute-pointer
        transform={rotation(minuteAngle)}
      >
        <path
          d={`M ${String(DIAL_CENTER)} ${String(DIAL_CENTER)} L ${String(DIAL_CENTER)} ${String(DIAL_CENTER - MINUTE_HAND_LENGTH)}`}
          stroke={paint}
        />
      </g>
      <g
        className={styles.hourHand}
        data-hour-pointer
        transform={rotation(hourAngle)}
      >
        <path
          d={`M ${String(DIAL_CENTER)} ${String(DIAL_CENTER)} L ${String(DIAL_CENTER)} ${String(DIAL_CENTER - HOUR_HAND_LENGTH)}`}
          stroke={paint}
        />
      </g>

      <circle
        className={styles.hub}
        cx={DIAL_CENTER}
        cy={DIAL_CENTER}
        r="20"
        fill={paint}
      />
      {includeHubVoid ? (
        <circle
          className={styles.hubVoid}
          cx={DIAL_CENTER}
          cy={DIAL_CENTER}
          r="8"
          fill={hubVoidFill}
        />
      ) : null}
    </>
  );
}
