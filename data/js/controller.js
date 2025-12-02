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

  // ---- Flow model (see README in index.html note) ---------------------------
  function qRef(model){
    return (model === "mp6-pp") ? 4.0 : 7.0; // mL/min @ 250 Vpp, ~100 Hz (typical, DI water)
  }
  function flowEstimate(model, vpp, hz){
  const Qref = qRef(model);          // 7.0 or 4.0 mL/min @ 250 Vpp, 100 Hz
  const vTerm = Math.max(0, Math.min(vpp, 150)) / 150;  // ← use 150
  const fTerm = Math.max(0, Math.min(hz, 100)) / 100;
  return Math.max(0, Number((Qref * vTerm * fTerm).toFixed(2)));
}

  function updateOneCard(id){
    const enabled = el(`en${id}`).checked;
    const amp  = Number(el(`amp${id}`).value);
    const freq = Number(el(`freq${id}`).value);
    const model = el(`model${id}`).value;
    const flow = enabled ? flowEstimate(model, amp, freq) : 0;
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
    const model   = el(`model${id}`).value;
    if(!enabled) return { pump:id, enabled:false, amplitude:0, frequency:0, model };
    const amp  = Math.round(Number(el(`amp${id}`).value));
    const freq = Math.round(Number(el(`freq${id}`).value));
    return { pump:id, enabled:true, amplitude:amp, frequency:freq, model };
  }

  async function postJSON(path, obj){
    const res = await fetch(path, {
      method:'POST',
      headers:{'Content-Type':'application/json'},
      body: obj ? JSON.stringify(obj) : undefined
    });
    return res.text();
  }

  // wiring
  document.querySelectorAll('input[type="number"]').forEach(inp=>{
    inp.addEventListener('input', updateAll);
    inp.addEventListener('blur', ()=>{ clampInput(inp); updateAll(); });
  });
  document.querySelectorAll('.switch input').forEach(t=>{
    t.addEventListener('change', updateAll);
  });
  document.querySelectorAll('select[id^="model"]').forEach(sel=>{
    sel.addEventListener('change', updateAll);
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
      try { await postJSON('/apply', { pumps:[ { pump:id, enabled:false, amplitude:0, frequency:0, model:el(`model${id}`).value } ] }); }
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
  updateAll();
})();
