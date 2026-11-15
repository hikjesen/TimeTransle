# ADR 0008: Process picker icons and speed-toggle global shortcuts

- Status: Accepted
- Date: 2026-08-29

## Context

Version 0.1.5 changes two user-visible surfaces that touch frozen WebView payload boundaries:

- The process picker showed only name/PID/architecture, which is hard to tell apart visually. Each entry now carries the executable's icon, produced natively and shipped through the existing `processList` WebView message.
- The global shortcuts `Insert+F10/F11/F12` (set 1x, restore previous, show window) are replaced by `` `+F1/F2/F3 `` speed toggles and `` `+Esc `` window show/hide. The native `shortcutInvoked` message shape changes accordingly.

## Decision

- `enumerate_processes` additionally records the full image path via `QueryFullProcessImageNameW` (the handle is already opened with `PROCESS_QUERY_LIMITED_INFORMATION`).
- The Controller extracts a 32×32 icon with `SHGetFileInfoW(SHGFI_ICON | SHGFI_LARGEICON)`, converts it via `GetIconInfo` + `GetDIBits` (unpremultiplied to straight alpha, AND-mask fallback), encodes PNG in memory with GDI+ and sends a `data:image/png;base64,...` string (≤16384 chars) in the new `icon` field of each `processList` entry. Extraction results are cached per case-insensitive path, including negative results; entries without a path or icon send an empty string and the UI renders a first-letter placeholder.
- The Controller now links `gdiplus` (system component, not a fetched dependency; ADR 0007's pinning policy does not apply).
- `shortcutInvoked` becomes `{action: "toggleSpeed", targetSpeed: 1 | 0.4 | 2.5}` with exact-key validation on the UI side. Checkpoint save/restore lives in the UI reducer (`speedCheckpoint`), mirroring the previous `shortcutRestoreSpeed` design. `` `+Esc `` show/hide is handled entirely in the native window layer and never reaches the WebView.
- `webview-v1.schema.json` is unchanged: it validates UI→Controller commands only, and no new UI→Controller command is introduced.

## Consequences

- `processList` payloads grow by roughly 1–3 KB per distinct executable; the list is only requested when the picker opens and the cache keeps repeat opens cheap.
- GDI+ is initialized once per process and never shut down (process-lifetime); acceptable for the Controller executable.
- `SHGetFileInfoW` and `CreateStreamOnHGlobal` require the calling thread to run on the interactive desktop and inside an initialized COM apartment; `post_process_list` already runs on the controller's STA UI thread.
- Removing `setOne`/`restorePrevious` is a breaking WebView message change consumed only by our own bundled UI, updated in the same release (0.1.5).
