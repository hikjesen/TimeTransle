# ADR 0004: V1 DLL lifecycle

- Status: Accepted
- Date: 2026-07-26

## Decision

V1 uses logical deactivation rather than remote hot unload. Deactivation restores `1.0×`, clears pause, stops accepting commands and leaves the DLL and its safe `1.0×` detours resident until the target exits.

## Consequences

V1 does not call remote `FreeLibrary`. A future hot-unload design requires proven detour rundown, code-lifetime safety and a no-jump reconciliation between virtual and real epochs.

