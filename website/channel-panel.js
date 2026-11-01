import { html, useState, useEffect, useRef } from 'https://lsong.org/scripts/react/index.js';
import { MODES, TONES, newChannel, validateChannel, sameChannel, importChannels } from './channels.js';

const fields = [
  ['name', '名称（最多 16 字节）', 'text'], ['frequency', 'RX 频率 (Hz)', 'number'],
  ['step', '步进 (Hz)', 'number'], ['bandwidth', '带宽 (Hz，WFM 为 0)', 'number'],
  ['mode', '接收模式', MODES.map((v, i) => [i, v])], ['squelch', '静噪 (0–10)', 'number'],
  ['rxTone', 'RX 亚音', TONES], ['txEnabled', '允许发射', 'checkbox'],
  ['power', 'TX 功率', [[0, 'Low'], [1, 'Medium'], [2, 'High']]],
  ['shift', 'TX 偏移', [[0, '关闭'], [1, '+'], [2, '−'], [3, '异频']]],
  ['offset', '偏移量 (Hz)', 'number'], ['split', '异频 TX (Hz，0 未设)', 'number'],
  ['txTone', 'TX 亚音', TONES], ['tail', 'TX 尾音', 'checkbox'],
];
const show = c => c ? `${c.name || '未命名'} · ${(c.frequency / 1e6).toFixed(6)} MHz · ${MODES[c.mode]}` : '空频道';
const exportFile = channels => JSON.stringify({ format: 'reborn-channels-v1', channels }, null, 2);

export function ChannelPanel({ info, radio, busy, run, log, progress, download }) {
  const [rows, setRows] = useState([]), [selected, setSelected] = useState(1);
  const [form, setForm] = useState(null), [editing, setEditing] = useState(false);
  const [error, setError] = useState(''), [review, setReview] = useState(false), [confirmed, setConfirmed] = useState(false);
  const [known, setKnown] = useState(false), [filter, setFilter] = useState('');
  const controller = useRef(null);
  useEffect(() => {
    controller.current?.abort(); setKnown(false); setEditing(false); setReview(false); setConfirmed(false); setError('');
  }, [info]);
  const row = rows.find(r => r.number === selected);
  useEffect(() => { setForm(row?.draft ? { ...row.draft } : null); setEditing(false); }, [selected, rows]);
  const changes = rows.filter(r => !sameChannel(r.original, r.draft));
  useEffect(() => {
    const warn = event => { if (editing || changes.length) { event.preventDefault(); event.returnValue = ''; } };
    window.addEventListener('beforeunload', warn);
    return () => window.removeEventListener('beforeunload', warn);
  }, [editing, changes.length]);
  const locked = !!busy;
  function resetReview() { setReview(false); setConfirmed(false); setError(''); }
  function stage(value) {
    try {
      if (value) validateChannel(value);
      setRows(previous => previous.map(r => r.number === selected ? { ...r, draft: value ? { ...value } : null } : r));
      setEditing(false); resetReview();
    } catch (e) { setError(e.message); }
  }
  function readAll() {
    run('读取频道', async () => {
      const device = radio.current, result = [];
      controller.current = new AbortController(); setKnown(false); resetReview();
      for (let number = 1; number <= 200; number++) {
        controller.current.signal.throwIfAborted();
        const channel = await device.readChannel(number);
        result.push({ number, original: channel, draft: channel }); progress(number, 200);
        await new Promise(resolve => setTimeout(resolve, 20));
      }
      controller.current.signal.throwIfAborted();
      setRows(result); setKnown(true); setEditing(false); log('已读取全部 200 个频道。');
    });
  }
  function apply() {
    run('写入频道', async () => {
      if (!known || !confirmed || editing || !changes.length) throw new Error('请先读取频道并确认待写差异。');
      for (const r of changes) if (r.draft) validateChannel(r.draft);
      const device = radio.current;
      download(exportFile(rows.map(r => ({ number: r.number, channel: r.original }))), `reborn-channels-before-write-${Date.now()}.json`, 'application/json');
      setConfirmed(false);
      let done = 0;
      try {
        for (const r of changes) {
          const actual = await device.writeChannel(r.number, r.draft, r.original);
          setRows(previous => previous.map(item => item.number === r.number ? { ...item, original: actual, draft: actual } : item));
          log(`频道 ${r.number} ${actual ? '写入' : '删除'}并回读确认。`); progress(++done, changes.length);
        }
        setReview(false); log(`写频完成，${done} 个频道已回读确认。`);
      } catch (e) {
        setKnown(false);
        throw new Error(`${e.message} 已确认 ${done}/${changes.length} 个频道；已停止，请导出待写列表并重新读取核对。`);
      }
    });
  }
  function loadFile(event) {
    const input = event.currentTarget, file = input.files?.[0];
    if (!file) return;
    run('导入频道', async () => {
      try {
        if (file.size > 1024 * 1024) throw new Error('频道文件过大。');
        const imported = importChannels(await file.text());
        setRows(previous => previous.map(r => {
          const item = imported.find(c => c.number === r.number);
          return item ? { ...r, draft: item.channel } : r;
        }));
        resetReview(); log(`导入 ${imported.length} 个频道到待写列表，尚未写入设备。`);
      } catch (e) { setError(e.message); }
      finally { input.value = ''; }
    });
  }
  return html`<section class="tool-panel channel-panel" id="channels" aria-labelledby="channels-title">
    <div class="panel-heading"><span class="step">03</span><div><h3 id="channels-title">频道管理 / 写频</h3><p>200 个频道 · 中文名称 · 编辑后预览，再写入电台</p></div><span class="tag">${known ? 'READY' : 'READ FIRST'}</span></div>
    <p class="field-note">正常开机后先在上方连接设备。需要支持频道接口的 Reborn 固件。保存或删除会立即更新正在使用该频道的工作槽。</p>
    <div class="channel-toolbar">
      <button class="button" disabled=${locked || !info || editing} onClick=${readAll}>${changes.length ? '放弃待写修改并重新读取' : '读取全部频道'}</button>
      ${busy === '读取频道' && html`<button class="button" onClick=${() => controller.current?.abort()}>取消读取</button>`}
      <button class="button" disabled=${locked || !rows.length || editing} onClick=${() => download(exportFile(rows.map(r => ({ number: r.number, channel: r.draft }))), `reborn-channels-${Date.now()}.json`, 'application/json')}>导出频道 JSON</button>
      <label class="button" for="channel-import" aria-disabled=${locked || !known || editing}>导入频道 JSON</label>
      <input id="channel-import" type="file" accept=".json,application/json" disabled=${locked || !known || editing} onChange=${loadFile}/>
      <button class="button button-primary" disabled=${locked || !known || !changes.length || editing} onClick=${() => { setReview(true); setConfirmed(false); }}>预览 ${changes.length} 项修改</button>
    </div>
    ${!known && html`<p class="field-note">${rows.length ? '当前数据需要重新读取核对；可先导出待写列表。' : '读取完成后显示频道列表；导入文件只覆盖文件中列出的编号。JSON 中的空频道会加入删除预览。'}</p>`}
    ${rows.length > 0 && html`<div class="channel-layout"><div class="channel-list">
      <input aria-label="筛选频道" placeholder="搜索名称、频道号或频率" value=${filter} disabled=${editing} onInput=${e => setFilter(e.currentTarget.value)}/>
      <div class="channel-table-scroll"><table><thead><tr><th>频道</th><th>内容</th><th>修改</th></tr></thead><tbody>${rows.filter(r => `${r.number} ${show(r.draft)}`.toLowerCase().includes(filter.toLowerCase())).map(r => html`<tr aria-selected=${r.number === selected}><td><button class="text-button" disabled=${locked || editing} onClick=${() => { setSelected(r.number); resetReview(); }}>${String(r.number).padStart(3, '0')}</button></td><td>${show(r.draft)}</td><td>${sameChannel(r.original, r.draft) ? '—' : r.draft ? '待保存' : '待删除'}</td></tr>`)}</tbody></table></div>
    </div><div class="channel-editor"><h4>频道 ${String(selected).padStart(3, '0')}</h4>
      ${form ? html`<form onSubmit=${e => { e.preventDefault(); stage(form); }}><fieldset disabled=${locked || !known}><div class="channel-fields">${fields.map(([key, label, type]) => html`<label class=${type === 'checkbox' ? 'channel-check' : ''}>${label}${Array.isArray(type) ? html`<select value=${form[key]} onChange=${e => { setForm({ ...form, [key]: Number(e.currentTarget.value) }); setEditing(true); resetReview(); }}>${type.map(([value, text]) => html`<option value=${value}>${text}</option>`)}</select>` : html`<input type=${type} value=${type === 'checkbox' ? undefined : form[key]} checked=${type === 'checkbox' ? form[key] : undefined} step="1" required=${type === 'number'} onInput=${e => { setForm({ ...form, [key]: type === 'checkbox' ? e.currentTarget.checked : type === 'number' ? (e.currentTarget.value === '' ? NaN : Number(e.currentTarget.value)) : e.currentTarget.value }); setEditing(true); resetReview(); }}/>`}</label>`)}</div>
      <div class="channel-toolbar"><button class="button" type="submit">保存到待写列表</button><button class="button" type="button" onClick=${() => { setForm(row.draft ? { ...row.draft } : null); setEditing(false); setError(''); }}>撤销表单编辑</button><button class="button" type="button" onClick=${() => stage(null)}>标记删除</button></div></fieldset></form>` : html`<p class="field-note">空频道</p><button class="button" disabled=${locked || !known} onClick=${() => { setForm(newChannel()); setEditing(true); }}>新建频道</button>`}
      ${editing && html`<p class="field-note">表单尚未加入待写列表，请先保存或撤销，再选择其他频道。</p>`}
    </div></div>`}
    ${error && html`<p class="notice error" role="alert">${error}</p>`}
    ${review && html`<div class="channel-review"><h4>写入预览 · ${changes.length} 个频道</h4><p class="field-note">操作逐条完成；中断时已完成的频道会保留。写入前下载原频道 JSON 备份，每条写入后从设备重新读取核对。</p><ul>${changes.map(r => html`<li><strong>频道 ${r.number}：${r.draft ? r.original ? '修改' : '新建' : '删除'}</strong><p>原：${show(r.original)} → 新：${show(r.draft)}</p><div class="channel-diff">${fields.filter(([key]) => r.original?.[key] !== r.draft?.[key]).map(([key, label, type]) => {
      const display = value => value === undefined ? '—' : Array.isArray(type) ? type.find(([v]) => v === value)?.[1] ?? value : typeof value === 'boolean' ? value ? '开启' : '关闭' : value;
      return html`<span>${label}：${display(r.original?.[key])} → ${display(r.draft?.[key])}</span>`;
    })}</div></li>`)}</ul><label class="confirm"><input type="checkbox" checked=${confirmed} disabled=${locked} onChange=${e => setConfirmed(e.currentTarget.checked)}/><span>我已核对以上修改和删除，并同意将它们写入已连接的电台。</span></label><button class="button button-primary" disabled=${locked || !known || !confirmed || !changes.length || editing} onClick=${apply}>确认写入 ${changes.length} 个频道</button></div>`}
  </section>`;
}
