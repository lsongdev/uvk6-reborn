import { html, render, useState, useEffect, useRef } from 'https://lsong.org/scripts/react/index.js';
import { SerialTransport } from './serial.js';
import { RadioLink, Radio, validateImage, flashFirmware } from './protocol.js';

const REPO = 'https://github.com/lsongdev/uvk6-reborn';
const VARIANTS = {
  'uvk6-si4732': { board: 'uvk6', label: 'UV-K6 · SI4732 改装版', detail: 'DP32G030 / 512 KiB EEPROM / SI4732' },
  'uvk6-bk1080': { board: 'uvk6', label: 'UV-K6 · BK1080 版', detail: 'DP32G030 / 512 KiB EEPROM / BK1080' },
  'uvk1-bk1080': { board: 'uvk1', label: 'UV-K1 · MiniKong', detail: 'PY32F071 / 2 MiB SPI Flash / BK1080' },
};
const formatBytes = size => `${(size / 1024).toLocaleString('en-US', { maximumFractionDigits: 2 })} KiB`;
const hex = (value, length = 4) => value.toString(16).toUpperCase().padStart(length, '0');
const download = (data, filename, type = 'application/octet-stream') => {
  const url = URL.createObjectURL(new Blob([data], { type }));
  const link = document.createElement('a');
  link.href = url; link.download = filename;
  document.body.append(link); link.click(); link.remove();
  setTimeout(() => URL.revokeObjectURL(url), 10000);
};
const stamp = () => new Date().toISOString().replace(/[:.]/g, '-');
async function sha256(bytes) {
  const digest = await crypto.subtle.digest('SHA-256', bytes);
  return Array.from(new Uint8Array(digest), byte => byte.toString(16).padStart(2, '0')).join('');
}

function Metric({ label, value }) {
  return html`<div class="metric"><dt>${label}</dt><dd>${value ?? '—'}</dd></div>`;
}

function App() {
  const [variant, setVariant] = useState('uvk6-si4732');
  const [manifest, setManifest] = useState(null);
  const [manifestError, setManifestError] = useState('');
  const [image, setImage] = useState(null);
  const [busy, setBusy] = useState('');
  const [progress, setProgress] = useState(null);
  const [notice, setNotice] = useState(null);
  const [logs, setLogs] = useState([]);
  const [info, setInfo] = useState(null);
  const [stats, setStats] = useState(null);
  const [rf, setRf] = useState(null);
  const [backupKind, setBackupKind] = useState('calibration');
  const [backupFile, setBackupFile] = useState(null);
  const [acknowledged, setAcknowledged] = useState(false);
  const transport = useRef(null), radio = useRef(null), task = useRef(''), abort = useRef(null), fileInput = useRef(null);
  const supported = !!navigator.serial && globalThis.isSecureContext;
  const selected = VARIANTS[variant];
  const log = message => setLogs(previous => [...previous.slice(-199), `${new Date().toLocaleTimeString('zh-CN', { hour12: false })}  ${message}`]);
  const updateProgress = (done, total) => setProgress({ done, total });

  useEffect(() => {
    const controller = new AbortController();
    fetch('./firmware/manifest.json', { cache: 'no-cache', signal: AbortSignal.any([controller.signal, AbortSignal.timeout(30000)]) })
      .then(response => { if (!response.ok) throw new Error('本站固件尚未发布，可先选择本地 .bin。'); return response.json(); })
      .then(value => { if (!value.variants || !value.commit) throw new Error('固件索引无效，请使用本地文件。'); setManifest(value); })
      .catch(error => { if (error.name !== 'AbortError') setManifestError(error.name === 'TimeoutError' ? '固件索引加载超时，请刷新页面或选择本地 .bin。' : error.message); });
    return () => controller.abort();
  }, []);

  useEffect(() => {
    const disconnected = event => {
      if ((event.port || event.target) !== transport.current?.port) return;
      setInfo(null); setStats(null); setRf(null);
      radio.current = null;
      abort.current?.abort();
      log('设备已拔出。');
      setNotice({ error: true, message: '设备已断开。刷写若未完成，请重新进入 Bootloader 后完整重刷。' });
      if (!task.current) disconnect().catch(() => {});
    };
    const beforeUnload = event => {
      if (task.current === '刷写固件' || task.current === '备份存储') { event.preventDefault(); event.returnValue = ''; }
    };
    navigator.serial?.addEventListener('disconnect', disconnected);
    window.addEventListener('beforeunload', beforeUnload);
    return () => { navigator.serial?.removeEventListener('disconnect', disconnected); window.removeEventListener('beforeunload', beforeUnload); };
  }, []);

  async function disconnect() {
    const current = transport.current;
    transport.current = null; radio.current = null;
    setInfo(null); setStats(null); setRf(null);
    if (current) await current.close();
  }

  async function run(label, operation) {
    if (task.current) return;
    task.current = label; setBusy(label); setNotice(null); setProgress(null);
    try { await operation(); }
    catch (error) {
      const cancelled = error.name === 'NotFoundError' || error.name === 'AbortError';
      const suffix = label === '刷写固件' ? ' 若刷写已开始，请重新进入 Bootloader 后完整重刷。' : '';
      const timedOut = label === '下载固件' && error.name === 'TimeoutError';
      const message = cancelled ? (label === '备份存储' ? '备份已取消，没有生成不完整文件。' : '已取消连接设备。') : timedOut ? '固件下载超时，请重试或选择本地 .bin 文件。' : `${error.message}${suffix}`;
      setNotice({ error: !cancelled, message }); log(message);
      // A failed exchange must not leave a seemingly usable session in the UI.
      if (['连接设备', '刷新诊断', '读取 RF', '备份存储', '重启设备'].includes(label)) await disconnect().catch(() => {});
    } finally { task.current = ''; setBusy(''); abort.current = null; }
  }

  function chooseVariant(value) {
    setVariant(value); setImage(null); setAcknowledged(false); setNotice(null);
    if (fileInput.current) fileInput.current.value = '';
  }

  function loadLocal(event) {
    const file = event.currentTarget.files?.[0];
    if (!file) return;
    run('检查固件', async () => {
      setImage(null); setAcknowledged(false);
      if (!file.name.endsWith('.bin') || /packed/i.test(file.name)) throw new Error('请选择原始 .bin 固件，不支持 .packed.bin。');
      if (file.size > (selected.board === 'uvk6' ? 61440 : 120832)) throw new Error('文件超出所选机型的应用分区。');
      const bytes = new Uint8Array(await file.arrayBuffer());
      validateImage(bytes, selected.board);
      const digest = await sha256(bytes);
      setImage({ bytes, name: file.name, sha256: digest, local: true });
      log(`本地固件已检查：${file.name} (${bytes.length} B)。`);
    });
  }

  function loadPublished() {
    run('下载固件', async () => {
      setImage(null); setAcknowledged(false);
      const entry = manifest?.variants?.[variant];
      if (!entry || entry.board !== selected.board || entry.variant !== variant || entry.file !== `${variant}.bin`) throw new Error('本站尚未提供该机型固件，请选择本地文件。');
      const response = await fetch(`./firmware/${entry.file}`, { cache: 'no-cache', signal: AbortSignal.timeout(30000) });
      if (!response.ok) throw new Error('固件下载失败，请重试或选择本地文件。');
      const bytes = new Uint8Array(await response.arrayBuffer());
      const digest = await sha256(bytes);
      if (bytes.length !== entry.bytes || digest !== entry.sha256) throw new Error('固件 SHA-256 或长度校验失败，已拒绝使用。请刷新页面重试。');
      validateImage(bytes, selected.board);
      setImage({ bytes, name: entry.file, sha256: digest, local: false });
      log(`网站固件 SHA-256 已验证：${entry.file}。`);
    });
  }

  function flash() {
    run('刷写固件', async () => {
      if (!image || !acknowledged || info) throw new Error('请先选择固件、确认机型，并断开应用连接。');
      validateImage(image.bytes, selected.board);
      const connection = await SerialTransport.request();
      transport.current = connection;
      try {
        await flashFirmware(new RadioLink(connection), image.bytes, selected.board, { log, progress: updateProgress });
        setNotice({ message: '刷写完成，全部数据块已确认。请关闭电源，再正常开机，然后点击「连接设备」。' });
        setAcknowledged(false);
      } finally { await disconnect(); }
    });
  }

  function connect() {
    run('连接设备', async () => {
      const connection = await SerialTransport.request();
      transport.current = connection;
      const device = new Radio(new RadioLink(connection, false));
      const details = await device.connect();
      radio.current = device;
      setInfo(details);
      log(`已连接：${details.version} / ${details.storage} / ${formatBytes(details.capacity)}。`);
      // A diagnostic timeout should not prevent a usable storage connection.
      try { setStats(await device.diagnostics()); }
      catch (error) { log(`诊断暂不可用：${error.message}`); }
      setNotice({ message: '设备已连接，可读取诊断或备份存储。' });
    });
  }

  function backup() {
    run('备份存储', async () => {
      setBackupFile(null);
      const full = backupKind === 'full';
      const k1 = info.capacity === 2097152;
      const address = full ? 0 : k1 ? 0x10000 : 0x1e00;
      const size = full ? info.capacity : k1 ? 4096 : 512;
      abort.current = new AbortController();
      log(`开始备份 0x${hex(address, 6)} 起 ${size} B；设备端只读。`);
      const bytes = await radio.current.backup(address, size, { progress: updateProgress, signal: abort.current.signal });
      const digest = await sha256(bytes);
      const name = `uvk${k1 ? '1' : '6'}-${full ? 'storage' : 'calibration'}-${stamp()}.bin`;
      const metadata = { file: name, address, bytes: size, sha256: digest, firmware: info.version, storage: info.storage, capacity: info.capacity };
      setBackupFile({ bytes, name, metadata });
      download(bytes, name);
      setNotice({ message: `备份完成：${formatBytes(size)}。文件已生成，可再次下载或保存校验信息。` });
      log(`备份完成：${name} / SHA-256 ${digest}。`);
    });
  }

  return html`
    <div class="app-shell">
      <header class="site-header"><a class="brand" href="#"><img src="./favicon.svg" alt="" width="32" height="32"/><span>UVK6 <strong>Reborn</strong></span></a><nav aria-label="主导航"><a href="#flash">刷入固件</a><a href="#device">连接电台</a><a href=${REPO}>GitHub ↗</a></nav></header>
      <main>
        <section class="hero" aria-labelledby="hero-title"><div><p class="eyebrow">OPEN SOURCE · A NEW RADIO EXPERIENCE</p><h1 id="hero-title">熟悉的电台。<br/><span>全新的可能。</span></h1><p class="hero-copy">为泉盛 UV-K6 与 UV-K1 打造的开源固件。<br/>多工作槽、频谱探索、广播接收，从浏览器开始。</p><div class="hero-actions"><a class="button button-primary" href="#flash">开始刷机 <span aria-hidden="true">↓</span></a><a href=${`${REPO}/blob/main/docs/user-manual.md`}>阅读使用手册 ↗</a></div><p class="hero-note"><span class="dot"></span> 无需安装 · 设备数据保留在浏览器</p></div><div class="radio-preview"><div class="preview-top"><span>UVK6 REBORN</span><span>RX ●</span></div><img src="./assets/manual-main.png" alt="固件界面示意：频率视图和保存频道视图，使用示例数据渲染" width="1024" height="416"/><div class="preview-bottom"><span>4 个工作槽</span><span>200 个频道</span><span>探索更多频率</span></div><p>固件 UI 渲染示意 · 非实机实时画面</p></div></section>

        <div class="workspace-heading"><div><p class="eyebrow">YOUR RADIO, CONNECTED</p><h2>电台工作台</h2></div><span class="capability ${supported ? '' : 'unavailable'}"><span class="dot"></span>${supported ? '浏览器支持 Web Serial' : '需要支持 Web Serial 的浏览器'}</span></div>
        ${!supported && html`<aside class="notice" role="status">设备操作需要桌面版 Chrome / Edge，以及 HTTPS 或 localhost。你仍然可以浏览说明、下载固件。<a href="https://developer.chrome.com/docs/capabilities/serial">浏览器支持说明 ↗</a></aside>`}
        <div class="workbench">
          <section class="tool-panel" id="flash" aria-labelledby="flash-title"><div class="panel-heading"><span class="step">01</span><div><h3 id="flash-title">刷入固件</h3><p>为你的硬件选择正确的版本</p></div><span class="tag">BOOTLOADER</span></div>
            <fieldset disabled=${!!busy}><label for="variant">设备与广播芯片</label><select id="variant" value=${variant} onChange=${event => chooseVariant(event.currentTarget.value)}>${Object.entries(VARIANTS).map(([id, value]) => html`<option value=${id}>${value.label}</option>`)}</select><p class="field-note">${selected.detail}</p>
            <label>固件文件</label><div class="firmware-picker"><button class="button" disabled=${!manifest?.variants?.[variant]} onClick=${loadPublished}>使用网站固件</button><label class="button file-button" for="firmware-file">选择本地 .bin</label><input ref=${fileInput} id="firmware-file" type="file" accept=".bin" onChange=${loadLocal}/></div>
            <p class="field-note">${manifest ? html`构建 <a href=${`${REPO}/commit/${manifest.commit}`}>${manifest.commit.slice(0, 7)}</a> · 原始 .bin · 下载后验证 SHA-256` : manifestError || '正在获取网站固件…'}</p>
            ${image ? html`<div class="file-info"><span class="file-icon" aria-hidden="true">BIN</span><div><strong>${image.name}</strong><span>${formatBytes(image.bytes.length)} · ${image.local ? '本地文件，向量表已检查' : '网站文件，SHA-256 已验证'}</span></div></div><details class="hash"><summary>查看 SHA-256</summary><code>${image.sha256}</code></details><button class="text-button" onClick=${() => download(image.bytes, image.name)}>下载原始固件 ↓</button>` : html`<div class="file-empty">选择固件后，在此查看文件与校验信息。</div>`}
            <ol class="instructions"><li>使用 K 型 UART 写频线连接电脑与电台。</li><li>点击下方按钮，选择写频线的串口。</li><li>按住 <kbd>PTT</kbd> 开机，等待刷写完成。</li></ol><p class="field-note">K1 的机身 USB 仅供开机后的连接；刷机同样使用 K 型 UART 写频线。</p>
            <label class="confirm"><input type="checkbox" checked=${acknowledged} onChange=${event => setAcknowledged(event.currentTarget.checked)}/><span>我已确认机型与芯片、保存原厂校准备份，并会在刷写期间保持供电和连接。本地固件须来自可信来源。</span></label>
            <button class="button button-primary main-action" disabled=${!supported || !image || !acknowledged || !!info} onClick=${flash}>${busy === '刷写固件' ? '正在刷写…' : '连接串口并刷机'} <span aria-hidden="true">↗</span></button>${info && html`<p class="field-note">刷机前，请先断开下方的设备连接。</p>`}</fieldset>
          </section>

          <section class="tool-panel" id="device" aria-labelledby="device-title"><div class="panel-heading"><span class="step">02</span><div><h3 id="device-title">连接电台</h3><p>正常开机后，查看状态与备份数据</p></div><span class="tag ${info ? 'connected' : ''}">${info ? 'CONNECTED' : 'APPLICATION'}</span></div>
            <p class="connection-help">刷机完成后关闭电源，再正常开机。K6 使用 K 型 UART 写频线；K1 也可使用机身 USB 数据线。</p>
            <div class="connection-actions"><button class="button ${info ? '' : 'button-primary'}" disabled=${!!busy || !supported || !!info} onClick=${connect}>${busy === '连接设备' ? '正在连接…' : '连接设备'}</button><button class="button" disabled=${!!busy || !info} onClick=${() => run('断开连接', async () => { await disconnect(); log('连接已关闭。'); })}>断开连接</button><span class="field-note">38,400 baud · 8N1</span></div>
            <dl class="device-details"><${Metric} label="协议版本" value=${info?.version}/><${Metric} label="物理存储" value=${info ? formatBytes(info.capacity) : null}/><${Metric} label="存储器" value=${info?.storage}/><${Metric} label="接收频率" value=${stats ? `${(stats.frequency / 1e6).toFixed(5)} MHz` : null}/></dl>
            <div class="diagnostics"><div><h4>运行诊断</h4><span>${stats ? '最近一次读取' : '连接后读取设备状态'}</span></div><button class="button button-small" disabled=${!!busy || !info} onClick=${() => run('刷新诊断', async () => { setStats(await radio.current.diagnostics()); log('已刷新运行诊断。'); })}>刷新</button></div>
            <dl class="stats-grid"><${Metric} label="RSSI 原始值" value=${stats?.rssi}/><${Metric} label="噪声原始值" value=${stats?.noise}/><${Metric} label="最长主循环" value=${stats ? `${stats.loopMaxMs} ms` : null}/><${Metric} label="静噪状态" value=${stats ? (stats.open ? '打开' : '关闭') : null}/></dl>
            <div class="backup-section"><h4>备份存储</h4><p class="field-note">读取设备报告的物理容量。校准备份通常数秒，全量备份可能需数分钟；完成后下载。</p><div class="backup-actions"><select aria-label="备份范围" value=${backupKind} disabled=${!!busy} onChange=${event => setBackupKind(event.currentTarget.value)}><option value="calibration">原厂校准区域</option><option value="full">完整物理存储</option></select><button class="button" disabled=${!!busy || !info} onClick=${backup}>备份并下载 ↓</button>${busy === '备份存储' && html`<button class="button button-danger" onClick=${() => abort.current?.abort()}>取消</button>`}</div>${backupFile && html`<div class="download-again"><button class="text-button" onClick=${() => download(backupFile.bytes, backupFile.name)}>再次下载备份</button><button class="text-button" onClick=${() => download(JSON.stringify(backupFile.metadata, null, 2), `${backupFile.name}.json`, 'application/json')}>下载 SHA-256 信息</button></div>`}</div>
            <div class="secondary-actions"><button class="text-button" disabled=${!!busy || !info} onClick=${() => run('读取 RF', async () => { setRf(await radio.current.registers()); log('已读取 RF 寄存器白名单。'); })}>读取 RF 寄存器</button><button class="text-button" disabled=${!!busy || !stats} onClick=${() => download(JSON.stringify({ device: info, diagnostics: stats, rf, readAt: new Date().toISOString() }, null, 2), `reborn-diagnostics-${stamp()}.json`, 'application/json')}>导出诊断</button><button class="text-button" disabled=${!!busy || !info} onClick=${() => run('重启设备', async () => { await radio.current.reset(); await disconnect(); log('已发送重启命令，请稍后重新连接。'); })}>重启设备</button></div>
            <p class="field-note">诊断与备份均为只读操作。当前固件未提供远程调频、频道编辑接口。</p>
          </section>
        </div>
        <section class="activity" aria-label="操作状态与日志">${busy && html`<div class="task-state" role="status"><span class="dot pulse"></span>${busy}…${progress && html`<span>${Math.floor(progress.done / progress.total * 100)}% · ${progress.done.toLocaleString()} / ${progress.total.toLocaleString()} B</span>`}</div>`}${progress && html`<progress aria-label="操作进度" value=${progress.done} max=${progress.total}/>`}${notice && html`<div class="notice ${notice.error ? 'error' : 'success'}" role=${notice.error ? 'alert' : 'status'}>${notice.message}</div>`}<details open=${logs.length > 0}><summary>操作日志 <span>${logs.length ? `${logs.length} 条记录` : '等待开始'}</span></summary><pre class="log" aria-live="polite" aria-relevant="additions text">${logs.join('\n') || '连接和刷写记录将在这里显示。'}</pre><button class="text-button" disabled=${!logs.length} onClick=${() => download(logs.join('\n'), `reborn-log-${stamp()}.txt`, 'text/plain')}>导出日志 ↓</button></details></section>
        ${rf && html`<section class="rf-section"><h3>BK${rf.chip} · RF 寄存器快照</h3><p class="field-note">逐个读取的白名单寄存器；REG30 是请求值缓存，不代表同时采样。</p><div class="registers">${rf.values.map(([reg, value]) => html`<div><span>REG ${hex(reg, 2)}</span><code>0x${hex(value)}</code></div>`)}</div></section>`}
        <section class="about"><div><p class="eyebrow">BUILT TO EXPLORE</p><h2>把电台的可能，<br/>交还给你。</h2><a href=${`${REPO}/blob/main/docs/user-manual.md`}>打开完整使用手册 ↗</a></div><div class="features"><article><span>01 / TUNE</span><h3>四个独立工作槽</h3><p>按频段、频率或频道收听，在兼容接收路径间进行多槽守听。</p></article><article><span>02 / DISCOVER</span><h3>发现身边的信号</h3><p>频谱、搜台、频率捕获与接收亚音扫描，探索更多无线电活动。</p></article><article><span>03 / LISTEN</span><h3>从广播到边带</h3><p>WFM 广播接收。SI4732 改装硬件支持 RDS；SSB 另需外部补丁。</p></article><article><span>04 / OPEN</span><h3>开源，持续进化</h3><p>AGPL-3.0。代码、文档、构建记录与设备适配依据均可公开查看。</p></article></div></section>
      </main>
      <footer class="site-footer"><span>UVK6 Reborn <span class="muted">/ 让电台焕新</span></span><nav><a href=${REPO}>源码</a><a href=${`${REPO}/actions/workflows/c-cpp.yml`}>构建</a><a href=${`${REPO}/issues`}>反馈</a><a href=${`${REPO}/blob/main/LICENSE`}>AGPL-3.0</a></nav></footer>
    </div>`;
}

const root = document.getElementById('app');
root.replaceChildren();
render(html`<${App}/>`, root);
