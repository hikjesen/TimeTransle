# TimeTransle third-party notices

> Reviewed direct-dependency inventory. `scripts/package.ps1` verifies this list, then collects exact upstream license/NOTICE texts for every installed production runtime dependency into each distributable package.

## Native runtime and test dependencies

| Dependency | Locked version/revision | License | Source |
| --- | --- | --- | --- |
| MinHook | `1.3.4` / `c3fcafdc10146beb5919319d0683e44e3c30d537` | BSD-2-Clause | https://github.com/TsudaKageyu/minhook |
| nlohmann/json | `3.12.0` / `65ee68451d8eb2b5f3a30b410476ab83deb3289b` | MIT | https://github.com/nlohmann/json |
| Catch2 | `3.15.0` / `b8acae26b0453dad6250447ad32f6b0015b162ae` | BSL-1.0 | https://github.com/catchorg/Catch2 |
| Microsoft.Web.WebView2 | `1.0.3856.49` | Microsoft WebView2 SDK license | https://www.nuget.org/packages/Microsoft.Web.WebView2/1.0.3856.49 |

## UI runtime dependencies

| Dependency | Locked version | License |
| --- | --- | --- |
| React | `19.2.8` | MIT |
| React DOM | `19.2.8` | MIT |
| @base-ui/react | `1.6.0` | MIT |
| Motion | `12.42.2` | MIT |
| @number-flow/react | `0.6.2` | MIT |
| Sonner | `2.0.7` | MIT |

## UI build and test dependencies

The authoritative transitive dependency inventory is `ui/package-lock.json`.

| Dependency | Locked version | License |
| --- | --- | --- |
| Vite | `8.1.5` | MIT |
| TypeScript | `5.9.3` | Apache-2.0 |
| Vitest | `4.1.10` | MIT |
| Ajv | `8.20.0` | MIT |

## Checkpoint prototype sources

The checkpoint prototype also references GTA Savestates, Hourglass, and pointlessdevice. Attribution, modification scope, and license-status notes are recorded in [third_party/checkpoint/NOTICE.md](third_party/checkpoint/NOTICE.md). The upstream Hourglass GPL v2 notice is preserved alongside it. No blanket license is asserted for all repository contents.
