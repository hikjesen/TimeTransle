import {
  useEffect,
  useId,
  useRef,
  type KeyboardEvent,
  type PointerEvent,
} from 'react';
import { animate } from 'motion';

import {
  DIAL_CENTER,
  MINUTE_HAND_LENGTH,
  describeClockwiseArc,
  linkedHourAngle,
  pointOnDial,
} from './geometry';
import {
  MAXIMUM_ANGLE,
  MINIMUM_ANGLE,
  SNAP_SPEEDS,
  angleToSpeed,
  formatSpeed,
  nearestSnapSpeed,
  resolveDragAngle,
  resolveSnap,
  speedToAngle,
} from './speedMapping';
import { ClockDialArtwork } from './ClockDialArtwork';
import styles from './ClockDial.module.css';

const movementThreshold = 9;
const hitArcPath = describeClockwiseArc(174, MINIMUM_ANGLE, MAXIMUM_ANGLE);

interface GestureState {
  pointerId: number;
  centerX: number;
  centerY: number;
  startX: number;
  startY: number;
  grabOffset: number;
  preservesGrabOffset: boolean;
  dragging: boolean;
  candidateAngle: number | null;
  heldSnapSpeed: number | null;
}

interface PointerSample {
  clientX: number;
  clientY: number;
}

interface ClockDialProps {
  value: number;
  appliedValue: number | null;
  paused: boolean;
  onPreview(value: number): void;
  onCommit(value: number): void;
}

function pointAngle(
  clientX: number,
  clientY: number,
  centerX: number,
  centerY: number,
): number {
  return (Math.atan2(clientX - centerX, centerY - clientY) * 180) / Math.PI;
}

function keyboardAngle(
  event: KeyboardEvent<SVGSVGElement>,
  current: number,
): number | null {
  const step = event.shiftKey ? 5 : event.altKey ? 0.2 : 1;
  switch (event.key) {
    case 'ArrowLeft':
    case 'ArrowDown':
      return Math.max(MINIMUM_ANGLE, current - step);
    case 'ArrowRight':
    case 'ArrowUp':
      return Math.min(MAXIMUM_ANGLE, current + step);
    case 'Home':
      return MINIMUM_ANGLE;
    case 'End':
      return MAXIMUM_ANGLE;
    case 'PageDown': {
      const previous = SNAP_SPEEDS
        .map(speedToAngle)
        .filter((angle) => angle < current - 0.01)
        .at(-1);
      return previous ?? MINIMUM_ANGLE;
    }
    case 'PageUp':
      return (
        SNAP_SPEEDS.map(speedToAngle).find(
          (angle) => angle > current + 0.01,
        ) ?? MAXIMUM_ANGLE
      );
    default:
      return null;
  }
}

export function ClockDial({
  value,
  appliedValue,
  paused,
  onPreview,
  onCommit,
}: ClockDialProps) {
  const id = useId().replaceAll(':', '');
  const svgRef = useRef<SVGSVGElement>(null);
  const readoutRef = useRef<HTMLOutputElement>(null);
  const gestureRef = useRef<GestureState | null>(null);
  const pendingSampleRef = useRef<PointerSample | null>(null);
  const animationFrameRef = useRef<number | null>(null);
  const currentAngleRef = useRef(speedToAngle(value));
  const snapAnimationRef = useRef<{ stop(): void } | null>(null);
  const lastEmittedSpeedRef = useRef<number | null>(null);

  const applyVisualAngle = (angle: number) => {
    currentAngleRef.current = angle;
    const svg = svgRef.current;
    if (!svg) {
      return;
    }
    for (const group of svg.querySelectorAll<SVGGElement>(
      '[data-minute-pointer], [data-minute-hit]',
    )) {
      group.setAttribute('transform', `rotate(${angle} ${DIAL_CENTER} ${DIAL_CENTER})`);
    }
    const hourAngle = linkedHourAngle(angle);
    for (const group of svg.querySelectorAll<SVGGElement>('[data-hour-pointer]')) {
      group.setAttribute(
        'transform',
        `rotate(${hourAngle} ${DIAL_CENTER} ${DIAL_CENTER})`,
      );
    }
    const speed = angleToSpeed(angle);
    svg.setAttribute('aria-valuenow', String(speed));
    svg.setAttribute('aria-valuetext', `${formatSpeed(speed)} 倍速度`);
    if (readoutRef.current) {
      readoutRef.current.value = formatSpeed(speed);
    }
  };

  // Holding a detent repeats the same speed for many frames; only forward a
  // change so the drag hot path does not resend identical IPC every frame.
  const emitPreview = (speed: number) => {
    if (lastEmittedSpeedRef.current === speed) {
      return;
    }
    lastEmittedSpeedRef.current = speed;
    onPreview(speed);
  };

  useEffect(() => {
    if (gestureRef.current === null && snapAnimationRef.current === null) {
      applyVisualAngle(speedToAngle(value));
    }
  }, [value]);

  useEffect(
    () => () => {
      if (animationFrameRef.current !== null) {
        cancelAnimationFrame(animationFrameRef.current);
      }
      snapAnimationRef.current?.stop();
    },
    [],
  );

  const consumePointerSample = () => {
    animationFrameRef.current = null;
    const gesture = gestureRef.current;
    const sample = pendingSampleRef.current;
    pendingSampleRef.current = null;
    if (!gesture || !sample) {
      return;
    }
    const movement = Math.hypot(
      sample.clientX - gesture.startX,
      sample.clientY - gesture.startY,
    );
    if (!gesture.dragging && movement >= movementThreshold) {
      gesture.dragging = true;
    }
    if (!gesture.dragging) {
      return;
    }

    const contact = pointAngle(
      sample.clientX,
      sample.clientY,
      gesture.centerX,
      gesture.centerY,
    );
    const rawAngle = contact + gesture.grabOffset;
    const bounded = resolveDragAngle(rawAngle, currentAngleRef.current);
    const resolved = resolveSnap(bounded, gesture.heldSnapSpeed);
    gesture.heldSnapSpeed = resolved.heldSpeed;
    gesture.candidateAngle = resolved.angle;
    applyVisualAngle(resolved.angle);
    emitPreview(resolved.speed);
  };

  const scheduleSample = (sample: PointerSample) => {
    pendingSampleRef.current = sample;
    if (animationFrameRef.current === null) {
      animationFrameRef.current = requestAnimationFrame(consumePointerSample);
    }
  };

  const handlePointerDown = (event: PointerEvent<SVGSVGElement>) => {
    if (gestureRef.current !== null) {
      return;
    }
    snapAnimationRef.current?.stop();
    snapAnimationRef.current = null;
    lastEmittedSpeedRef.current = null;

    const bounds = event.currentTarget.getBoundingClientRect();
    const centerX = bounds.left + bounds.width / 2;
    const centerY = bounds.top + bounds.height / 2;
    const contact = pointAngle(event.clientX, event.clientY, centerX, centerY);
    const target = event.target;
    const preservesGrabOffset =
      target instanceof Element &&
      target.closest('[data-grab-preserve="true"]') !== null;
    const contactIsValid = contact >= MINIMUM_ANGLE && contact <= MAXIMUM_ANGLE;

    gestureRef.current = {
      pointerId: event.pointerId,
      centerX,
      centerY,
      startX: event.clientX,
      startY: event.clientY,
      grabOffset: preservesGrabOffset
        ? currentAngleRef.current - contact
        : 0,
      preservesGrabOffset,
      dragging: false,
      candidateAngle:
        !preservesGrabOffset && contactIsValid ? contact : null,
      heldSnapSpeed: null,
    };
    event.currentTarget.setPointerCapture(event.pointerId);
    event.currentTarget.dataset.pressed = 'true';
  };

  const handlePointerMove = (event: PointerEvent<SVGSVGElement>) => {
    if (gestureRef.current?.pointerId !== event.pointerId) {
      return;
    }
    scheduleSample({ clientX: event.clientX, clientY: event.clientY });
  };

  const commitAngle = (angle: number) => {
    const snapSpeed = nearestSnapSpeed(angle);
    const finalSpeed = snapSpeed ?? angleToSpeed(angle);
    const finalAngle = speedToAngle(finalSpeed);
    onPreview(finalSpeed);
    onCommit(finalSpeed);

    const reduceMotion = window.matchMedia(
      '(prefers-reduced-motion: reduce)',
    ).matches;
    if (reduceMotion || snapSpeed === null || finalAngle === angle) {
      applyVisualAngle(finalAngle);
      return;
    }
    snapAnimationRef.current = animate(angle, finalAngle, {
      type: 'spring',
      bounce: 0,
      duration: 0.28,
      onUpdate: applyVisualAngle,
      onComplete: () => {
        snapAnimationRef.current = null;
      },
    });
  };

  const finishPointer = (
    event: PointerEvent<SVGSVGElement>,
    cancelled: boolean,
  ) => {
    const gesture = gestureRef.current;
    if (!gesture || gesture.pointerId !== event.pointerId) {
      return;
    }
    if (animationFrameRef.current !== null) {
      cancelAnimationFrame(animationFrameRef.current);
      animationFrameRef.current = null;
      consumePointerSample();
    }
    gestureRef.current = null;
    pendingSampleRef.current = null;
    delete event.currentTarget.dataset.pressed;
    if (event.currentTarget.hasPointerCapture(event.pointerId)) {
      event.currentTarget.releasePointerCapture(event.pointerId);
    }

    if (cancelled) {
      applyVisualAngle(speedToAngle(value));
      return;
    }
    if (gesture.dragging && gesture.candidateAngle !== null) {
      commitAngle(gesture.candidateAngle);
    } else if (!gesture.preservesGrabOffset && gesture.candidateAngle !== null) {
      commitAngle(gesture.candidateAngle);
    }
  };

  const handleKeyDown = (event: KeyboardEvent<SVGSVGElement>) => {
    const angle = keyboardAngle(event, currentAngleRef.current);
    if (angle === null) {
      return;
    }
    event.preventDefault();
    snapAnimationRef.current?.stop();
    snapAnimationRef.current = null;
    applyVisualAngle(angle);
    const speed = angleToSpeed(angle);
    onPreview(speed);
    onCommit(speed);
  };

  const appliedMarker =
    appliedValue === null || Math.abs(appliedValue - value) < 0.0005
      ? null
      : pointOnDial(194, speedToAngle(appliedValue));
  const initialMinuteAngle = speedToAngle(value);

  return (
    <figure className={styles.figure} data-paused={paused || undefined}>
      <div className={styles.dialFace}>
        <div className={styles.ambient} aria-hidden="true" />
        <svg
          ref={svgRef}
          className={styles.svg}
          viewBox="0 0 512 512"
          role="slider"
          tabIndex={0}
          aria-label="时间倍率"
          aria-valuemin={0.05}
          aria-valuemax={8}
          aria-valuenow={value}
          aria-valuetext={`${formatSpeed(value)} 倍速度`}
          aria-describedby={`${id}-dial-help`}
          onKeyDown={handleKeyDown}
          onPointerDown={handlePointerDown}
          onPointerMove={handlePointerMove}
          onPointerUp={(event) => finishPointer(event, false)}
          onPointerCancel={(event) => finishPointer(event, true)}
        >
          <defs>
            <linearGradient
              id={`${id}-halo-paint`}
              gradientUnits="userSpaceOnUse"
              x1="0"
              y1="64"
              x2="0"
              y2="448"
            >
              <stop offset="0" stopColor="#8f552d" />
              <stop offset="0.58" stopColor="#c98245" />
              <stop offset="1" stopColor="#e29a58" />
            </linearGradient>
            <linearGradient
              id={`${id}-soft-paint`}
              gradientUnits="userSpaceOnUse"
              x1="0"
              y1="64"
              x2="0"
              y2="448"
            >
              <stop offset="0" stopColor="#bc8d63" />
              <stop offset="0.52" stopColor="#e5b77f" />
              <stop offset="1" stopColor="#f6d19d" />
            </linearGradient>
            <linearGradient
              id={`${id}-core-paint`}
              gradientUnits="userSpaceOnUse"
              x1="0"
              y1="64"
              x2="0"
              y2="448"
            >
              <stop offset="0" stopColor="#c69a73" />
              <stop offset="0.48" stopColor="#e7c092" />
              <stop offset="1" stopColor="#ffe1ad" />
            </linearGradient>
            <filter
              id={`${id}-halo`}
              x="-22%"
              y="-22%"
              width="144%"
              height="148%"
              colorInterpolationFilters="sRGB"
            >
              <feGaussianBlur stdDeviation="17" />
            </filter>
            <filter
              id={`${id}-glow`}
              x="-10%"
              y="-10%"
              width="120%"
              height="124%"
              colorInterpolationFilters="sRGB"
            >
              <feGaussianBlur stdDeviation="4.5" />
            </filter>
          </defs>

          <g
            className={`${styles.layer} ${styles.halo}`}
            filter={`url(#${id}-halo)`}
            aria-hidden="true"
          >
            <ClockDialArtwork
              minuteAngle={initialMinuteAngle}
              paint={`url(#${id}-halo-paint)`}
            />
          </g>
          <g
            className={`${styles.layer} ${styles.softGlow}`}
            filter={`url(#${id}-glow)`}
            aria-hidden="true"
          >
            <ClockDialArtwork
              minuteAngle={initialMinuteAngle}
              paint={`url(#${id}-soft-paint)`}
            />
          </g>
          <g className={`${styles.layer} ${styles.core}`} aria-hidden="true">
            <ClockDialArtwork
              minuteAngle={initialMinuteAngle}
              paint={`url(#${id}-core-paint)`}
              includeHubVoid
            />
            {appliedMarker ? (
              <circle
                className={styles.appliedMarker}
                cx={appliedMarker.x}
                cy={appliedMarker.y}
                r="3.25"
              />
            ) : null}
          </g>

          <path className={styles.hitArea} d={hitArcPath} />
          <g
            className={styles.minuteHitTarget}
            data-minute-hit
            data-grab-preserve="true"
            transform={`rotate(${String(initialMinuteAngle)} ${String(DIAL_CENTER)} ${String(DIAL_CENTER)})`}
            aria-hidden="true"
          >
            <line
              x1={DIAL_CENTER}
              y1={DIAL_CENTER}
              x2={DIAL_CENTER}
              y2={DIAL_CENTER - MINUTE_HAND_LENGTH}
            />
            <circle cx={DIAL_CENTER} cy={DIAL_CENTER} r="27" />
          </g>
        </svg>
      </div>

      <figcaption className={styles.readout}>
        <span className={styles.valueLine}>
          <output ref={readoutRef} aria-live="off">
            {formatSpeed(value)}
          </output>
          <span aria-hidden="true">×</span>
        </span>
        <span id={`${id}-dial-help`} className={styles.visuallyHidden}>
          使用方向键调整倍率，Shift 加速，Alt 精细调整。
        </span>
      </figcaption>
    </figure>
  );
}
