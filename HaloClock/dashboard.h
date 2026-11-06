// The Wi-Fi dashboard page, served at http://<clock ip>/ (styled after Code/index.html).
#pragma once

static const char DASHBOARD_HTML[] PROGMEM = R"HTML(<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Halo Clock</title>
<style>
  :root { --bg:#0e0e10; --panel:#18181b; --panel-2:#202024; --line:#2a2a2e; --text:#f2f2f5; --dim:#9a9aa1; --accent:#7cc4ff; --radius:14px; }
  * { margin:0; padding:0; box-sizing:border-box; }
  body { background:var(--bg); color:var(--text); font-family:-apple-system, BlinkMacSystemFont, "Segoe UI", system-ui, sans-serif; padding:22px 16px 48px; -webkit-font-smoothing:antialiased; }
  .wrap { max-width:560px; margin:0 auto; }
  header { display:flex; align-items:center; justify-content:space-between; }
  h1 { font-size:22px; font-weight:650; }
  .status { font-size:13px; color:var(--dim); }
  .dot { display:inline-block; width:8px; height:8px; border-radius:50%; background:#5a5a5f; margin-right:6px; }
  .dot.ok { background:#a8e6cf; } .dot.bad { background:#ffaaa5; }
  .now { font-size:46px; font-weight:300; font-variant-numeric:tabular-nums; margin:6px 0 16px; }
  .now small { font-size:14px; font-weight:500; color:var(--dim); margin-left:8px; }
  .faces { display:grid; grid-template-columns:repeat(3, 1fr); gap:6px; background:var(--panel); border:1px solid var(--line); padding:6px; border-radius:16px; margin-bottom:14px; }
  .faces button { font:inherit; font-size:14px; font-weight:560; color:var(--dim); background:transparent; border:0; padding:10px 6px; border-radius:11px; cursor:pointer; }
  .faces button.on { color:#0e0e10; background:var(--text); }
  .card { background:var(--panel); border:1px solid var(--line); border-radius:var(--radius); padding:18px; margin-bottom:14px; }
  .card h2 { font-size:12px; font-weight:650; text-transform:uppercase; letter-spacing:.1em; color:var(--dim); margin-bottom:14px; }
  .field { margin-bottom:12px; }
  label { display:flex; justify-content:space-between; font-size:12px; color:var(--dim); margin-bottom:5px; }
  .note { font-size:13px; color:var(--dim); line-height:1.5; }
  input, select { font:inherit; font-size:15px; width:100%; background:var(--panel-2); color:var(--text); border:1px solid var(--line); border-radius:9px; padding:10px; }
  input:focus, select:focus { outline:none; border-color:var(--accent); }
  input[type=range] { padding:0; background:none; border:0; accent-color:var(--accent); }
  .row { display:flex; gap:8px; align-items:center; }
  .row > * { flex:1; }
  .btn { font:inherit; font-size:14px; font-weight:600; cursor:pointer; background:var(--accent); color:#08151f; border:0; border-radius:9px; padding:11px 14px; }
  .btn.ghost { background:var(--panel-2); color:var(--text); border:1px solid var(--line); }
  .btn.stop { background:#3a2224; color:#ffaaa5; }
  .seg { display:flex; gap:8px; }
  .seg div { flex:1; text-align:center; font-size:14px; font-weight:560; cursor:pointer; background:var(--panel-2); border:1px solid var(--line); color:var(--dim); padding:10px; border-radius:9px; }
  .seg div.on { background:var(--accent); border-color:var(--accent); color:#08151f; }
  .timer { font-size:42px; font-weight:300; font-variant-numeric:tabular-nums; margin:0 0 12px; }
  .sched { display:grid; grid-template-columns:10px 1.3fr 1fr 1fr 38px; gap:6px; align-items:center; margin-bottom:8px; }
  .sched input { padding:9px 5px; font-size:14px; }
  .sw { width:10px; height:28px; border-radius:4px; }
  .x { height:38px; border-radius:9px; background:var(--panel-2); border:1px solid var(--line); color:var(--dim); font-size:18px; cursor:pointer; }
  .saved { font-size:13px; color:#a8e6cf; opacity:0; transition:opacity .3s; margin-top:10px; }
  .saved.show { opacity:1; }
  footer { font-size:12px; color:#5a5a5f; text-align:center; margin-top:18px; }
</style>
</head>
<body>
<div class="wrap">
  <header><h1>Halo</h1><div class="status" id="status"><span class="dot"></span>Connecting…</div></header>
  <div class="now" id="now">--:--</div>
  <div class="faces" id="faces"></div>
  <div id="faceCard"></div>
  <div class="card" id="displayCard"></div>
  <div class="card" id="wifiCard"></div>
  <div class="card" id="timeCard"></div>
  <footer id="net"></footer>
</div>
<script>
(() => {
  const FACES = [['classic','Classic'], ['learn','Learn'], ['exam','Exam'], ['zones','Zones'], ['focus','Focus'], ['schedule','Schedule']];
  const PASTELS = ['#a8d8ea', '#cdb9f0', '#ffe8a3', '#a8e6cf', '#ffd3b6', '#ffaaa5'];
  // Same order as kCities in settings.h.
  const TZ = ['Europe/London', 'Europe/Paris', 'Europe/Berlin', 'America/New_York', 'America/Los_Angeles', 'Asia/Tokyo', 'Asia/Shanghai',
              'Asia/Singapore', 'Asia/Dubai', 'Asia/Kolkata', 'Australia/Sydney', 'Australia/Melbourne', 'Pacific/Auckland', 'Pacific/Honolulu'];

  let st = null, polledAt = 0, online = false;
  const $ = id => document.getElementById(id);
  const pad = n => String(n).padStart(2, '0');
  const m2t = m => `${pad(Math.floor(m / 60))}:${pad(m % 60)}`;
  const t2m = t => { const [a, b] = t.split(':').map(Number); return a * 60 + b; };
  const mmss = s => `${pad(Math.floor(s / 60))}:${pad(s % 60)}`;

  const h = (tag, props = {}, kids = []) => {
    const e = document.createElement(tag);
    for (const k in props) {
      if (k === 'class') e.className = props[k];
      else if (k === 'text') e.textContent = props[k];
      else if (k.startsWith('on') && typeof props[k] === 'function') e.addEventListener(k.slice(2), props[k]);
      else if (props[k] !== false && props[k] != null) e.setAttribute(k, props[k]);
    }
    (Array.isArray(kids) ? kids : [kids]).forEach(c => c != null && e.append(c));
    return e;
  };

  let debounceTimer;
  const debounce = (fn, ms) => { clearTimeout(debounceTimer); debounceTimer = setTimeout(fn, ms); };

  // ---------- API ----------
  async function call(path, params = {}, { method = 'POST', rerender = true } = {}) {
    try {
      const opts = method === 'POST'
        ? { method, headers: { 'Content-Type': 'application/x-www-form-urlencoded' }, body: new URLSearchParams(params) }
        : { method };
      const r = await fetch(path, opts);
      if (!r.ok) throw new Error(r.status);
      const s = await r.json();
      online = true;
      apply(s, rerender);
      return s;
    } catch (e) {
      online = false;
      renderStatus();
    }
  }
  const set = (params, rerender = true) => call('/api/set', params, { rerender });
  const action = a => call('/api/action', { a });
  const syncTime = () => call('/api/time', { epoch: Math.floor(Date.now() / 1000) }, { rerender: false });
  const poll = () => call('/api/state', {}, { method: 'GET', rerender: false });

  function apply(s, rerender) {
    const first = !st, faceChanged = !st || st.face !== s.face;
    st = s;
    polledAt = Date.now();
    if (first || faceChanged || rerender) { renderFaces(); renderFaceCard(); }
    if (first) { renderDisplay(); renderWifi(); renderTime(); }
    renderStatus();
    tick();
  }

  // Time shown on the clock, in the clock's own time zone.
  function clockParts() {
    const epoch = st.now + (Date.now() - polledAt) / 1000 + st.testOffset;
    const parts = new Intl.DateTimeFormat('en-GB', { timeZone: TZ[st.home], hour: 'numeric', minute: 'numeric', second: 'numeric', hour12: false })
      .formatToParts(new Date(epoch * 1000));
    const get = t => +parts.find(p => p.type === t).value;
    return { h: get('hour') % 24, m: get('minute'), s: get('second') };
  }

  // ---------- rendering ----------
  function renderStatus() {
    const s = $('status');
    s.innerHTML = '';
    const ok = online && st && st.timeValid;
    s.append(h('span', { class: 'dot ' + (ok ? 'ok' : 'bad') }), !online ? 'Offline' : ok ? 'Connected' : 'Time not set');
    if (st) $('net').textContent = (st.ap ? `Clock hotspot "${st.ssid}" · ${st.ip}` : `On "${st.ssid}" · ${st.ip} · halo.local`) + (st.firmware ? ` · v${st.firmware}` : '');
  }

  function tick() {
    if (!st) return;
    if (st.timeValid) {
      const { h: hh, m } = clockParts();
      $('now').innerHTML = '';
      $('now').append(`${hh % 12 || 12}:${pad(m)}`, h('small', { text: `${hh >= 12 ? 'pm' : 'am'} · ${st.cities[st.home]}${st.testOffset ? ' · custom' : ''}` }));
    }
    const tl = $('timerLeft');
    if (tl) {
      const exam = st.face === 'exam';
      const left = exam ? st.examLeft : st.focusLeft, mins = exam ? st.examMin : st.focusMin;
      const elapsed = Math.floor((Date.now() - polledAt) / 1000);
      tl.textContent = left < 0 ? mmss(mins * 60) : mmss(Math.max(0, left - elapsed));
      tl.style.color = left < 0 ? 'var(--dim)' : '';
    }
  }

  function renderFaces() {
    const bar = $('faces');
    bar.innerHTML = '';
    FACES.forEach(([id, label]) => bar.append(h('button', { class: st.face === id ? 'on' : '', text: label, onclick: () => set({ face: id }) })));
  }

  function renderFaceCard() {
    const holder = $('faceCard');
    holder.innerHTML = '';
    const card = h('div', { class: 'card' });
    holder.append(card);
    const f = st.face;

    if (f === 'classic' || f === 'learn') {
      card.append(h('h2', { text: f === 'classic' ? 'Classic' : 'Learn' }),
        h('div', { class: 'note', text: f === 'classic'
          ? 'A plain analogue face for everyday reading.'
          : 'Colour-matched hands with the time in words, like "quarter to one", to help students learn to read the clock.' }));
    } else if (f === 'exam' || f === 'focus') {
      const exam = f === 'exam';
      const label = h('input', { type: 'text', maxlength: exam ? 38 : 30, value: exam ? st.examSubject : st.focusLabel });
      const dur = h('input', { type: 'number', min: 1, max: exam ? 240 : 180, value: exam ? st.examMin : st.focusMin });
      card.append(h('h2', { text: exam ? 'Exam timer' : 'Focus timer' }),
        h('div', { class: 'timer', id: 'timerLeft' }),
        h('div', { class: 'field' }, [h('label', { text: exam ? 'Subject / label' : 'Label' }), label]),
        h('div', { class: 'field' }, [h('label', { text: 'Duration (minutes)' }), dur]),
        h('div', { class: 'row' }, [
          h('button', { class: 'btn', text: 'Start / Restart', onclick: async () => {
            await set(exam ? { examSubject: label.value, examMin: dur.value } : { focusLabel: label.value, focusMin: dur.value }, false);
            action(exam ? 'examStart' : 'focusStart');
          } }),
          h('button', { class: 'btn stop', text: 'Stop', onclick: () => action(exam ? 'examStop' : 'focusStop') }),
        ]));
    } else if (f === 'zones') {
      card.append(h('h2', { text: 'Time zones' }));
      st.zones.forEach((zone, i) => {
        const sel = h('select', { onchange: e => { const z = [...st.zones]; z[i] = e.target.value; set({ zones: z.join(',') }); } },
          st.cities.map((name, ci) => h('option', { value: ci, selected: ci === zone }, name)));
        card.append(h('div', { class: 'field' }, [
          h('label', { text: `City ${i + 1}` }),
          h('div', { class: 'row' }, [h('div', { class: 'sw', style: `flex:0 0 10px;background:${PASTELS[i]}` }), sel]),
        ]));
      });
      card.append(h('div', { class: 'note', text: 'White hands show local time; each coloured hand shows a city.' }));
    } else {
      scheduleCard(card);
    }
    tick();
  }

  function scheduleCard(card) {
    let rows = st.schedule.map(p => ({ ...p }));
    const list = h('div');
    const saved = h('div', { class: 'saved', text: 'Saved to the clock' });
    const draw = () => {
      list.innerHTML = '';
      rows.forEach((p, i) => list.append(h('div', { class: 'sched' }, [
        h('div', { class: 'sw', style: `background:${PASTELS[i % PASTELS.length]}` }),
        h('input', { type: 'text', maxlength: 23, value: p.name, oninput: e => { p.name = e.target.value; } }),
        h('input', { type: 'time', value: m2t(p.start), onchange: e => { p.start = t2m(e.target.value); } }),
        h('input', { type: 'time', value: m2t(p.end), onchange: e => { p.end = t2m(e.target.value); } }),
        h('button', { class: 'x', text: '×', title: 'Remove', onclick: () => { rows.splice(i, 1); draw(); } }),
      ])));
    };
    draw();
    card.append(h('h2', { text: 'Timetable' }), list,
      h('div', { class: 'row', style: 'margin-top:6px' }, [
        h('button', { class: 'btn ghost', text: '+ Add period', onclick: () => {
          if (rows.length >= 12) return;
          const last = rows[rows.length - 1], start = last ? last.end : 540;
          rows.push({ name: 'New', start, end: Math.min(1439, start + 45) });
          draw();
        } }),
        h('button', { class: 'btn', text: 'Save timetable', onclick: async () => {
          rows.sort((a, b) => a.start - b.start);
          const text = rows.map(p => `${p.name.replace(/[|\n]/g, ' ').trim() || 'Period'}|${p.start}|${p.end}`).join('\n');
          await set({ schedule: text }, false);
          draw();
          saved.classList.add('show');
          setTimeout(() => saved.classList.remove('show'), 1800);
        } }),
      ]), saved);
  }

  function renderDisplay() {
    const card = $('displayCard');
    card.innerHTML = '';
    const val = h('span', { text: `${st.brightness}%` });
    const range = h('input', { type: 'range', min: 5, max: 100, value: st.brightness, oninput: e => {
      val.textContent = `${e.target.value}%`;
      debounce(() => set({ brightness: e.target.value }, false), 150);
    } });
    const home = h('select', { onchange: e => set({ home: e.target.value }, false) },
      st.cities.map((name, i) => h('option', { value: i, selected: i === st.home }, name)));
    card.append(h('h2', { text: 'Display' }),
      h('div', { class: 'field' }, [h('label', {}, ['Brightness', val]), range]),
      h('div', { class: 'field' }, [h('label', { text: 'Clock location (time zone)' }), home]));
  }

  // Which network the clock joins. Passwords are write-only: the clock never sends them back.
  function renderWifi(kindOverride) {
    const card = $('wifiCard');
    const kind = kindOverride == null ? st.wifiKind : kindOverride;
    const school = kind === 1;
    card.innerHTML = '';
    const field = (label, input) => h('div', { class: 'field' }, [h('label', { text: label }), input]);
    const ssid = h('input', { type: 'text', value: st.wifiSsid, placeholder: 'Network name' });
    const pass = h('input', { type: 'password', placeholder: st.wifiHasPass ? 'unchanged' : 'network password' });
    const user = h('input', { type: 'text', value: st.wifiUsername, placeholder: 'school username', autocapitalize: 'none' });
    const eapPass = h('input', { type: 'password', placeholder: st.wifiHasEapPass ? 'unchanged' : 'school password' });
    const ident = h('input', { type: 'text', value: st.wifiIdentity, placeholder: 'same as username', autocapitalize: 'none' });
    const method = h('select', {}, [
      h('option', { value: '0', selected: st.wifiEapMethod === 0 }, 'PEAP (usual)'),
      h('option', { value: '1', selected: st.wifiEapMethod === 1 }, 'TTLS'),
    ]);
    const status = h('div', { class: 'note', style: 'margin-top:10px' });
    card.append(h('h2', { text: 'Wi-Fi' }),
      h('div', { class: 'field seg' }, [
        h('div', { class: school ? '' : 'on', text: 'Home', onclick: () => renderWifi(0) }),
        h('div', { class: school ? 'on' : '', text: 'School (802.1X)', onclick: () => renderWifi(1) }),
      ]),
      field('Network name', ssid));
    if (school) card.append(field('Username', user), field('Password', eapPass),
                            field('Identity (leave blank if unsure)', ident), field('Method', method));
    else card.append(field('Password', pass));
    card.append(h('button', {
      class: 'btn', style: 'width:100%', text: 'Save and restart clock', onclick: async (e) => {
        e.target.disabled = true;
        status.textContent = 'Saving\u2026';
        const body = { kind, ssid: ssid.value, eapMethod: method.value };
        if (school) {
          body.username = user.value;
          body.identity = ident.value;
          if (eapPass.value) body.eappass = eapPass.value;
        } else if (pass.value) {
          body.pass = pass.value;
        }
        await call('/api/wifi', body, { rerender: false });
        status.textContent = 'Clock restarting. If it can\'t join, it starts its own "' + 'Halo-Clock' + '" network and you can try again there.';
      },
    }), status,
      h('div', { class: 'note', style: 'margin-top:8px' , text: 'School networks ask each person to sign in, so the clock needs a username and password. They are stored on the clock and never shown again \u2014 ask IT for a device account rather than using your own login.' }));
  }

  function renderTime() {
    const card = $('timeCard');
    card.innerHTML = '';
    const custom = st.testOffset !== 0;
    const now = st.timeValid ? clockParts() : { h: 9, m: 0, s: 0 };
    const input = h('input', { type: 'time', value: `${pad(now.h)}:${pad(now.m)}` });
    const applyCustom = async () => {
      const cur = clockParts();
      const real = { h: (cur.h * 3600 + cur.m * 60 + cur.s - st.testOffset + 864000) % 86400 };
      const offset = t2m(input.value) * 60 - real.h;
      await set({ testOffset: offset || 1 }, false);
      renderTime();
    };
    card.append(h('h2', { text: 'Time' }),
      h('div', { class: 'field seg' }, [
        h('div', { class: custom ? '' : 'on', text: 'Real time', onclick: async () => { await set({ testOffset: 0 }, false); renderTime(); } }),
        h('div', { class: custom ? 'on' : '', text: 'Custom', onclick: applyCustom }),
      ]));
    if (custom) {
      card.append(h('div', { class: 'field' }, [
        h('label', { text: 'Set clock to' }),
        h('div', { class: 'row' }, [input, h('button', { class: 'btn', text: 'Set', style: 'flex:0 0 auto', onclick: applyCustom })]),
      ]), h('div', { class: 'note', text: 'For trying out the faces at other times. Timers still use real time.' }));
    } else {
      card.append(h('div', { class: 'note', text: 'The clock shows real local time.' }));
    }
    card.append(h('button', { class: 'btn ghost', style: 'margin-top:12px;width:100%', text: 'Sync clock time from this device', onclick: syncTime }));
  }

  // ---------- start ----------
  poll().then(s => {
    if (s && (!s.timeValid || Math.abs(s.now - Date.now() / 1000) > 30)) syncTime();
  });
  setInterval(poll, 3000);
  setInterval(tick, 500);
})();
</script>
</body>
</html>
)HTML";
