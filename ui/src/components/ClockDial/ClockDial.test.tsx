import { renderToStaticMarkup } from 'react-dom/server';
import { describe, expect, it } from 'vitest';

import { ClockDial } from './ClockDial';

describe('ClockDial', () => {
  it('exposes one accessible slider for the desired-speed minute hand', () => {
    const markup = renderToStaticMarkup(
      <ClockDial
        value={0.5}
        appliedValue={null}
        paused={false}
        onPreview={() => undefined}
        onCommit={() => undefined}
      />,
    );

    expect(markup).toContain('role="slider"');
    expect(markup).toContain('aria-valuemin="0.05"');
    expect(markup).toContain('aria-valuemax="8"');
    expect(markup).toContain('aria-valuetext="0.50 倍速度"');
    expect(markup.match(/data-minute-pointer/g)).toHaveLength(3);
    expect(markup.match(/data-hour-pointer/g)).toHaveLength(3);
    expect(markup).not.toContain('data-major');
  });

  it('keeps the applied value as a small outer marker instead of a third hand', () => {
    const markup = renderToStaticMarkup(
      <ClockDial
        value={0.5}
        appliedValue={2}
        paused={false}
        onPreview={() => undefined}
        onCommit={() => undefined}
      />,
    );

    expect(markup.match(/data-minute-pointer/g)).toHaveLength(3);
    expect(markup.match(/data-hour-pointer/g)).toHaveLength(3);
    expect(markup).not.toContain('data-applied-pointer');
  });

  it('renders the minute and mechanically linked hour hands independently', () => {
    const markup = renderToStaticMarkup(
      <ClockDial
        value={1}
        appliedValue={1}
        paused={false}
        onPreview={() => undefined}
        onCommit={() => undefined}
      />,
    );

    expect(markup.match(/transform="rotate\(0 256 256\)"/g)).toHaveLength(4);
    expect(markup.match(/transform="rotate\(90 256 256\)"/g)).toHaveLength(3);
  });
});
