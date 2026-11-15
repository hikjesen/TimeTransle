import { useState } from 'react';

import { ClockDialLift } from '../components/ClockDialLift/ClockDialLift';

/**
 * Standalone dev preview for ClockDialLift. Only reachable via the dev server
 * at /lift-preview.html; `npm run build` still ships only index.html.
 */
export function LiftPreviewApp() {
  const [speed, setSpeed] = useState(1.5);

  return (
    <main
      style={{
        display: 'grid',
        height: '100%',
        placeContent: 'center',
        placeItems: 'center',
        gap: '12px',
      }}
    >
      <ClockDialLift
        value={speed}
        appliedValue={null}
        paused={false}
        onPreview={() => undefined}
        onCommit={setSpeed}
      />
      <p
        style={{
          margin: 0,
          color: '#625a52',
          fontSize: '0.66rem',
        }}
      >
        悬停表盘观察上浮与提亮 · 当前 {speed.toFixed(2)}×
      </p>
    </main>
  );
}
