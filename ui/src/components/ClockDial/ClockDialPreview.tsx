import { useId } from 'react';

import { ClockDialArtwork } from './ClockDialArtwork';
import styles from './ClockDialPreview.module.css';

export function ClockDialPreview() {
  const id = useId().replaceAll(':', '');
  const titleId = `${id}-dial-title`;
  const descriptionId = `${id}-dial-description`;
  const haloFilterId = `${id}-dial-halo`;
  const softGlowFilterId = `${id}-dial-soft-glow`;

  return (
    <figure className={styles.figure} data-stage="visual-prototype">
      <div className={styles.ambient} aria-hidden="true" />
      <svg
        className={styles.svg}
        viewBox="0 0 512 512"
        role="img"
        aria-labelledby={`${titleId} ${descriptionId}`}
        focusable="false"
      >
        <title id={titleId}>TimeTransle 时钟旋钮视觉原型</title>
        <desc id={descriptionId}>
          暖金色完整双外环、四向连接桥、完整内环，以及彼此独立的倍率分针与机械联动时针。
          当前仅用于视觉校准，尚不可操作。
        </desc>
        <defs>
          <linearGradient
            id={`${id}-dial-halo-paint`}
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
            id={`${id}-dial-soft-paint`}
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
            id={`${id}-dial-core-paint`}
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
            id={haloFilterId}
            x="-22%"
            y="-22%"
            width="144%"
            height="148%"
            colorInterpolationFilters="sRGB"
          >
            <feGaussianBlur stdDeviation="17" />
          </filter>
          <filter
            id={softGlowFilterId}
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
          filter={`url(#${haloFilterId})`}
          aria-hidden="true"
        >
          <ClockDialArtwork
            minuteAngle={0}
            paint={`url(#${id}-dial-halo-paint)`}
          />
        </g>
        <g
          className={`${styles.layer} ${styles.softGlow}`}
          filter={`url(#${softGlowFilterId})`}
          aria-hidden="true"
        >
          <ClockDialArtwork
            minuteAngle={0}
            paint={`url(#${id}-dial-soft-paint)`}
          />
        </g>
        <g className={`${styles.layer} ${styles.core}`} aria-hidden="true">
          <ClockDialArtwork
            minuteAngle={0}
            paint={`url(#${id}-dial-core-paint)`}
            includeHubVoid
            hubVoidFill="#070402"
          />
        </g>
      </svg>
    </figure>
  );
}
