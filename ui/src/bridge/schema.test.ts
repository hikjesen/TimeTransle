import Ajv2020 from 'ajv/dist/2020.js';
import { describe, expect, it } from 'vitest';

import webviewSchema from '../../../contracts/webview-v1.schema.json';
import setSpeedVector from '../../../contracts/test-vectors/webview-set-speed-v1.json';

const ajv = new Ajv2020({
  allErrors: true,
  strict: true,
});
const validate = ajv.compile(webviewSchema);

describe('WebView V1 JSON Schema', () => {
  it('accepts the checked-in setSpeed vector', () => {
    expect(validate(setSpeedVector), JSON.stringify(validate.errors)).toBe(true);
  });

  it('rejects an out-of-range speed', () => {
    const invalid = structuredClone(setSpeedVector);
    invalid.payload.speed = 8.1;

    expect(validate(invalid)).toBe(false);
  });

  it('rejects privileged or unknown fields', () => {
    const invalid = {
      ...structuredClone(setSpeedVector),
      dllPath: 'C:\\untrusted.dll',
    };

    expect(validate(invalid)).toBe(false);
  });

  it('requires an empty payload for reset', () => {
    const invalid = {
      ...structuredClone(setSpeedVector),
      type: 'reset',
    };

    expect(validate(invalid)).toBe(false);
  });
});

