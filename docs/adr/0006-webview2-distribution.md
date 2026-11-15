# ADR 0006: WebView2 distribution

- Status: Accepted
- Date: 2026-07-26

## Decision

The x64 Win32 Controller hosts a WebView2 Evergreen Runtime and loads only packaged Vite static assets from a trusted local origin. React sends high-level intents through a strictly validated JSON bridge and never receives process handles, DLL paths, pipe names or session secrets.

## Consequences

The portable package detects a missing runtime and links to the official Evergreen installer. WebView2 initialization and origin checks are native security boundaries, not frontend conventions.

