'use strict';
/* 우사기 시계 설정 페이지. 프레임워크 없이 REST API(/api/...)만 쓴다. */

const $ = (sel, root = document) => root.querySelector(sel);
const $$ = (sel, root = document) => Array.from(root.querySelectorAll(sel));
const DAY_NAMES = ['월', '화', '수', '목', '금', '토', '일'];

let alarms = [];
let sounds = { songs: [], clipsets: {} };
let settings = null;
let editing = null;   // 편집 중인 알람 id(새 알람이면 0)
let toastTimer = 0;

/* ===== 공통 ===== */

function toast(msg, isError = false) {
  const t = $('#toast');
  t.textContent = msg;
  t.classList.toggle('error', isError);
  t.hidden = false;
  clearTimeout(toastTimer);
  toastTimer = setTimeout(() => { t.hidden = true; }, isError ? 4000 : 2000);
}

async function api(method, path, body) {
  const opt = { method, headers: {} };
  if (body !== undefined) {
    opt.headers['Content-Type'] = 'application/json';
    opt.body = JSON.stringify(body);
  }
  const res = await fetch(path, opt);
  const text = await res.text();
  const data = text ? JSON.parse(text) : null;
  if (!res.ok) {
    throw new Error((data && data.error) || `HTTP ${res.status}`);
  }
  return data;
}

function twelve(h, m) {
  const pm = h >= 12;
  const h12 = h % 12 === 0 ? 12 : h % 12;
  return { text: `${h12}:${String(m).padStart(2, '0')}`, ampm: pm ? 'PM' : 'AM' };
}

function daysText(mask) {
  if (mask === 0) return '한 번만';
  if (mask === 127) return '매일';
  if (mask === 31) return '평일';
  if (mask === 96) return '주말';
  return DAY_NAMES.filter((_, i) => mask & (1 << i)).join(' ');
}

function bindRangeOutputs(root) {
  $$('input[type=range]', root).forEach((r) => {
    const out = r.parentElement.querySelector('output');
    const show = () => { if (out) out.textContent = r.value; };
    r.addEventListener('input', show);
    show();
  });
}

/* ===== 탭 ===== */

$$('.tabs button').forEach((b) => b.addEventListener('click', () => {
  $$('.tabs button').forEach((x) => x.classList.toggle('active', x === b));
  $$('.tab').forEach((t) => t.classList.toggle('active', t.id === `tab-${b.dataset.tab}`));
  if (b.dataset.tab === 'sounds') loadSounds();
  if (b.dataset.tab === 'status') loadStatus();
}));

/* ===== 알람 ===== */

function renderAlarms() {
  const list = $('#alarm-list');
  list.innerHTML = '';
  const sorted = [...alarms].sort((a, b) => a.hour * 60 + a.minute - (b.hour * 60 + b.minute));
  sorted.forEach((a) => {
    const t = twelve(a.hour, a.minute);
    const li = document.createElement('li');
    li.className = a.enabled ? '' : 'off';
    li.innerHTML = `
      <div class="alarm-main">
        <div class="alarm-time">${t.text}<small>${t.ampm}</small></div>
        <div class="alarm-sub"></div>
      </div>
      <input type="checkbox" class="switch" aria-label="켜기/끄기">`;
    const sub = [daysText(a.days_mask ?? a.days), a.pre_minutes ? `미리 ${a.pre_minutes}분` : '', a.label]
      .filter(Boolean).join(' · ');
    $('.alarm-sub', li).textContent = sub;
    const sw = $('.switch', li);
    sw.checked = a.enabled;
    sw.addEventListener('change', () => saveAlarm({ ...a, enabled: sw.checked }, a.id));
    $('.alarm-main', li).addEventListener('click', () => openEditor(a));
    list.appendChild(li);
  });
  $('#alarm-empty').hidden = alarms.length > 0;
}

async function loadAlarms() {
  try {
    alarms = (await api('GET', '/api/alarms')).alarms;
    renderAlarms();
  } catch (e) {
    toast(`알람을 불러오지 못했어요: ${e.message}`, true);
  }
}

async function saveAlarm(a, id) {
  const body = {
    id: id || 0, enabled: a.enabled, hour: a.hour, minute: a.minute, days: a.days,
    pre_minutes: a.pre_minutes, pre_volume: a.pre_volume, main_volume: a.main_volume,
    label: a.label || '', clipset: a.clipset || '', song: a.song || '',
  };
  try {
    if (id) await api('PUT', `/api/alarms/${id}`, body);
    else await api('POST', '/api/alarms', body);
    await loadAlarms();
    toast('저장했어요');
    return true;
  } catch (e) {
    toast(e.message, true);
    await loadAlarms();
    return false;
  }
}

function setDays(mask) {
  $$('.days button').forEach((b) => b.classList.toggle('on', (mask & (1 << Number(b.dataset.bit))) !== 0));
}

function getDays() {
  return $$('.days button').reduce((m, b) => (b.classList.contains('on') ? m | (1 << Number(b.dataset.bit)) : m), 0);
}

$$('.days button').forEach((b) => b.addEventListener('click', () => b.classList.toggle('on')));
$$('[data-preset]').forEach((b) => b.addEventListener('click', () => setDays(Number(b.dataset.preset))));

function fillSelect(sel, values, current, emptyLabel) {
  sel.innerHTML = '';
  const opt0 = new Option(emptyLabel, '');
  sel.add(opt0);
  values.forEach((v) => sel.add(new Option(v, v)));
  if (current && !values.includes(current)) sel.add(new Option(`${current} (없음)`, current));
  sel.value = current || '';
}

async function openEditor(a) {
  if (!sounds.songs.length && !Object.keys(sounds.clipsets).length) await loadSounds(true);
  const f = $('#alarm-form');
  const vol = settings ? settings.default_volume : 70;
  const cur = a || { enabled: true, hour: 7, minute: 0, days: 31, pre_minutes: 5, pre_volume: vol, main_volume: vol, label: '', clipset: '', song: '' };
  editing = a ? a.id : 0;
  $('#editor-title').textContent = a ? '알람 수정' : '새 알람';
  f.time.value = `${String(cur.hour).padStart(2, '0')}:${String(cur.minute).padStart(2, '0')}`;
  setDays(cur.days);
  f.label.value = cur.label;
  f.pre_minutes.value = cur.pre_minutes;
  f.pre_volume.value = cur.pre_volume;
  f.main_volume.value = cur.main_volume;
  fillSelect(f.song, sounds.songs.map((s) => s.name), cur.song, '첫 번째 곡');
  fillSelect(f.clipset, Object.keys(sounds.clipsets), cur.clipset, '기본 세트(default)');
  $('#delete-alarm').hidden = !a;
  bindRangeOutputs(f);
  $('#editor').showModal();
}

$('#add-alarm').addEventListener('click', () => {
  if (alarms.length >= 32) { toast('알람은 32개까지예요', true); return; }
  openEditor(null);
});
$('#cancel-alarm').addEventListener('click', () => $('#editor').close());

$('#alarm-form').addEventListener('submit', async (ev) => {
  ev.preventDefault();
  const f = ev.target;
  const [h, m] = f.time.value.split(':').map(Number);
  const pre = Number(f.pre_minutes.value);
  if (!(pre >= 0 && pre <= 60)) { toast('미리 알림은 0~60분이에요', true); return; }
  const ok = await saveAlarm({
    enabled: editing ? alarms.find((x) => x.id === editing).enabled : true,
    hour: h, minute: m, days: getDays(), pre_minutes: pre,
    pre_volume: Number(f.pre_volume.value), main_volume: Number(f.main_volume.value),
    label: f.label.value.trim(), clipset: f.clipset.value, song: f.song.value,
  }, editing);
  if (ok) $('#editor').close();
});

$('#delete-alarm').addEventListener('click', async () => {
  if (!editing || !confirm('이 알람을 지울까요?')) return;
  try {
    await api('DELETE', `/api/alarms/${editing}`);
    $('#editor').close();
    await loadAlarms();
    toast('지웠어요');
  } catch (e) { toast(e.message, true); }
});

/* ===== 소리 ===== */

async function loadSounds(quiet = false) {
  try {
    sounds = await api('GET', '/api/sounds');
  } catch (e) {
    sounds = { songs: [], clipsets: {} };
    if (!quiet) toast(`SD카드: ${e.message}`, true);
  }
  renderSounds();
}

function soundItem(name, extra, set) {
  const li = document.createElement('li');
  const span = document.createElement('span');
  span.className = 'alarm-sub';
  span.textContent = extra ? `${name} · ${extra}` : name;
  li.appendChild(span);
  const q = set ? `?set=${encodeURIComponent(set)}` : '';
  if (!set) {
    const play = Object.assign(document.createElement('button'), { className: 'btn small', textContent: '미리 듣기' });
    play.addEventListener('click', async () => {
      try { await api('POST', `/api/sounds/${encodeURIComponent(name)}/preview`); toast('10초 동안 틀어요'); }
      catch (e) { toast(e.message, true); }
    });
    li.appendChild(play);
  }
  const del = Object.assign(document.createElement('button'), { className: 'btn small danger', textContent: '삭제' });
  del.addEventListener('click', async () => {
    if (!confirm(`${name}을(를) 지울까요?`)) return;
    try { await api('DELETE', `/api/sounds/${encodeURIComponent(name)}${q}`); await loadSounds(); }
    catch (e) { toast(e.message, true); }
  });
  li.appendChild(del);
  return li;
}

function renderSounds() {
  const list = $('#song-list');
  list.innerHTML = '';
  sounds.songs.forEach((s) => list.appendChild(soundItem(s.name, `${(s.size / 1048576).toFixed(1)}MB`)));
  if (!sounds.songs.length) list.innerHTML = '<p class="empty">노래가 없어요. 알람은 비프음으로 울려요.</p>';

  const box = $('#clipsets');
  box.innerHTML = '';
  Object.entries(sounds.clipsets).forEach(([set, clips]) => {
    const div = document.createElement('div');
    div.className = 'clipset';
    div.innerHTML = '<h3></h3><ul></ul>';
    $('h3', div).textContent = set;
    clips.forEach((c) => $('ul', div).appendChild(soundItem(c, '', set)));
    box.appendChild(div);
  });
}

function upload(file, set) {
  return new Promise((resolve, reject) => {
    if (file.size > 20 * 1024 * 1024) { reject(new Error('20MB보다 커요')); return; }
    const q = `name=${encodeURIComponent(file.name)}${set ? `&set=${encodeURIComponent(set)}` : ''}`;
    const xhr = new XMLHttpRequest();
    const bar = $('#progress');
    bar.hidden = false;
    xhr.upload.onprogress = (e) => { if (e.lengthComputable) $('div', bar).style.width = `${(e.loaded / e.total) * 100}%`; };
    xhr.onload = () => {
      bar.hidden = true;
      if (xhr.status >= 200 && xhr.status < 300) resolve();
      else reject(new Error((JSON.parse(xhr.responseText || '{}').error) || `HTTP ${xhr.status}`));
    };
    xhr.onerror = () => { bar.hidden = true; reject(new Error('연결이 끊겼어요')); };
    xhr.open('POST', `/api/sounds?${q}`);
    xhr.setRequestHeader('Content-Type', 'application/octet-stream');
    xhr.send(file);
  });
}

$('#song-file').addEventListener('change', async (ev) => {
  const file = ev.target.files[0];
  ev.target.value = '';
  if (!file) return;
  try { await upload(file); toast('올렸어요'); await loadSounds(); } catch (e) { toast(e.message, true); }
});

$('#clip-file').addEventListener('change', async (ev) => {
  const file = ev.target.files[0];
  ev.target.value = '';
  if (!file) return;
  const set = $('#new-set').value.trim() || 'default';
  if (!/^[a-z0-9_-]{1,31}$/.test(set)) { toast('세트 이름은 영문 소문자·숫자·-·_ 로', true); return; }
  try { await upload(file, set); toast('클립을 올렸어요'); await loadSounds(); } catch (e) { toast(e.message, true); }
});

/* ===== 설정 ===== */

const minToTime = (m) => `${String(Math.floor(m / 60)).padStart(2, '0')}:${String(m % 60).padStart(2, '0')}`;
const timeToMin = (t) => { const [h, m] = t.split(':').map(Number); return h * 60 + m; };

async function loadSettings() {
  try {
    settings = await api('GET', '/api/settings');
    const f = $('#settings-form');
    f.default_volume.value = settings.default_volume;
    f.snooze_min.value = settings.snooze_min;
    f.long_press_s.value = settings.long_press_ms / 1000;
    f.auto_stop_min.value = settings.auto_stop_min;
    f.day_brightness.value = settings.day_brightness;
    f.night_brightness.value = settings.night_brightness;
    f.night_start.value = minToTime(settings.night_start_min);
    f.night_end.value = minToTime(settings.night_end_min);
    f.device_name.value = settings.device_name;
    bindRangeOutputs(f);
  } catch (e) {
    toast(`설정을 불러오지 못했어요: ${e.message}`, true);
  }
}

$('#settings-form').addEventListener('submit', async (ev) => {
  ev.preventDefault();
  const f = ev.target;
  const body = {
    default_volume: Number(f.default_volume.value),
    snooze_min: Number(f.snooze_min.value),
    long_press_ms: Math.round(Number(f.long_press_s.value) * 1000),
    auto_stop_min: Number(f.auto_stop_min.value),
    day_brightness: Number(f.day_brightness.value),
    night_brightness: Number(f.night_brightness.value),
    night_start_min: timeToMin(f.night_start.value),
    night_end_min: timeToMin(f.night_end.value),
    device_name: f.device_name.value.trim(),
  };
  try {
    const before = settings && settings.device_name;
    settings = await api('PUT', '/api/settings', body);
    toast(before && before !== settings.device_name ? `저장했어요. 새 주소: ${settings.device_name}.local` : '저장했어요');
  } catch (e) {
    toast(e.message, true);
    loadSettings();
  }
});

$('#sync-time').addEventListener('click', async () => {
  try {
    await api('POST', '/api/time', { utc: Math.floor(Date.now() / 1000) });
    toast('시간을 맞췄어요');
  } catch (e) { toast(e.message, true); }
});

$('#wifi-form').addEventListener('submit', async (ev) => {
  ev.preventDefault();
  const f = ev.target;
  try {
    await api('POST', '/api/wifi', { ssid: f.ssid.value, pass: f.pass.value });
    f.pass.value = '';
    toast('저장했어요. 시계가 새 Wi-Fi에 연결해요');
  } catch (e) { toast(e.message, true); }
});

/* ===== 상태 ===== */

async function loadStatus() {
  try {
    const s = await api('GET', '/api/status');
    const fmt = (sec) => (sec ? new Date(sec * 1000).toLocaleString('ko-KR') : '없음');
    const rows = [
      ['시계 시각', s.time_known ? new Date((s.now_local - 9 * 3600) * 1000).toLocaleString('ko-KR') : '모름'],
      ['마지막 시간 동기화', fmt(s.last_sync_utc)],
      ['Wi-Fi', s.wifi_connected ? `연결됨 (${s.wifi_rssi} dBm)` : s.wifi_mode],
      ['IP', s.ip || '-'],
      ['SD카드', s.sd_ok ? `${(s.sd_free / 1073741824).toFixed(1)} / ${(s.sd_total / 1073741824).toFixed(1)} GB 남음` : '없음'],
      ['펌웨어', s.fw_version],
    ];
    const dl = $('#status');
    dl.innerHTML = '';
    rows.forEach(([k, v]) => {
      dl.appendChild(Object.assign(document.createElement('dt'), { textContent: k }));
      dl.appendChild(Object.assign(document.createElement('dd'), { textContent: v }));
    });
  } catch (e) { toast(e.message, true); }
}

$('#fw-file').addEventListener('change', async (ev) => {
  const file = ev.target.files[0];
  ev.target.value = '';
  if (!file || !confirm(`${file.name}(${(file.size / 1048576).toFixed(1)}MB)로 업데이트할까요?`)) return;
  try {
    const res = await fetch('/api/ota', { method: 'POST', body: file, headers: { 'Content-Type': 'application/octet-stream' } });
    const data = await res.json();
    if (!res.ok) throw new Error(data.error);
    toast('업데이트했어요. 시계가 다시 켜져요');
  } catch (e) { toast(e.message, true); }
});

/* ===== 머리글 시계 ===== */

function tickClock() {
  const d = new Date();
  const t = twelve(d.getHours(), d.getMinutes());
  $('#clock').textContent = `${t.text} ${t.ampm}`;
}

setInterval(tickClock, 10000);
tickClock();
loadSettings().then(loadAlarms);
