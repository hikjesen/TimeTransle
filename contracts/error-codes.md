# TimeTransle stable error codes

> Status: V1 contract. Numeric values and symbolic names are stable once consumed by an implementation.

## Success

| Value | Name |
| ---: | --- |
| `0` | `OK` |

## Parameters and protocol (`1000–1999`)

| Value | Name |
| ---: | --- |
| `1000` | `INVALID_MESSAGE` |
| `1001` | `INVALID_SPEED` |
| `1002` | `PAYLOAD_TOO_LARGE` |
| `1003` | `UNSUPPORTED_PROTOCOL` |
| `1004` | `AUTHENTICATION_FAILED` |
| `1005` | `SESSION_MISMATCH` |
| `1006` | `TARGET_GENERATION_MISMATCH` |
| `1007` | `STALE_SEQUENCE` |
| `1008` | `TIMEOUT` |
| `1009` | `PIPE_DISCONNECTED` |

## Target and safety (`2000–2999`)

| Value | Name |
| ---: | --- |
| `2000` | `TARGET_NOT_FOUND` |
| `2001` | `TARGET_IDENTITY_CHANGED` |
| `2002` | `TARGET_EXITED` |
| `2003` | `ARCH_UNSUPPORTED` |
| `2004` | `ACCESS_DENIED` |
| `2005` | `INTEGRITY_MISMATCH` |
| `2006` | `PROTECTED_PROCESS` |
| `2007` | `CRITICAL_PROCESS` |
| `2008` | `APPCONTAINER_UNSUPPORTED` |
| `2009` | `MITIGATION_POLICY_UNSUPPORTED` |
| `2010` | `RISK_MODULE_DETECTED` |
| `2011` | `RISK_CHECK_INCOMPLETE` |
| `2012` | `ELEVATION_FAILED` |

## Loading and Hook (`3000–3999`)

| Value | Name |
| ---: | --- |
| `3000` | `DLL_NOT_TRUSTED` |
| `3001` | `DLL_ARCH_MISMATCH` |
| `3002` | `DLL_BUILD_MISMATCH` |
| `3003` | `DLL_LOAD_FAILED` |
| `3004` | `DLL_INITIALIZE_FAILED` |
| `3005` | `ALREADY_ATTACHED` |
| `3006` | `EXISTING_MODULE_INCOMPATIBLE` |
| `3007` | `HOOK_CREATE_FAILED` |
| `3008` | `HOOK_ENABLE_FAILED` |
| `3009` | `HOOK_ROLLBACK_FAILED` |
| `3010` | `HOOK_STATE_INVALID` |

## Lifecycle (`4000–4999`)

| Value | Name |
| ---: | --- |
| `4000` | `NOT_INITIALIZED` |
| `4001` | `ALREADY_DEACTIVATED` |
| `4002` | `CONTROLLER_RESTARTED` |
| `4003` | `SHUTDOWN_INCOMPLETE` |
| `4099` | `INTERNAL_ERROR` |
| `4100` | `CHECKPOINT_UNAVAILABLE` |
| `4101` | `CHECKPOINT_EMPTY` |
| `4102` | `CHECKPOINT_BUSY` |
| `4103` | `CHECKPOINT_CAPTURE_FAILED` |
| `4104` | `CHECKPOINT_RESTORE_FAILED` |
| `4105` | `CHECKPOINT_DEVICE_RESET` |
| `4106` | `CHECKPOINT_ALLOCATION_OUTSIDE_ARENA` |

Every error sent to UI includes one stable code plus a short, non-secret context message. It must not expose session tokens, raw handles, remote addresses or unnecessary absolute paths.

## Stage 11 classification rule

`ACCESS_DENIED` is reserved for an actual operating-system access failure, such as failing to obtain the minimum required process handle. It must not be used merely because a target executable is not a TimeTransle TestApp.

`INTEGRITY_MISMATCH` is recoverable for an explicitly selected ordinary offline target. The UI may offer a clearly labeled Windows standard UAC relaunch; the elevated Controller must re-identify the target and rerun every gate. Cancellation or elevation failure leaves the target disconnected and must not cause repeated prompts.

`APPCONTAINER_UNSUPPORTED`, `MITIGATION_POLICY_UNSUPPORTED`,
`RISK_MODULE_DETECTED`, and `RISK_CHECK_INCOMPLETE` remain reserved for ABI
compatibility. The ordinary-target admission path does not emit them and does
not pre-classify AppContainer, mitigation, network, game, or module-name
signals. Only `INTEGRITY_MISMATCH` is recoverable through the explicit standard
UAC relaunch flow. Other failures report the actual stable access, identity,
architecture, DLL-load, or Hook result. If a genuinely new result cannot be
represented accurately, update this contract, the C++ enum/string mapping,
tests, WebView validation, and frontend localization together before use.
