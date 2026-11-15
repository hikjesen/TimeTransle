# ADR 0002: Cross-architecture injectors

- Status: Accepted
- Date: 2026-07-26

## Decision

The Controller is x64. It delegates ordinary DLL loading to a short-lived Injector matching the target architecture:

- `TimeTransleInjector32.exe` for x86/WOW64 targets.
- `TimeTransleInjector64.exe` for x64 targets.

Injector and Hook sources are shared across architectures. The Injector accepts only the fixed product DLL selected from the trusted installation layout; it is not a general-purpose injector.

## Consequences

x86 and x64 use separate CMake build trees and artifacts. Target identity and architecture are checked again immediately before any write.

