# ADR 0007: Native JSON and test dependencies

- Status: Accepted
- Date: 2026-07-26

## Context

The development plan requires a maintained C++ JSON parser and test framework. Handwritten JSON parsing would make strict WebView2 schema validation fragile, while a test framework is needed for deterministic VirtualClock and protocol vectors.

## Decision

- Use nlohmann/json `3.12.0`, pinned to revision `65ee68451d8eb2b5f3a30b410476ab83deb3289b`, for native JSON parsing.
- Use Catch2 `3.15.0`, pinned to revision `b8acae26b0453dad6250447ad32f6b0015b162ae`, for native unit and integration tests.
- Keep the stage-1 smoke executable dependency-free. Fetch the pinned dependencies only when a later target needs them.

## Consequences

Third-party targets do not inherit TimeTransle `/WX`. Exact license texts must be staged before distribution. JSON schema validation still requires explicit application-level schema and range checks; selecting a parser alone is not validation.

