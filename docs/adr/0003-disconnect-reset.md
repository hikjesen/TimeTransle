# ADR 0003: Immediate disconnect reset

- Status: Accepted
- Date: 2026-07-26

## Decision

Once the Hook DLL confirms that its Controller connection is disconnected, the DLL immediately and locally performs a continuous re-anchor to:

```text
configuredSpeed = 1.0
paused = false
effectiveSpeed = 1.0
```

There is no grace period, keep-last mode, disk policy lookup or dependency on a final Controller command.

## Consequences

The disconnect path shares the serialized VirtualClock writer used by normal commands. Reconnection starts a new authenticated session and never replays the old speed automatically.

