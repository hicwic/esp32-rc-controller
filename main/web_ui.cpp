#include "web_ui.h"

String buildWebUiPage(const String& inputsJson) {
    String page = R"HTML(
<!doctype html>
<html>
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width,initial-scale=1">
  <title>RC Controller</title>
  <style>
    :root{--surface:#fff;--line:#dbe4f1;--text:#0f172a;--muted:#64748b;--primary:#2563eb;--ok:#16a34a;--danger:#dc2626;--alt:#334155}
    *{box-sizing:border-box}
    body{margin:0;background:linear-gradient(180deg,#f8fbff,#f1f5ff);font-family:Roboto,'Noto Sans',Arial,sans-serif;color:var(--text)}
    .wrap{max-width:980px;margin:0 auto;padding:14px;display:grid;gap:10px}
    .block{background:var(--surface);border:1px solid var(--line);border-radius:16px;padding:12px;box-shadow:0 10px 24px rgba(37,99,235,.06)}
    h1{margin:0;font-size:22px}
    h2{margin:0;font-size:16px}
    .row{display:flex;gap:8px;flex-wrap:wrap;align-items:center}
    .chip{background:#eef3ff;border:1px solid #d7e5ff;padding:6px 10px;border-radius:999px;font-size:12px}
    .btn{border:0;border-radius:12px;padding:9px 12px;color:#fff;font-weight:700;cursor:pointer;background:var(--primary)}
    .btn.ok{background:var(--ok)} .btn.danger{background:var(--danger)} .btn.alt{background:var(--alt)}
    .btn.ghost{background:#fff;color:#1e3a8a;border:1px solid #bfd8ff}
    .btn:disabled{opacity:.45;cursor:not-allowed}
    select,input{height:40px;border:1px solid #cfd9eb;border-radius:12px;padding:0 10px;background:#fff}
    .grow{flex:1}
    .cfg-toolbar{margin-bottom:16px}
    .cfg-left{display:flex;gap:8px;align-items:center;flex-wrap:wrap}
    .cfg-right{display:flex;gap:8px;align-items:center;margin-left:20px}
    .channels{display:grid;grid-template-columns:repeat(auto-fit,minmax(240px,1fr));gap:10px;margin-top:18px}
    .ch{border:1px solid #dbe4f1;border-radius:14px;padding:10px;background:linear-gradient(180deg,#fff,#f9fbff)}
    .ch-title{font-weight:700}
    .meta{font-size:12px;color:var(--muted);display:flex;gap:8px;flex-wrap:wrap;margin-top:4px}
    .bar-bg{height:8px;border-radius:999px;background:#dbeafe;overflow:hidden;margin-top:8px;position:relative}
    .bar-mid{position:absolute;left:50%;top:0;bottom:0;width:1px;background:#94a3b8}
    .bar-signed{position:absolute;top:0;bottom:0;background:linear-gradient(90deg,#22c55e,#16a34a)}
    .active{outline:2px solid #86efac;outline-offset:2px}
    .hint{font-size:12px;color:var(--muted)}
    .toast{position:fixed;left:50%;bottom:16px;transform:translateX(-50%);background:#0f172a;color:#fff;padding:8px 12px;border-radius:10px;font-size:12px;opacity:0;pointer-events:none;transition:opacity .2s;z-index:90}
    .toast.show{opacity:1}
    .modal{position:fixed;inset:0;background:rgba(15,23,42,.35);display:none;align-items:center;justify-content:center;padding:14px;z-index:50}
    .modal.open{display:flex}
    .sheet{max-width:700px;width:100%;background:#fff;border:1px solid #dbe4f1;border-radius:20px;padding:14px;display:grid;gap:10px}
    .grid{display:grid;grid-template-columns:1fr 1fr;gap:10px}
    .field-inline{display:flex;gap:8px;align-items:center}
    .field-inline select{flex:1}
    .field-inline .grow{flex:1}
    .btn.mini{padding:8px 10px;border-radius:10px;font-size:12px;white-space:nowrap}
    .lbl{display:block;font-size:12px;color:var(--muted);margin-bottom:4px}
    .mod-box{border:1px solid #dbe4f1;border-radius:12px;padding:10px;background:#f8fbff}
    .mod-title{font-size:12px;color:var(--muted);margin:0 0 8px 0}
    @media (max-width:640px){.grid{grid-template-columns:1fr}}
  </style>
</head>
<body>
  <div class="wrap">
    <div class="row"><h1>RC Controller</h1></div>

    <section class="block">
      <h2>Status</h2>
      <div class="row" id="statusChips" style="margin-top:8px"></div>
      <div class="row" style="margin-top:8px">
        <button class="btn" id="pairOnBtn">Pair 120s</button>
        <button class="btn alt" id="pairOffBtn">Stop Pairing</button>
        <button class="btn ghost" id="apConfigBtn">AP</button>
      </div>
    </section>

    <section class="block">
      <h2>Model</h2>
      <div class="row" style="margin-top:8px">
        <select id="presetSelect" class="grow"></select>
        <button class="btn ghost" id="newModelBtn">+</button>
        <button class="btn alt" id="setDefaultBtn">Set as default</button>
      </div>
    </section>

    <section class="block">
      <div class="row cfg-toolbar" style="justify-content:space-between;align-items:center">
        <div class="cfg-left">
          <h2>Preset Configuration</h2>
          <button class="btn ok" id="saveModelBtn" disabled>Save</button>
          <button class="btn ghost" id="revertModelBtn" disabled>Revert</button>
        </div>
        <div class="cfg-right">
          <button class="btn ghost" id="addChannelBtn">Add Channel</button>
        </div>
      </div>
      <div id="channels" class="channels"></div>
      <div class="row" style="margin-top:10px">
        <button class="btn danger" id="deletePresetBtn">Delete Preset</button>
      </div>
    </section>
  </div>

  <div class="modal" id="channelModal">
    <div class="sheet">
      <div class="row" style="justify-content:space-between">
        <strong id="modalTitle">Add Channel</strong>
        <button class="btn alt" id="modalCloseBtn" type="button">Close</button>
      </div>
      <input id="modalIndex" type="hidden">
      <div class="grid">
        <div><label class="lbl">Name</label><input id="modalName"></div>
        <div>
          <label class="lbl">GPIO / Type</label>
          <div class="field-inline">
            <select id="modalPin" class="grow"></select>
            <select id="modalType" class="grow"><option value="0">PWM</option><option value="1">ON/OFF</option></select>
          </div>
        </div>
        <div>
          <label class="lbl">Primary input</label>
          <div class="field-inline">
            <select id="modalInput"></select>
            <button class="btn alt mini" id="modalLearnPrimaryBtn" type="button">Learn</button>
          </div>
        </div>
        <div>
          <label class="lbl">Secondary input (optional)</label>
          <div class="field-inline">
            <select id="modalInputSecondary"></select>
            <button class="btn alt mini" id="modalLearnSecondaryBtn" type="button">Learn</button>
          </div>
        </div>
        <div class="mod-box">
          <p class="mod-title">Modifier</p>
          <label class="lbl">Modifier input (optional)</label>
          <div class="field-inline" style="margin-bottom:8px">
            <select id="modalInputModifier"></select>
            <button class="btn alt mini" id="modalLearnModifierBtn" type="button">Learn</button>
          </div>
          <label class="lbl">Modifier function</label>
          <select id="modalModifierFunction">
            <option value="none">None</option>
            <option value="reverse">Reverse</option>
          </select>
        </div>
        <div><label class="lbl">Deadzone (%)</label><input id="modalThreshold" type="number" min="0" max="100"></div>
        <div style="display:flex;align-items:flex-end"><label class="lbl" style="margin:0;display:flex;gap:8px;align-items:center"><input id="modalInverted" type="checkbox" style="height:auto;margin:0"> Reversed</label></div>
      </div>
      <div class="row">
        <button class="btn ok" id="modalSaveBtn" type="button">Save</button>
        <span class="hint" id="modalLearnStatus"></span>
      </div>
    </div>
  </div>

  <div class="modal" id="modelCreateModal">
    <div class="sheet" style="max-width:560px">
      <div class="row" style="justify-content:space-between">
        <strong>Create Preset</strong>
        <button class="btn alt" id="modelCreateCloseBtn" type="button">Close</button>
      </div>
      <div class="grid">
        <div><label class="lbl">Name</label><input id="modelCreateName" placeholder="my_model"></div>
        <div>
          <label class="lbl">Clone from</label>
          <select id="modelCreateClone">
            <option value="">None</option>
          </select>
        </div>
      </div>
      <div class="row">
        <button class="btn ok" id="modelCreateSaveBtn" type="button">Create</button>
      </div>
    </div>
  </div>

  <div class="modal" id="apModal">
    <div class="sheet" style="max-width:560px">
      <div class="row" style="justify-content:space-between">
        <strong>AP Configuration</strong>
        <button class="btn alt" id="apCloseBtn" type="button">Close</button>
      </div>
      <div>
        <label class="lbl">AP SSID</label>
        <input id="apSsidInput" maxlength="31">
        <label class="lbl" style="margin-top:8px">AP Password</label>
        <input id="apPasswordInput" maxlength="63" placeholder="empty => open network">
        <div class="hint">Change takes effect after reboot.</div>
      </div>
      <div class="row">
        <button class="btn ok" id="apSaveBtn" type="button">Save</button>
        <button class="btn alt" id="apApplyRebootBtn" type="button">Apply & Reboot Now</button>
      </div>
    </div>
  </div>
  <div class="toast" id="toast"></div>

  <script>
  (function(){
    const INPUTS = __INPUTS_JSON__;
    const state = {channels:[], presets:[], bootModel:"-", modelDirty:false, currentModel:"-", pwmPins:[], apSsid:"", apPassword:""};
    const createState = {forkReadonly:false};
    let suppressPresetChange = false;

    const ui = {
      statusChips: document.getElementById('statusChips'),
      presetSelect: document.getElementById('presetSelect'),
      newModelBtn: document.getElementById('newModelBtn'),
      setDefaultBtn: document.getElementById('setDefaultBtn'),
      pairOnBtn: document.getElementById('pairOnBtn'),
      pairOffBtn: document.getElementById('pairOffBtn'),
      apConfigBtn: document.getElementById('apConfigBtn'),
      saveModelBtn: document.getElementById('saveModelBtn'),
      revertModelBtn: document.getElementById('revertModelBtn'),
      addChannelBtn: document.getElementById('addChannelBtn'),
      deletePresetBtn: document.getElementById('deletePresetBtn'),
      channels: document.getElementById('channels'),
      toast: document.getElementById('toast'),
      modal: document.getElementById('channelModal'),
      modalTitle: document.getElementById('modalTitle'),
      modalCloseBtn: document.getElementById('modalCloseBtn'),
      modalIndex: document.getElementById('modalIndex'),
      modalName: document.getElementById('modalName'),
      modalPin: document.getElementById('modalPin'),
      modalType: document.getElementById('modalType'),
      modalInput: document.getElementById('modalInput'),
      modalInputSecondary: document.getElementById('modalInputSecondary'),
      modalInputModifier: document.getElementById('modalInputModifier'),
      modalThreshold: document.getElementById('modalThreshold'),
      modalInverted: document.getElementById('modalInverted'),
      modalModifierFunction: document.getElementById('modalModifierFunction'),
      modalSaveBtn: document.getElementById('modalSaveBtn'),
      modalLearnPrimaryBtn: document.getElementById('modalLearnPrimaryBtn'),
      modalLearnSecondaryBtn: document.getElementById('modalLearnSecondaryBtn'),
      modalLearnModifierBtn: document.getElementById('modalLearnModifierBtn'),
      modalLearnStatus: document.getElementById('modalLearnStatus'),
      modelCreateModal: document.getElementById('modelCreateModal'),
      modelCreateCloseBtn: document.getElementById('modelCreateCloseBtn'),
      modelCreateName: document.getElementById('modelCreateName'),
      modelCreateClone: document.getElementById('modelCreateClone'),
      modelCreateSaveBtn: document.getElementById('modelCreateSaveBtn'),
      apModal: document.getElementById('apModal'),
      apCloseBtn: document.getElementById('apCloseBtn'),
      apSsidInput: document.getElementById('apSsidInput'),
      apPasswordInput: document.getElementById('apPasswordInput'),
      apSaveBtn: document.getElementById('apSaveBtn'),
      apApplyRebootBtn: document.getElementById('apApplyRebootBtn')
    };

    let toastTimer = 0;
    function esc(s){return String(s).replace(/[&<>"']/g,function(c){return {"&":"&amp;","<":"&lt;",">":"&gt;","\"":"&quot;","'":"&#39;"}[c];});}
    function setMsg(t){
      const txt = t || "Ready.";
      ui.toast.textContent = txt;
      ui.toast.classList.add("show");
      if (toastTimer) clearTimeout(toastTimer);
      toastTimer = setTimeout(function(){ ui.toast.classList.remove("show"); }, 1800);
    }

    function fillInputOptions(el, selected, withNone){
      const opts = [];
      if (withNone) opts.push({id:0,label:"None"});
      INPUTS.forEach(x=>opts.push(x));
      el.innerHTML = opts.map(o=>"<option value='"+o.id+"' "+(Number(o.id)===Number(selected)?"selected":"")+">"+esc(o.label)+"</option>").join("");
    }
    function fillPinOptions(selectedPin, editingIndex){
      const usedByOthers = new Set((state.channels||[])
        .filter(function(ch){ return Number(ch.index)!==Number(editingIndex); })
        .map(function(ch){ return Number(ch.pin); }));
      const pins = (state.pwmPins||[]).slice().sort(function(a,b){ return Number(a)-Number(b); });
      ui.modalPin.innerHTML = pins.map(function(pin){
        const p = Number(pin);
        const used = usedByOthers.has(p);
        const label = "GPIO "+p+(used ? " (used)" : "");
        const sel = p===Number(selectedPin) ? " selected" : "";
        const dis = used ? " disabled" : "";
        return "<option value='"+p+"'"+sel+dis+">"+label+"</option>";
      }).join("");
      if (!ui.modalPin.value && pins.length) {
        const firstFree = pins.find(function(pin){ return !usedByOthers.has(Number(pin)); });
        ui.modalPin.value = String(firstFree !== undefined ? firstFree : pins[0]);
      }
    }

    fillInputOptions(ui.modalInput, 1, false);
    fillInputOptions(ui.modalInputSecondary, 0, true);
    fillInputOptions(ui.modalInputModifier, 0, true);

    async function post(url, data){
      const body = new URLSearchParams(data||{});
      const r = await fetch(url, {method:"POST", body:body});
      const txt = await r.text();
      try {
        const parsed = JSON.parse(txt);
        if (!r.ok && parsed && parsed.ok === undefined) parsed.ok = false;
        return parsed;
      } catch (_e) {
        return {ok:r.ok, message: txt || ("HTTP " + r.status)};
      }
    }

    function inputLabel(inputId){
      const found = INPUTS.find(i=>Number(i.id)===Number(inputId));
      return found ? found.label : "None";
    }

    function signedBar(v){
      const n = Number(v)||0;
      const width = Math.round(Math.abs(n) * 50);
      const left = n >= 0 ? 50 : (50 - width);
      return "<div class='bar-bg'><div class='bar-mid'></div><div class='bar-signed' style='left:"+left+"%;width:"+width+"%'></div></div>";
    }

    function renderChannels(){
      ui.channels.innerHTML = state.channels.map(function(ch){
        const second = Number(ch.input_secondary) > 0 ? (" / "+esc(inputLabel(ch.input_secondary))) : "";
        let mod = "";
        if (Number(ch.input_modifier) > 0) {
          mod = " | mod "+esc(inputLabel(ch.input_modifier));
          if (ch.modifier_reverses) mod += " (reverse)";
        }
        return "<div class='ch "+(ch.active?"active":"")+"' data-index='"+ch.index+"'>"
          +"<div class='ch-title'>CH"+ch.index+" - "+esc(ch.name)+"</div>"
          +"<div class='meta'><span>GPIO "+ch.pin+"</span><span>"+esc(ch.type_label)+"</span><span>"+esc(inputLabel(ch.input))+second+mod+"</span></div>"
          +signedBar(ch.signed_activity)
          +"<div class='row' style='margin-top:8px'><button class='btn ghost' data-action='edit'>Edit</button><button class='btn danger' data-action='delete'>Delete</button></div>"
          +"</div>";
      }).join("");
    }

    function renderPresetSelect(){
      const sig = state.presets.join("|")+"|boot:"+state.bootModel;
      const focused = document.activeElement === ui.presetSelect;
      if (!focused && ui.presetSelect.dataset.sig !== sig) {
        const current = state.currentModel;
        ui.presetSelect.innerHTML = state.presets.map(function(p){
          const label = p + (p===state.bootModel ? " (default)" : "");
          return "<option value='"+esc(p)+"'>"+esc(label)+"</option>";
        }).join("");
        ui.presetSelect.dataset.sig = sig;
        if (state.presets.indexOf(current) >= 0) {
          suppressPresetChange = true;
          ui.presetSelect.value = current;
          suppressPresetChange = false;
        }
      }
    }
    function isReadonlyPreset(name){
      return name==="car" || name==="excavator";
    }

    function renderCloneOptions() {
      const sig = state.presets.join("|");
      const focused = document.activeElement === ui.modelCreateClone;
      if (focused && ui.modelCreateModal.classList.contains("open")) return;
      if (ui.modelCreateClone.dataset.sig === sig && ui.modelCreateClone.options.length) return;
      const currentVal = ui.modelCreateClone.value;
      ui.modelCreateClone.innerHTML = "<option value=''>None</option>" + state.presets.map(function(p){
        return "<option value='"+esc(p)+"'>"+esc(p)+"</option>";
      }).join("");
      ui.modelCreateClone.dataset.sig = sig;
      if (currentVal && Array.from(ui.modelCreateClone.options).some(function(o){ return o.value===currentVal; })) {
        ui.modelCreateClone.value = currentVal;
      }
    }

    function openModal(mode, ch){
      ui.modalTitle.textContent = mode==="add" ? "Add Channel" : "Edit Channel";
      ui.modalIndex.value = mode==="add" ? "" : String(ch.index);
      ui.modalName.value = mode==="add" ? "CH" : ch.name;
      fillPinOptions(mode==="add" ? 13 : Number(ch.pin), mode==="add" ? -1 : Number(ch.index));
      ui.modalType.value = mode==="add" ? "0" : String(ch.type);
      fillInputOptions(ui.modalInput, mode==="add" ? 1 : ch.input, false);
      fillInputOptions(ui.modalInputSecondary, mode==="add" ? 0 : ch.input_secondary, true);
      fillInputOptions(ui.modalInputModifier, mode==="add" ? 0 : ch.input_modifier, true);
      ui.modalThreshold.value = mode==="add" ? "10" : String(ch.threshold);
      ui.modalInverted.checked = mode==="add" ? false : !!ch.inverted;
      ui.modalModifierFunction.value = (mode==="add" || !ch.modifier_reverses) ? "none" : "reverse";
      ui.modalLearnStatus.textContent = "";
      ui.modal.classList.add("open");
    }

    function closeModal(){ ui.modal.classList.remove("open"); ui.modalLearnStatus.textContent = ""; }
    function delay(ms){ return new Promise(function(resolve){ setTimeout(resolve, ms); }); }

    async function refresh(){
      try{
        const r = await fetch("/api/state");
        const data = await r.json();
        state.channels = data.channels || [];
        state.presets = data.presets || [];
        state.currentModel = data.current_model || "-";
        state.bootModel = data.boot_model || "-";
        state.modelDirty = !!data.model_dirty;
        state.pwmPins = data.pwm_pins || [];
        state.apSsid = data.ap_ssid || "";
        state.apPassword = data.ap_password || "";
        ui.statusChips.innerHTML = [
          "Gamepad "+(data.gamepad?"ON":"OFF"),
          "Scan "+(data.bt_scan?"ON":"OFF"),
          "Pairing "+(data.pairing?"ON":"OFF")
        ].map(x=>"<span class='chip'>"+x+"</span>").join("");
        renderPresetSelect();
        renderCloneOptions();
        if (!ui.modal.classList.contains("open")) renderChannels();
        ui.saveModelBtn.disabled = !state.modelDirty;
        ui.revertModelBtn.disabled = !state.modelDirty;
        ui.deletePresetBtn.disabled = isReadonlyPreset(state.currentModel);
      } catch(_e){
        setMsg("Refresh failed.");
      }
    }

    ui.presetSelect.addEventListener("change", async function(){
      if (suppressPresetChange) return;
      const selected = ui.presetSelect.value;
      if (!selected || selected === state.currentModel) return;
      if (state.modelDirty) {
        const ok = confirm("You have unsaved changes. Loading another preset will discard them. Continue?");
        if (!ok) {
          suppressPresetChange = true;
          ui.presetSelect.value = state.currentModel;
          suppressPresetChange = false;
          return;
        }
      }
      const r = await post("/api/preset_apply", {name:selected});
      setMsg(r.message || "Preset loaded");
      refresh();
    });

    function openCreateModelModal(opts){
      const o = opts || {};
      createState.forkReadonly = !!o.forkReadonly;
      ui.modelCreateName.value = o.name || "";
      ui.modelCreateClone.value = o.base || "";
      ui.modelCreateClone.disabled = !!o.lockBase;
      ui.modelCreateModal.classList.add("open");
      ui.modelCreateName.focus();
    }
    function closeCreateModelModal(){
      ui.modelCreateModal.classList.remove("open");
      createState.forkReadonly = false;
      ui.modelCreateClone.disabled = false;
    }
    function openApModal(){
      ui.apSsidInput.value = state.apSsid || "";
      ui.apPasswordInput.value = state.apPassword || "";
      ui.apModal.classList.add("open");
      ui.apSsidInput.focus();
    }
    function closeApModal(){ ui.apModal.classList.remove("open"); }

    ui.newModelBtn.addEventListener("click", function(){ openCreateModelModal(); });
    ui.modelCreateCloseBtn.addEventListener("click", closeCreateModelModal);
    ui.modelCreateModal.addEventListener("click", function(ev){ if (ev.target===ui.modelCreateModal) closeCreateModelModal(); });
    ui.modelCreateSaveBtn.addEventListener("click", async function(){
      const name = (ui.modelCreateName.value || "").trim();
      if (!name) { setMsg("Model name is required"); return; }
      const base = ui.modelCreateClone.value || "";
      const payload = {name:name, base:base};
      if (createState.forkReadonly) payload.from_current = "1";
      const r = await post("/api/model_create", payload);
      setMsg(r.message || "Model created");
      if (r.ok) closeCreateModelModal();
      refresh();
    });
    ui.setDefaultBtn.addEventListener("click", async function(){
      const r = await post("/api/model_set_default", {name:ui.presetSelect.value});
      setMsg(r.message || "Default updated");
      refresh();
    });
    ui.saveModelBtn.addEventListener("click", async function(){
      const r = await post("/api/model_save_current");
      if (r && r.readonly) {
        setMsg("Readonly preset detected: create a fork to keep your changes.");
        openCreateModelModal({
          forkReadonly: true,
          lockBase: true,
          base: r.base || state.currentModel || "",
          name: r.suggested_name || "custom_model"
        });
        return;
      }
      setMsg(r.message || "Saved");
      refresh();
    });
    ui.revertModelBtn.addEventListener("click", async function(){
      const r = await post("/api/model_revert");
      setMsg(r.message || "Reverted");
      refresh();
    });

    ui.pairOnBtn.addEventListener("click", async function(){ const r = await post("/api/pairing_on"); setMsg(r.message||"Pairing on"); refresh(); });
    ui.pairOffBtn.addEventListener("click", async function(){ const r = await post("/api/pairing_off"); setMsg(r.message||"Pairing off"); refresh(); });
    ui.deletePresetBtn.addEventListener("click", async function(){
      const name = state.currentModel;
      if (!name || isReadonlyPreset(name)) return;
      const ok = confirm("Delete preset '"+name+"'?");
      if (!ok) return;
      const r = await post("/api/model_delete", {name:name});
      setMsg(r.message || "Preset deleted");
      refresh();
    });

    ui.apConfigBtn.addEventListener("click", openApModal);
    ui.apCloseBtn.addEventListener("click", closeApModal);
    ui.apModal.addEventListener("click", function(ev){ if (ev.target===ui.apModal) closeApModal(); });
    ui.apSaveBtn.addEventListener("click", async function(){
      const ssid = (ui.apSsidInput.value || "").trim();
      const password = ui.apPasswordInput.value || "";
      const r = await post("/api/ap_config_set", {ssid:ssid, password:password});
      setMsg(r.message || "AP settings saved");
      if (r.ok) closeApModal();
      refresh();
    });
    ui.apApplyRebootBtn.addEventListener("click", async function(){
      const ssid = (ui.apSsidInput.value || "").trim();
      const password = ui.apPasswordInput.value || "";
      const r = await post("/api/ap_config_apply_reboot", {ssid:ssid, password:password});
      setMsg(r.message || "Applying...");
    });
    ui.addChannelBtn.addEventListener("click", function(){ openModal("add"); });

    ui.channels.addEventListener("click", async function(ev){
      const btn = ev.target.closest("button");
      if (!btn) return;
      const card = ev.target.closest(".ch");
      if (!card) return;
      const idx = Number(card.dataset.index);
      const ch = state.channels.find(c=>Number(c.index)===idx);
      if (!ch) return;
      const action = btn.dataset.action;
      if (action==="edit") { openModal("edit", ch); return; }
      if (action==="delete") { const r = await post("/api/channel_delete", {index:idx}); setMsg(r.message||"Deleted"); refresh(); }
    });

    ui.modalCloseBtn.addEventListener("click", closeModal);
    ui.modal.addEventListener("click", function(ev){ if (ev.target===ui.modal) closeModal(); });

    async function learnInto(target){
      ui.modalLearnStatus.textContent = "Listening...";
      for (let attempt = 0; attempt < 20; attempt++) {
        try {
          const r = await post("/api/learn_detect");
          if (r.ok) {
            target.value = String(r.input);
            const txt = "Detected: " + (r.label || r.input);
            ui.modalLearnStatus.textContent = txt;
            setMsg(txt);
            return;
          }
          if (r.message && r.message.indexOf("No gamepad") >= 0) {
            ui.modalLearnStatus.textContent = r.message;
            setMsg(r.message);
            return;
          }
        } catch (_e) {
          ui.modalLearnStatus.textContent = "Learn request failed";
          setMsg("Learn request failed");
          return;
        }
        await delay(150);
      }
      ui.modalLearnStatus.textContent = "No input detected";
      setMsg("No input detected");
    }
    ui.modalLearnPrimaryBtn.addEventListener("click", function(){ learnInto(ui.modalInput); });
    ui.modalLearnSecondaryBtn.addEventListener("click", function(){ learnInto(ui.modalInputSecondary); });
    ui.modalLearnModifierBtn.addEventListener("click", function(){ learnInto(ui.modalInputModifier); });

    ui.modalSaveBtn.addEventListener("click", async function(){
      const isAdd = !ui.modalIndex.value;
      ui.modalLearnStatus.textContent = "";
      const payload = {
        name: ui.modalName.value,
        pin: ui.modalPin.value,
        type: ui.modalType.value,
        input: ui.modalInput.value,
        input_secondary: ui.modalInputSecondary.value,
        input_modifier: ui.modalInputModifier.value,
        modifier_reverses: ui.modalModifierFunction.value === "reverse" ? "1":"0",
        threshold: ui.modalThreshold.value,
        inverted: ui.modalInverted.checked ? "1":"0"
      };
      if (!isAdd) payload.index = ui.modalIndex.value;
      const r = await post(isAdd?"/api/channel_add":"/api/channel_update", payload);
      setMsg(r.message || (r.ok ? "Saved":"Error"));
      if (r.ok) {
        closeModal();
      } else {
        ui.modalLearnStatus.textContent = r.message || "Save failed";
      }
      refresh();
    });

    refresh();
    setInterval(refresh, 500);
  })();
  </script>
</body>
</html>
)HTML";

    page.replace("__INPUTS_JSON__", inputsJson);
    return page;
}
