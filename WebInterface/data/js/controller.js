// data/js/controller.js
// Micropump Controller UI logic with flow estimation from datasheet baselines.
// Baselines (DI water, SRS): mp6 ~7 mL/min @ 250 Vpp & ~100 Hz; mp6-pp ~4 mL/min @ 250 Vpp & ~100 Hz.
// Sources: Bartels manuals. (See footnote in index.html.)

(function(){
  const PUMPS = ["A","B","C"];
  const applyAll = document.getElementById("applyAll");
  const stopAll  = document.getElementById("stopAll");
  const scanBtn  = document.getElementById("scan");
  const buzzBtn  = document.getElementById("buzz");

  function el(id){ return document.getElementById(id); }
  function clampInput(inp){
    const min = Number(inp.min)||-Infinity, max=Number(inp.max)||Infinity, step = Number(inp.step||1);
    let v = Number(inp.value);
    if(!Number.isFinite(v)){ inp.value=""; return; }
    if(v < min) v = min;
    if(v > max) v = max;
    if(step >= 1) v = Math.round(v/step)*step;
    else {
      const d = (step.toString().split(".")[1]||"").length;
      v = Number(v.toFixed(d));
    }
    inp.value = v;
  }

  // ---- Flow model (empirical fit) ------------------------------------------
  // Mean flow (uL/min @ 100 Hz): Q = 1.20 * Vpp - 29.1
  // Scale linearly with frequency up to 100 Hz; clamp negatives to zero.
  function flowEstimate(vpp, hz){
    const vClamped = Math.max(0, Math.min(vpp, 150));
    const baseUL   = (1.20 * vClamped) - 29.1; // uL/min @ 100 Hz
    const fTerm    = Math.max(0, Math.min(hz, 100)) / 100;
    const mlPerMin = Math.max(0, baseUL) * fTerm / 1000.0; // convert to mL/min
    return Number(mlPerMin.toFixed(4));
  }

  function updateOneCard(id){
    const enabled = el(`en${id}`).checked;
    const amp  = Number(el(`amp${id}`).value);
    const freq = Number(el(`freq${id}`).value);
    const flow = enabled ? flowEstimate(amp, freq) : 0;
    el(`flow${id}`).textContent = Number.isFinite(flow) ? flow.toFixed(2) : "–";

    // validity colors + disabled state
    const card = document.querySelector(`section[data-pump="${id}"]`);
    card.querySelectorAll('.row[data-field]').forEach(row=>{
      const input = row.querySelector('input');
      const valid = !enabled || (input.value !== "" && input.checkValidity());
      row.classList.toggle('ok',  enabled && valid);
      row.classList.toggle('err', enabled && !valid && input.value !== "");
      input.disabled = !enabled;
    });

    // enable/disable buttons
    const btnApply = document.querySelector(`.btn-apply-one[data-apply="${id}"]`);
    btnApply.disabled = !enabled || !el(`amp${id}`).checkValidity() || !el(`freq${id}`).checkValidity();
  }

  function updateAll(){
    PUMPS.forEach(updateOneCard);
    const allOK = PUMPS.every(id=>{
      const en = el(`en${id}`).checked;
      return !en || (el(`amp${id}`).checkValidity() && el(`freq${id}`).checkValidity());
    });
    applyAll.disabled = !allOK;
  }

  function payloadFor(id){
    const enabled = el(`en${id}`).checked;
    if(!enabled) return { pump:id, enabled:false, amplitude:0, frequency:0 };
    const amp  = Math.round(Number(el(`amp${id}`).value));
    const freq = Math.round(Number(el(`freq${id}`).value));
    return { pump:id, enabled:true, amplitude:amp, frequency:freq };
  }

  async function postJSON(path, obj){
    const res = await fetch(path, {
      method:'POST',
      headers:{'Content-Type':'application/json'},
      body: obj ? JSON.stringify(obj) : undefined
    });
    return res.text();
  }

  async function loadStatus(){
    try {
      const res = await fetch('/status');
      if (!res.ok) throw new Error(`HTTP ${res.status}`);
      const data = await res.json();
      if (!data || !Array.isArray(data.pumps)) return;
      data.pumps.forEach(p=>{
        const id = (p.pump||"").toString().toUpperCase();
        if (!PUMPS.includes(id)) return;
        if (typeof p.enabled === "boolean") el(`en${id}`).checked = p.enabled;
        if (Number.isFinite(p.amplitude)) el(`amp${id}`).value = Math.round(p.amplitude);
        if (Number.isFinite(p.frequency)) el(`freq${id}`).value = Math.round(p.frequency);
      });
    } catch(err){
      console.error('status load error', err);
    }
  }

  // wiring
  document.querySelectorAll('input[type="number"]').forEach(inp=>{
    inp.addEventListener('input', updateAll);
    inp.addEventListener('blur', ()=>{ clampInput(inp); updateAll(); });
  });
  document.querySelectorAll('.switch input').forEach(t=>{
    t.addEventListener('change', async ()=>{
      const id = t.id.slice(-1).toUpperCase();
      if (!PUMPS.includes(id)) { updateAll(); return; }
      clampInput(el(`amp${id}`));
      clampInput(el(`freq${id}`));
      updateAll();
      try { await postJSON('/apply', { pumps:[ payloadFor(id) ] }); }
      catch(e){ console.error('toggle apply error', e); }
    });
  });
  // per-pump Apply
  document.querySelectorAll('.btn-apply-one').forEach(btn=>{
    btn.addEventListener('click', async ()=>{
      const id = btn.dataset.apply;
      clampInput(el(`amp${id}`));
      clampInput(el(`freq${id}`));
      updateOneCard(id);
      try { await postJSON('/apply', { pumps:[ payloadFor(id) ] }); }
      catch(e){ console.error('apply error', e); }
    });
  });

  // per-pump Off
  document.querySelectorAll('.btn-off-one').forEach(btn=>{
    btn.addEventListener('click', async ()=>{
      const id = btn.dataset.off;
      el(`en${id}`).checked = false;
      updateOneCard(id);
      try { await postJSON('/apply', { pumps:[ { pump:id, enabled:false, amplitude:0, frequency:0 } ] }); }
      catch(e){ console.error('off error', e); }
    });
  });

  // Apply All
  document.getElementById('applyAll').addEventListener('click', async ()=>{
    PUMPS.forEach(id => { clampInput(el(`amp${id}`)); clampInput(el(`freq${id}`)); });
    updateAll();
    try { await postJSON('/apply', { pumps: PUMPS.map(payloadFor) }); }
    catch(e){ console.error('apply all error', e); }
  });

  // STOP ALL
  document.getElementById('stopAll').addEventListener('click', async ()=>{
    PUMPS.forEach(id => { el(`en${id}`).checked = false; updateOneCard(id); });
    try { await postJSON('/stop', {}); }
    catch(e){ console.error('stop error', e); }
  });

  // Scan (see results in Serial)
  document.getElementById('scan').addEventListener('click', async ()=>{
    try { await fetch('/scan'); } catch(e){ console.error('scan error', e); }
  });

  // Buzz quick test
  document.getElementById('buzz').addEventListener('click', async ()=>{
    try { await postJSON('/buzz', { vpp:120, hz:100, ms:2000 }); }
    catch(e){ console.error('buzz error', e); }
  });

  // Prevent wheel changing number inputs
  document.addEventListener('wheel', e=>{
    const a = document.activeElement;
    if(a && a.type === 'number') a.blur();
  }, {passive:true});

  // init UI
  loadStatus().finally(updateAll);
})();
