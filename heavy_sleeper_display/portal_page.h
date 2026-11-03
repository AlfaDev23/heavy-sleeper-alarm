/*
 * portal_page.h — Pagina unica do portal de configuracao (HTML + CSS + JS).
 *
 * Fica na flash (PROGMEM). A pagina so fala com a API JSON do portal.cpp:
 *   GET  /api/state          estado completo
 *   POST /api/save           alarme, lampada, modo noturno e fuso
 *   GET  /api/scan           redes Wi-Fi proximas
 *   POST /api/wifi           grava SSID/senha e reconecta
 *   POST /api/wifi/forget    apaga a rede salva
 *   POST /api/wiz/discover   procura lampadas WiZ
 *   POST /api/wiz/test       pisca a lampada
 *
 * Para acrescentar uma opcao: um campo no <form id="cfg"> com name=... e a
 * mesma chave tratada em handleSave() no portal.cpp.
 */
#ifndef PORTAL_PAGE_H
#define PORTAL_PAGE_H

#include <pgmspace.h>

static const char PORTAL_HTML[] PROGMEM = R"HTML(<!DOCTYPE html>
<html lang="pt-BR"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Despertador Heavy Sleeper</title>
<style>
:root{--bg:#0d1117;--card:#161b22;--line:#30363d;--tx:#e6edf3;--sub:#8b949e;--ac:#2fd4e0;--ok:#3fb950;--warn:#f0883e;--bad:#f85149}
*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--tx);font:16px/1.4 system-ui,-apple-system,Segoe UI,Roboto,sans-serif}
header{padding:18px 16px 8px}h1{font-size:20px;margin:0}header p{margin:4px 0 0;color:var(--sub);font-size:14px}
main{padding:8px 12px 40px;max-width:560px;margin:0 auto}
section{background:var(--card);border:1px solid var(--line);border-radius:12px;padding:14px 14px 6px;margin:12px 0}
h2{font-size:15px;margin:0 0 10px;color:var(--ac);letter-spacing:.3px;text-transform:uppercase}
label{display:block;font-size:13px;color:var(--sub);margin:10px 0 4px}
input,select,button{font:inherit;width:100%;padding:10px 12px;border-radius:8px;border:1px solid var(--line);background:#0d1117;color:var(--tx)}
input[type=checkbox]{width:auto;margin-right:8px;transform:scale(1.2)}
.row{display:flex;gap:10px}.row>*{flex:1}
.chk{display:flex;align-items:center;color:var(--tx);font-size:15px;margin:12px 0 4px}
button{background:#21262d;cursor:pointer;margin:10px 0}button.pri{background:#1f6feb;border-color:#1f6feb}
button.small{width:auto;padding:6px 12px;margin:4px 0}
.kv{display:flex;justify-content:space-between;border-bottom:1px solid var(--line);padding:7px 0;font-size:14px}
.kv:last-child{border:0}.kv b{font-weight:600;text-align:right;max-width:60%;overflow-wrap:anywhere}
.ok{color:var(--ok)}.bad{color:var(--bad)}.warn{color:var(--warn)}.sub{color:var(--sub);font-size:13px}
.list div{display:flex;justify-content:space-between;align-items:center;gap:8px;padding:6px 0;border-bottom:1px solid var(--line)}
#toast{position:fixed;left:50%;bottom:18px;transform:translateX(-50%);background:#21262d;border:1px solid var(--line);padding:10px 16px;border-radius:10px;display:none;max-width:90%}
.busy{background:#3d1d1d;border:1px solid var(--bad);border-radius:10px;padding:10px;margin:10px 0;display:none}
output{float:right;color:var(--tx)}
</style></head><body>
<header><h1>Despertador Heavy Sleeper</h1><p id="hdr">carregando...</p></header>
<main>
<div class="busy" id="busy">O alarme esta tocando. Resolva o desafio na tela: as configuracoes do alarme ficam bloqueadas ate la.</div>

<section><h2>Status</h2>
<div class="kv"><span>Hora</span><b id="s_time">--</b></div>
<div class="kv"><span>Wi-Fi</span><b id="s_net">--</b></div>
<div class="kv"><span>Lampada</span><b id="s_wiz">--</b></div>
<div class="kv"><span>Tela</span><b id="s_night">--</b></div>
</section>

<form id="cfg" onsubmit="return save(event)">
<section><h2>Alarme</h2>
<div class="row"><div><label>Horario</label><input type="time" name="almTime" required></div>
<div><label>&nbsp;</label><label class="chk"><input type="checkbox" name="almEn" value="1">Ativo</label></div></div>
<div class="row"><div><label>Desafio</label><select name="type">
<option value="0">Matematica</option><option value="1">Memoria</option><option value="2">Sequencia</option><option value="3">Reacao</option></select></div>
<div><label>Dificuldade</label><select name="diff">
<option value="0">Facil</option><option value="1">Medio</option><option value="2">Dificil</option><option value="3">Extremo</option></select></div></div>
<div class="row"><div><label>Tempo para resolver</label><select name="chal">
<option value="30">30 s</option><option value="60">60 s</option><option value="90">90 s</option><option value="120">120 s</option></select></div>
<div><label>Vibracao</label><select name="vib"><option value="0">Desligada</option><option value="1">Fraca</option><option value="2">Forte</option></select></div></div>
</section>

<section><h2>Luz (lampada WiZ)</h2>
<div class="row"><div><label>Acender antes do alarme</label><select name="ramp">
<option value="5">5 min</option><option value="10">10 min</option><option value="15">15 min</option><option value="20">20 min</option></select></div>
<div><label>Brilho maximo <output id="lv"></output></label><input type="range" name="light" min="10" max="100" step="10" oninput="lv.value=this.value+'%'"></div></div>
<label>IP da lampada (vazio = procurar sozinho)</label><input name="wizIp" placeholder="automatico" inputmode="decimal" pattern="^$|^(\d{1,3}\.){3}\d{1,3}$">
<div class="list" id="found"></div>
<div class="row"><button type="button" onclick="post('/api/wiz/discover','Procurando lampadas...')">Procurar</button>
<button type="button" onclick="post('/api/wiz/test','Piscando a lampada')">Testar</button></div>
<p class="sub">Se a lampada nao responder, ligue a opcao de comunicacao local no app WiZ. Reservar o IP dela no roteador evita que ele mude.</p>
</section>

<section><h2>Modo noturno</h2>
<label>Modo</label><select name="nightMode"><option value="0">Automatico (por horario)</option><option value="1">Sempre dia</option><option value="2">Sempre noite</option></select>
<div class="row"><div><label>Comeca as</label><input type="time" name="nightStart" step="1800"></div>
<div><label>Termina as</label><input type="time" name="nightEnd" step="1800"></div></div>
<p class="sub">A noite a tela mostra so a hora, em vermelho e com brilho minimo. Um toque mostra tudo por 10 s.</p>
</section>

<section><h2>Fuso horario</h2>
<select name="tz"><option value="&lt;-03&gt;3">Brasilia (UTC-3)</option><option value="&lt;-04&gt;4">Amazonas / MT / MS (UTC-4)</option>
<option value="&lt;-05&gt;5">Acre (UTC-5)</option><option value="&lt;-02&gt;2">Fernando de Noronha (UTC-2)</option></select>
<button class="pri" type="submit">Salvar configuracoes</button>
</section>
</form>

<section><h2>Rede Wi-Fi</h2>
<div class="list" id="nets"></div>
<button type="button" onclick="scan()">Procurar redes</button>
<form onsubmit="return wifi(event)">
<label>Nome da rede (SSID)</label><input name="ssid" id="ssid" required maxlength="32" autocapitalize="off">
<label>Senha</label><input name="pass" type="password" maxlength="64" autocomplete="off">
<button class="pri" type="submit">Conectar</button></form>
<button type="button" onclick="if(confirm('Apagar a rede salva?'))post('/api/wifi/forget','Rede apagada')">Esquecer rede</button>
<p class="sub">Depois de conectar, a tela REDE do despertador mostra o IP. Na rede de casa o portal fica em <b>http://despertador.local</b>.</p>
</section>
<p class="sub" id="fw"></p>
</main><div id="toast"></div>
<script>
const $=s=>document.querySelector(s),F=$('#cfg');let loaded=false;
const hm=m=>String(Math.floor(m/60)).padStart(2,'0')+':'+String(m%60).padStart(2,'0');
function toast(t){const e=$('#toast');e.textContent=t;e.style.display='block';clearTimeout(e._t);e._t=setTimeout(()=>e.style.display='none',3500)}
function esc(s){return String(s).replace(/[&<>"]/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;'}[c]))}
async function post(u,msg,body){try{const r=await fetch(u,{method:'POST',body:body||new URLSearchParams()});const j=await r.json().catch(()=>({}));
toast(r.ok?(msg||'OK'):(j.error||('Erro '+r.status)));setTimeout(refresh,800);return r.ok}catch(e){toast('Sem resposta do despertador');return false}}
function fill(s){const a=s.alarm,n=s.night;F.almTime.value=hm(a.h*60+a.m);F.almEn.checked=a.en;F.type.value=a.type;F.diff.value=a.diff;
F.chal.value=a.chal;F.vib.value=a.vib;F.ramp.value=a.ramp;F.light.value=a.light;$('#lv').value=a.light+'%';
F.wizIp.value=s.wiz.cfgIp;F.nightMode.value=n.mode;F.nightStart.value=hm(n.start);F.nightEnd.value=hm(n.end);F.tz.value=s.tz}
function show(s){const N=s.net,W=s.wiz;$('#hdr').textContent=N.state==='CONECTADO'?('Na rede '+N.ssid+' - '+N.ip):(N.ap?('Portal: '+N.apSsid):N.state);
$('#s_time').innerHTML=esc(s.time)+(s.ntp?' <span class="ok">(internet)</span>':' <span class="warn">(manual)</span>');
$('#s_net').innerHTML='<span class="'+(N.state==='CONECTADO'?'ok':'warn')+'">'+esc(N.state)+'</span>'+(N.ssid?' '+esc(N.ssid):'')+(N.ip&&N.state==='CONECTADO'?' - '+esc(N.ip):'');
$('#s_wiz').innerHTML='<span class="'+(W.status==='ONLINE'?'ok':'warn')+'">'+esc(W.status)+'</span>'+(W.ip?' '+esc(W.ip)+(W.fixed?' (fixo)':' (auto)'):'');
$('#s_night').textContent=(s.night.active?'modo noturno':'modo dia')+(s.sunrise?' - amanhecer em andamento':'');
$('#found').innerHTML=W.found.map(f=>'<div><span>'+esc(f.ip)+' <span class="sub">'+esc(f.mac)+'</span></span><button type="button" class="small" onclick="F.wizIp.value=\''+esc(f.ip)+'\';toast(\'Toque em Salvar para fixar\')">Usar</button></div>').join('');
$('#busy').style.display=s.alarm.busy?'block':'none';$('#fw').textContent='Firmware '+s.fw}
async function refresh(){try{const s=await (await fetch('/api/state')).json();if(!loaded){fill(s);loaded=true}show(s)}catch(e){$('#hdr').textContent='sem conexao com o despertador'}}
async function save(e){e.preventDefault();const b=new URLSearchParams(new FormData(F));if(!F.almEn.checked)b.set('almEn','0');
if(await post('/api/save','Configuracoes salvas',b)){loaded=false}return false}
async function wifi(e){e.preventDefault();const b=new URLSearchParams(new FormData(e.target));
await post('/api/wifi','Conectando... veja o IP na tela do despertador',b);return false}
async function scan(){$('#nets').innerHTML='<div class="sub">procurando...</div>';for(let i=0;i<15;i++){try{const j=await (await fetch('/api/scan')).json();
if(!j.scanning){$('#nets').innerHTML=j.nets.length?j.nets.map(n=>'<div><span>'+esc(n.ssid)+' <span class="sub">'+n.rssi+' dBm'+(n.open?' aberta':'')+'</span></span><button type="button" class="small" onclick="$(\'#ssid\').value=this.dataset.s" data-s="'+esc(n.ssid)+'">Usar</button></div>').join(''):'<div class="sub">nenhuma rede encontrada</div>';return}}catch(e){}
await new Promise(r=>setTimeout(r,1000))}$('#nets').innerHTML='<div class="sub">a busca demorou demais, tente de novo</div>'}
refresh();setInterval(refresh,4000);
</script></body></html>)HTML";

#endif /* PORTAL_PAGE_H */
