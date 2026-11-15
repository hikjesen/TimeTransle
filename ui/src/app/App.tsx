import { Dialog } from '@base-ui/react/dialog';
import { NumberField } from '@base-ui/react/number-field';
import {
  useEffect,
  useMemo,
  useReducer,
  useRef,
  useState,
} from 'react';

import {
  isValidSpeed,
  type NativeMessage,
  type ProcessSummary,
} from '../bridge/contracts';
import { NativeBridge } from '../bridge/nativeBridge';
import { ClockDialLift } from '../components/ClockDialLift/ClockDialLift';
import {
  controllerReducer,
  createInitialControllerState,
} from '../state/controllerState';
import styles from './App.module.css';

function PauseIcon({ paused }: { paused: boolean }) {
  return paused ? (
    <svg viewBox="0 0 20 20" aria-hidden="true">
      <path d="m7 5 8 5-8 5V5Z" />
    </svg>
  ) : (
    <svg viewBox="0 0 20 20" aria-hidden="true">
      <path d="M6.5 5.5v9M13.5 5.5v9" />
    </svg>
  );
}

function ResetIcon() {
  return (
    <svg viewBox="0 0 20 20" aria-hidden="true">
      <path d="M5.3 6.4A6 6 0 1 1 4 10.2M5.2 3.8v3.1H2.1" />
    </svg>
  );
}

function TargetIcon() {
  return (
    <svg viewBox="0 0 20 20" aria-hidden="true">
      <circle cx="10" cy="10" r="5.5" />
      <circle cx="10" cy="10" r="1.5" />
      <path d="M10 1.8v2.4M10 15.8v2.4M1.8 10h2.4M15.8 10h2.4" />
    </svg>
  );
}

function DetailsIcon() {
  return (
    <svg viewBox="0 0 20 20" aria-hidden="true">
      <path d="M4 5.5h12M4 10h12M4 14.5h12" />
      <circle cx="8" cy="5.5" r="1.4" />
      <circle cx="13" cy="10" r="1.4" />
      <circle cx="7" cy="14.5" r="1.4" />
    </svg>
  );
}

function CloseIcon() {
  return (
    <svg viewBox="0 0 20 20" aria-hidden="true">
      <path d="m6 6 8 8M14 6l-8 8" />
    </svg>
  );
}

function errorText(error: string): string {
  const known: Record<string, string> = {
    CHECKPOINT_UNAVAILABLE: '当前运行模型不支持检查点，或应用内存已超出 arena。',
    CHECKPOINT_EMPTY: '还没有存档。',
    CHECKPOINT_BUSY: '正在处理存读档。',
    CHECKPOINT_CAPTURE_FAILED: '无法保存内存或设备资源，旧存档仍保留。',
    CHECKPOINT_RESTORE_FAILED: '读取内存或设备资源失败。',
    CHECKPOINT_ALLOCATION_OUTSIDE_ARENA: '应用分配已落到检查点 arena 外，本次会话的存读档不可用。',
    CHECKPOINT_DEVICE_RESET: '图形设备已重置，存档已清除。',
    launchFailed: 'Windows 无法启动此程序。',
    hookNotConnected: '目标尚未连接，设置仅保留在本地。',
    invalidState: '当前状态暂时不能执行此操作。',
    staleUiSequence: '已忽略过期操作。',
    wrongTargetGeneration: '目标已经变化，请重新选择。',
    targetIdentityChanged: '目标进程已经变化，请重新选择。',
    targetUnavailable: '目标程序当前不可用。',
    attachStartFailed: '无法启动目标连接。',
    productLayoutInvalid: '程序文件不完整，请重新安装。',
    hookStateInvalid: '目标返回了无效的时钟状态。',
    staleHookAcknowledgement: '已忽略过期的目标确认。',
    PIPE_DISCONNECTED: '目标连接已断开，并已触发恢复 1× 保护。',
    TIMEOUT: '目标暂时没有响应。',
    ACCESS_DENIED: '当前权限不足，无法连接目标。',
    ARCH_UNSUPPORTED: '缺少与目标架构匹配的连接组件。',
    AUTHENTICATION_FAILED: '目标连接身份校验失败。',
    TARGET_IDENTITY_CHANGED: '目标进程身份已经变化。',
    PROTECTED_PROCESS: 'Windows 受保护进程不允许连接。',
    CRITICAL_PROCESS: 'Windows 关键系统进程不允许连接。',
    INTEGRITY_MISMATCH: '目标权限高于 TimeTransle，需要以管理员身份重新启动。',
    DLL_NOT_TRUSTED: '本地时钟组件身份校验失败，请重新安装。',
    DLL_ARCH_MISMATCH: '时钟组件与目标架构不匹配。',
    DLL_BUILD_MISMATCH: '时钟组件与当前 Controller 构建不匹配。',
    DLL_LOAD_FAILED: '无法加载受信的时钟组件。',
    DLL_INITIALIZE_FAILED: '时钟组件初始化失败。',
    ALREADY_ATTACHED: '目标已存在 TimeTransle 会话。',
    HOOK_CREATE_FAILED: '时钟 Hook 创建失败。',
    HOOK_ENABLE_FAILED: '时钟 Hook 启用失败。',
    ELEVATION_FAILED: '无法以管理员身份重新启动 TimeTransle。',
    untrustedOrigin: '界面来源未通过安全校验。',
    invalidJson: 'Controller 返回了无效消息。',
    schemaViolation: '消息未通过格式校验。',
  };
  return known[error] ?? error;
}

type StatusLineState = 'idle' | 'running' | 'failed';

function statusLineState(
  controller: string,
  target: string,
  apply: string,
): StatusLineState {
  if (controller !== 'ready' || target === 'none' || target === 'exited') {
    return 'idle';
  }
  if (
    target === 'error' ||
    target === 'elevationRequired' ||
    apply === 'failed' ||
    apply === 'rejected'
  ) {
    return 'failed';
  }
  return 'running';
}

const STATUS_LINE_TEXT: Record<StatusLineState, string> = {
  idle: '等待抉择',
  running: '正在翻译',
  failed: '翻译失败',
};

export function App() {
  const [bridge] = useState(() => new NativeBridge());
  const [state, dispatch] = useReducer(
    controllerReducer,
    bridge.initialTargetGeneration,
    createInitialControllerState,
  );
  const stateRef = useRef(state);
  const [processPickerOpen, setProcessPickerOpen] = useState(false);
  const [detailsOpen, setDetailsOpen] = useState(false);
  const [processQuery, setProcessQuery] = useState('');
  const [preciseSpeed, setPreciseSpeed] = useState<number | null>(
    state.desiredSpeed,
  );

  useEffect(() => {
    stateRef.current = state;
  }, [state]);

  useEffect(() => {
    if (
      state.target !== 'hooked' ||
      state.effectState !== 'pending' ||
      state.apply !== 'inFlight'
    ) {
      return undefined;
    }
    const timeout = window.setTimeout(() => {
      dispatch({ type: 'markStalled' });
    }, 2_000);
    return () => window.clearTimeout(timeout);
  }, [state.target, state.effectState, state.apply, state.lastClockRevision]);

  useEffect(() => {
    if (!bridge.available) {
      dispatch({ type: 'bridgeUnavailable' });
      return;
    }
    dispatch({ type: 'bridgeReady' });
    const unsubscribe = bridge.subscribe((message: NativeMessage) => {
      const current = stateRef.current;
      if (
        message.uiEpoch !== undefined &&
        message.uiEpoch !== bridge.uiEpoch
      ) {
        return;
      }
      if (
        current.controllerSessionId !== null &&
        message.controllerSessionId !== current.controllerSessionId
      ) {
        return;
      }
      if (
        message.targetGeneration !== undefined &&
        current.selectedProcess !== null &&
        message.targetGeneration !== current.targetGeneration
      ) {
        return;
      }
      switch (message.type) {
        case 'processList':
          dispatch({
            type: 'processesReceived',
            processes: message.payload.processes,
          });
          break;
        case 'status':
        case 'targetSelected':
          dispatch({ type: 'statusReceived', message });
          break;
        case 'targetInspection':
          dispatch({
            type: 'targetInspection',
            message,
            errorText:
              message.payload.error === null
                ? null
                : errorText(message.payload.error),
          });
          if (message.payload.result !== 'ready') {
            setDetailsOpen(true);
          }
          break;
        case 'programLaunched':
        case 'elevatedTargetReady': {
          const process = message.payload.process;
          const targetGeneration = bridge.createTargetGeneration();
          dispatch({
            type: 'targetRequested',
            process,
            targetGeneration,
          });
          bridge.send('selectTarget', targetGeneration, {
            processId: process.processId,
            creationTime: process.creationTime,
          });
          break;
        }
        case 'elevationCancelled':
          dispatch({ type: 'elevationCancelled' });
          break;
        case 'checkpointStatus':
          dispatch({ type: 'checkpointReceived', checkpoint: message.payload });
          break;
        case 'shortcutInvoked': {
          const current = stateRef.current;
          if (message.payload.action !== 'toggleSpeed') {
            requestCheckpoint(message.payload.action);
            break;
          }
          if (current.target !== 'hooked' || current.checkpoint.operation !== 0) {
            break;
          }
          const targetSpeed = message.payload.targetSpeed;
          const atTarget =
            Math.abs(current.desiredSpeed - targetSpeed) < 0.005;
          if (atTarget && current.speedCheckpoint === null) {
            break;
          }
          const nextSpeed =
            atTarget && current.speedCheckpoint !== null
              ? current.speedCheckpoint
              : targetSpeed;
          dispatch({ type: 'shortcutToggleSpeed', targetSpeed });
          setPreciseSpeed(nextSpeed);
          if (current.desiredPaused) {
            bridge.send('resume', current.targetGeneration);
          }
          bridge.send('setSpeed', current.targetGeneration, {
            speed: nextSpeed,
          });
          break;
        }
        case 'speedApplied':
          dispatch({ type: 'speedApplied', message });
          break;
        case 'targetExited':
          dispatch({
            type: 'targetExited',
            targetGeneration: message.targetGeneration,
          });
          break;
        case 'commandRejected':
          dispatch({
            type: 'commandRejected',
            errorCode: message.error,
            error: errorText(message.error),
          });
          break;
      }
    });
    bridge.send(
      'getStatus',
      stateRef.current.targetGeneration,
    );
    return unsubscribe;
  }, [bridge]);

  const filteredProcesses = useMemo(() => {
    const query = processQuery.trim().toLocaleLowerCase();
    if (!query) {
      return state.processes;
    }
    return state.processes.filter(
      (process) =>
        process.name.toLocaleLowerCase().includes(query) ||
        String(process.processId).includes(query),
    );
  }, [processQuery, state.processes]);

  const hooked = state.target === 'hooked';
  const statusLine = statusLineState(
    state.controller,
    state.target,
    state.apply,
  );
  const busy = state.checkpoint.operation !== 0;
  const mainStatus = state.checkpoint.operation === 1 ? '保存中'
    : state.checkpoint.operation === 2 ? '读取中'
    : state.checkpoint.error ? '存读档失败'
    : state.checkpoint.hasSnapshot ? '存档就绪' : STATUS_LINE_TEXT[statusLine];
  const failureReason =
    (state.checkpoint.error ? errorText(state.checkpoint.error) : null) ?? state.lastError ??
    (state.lastErrorCode ? errorText(state.lastErrorCode) : null);

  const sendSpeed = (speed: number) => {
    if (stateRef.current.checkpoint.operation !== 0) return;
    if (stateRef.current.target === 'hooked') {
      bridge.send(
        'setSpeed',
        stateRef.current.targetGeneration,
        { speed },
      );
    }
  };

  const previewSpeed = (speed: number) => {
    sendSpeed(speed);
  };

  const commitSpeed = (speed: number) => {
    if (stateRef.current.checkpoint.operation !== 0) return;
    dispatch({
      type: 'setDesiredSpeed',
      speed,
      inFlight: hooked,
    });
    setPreciseSpeed(speed);
    sendSpeed(speed);
  };

  const selectProcess = (process: ProcessSummary) => {
    const targetGeneration = bridge.createTargetGeneration();
    dispatch({
      type: 'targetRequested',
      process,
      targetGeneration,
    });
    bridge.send('selectTarget', targetGeneration, {
      processId: process.processId,
      creationTime: process.creationTime,
    });
    setProcessPickerOpen(false);
  };

  const relaunchElevated = () => {
    if (
      stateRef.current.target !== 'elevationRequired' ||
      stateRef.current.lastErrorCode !== 'INTEGRITY_MISMATCH'
    ) {
      return;
    }
    bridge.send(
      'relaunchElevated',
      stateRef.current.targetGeneration,
    );
  };

  const openProcessPicker = (open: boolean) => {
    setProcessPickerOpen(open);
    if (open) {
      setProcessQuery('');
      bridge.send(
        'listProcesses',
        stateRef.current.targetGeneration,
      );
    }
  };

  const togglePause = () => {
    if (!hooked || busy) {
      return;
    }
    const paused = !state.desiredPaused;
    dispatch({ type: 'setDesiredPaused', paused });
    bridge.send(
      paused ? 'pause' : 'resume',
      state.targetGeneration,
    );
  };

  const resetToOne = () => {
    if (busy) return;
    dispatch({
      type: 'setDesiredSpeed',
      speed: 1,
      inFlight: hooked,
    });
    dispatch({ type: 'setDesiredPaused', paused: false });
    setPreciseSpeed(1);
    if (hooked) {
      bridge.send('reset', state.targetGeneration);
    }
  };

  const commitPreciseSpeed = (value: number | null) => {
    if (!isValidSpeed(value)) {
      dispatch({
        type: 'commandRejected',
        errorCode: 'INVALID_SPEED',
        error: '倍率必须在 0.05× 到 8.00× 之间。',
      });
      return;
    }
    commitSpeed(value);
  };

  function requestCheckpoint(action: 'saveCheckpoint' | 'loadCheckpoint') {
    const current = stateRef.current;
    if (current.target !== 'hooked' || !current.checkpoint.available ||
        current.checkpoint.operation !== 0 ||
        (action === 'loadCheckpoint' && !current.checkpoint.hasSnapshot)) return;
    const operation = action === 'saveCheckpoint' ? 1 : 2;
    stateRef.current = { ...current, checkpoint: { ...current.checkpoint, operation } };
    dispatch({ type: 'checkpointRequested', operation });
    bridge.send(action, current.targetGeneration);
  }

  return (
    <main className={styles.shell}>
      <header className={styles.header}>
        <div className={styles.identity}>
          <h1>TimeTransle</h1>
          <span
            className={styles.connectionDot}
            data-state={state.controller}
            aria-hidden="true"
          />
        </div>
        <Dialog.Root open={detailsOpen} onOpenChange={setDetailsOpen}>
          <Dialog.Trigger
            className={styles.iconButton}
            aria-label="打开详细状态与设置"
          >
            <DetailsIcon />
          </Dialog.Trigger>
          <Dialog.Portal>
            <Dialog.Backdrop className={styles.backdrop} />
            <Dialog.Viewport className={styles.dialogViewport}>
              <Dialog.Popup className={`${styles.panel} ${styles.detailsPanel}`}>
                <div className={styles.panelHeader}>
                  <div>
                    <Dialog.Title>详细状态</Dialog.Title>
                    <Dialog.Description>
                      精确倍率与当前连接信息
                    </Dialog.Description>
                  </div>
                  <Dialog.Close className={styles.iconButton} aria-label="关闭详细状态">
                    <CloseIcon />
                  </Dialog.Close>
                </div>

                <section className={styles.preciseSection}>
                  <label htmlFor="precise-speed">精确倍率</label>
                  <NumberField.Root
                    disabled={busy}
                    id="precise-speed"
                    className={styles.numberField}
                    value={preciseSpeed}
                    min={0.05}
                    max={8}
                    step={0.05}
                    smallStep={0.01}
                    largeStep={0.5}
                    onValueChange={setPreciseSpeed}
                    onValueCommitted={commitPreciseSpeed}
                  >
                    <NumberField.Group className={styles.numberGroup}>
                      <NumberField.Decrement aria-label="降低倍率">−</NumberField.Decrement>
                      <NumberField.Input
                        aria-label="精确倍率"
                        onKeyDown={(event) => {
                          if (event.key === 'Enter') {
                            event.preventDefault();
                            commitPreciseSpeed(preciseSpeed);
                          }
                        }}
                      />
                      <span aria-hidden="true">×</span>
                      <NumberField.Increment aria-label="提高倍率">＋</NumberField.Increment>
                    </NumberField.Group>
                  </NumberField.Root>
                </section>

                <dl className={styles.detailsList}>
                  <div>
                    <dt>时间后端</dt>
                    <dd>
                      {state.timeBackend === 'unityMono'
                        ? 'Unity Mono'
                        : 'Win32 时钟'}
                    </dd>
                  </div>
                  <div>
                    <dt>效果状态</dt>
                    <dd>{mainStatus}</dd>
                  </div>
                  <div>
                    <dt>Controller</dt>
                    <dd>{state.controller === 'ready' ? '已连接' : '未连接'}</dd>
                  </div>
                  <div>
                    <dt>目标</dt>
                    <dd>{state.selectedProcess?.name ?? '未选择'}</dd>
                  </div>
                  <div>
                    <dt>期望</dt>
                    <dd>{state.desiredSpeed.toFixed(2)}×</dd>
                  </div>
                  <div>
                    <dt>已应用</dt>
                    <dd>
                      {state.appliedSpeed === null
                        ? '未知'
                        : `${state.appliedSpeed.toFixed(2)}×`}
                    </dd>
                  </div>
                </dl>

                {state.target === 'checking' ? (
                  <section className={styles.connectionGate} aria-live="polite">
                    <span>连接检查</span>
                    <p>正在核对进程身份、架构、权限和本地组件。</p>
                  </section>
                ) : null}

                {state.target === 'ready' && state.selectedProcess ? (
                  <section className={styles.connectionGate}>
                    <span>检查通过 · 正在自动连接</span>
                    <p>
                      {state.selectedProcess.name} · {state.selectedProcess.architecture} · PID{' '}
                      {state.selectedProcess.processId}
                    </p>
                  </section>
                ) : null}

                {state.target === 'elevationRequired' &&
                state.lastErrorCode === 'INTEGRITY_MISMATCH' ? (
                  <section className={styles.connectionGate}>
                    <span>需要匹配目标权限</span>
                    <p>
                      TimeTransle 将请求标准 Windows UAC
                      重启。新实例会重新枚举并检查目标，通过后自动连接。
                    </p>
                    <button type="button" onClick={relaunchElevated}>
                      以管理员身份重启 TimeTransle
                    </button>
                  </section>
                ) : null}

                {state.notice ? (
                  <p className={styles.noticeBox}>{state.notice}</p>
                ) : null}

                {failureReason ? (
                  <div className={styles.errorBox} role="alert">
                    <span>需要注意</span>
                    <p>{failureReason}</p>
                    <button type="button" onClick={() => dispatch({ type: 'clearError' })}>
                      知道了
                    </button>
                  </div>
                ) : (
                  <p className={styles.quietNote}>
                    诊断仅记录阶段、API/Hook 结果和稳定错误码。
                  </p>
                )}
                <button
                  type="button"
                  className={styles.diagnosticsButton}
                  onClick={() =>
                    bridge.send(
                      'openDiagnostics',
                      stateRef.current.targetGeneration,
                    )
                  }
                >
                  打开诊断日志文件夹
                </button>
              </Dialog.Popup>
            </Dialog.Viewport>
          </Dialog.Portal>
        </Dialog.Root>
      </header>

      <section className={styles.dialArea} aria-label="倍率控制" inert={busy}>
        <ClockDialLift
          value={state.desiredSpeed}
          appliedValue={state.appliedSpeed}
          paused={state.desiredPaused}
          onPreview={previewSpeed}
          onCommit={commitSpeed}
        />
      </section>

      <div className={styles.targetLine}>
        {statusLine === 'idle' ? (
          <span className={styles.targetStatus}>{mainStatus}</span>
        ) : (
          <>
            <span
              className={styles.targetName}
              title={state.selectedProcess?.name}
            >
              {state.selectedProcess?.name}
            </span>
            <span aria-hidden="true">·</span>
            {statusLine === 'failed' ? (
              <span className={styles.failureWrap}>
                <span className={styles.targetStatus}>{mainStatus}</span>
                {failureReason ? (
                  <span className={styles.statusTooltip} role="tooltip">
                    {failureReason}
                  </span>
                ) : null}
              </span>
            ) : (
              <span className={styles.targetStatus}>{mainStatus}</span>
            )}
          </>
        )}
        {state.lastError ? (
          <button
            type="button"
            className={styles.errorIndicator}
            onClick={() => setDetailsOpen(true)}
            aria-label="查看错误详情"
          >
            !
          </button>
        ) : null}
      </div>

      <nav className={styles.toolbar} aria-label="核心控制">
        <button
          type="button"
          className={styles.toolButton}
          disabled={!hooked || busy}
          onClick={togglePause}
        >
          <PauseIcon paused={state.desiredPaused} />
          <span>{state.desiredPaused ? '继续' : '暂停'}</span>
        </button>
        <button type="button" className={styles.toolButton} disabled={busy} onClick={resetToOne}>
          <ResetIcon />
          <span>1×</span>
        </button>
        <button type="button" className={styles.toolButton}
          disabled={!hooked || !state.checkpoint.available || busy}
          title="反引号 + F5" onClick={() => requestCheckpoint('saveCheckpoint')}>保存</button>
        <button type="button" className={styles.toolButton}
          disabled={!hooked || !state.checkpoint.hasSnapshot || !state.checkpoint.available || busy}
          title="反引号 + F9" onClick={() => requestCheckpoint('loadCheckpoint')}>读取</button>
        <Dialog.Root open={processPickerOpen} onOpenChange={openProcessPicker}>
          <Dialog.Trigger className={styles.toolButton}>
            <TargetIcon />
            <span>选择</span>
          </Dialog.Trigger>
          <Dialog.Portal>
            <Dialog.Backdrop className={styles.backdrop} />
            <Dialog.Viewport className={styles.dialogViewport}>
              <Dialog.Popup className={`${styles.panel} ${styles.processPanel}`}>
                <div className={styles.panelHeader}>
                  <div>
                    <Dialog.Title>选择目标程序</Dialog.Title>
                    <Dialog.Description>
                      当前运行且可识别 x86/x64 架构的进程
                    </Dialog.Description>
                  </div>
                  <Dialog.Close className={styles.iconButton} aria-label="关闭进程选择">
                    <CloseIcon />
                  </Dialog.Close>
                </div>
                <button type="button" className={styles.diagnosticsButton} onClick={() => {
                  setProcessPickerOpen(false);
                  bridge.send('launchProgram', stateRef.current.targetGeneration);
                }}>启动程序…</button>
                <input
                  className={styles.searchInput}
                  type="search"
                  value={processQuery}
                  onChange={(event) => setProcessQuery(event.target.value)}
                  placeholder="搜索名称或 PID"
                  aria-label="搜索进程"
                />
                <div className={styles.processList}>
                  {filteredProcesses.length > 0 ? (
                    filteredProcesses.map((process) => (
                      <button
                        type="button"
                        className={styles.processItem}
                        key={`${process.processId}-${process.creationTime}`}
                        onClick={() => selectProcess(process)}
                      >
                        {process.icon ? (
                          <img
                            className={styles.processIcon}
                            src={process.icon}
                            alt=""
                            width={28}
                            height={28}
                          />
                        ) : (
                          <span
                            className={styles.processIconFallback}
                            aria-hidden="true"
                          >
                            {process.name.charAt(0)}
                          </span>
                        )}
                        <span className={styles.processName}>
                          {process.name}
                        </span>
                        <small>
                          {process.architecture} · PID {process.processId}
                        </small>
                      </button>
                    ))
                  ) : (
                    <p className={styles.emptyState}>
                      {bridge.available ? '没有匹配的程序' : 'Controller 未连接'}
                    </p>
                  )}
                </div>
              </Dialog.Popup>
            </Dialog.Viewport>
          </Dialog.Portal>
        </Dialog.Root>
      </nav>

      <p className={styles.liveRegion} aria-live="polite">
        {statusLine === 'idle'
          ? mainStatus
          : `${state.selectedProcess?.name ?? ''} ${mainStatus}`}
      </p>
    </main>
  );
}
