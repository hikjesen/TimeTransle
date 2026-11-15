# ADR 0001: Supported targets

- Status: Accepted
- Date: 2026-07-26

## Decision

V1 runs on x64 Windows 10/11 and connects only to a user-selected, already-running x86/WOW64 or x64 process.

ARM64/ARM64EC, protected processes, PPL, and critical system processes are rejected. The product does not classify a target as a game, single-player, networked, competitive, anti-cheat protected, AppContainer, or mitigation-incompatible before connection.

An elevated target is not rejected merely for being elevated. The default Controller remains non-elevated. If the selected target has a higher integrity level, the product may offer a clearly labeled, user-confirmed Windows standard `runas`/UAC relaunch. The new Controller re-enumerates and re-inspects the target and never auto-injects. Silent elevation, UAC bypass, token theft and `SeDebugPrivilege` escalation remain prohibited.

## Consequences

Every hard-gate decision is fail-closed and bound to `PID + creationTime`. After the hard gates pass, the product transparently attempts `LoadLibraryW + CreateRemoteThread` and reports the actual access, load, or Hook result. It does not expose a protection-bypass path.

Eligibility is not based on title, publisher, store, installation path, launcher, window title, network status, or loaded module names. The admission layer does not scan risk fragments and does not pre-reject AppContainer, Dynamic Code, signature, or CFG policy.

## Implementation status

The TestApp executable-name gate, mitigation prechecks, module-risk scan, and existing-Hook module scan have been removed. The active path retains only identity, architecture, integrity, protected/PPL, critical-process, and fixed product DLL identity gates. Duplicate sessions are reported by Hook initialization. TestApp32/64 remain mandatory deterministic fixtures, while ordinary-name fixtures verify that names and module labels do not affect admission.
