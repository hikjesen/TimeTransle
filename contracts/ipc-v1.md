# TimeTransle IPC protocol V1

> Status: Implemented wire contract, protocol version 1.2.

## Transport

- Local named pipe only; remote clients are rejected.
- Controller is the pipe server and Hook DLL is the client.
- Implementations must support partial reads and writes even if message mode is used.
- All integers are unsigned little-endian unless stated otherwise.
- Compiler-native struct layout must never be transmitted.
- `payloadBytes` is limited to `65,536`; the complete V1 frame is limited to `65,600` bytes.
- Reserved bytes and unknown mandatory flags must be zero.

## Fixed 64-byte header

| Offset | Bytes | Field | V1 rule |
| ---: | ---: | --- | --- |
| `0` | `4` | `magic` | ASCII `TTLE` |
| `4` | `2` | `protocolMajor` | `1` |
| `6` | `2` | `protocolMinor` | `2` |
| `8` | `2` | `headerBytes` | `64` |
| `10` | `2` | `messageType` | One value from the table below |
| `12` | `4` | `flags` | `0` in V1 |
| `16` | `4` | `payloadBytes` | `0..65,536` |
| `20` | `16` | `sessionId` | Opaque 128-bit value; wire byte order is preserved |
| `36` | `8` | `requestId` | Request/response correlation ID |
| `44` | `8` | `commandSequence` | Strictly increasing for state-changing commands |
| `52` | `12` | `reserved` | All zero |
| `64` | variable | `payload` | Exactly `payloadBytes` bytes |

Validation order is:

1. Read exactly the fixed prefix needed to obtain `headerBytes` and `payloadBytes`.
2. Validate magic, supported header size, version, flags and size bounds before allocating.
3. Read the remaining header and reject non-zero reserved bytes.
4. Read exactly `payloadBytes`; EOF or extra bytes in a message frame are invalid.
5. Authenticate the session and validate message-specific payload fields.

An unsupported major or minor version terminates the session. Controller and Hook DLL are shipped as one build and require an exact version match. When the Hook DLL observes an unusable connection it invokes the local `1.0×`, `paused=false` recovery path.

## Message identifiers

| Value | Message | Direction |
| ---: | --- | --- |
| `1` | `HELLO` | DLL → Controller |
| `2` | `HELLO_ACK` | Controller → DLL |
| `3` | `GET_STATUS` | Controller → DLL |
| `4` | `STATUS` | DLL → Controller |
| `5` | `SET_SPEED` | Controller → DLL |
| `6` | `SET_SPEED_ACK` | DLL → Controller |
| `7` | `PAUSE` | Controller → DLL |
| `8` | `PAUSE_ACK` | DLL → Controller |
| `9` | `RESUME` | Controller → DLL |
| `10` | `RESUME_ACK` | DLL → Controller |
| `11` | `RESET` | Controller → DLL |
| `12` | `RESET_ACK` | DLL → Controller |
| `13` | `PING` | Either direction |
| `14` | `PONG` | Either direction |
| `15` | `SHUTDOWN` | Controller → DLL |
| `16` | `SHUTDOWN_ACK` | DLL → Controller |
| `17` | `ERROR` | Either direction |
| `18` | `HOOK_STATE_CHANGED` | DLL → Controller |

`SHUTDOWN` means logical session deactivation. It never authorizes remote `FreeLibrary`.

## State command invariants

- `SET_SPEED` carries one IEEE-754 binary64 value. It must be finite and within `[0.05, 8.0]`.
- `PAUSE` and `RESUME` are idempotent but still publish a revision and return authoritative state.
- `RESET` atomically sets `configuredSpeed=1.0` and `paused=false` using continuous re-anchoring.
- The DLL accepts state-changing commands through one serialized writer.
- An ACK is emitted after the new desired state has been published. For a Unity Mono backend, `effectState=pending` until the Unity main thread applies the matching revision.
- Responses echo `requestId` and `commandSequence`.
- A stale sequence returns `STALE_SEQUENCE` without applying the payload.

STATUS and state ACK use this fixed 40-byte payload:

| Offset | Bytes | Field |
| ---: | ---: | --- |
| `0` | `8` | `configuredSpeed`, IEEE-754 binary64 |
| `8` | `8` | `effectiveSpeed`, IEEE-754 binary64 |
| `16` | `8` | `clockRevision`, u64 |
| `24` | `8` | `lastAppliedCommandSequence`, u64 |
| `32` | `1` | `paused`, boolean |
| `33` | `1` | `acceptingCommands`, boolean |
| `34` | `1` | `disconnectRecoveryOccurred`, boolean |
| `35` | `1` | `targetRuntime`: `0=generic`, `1=unityMono` |
| `36` | `1` | `timeBackend`: `0=win32Clock`, `1=unityMono` |
| `37` | `1` | `effectState`: `0=unverified`, `1=pending`, `2=applied`, `3=degraded`, `4=failed` |
| `38` | `2` | Reserved; both bytes must be zero |

For the generic backend, an ACK confirms Hook state synchronization but the observable target effect remains `unverified`. For Unity Mono, `lastAppliedCommandSequence` advances only after the corresponding `Time.timeScale` value has been written and read back on the Unity main thread.

## 检查点扩展（1.2）

| 值 | 消息 | 方向 |
| --- | --- | --- |
| 19 | SAVE_CHECKPOINT | Controller → DLL |
| 20 | SAVE_CHECKPOINT_ACK | DLL → Controller |
| 21 | LOAD_CHECKPOINT | Controller → DLL |
| 22 | LOAD_CHECKPOINT_ACK | DLL → Controller |
| 23 | GET_CHECKPOINT_STATUS | Controller → DLL |
| 24 | CHECKPOINT_STATUS | DLL → Controller |

请求为空载荷、无路径和槽编号；保存和读取递增 commandSequence，查询使用 0。三个成功响应均为 56 字节：原有 40 字节时间状态布局不变，后接 16 字节检查点状态。偏移 40/44/48/52 依次为 available、hasSnapshot、operation（0=空闲、1=保存、2=读取）、lastError，各 u32 小端，前两个字段仅 0 或 1。失败返回原 ERROR，错误码 4100–4106 为 unavailable、empty、busy、capture_failed、restore_failed、device_reset、allocation_outside_arena；会话继续。

初始化 flags 新增 4（检查点启动模式）；InjectorBootstrap flags 新增 2，翻译为初始化 flags 4。x64 初始化仍支持时间控制，检查点 available=false。数据留在目标运行时，不经管道传输。

LOAD 的 ACK 在恢复后第一张重新绘制的场景成功 Present 后发送。虚拟时间可以显式回退，但时钟 revision 与命令序号继续递增。保留当前 speed 与 paused。保存成功替换单槽，保存失败保留旧槽。
