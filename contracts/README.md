# TimeTransle protocol contracts

This directory contains the implementation-facing V1 contracts shared by native code, tests and the WebView UI.

- `ipc-v1.md` fixes the binary named-pipe frame and message identifiers.
- `error-codes.md` fixes numeric and symbolic error identifiers.
- `webview-v1.schema.json` validates UI-to-Controller commands.
- `test-vectors/` contains byte-level and JSON examples for later serializers.

The architecture document remains authoritative for lifecycle and security behavior. Contract changes that alter a frozen product boundary require explicit user approval and an ADR update.

