import {
  useEffect,
  useState,
  type ComponentProps,
  type PointerEvent as ReactPointerEvent,
} from 'react';

import { ClockDial } from '../ClockDial/ClockDial';
import styles from './ClockDialLift.module.css';

/**
 * ClockDialLift — hover-interactive dial, the dial actually rendered by
 * App.tsx since 0.1.4. The original ClockDial files are untouched; this
 * wrapper owns the resting dim and the hover lift.
 *
 * Behavior (implemented in ClockDialLift.module.css):
 * - resting state is dimmer than before: brightness(0.86);
 * - hovering with a fine pointer lifts the whole dial by -3px and brightens
 *   it to brightness(1.06);
 * - pointer leave restores the resting state;
 * - while a press is active, a `dragging` class pins the lifted state: the
 *   dial drag uses pointer capture, and capture can drop :hover once the
 *   pointer leaves the dial bounds, which would otherwise flash the dimmed
 *   resting style mid-drag;
 * - prefers-reduced-motion: no lift, only a fast brightness transition.
 *
 * Preview without the native app: npm run dev ->
 * http://localhost:5173/lift-preview.html
 */

export type ClockDialLiftProps = ComponentProps<typeof ClockDial>;

export function ClockDialLift(props: ClockDialLiftProps) {
  const [pressed, setPressed] = useState(false);

  // Press may start on the dial and end anywhere (pointer capture retargets
  // pointerup to the capturing SVG), so release is observed on the window.
  useEffect(() => {
    if (!pressed) {
      return;
    }
    const release = () => setPressed(false);
    window.addEventListener('pointerup', release);
    window.addEventListener('pointercancel', release);
    return () => {
      window.removeEventListener('pointerup', release);
      window.removeEventListener('pointercancel', release);
    };
  }, [pressed]);

  const handlePointerDown = (event: ReactPointerEvent<HTMLDivElement>) => {
    if (event.button === 0) {
      setPressed(true);
    }
  };

  return (
    <div
      className={
        pressed ? `${styles.liftScope} ${styles.dragging}` : styles.liftScope
      }
      onPointerDown={handlePointerDown}
    >
      <ClockDial {...props} />
    </div>
  );
}
