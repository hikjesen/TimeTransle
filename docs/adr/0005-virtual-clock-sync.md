# ADR 0005: VirtualClock synchronization

- Status: Accepted
- Date: 2026-07-26

## Decision

All supported APIs derive from one virtual QPC timeline. A single writer publishes atomic payload fields under a seqlock sequence; readers take consistent snapshots without ordinary mutexes, heap allocation, logging or IPC.

State transitions use continuous re-anchoring:

```text
virtualNow = virtualAnchor + (realNow - realAnchor) × effectiveSpeed
```

## Consequences

All writes are serialized. `GetTickCount`, `GetTickCount64` and `timeGetTime` are conversions from the same virtual elapsed value, with natural DWORD wrap retained.

