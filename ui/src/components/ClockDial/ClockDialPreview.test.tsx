import { renderToStaticMarkup } from 'react-dom/server';
import { describe, expect, it } from 'vitest';

import { ClockDialPreview } from './ClockDialPreview';

describe('ClockDialPreview', () => {
  it('identifies itself as a non-interactive visual prototype', () => {
    const markup = renderToStaticMarkup(<ClockDialPreview />);

    expect(markup).toContain('data-stage="visual-prototype"');
    expect(markup).toContain('role="img"');
    expect(markup).toContain('时钟旋钮视觉原型');
    expect(markup).toContain('尚不可操作');
    expect(markup).not.toMatch(/<(button|input)\b/);
    expect(markup).not.toContain('role="slider"');
  });

  it('renders two complete outer rings joined by four bridges in every visual layer', () => {
    const markup = renderToStaticMarkup(<ClockDialPreview />);

    expect(markup.match(/data-ring="outer"/g)).toHaveLength(3);
    expect(markup.match(/data-ring="middle"/g)).toHaveLength(3);
    expect(markup.match(/data-connector-angle=/g)).toHaveLength(12);
    expect(markup.match(/data-minute-pointer/g)).toHaveLength(3);
    expect(markup.match(/data-hour-pointer/g)).toHaveLength(3);
  });
});
