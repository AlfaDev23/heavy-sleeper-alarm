/*
 * portal_page.h — Pagina unica do portal de configuracao (HTML + CSS + JS).
 *
 * Fica na flash (PROGMEM). A pagina so fala com a API JSON do portal.cpp:
 *   GET  /api/state          estado completo
 *   POST /api/alarms         list=HH:MM,ativo,dias;... (ate 6; dias 0-127, bit0=dom)
 *   POST /api/save           opcoes (chalMask = tipos marcados, ringMax), lampada, modo noturno e fuso
 *   POST /api/mode           demo=1 (demonstracao) / demo=0 (producao)
 *   POST /api/vib/test       duty=0-100: vibra 2 s nessa intensidade
 *   POST /api/sound/test     lvl=1|4, pct=0-100: toca 2 s a melodia nesse volume
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
.alm{border-bottom:1px solid var(--line);padding:10px 0}.alm .row{align-items:center}.alm .row>input{flex:2}.alm .row>button{flex:0 0 auto}
.alm .chk{margin:0}.days{display:flex;gap:4px;margin:8px 0 4px}.days span{flex:1;text-align:center;padding:7px 0;border:1px solid var(--line);border-radius:6px;cursor:pointer;user-select:none;font-size:14px}
.days span.on{background:#1f6feb;border-color:#1f6feb}.off{opacity:.55}
.types{display:grid;grid-template-columns:1fr 1fr;gap:0 10px}.types .chk{margin:6px 0}
.vib .row{align-items:center}.vib .row>button{flex:0 0 auto}
</style></head><body>
<header><h1>Despertador Heavy Sleeper</h1><p id="hdr">carregando...</p></header>
<main>
<div class="busy" id="busy">O alarme esta tocando. Resolva o desafio na tela: as configuracoes do alarme ficam bloqueadas ate la.</div>

<section><h2>Status</h2>
<div class="kv"><span>Hora</span><b id="s_time">--</b></div>
<div class="kv"><span>Proximo alarme</span><b id="s_next">--</b></div>
<div class="kv"><span>Wi-Fi</span><b id="s_net">--</b></div>
<div class="kv"><span>Lampada</span><b id="s_wiz">--</b></div>
<div class="kv"><span>Tela</span><b id="s_night">--</b></div>
<div class="kv"><span>Modo</span><b id="s_mode">--</b></div>
</section>

<section><h2>Alarmes</h2>
<div id="alms"></div>
<div class="row"><button type="button" onclick="addAlm()">+ Adicionar</button><button type="button" class="pri" onclick="saveAlms()">Salvar alarmes</button></div>
<p class="sub">Toque nos dias para marcar. Sem nenhum dia marcado o alarme toca uma vez so e depois se desativa. Ate 6 alarmes.</p>
</section>

<form id="cfg" onsubmit="return save(event)">
<section><h2>Opcoes do despertar</h2>
<label>Tocar no maximo (se ninguem desligar)</label><select name="ringMax">
<option value="5">5 min</option><option value="10">10 min</option><option value="15">15 min</option><option value="20">20 min</option><option value="30">30 min</option><option value="0">Sem limite</option></select>
<label>Desafios (marque um ou mais: cada desafio sorteia um deles)</label>
<div class="types"><label class="chk"><input type="checkbox" class="tchk" value="0">Matematica</label><label class="chk"><input type="checkbox" class="tchk" value="1">Memoria</label>
<label class="chk"><input type="checkbox" class="tchk" value="2">Sequencia</label><label class="chk"><input type="checkbox" class="tchk" value="3">Reacao</label></div>
<div class="row"><div><label>Dificuldade</label><select name="diff">
<option value="0">Facil</option><option value="1">Medio</option><option value="2">Dificil</option><option value="3">Extremo</option></select></div></div>
<div class="row"><div><label>Tempo para resolver</label><select name="chal">
<option value="30">30 s</option><option value="60">60 s</option><option value="90">90 s</option><option value="120">120 s</option></select></div>
<div><label>Vibracao (ate onde sobe)</label><select name="vib"><option value="0">Desligada</option><option value="1">Fraca</option><option value="2">Media</option><option value="3">Maxima</option></select></div></div>
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

<section><h2>Volume do alarme</h2>
<p class="sub">O som comeca no volume inicial e sobe ate o maximo conforme o alarme escala (2, 5 e 10 min). <b>Testar</b> toca 2 s a melodia daquele momento, sem salvar.</p>
<div class="vib"><label>Inicial <output id="o_volS"></output></label><div class="row"><input type="range" name="volS" min="5" max="100" step="5" oninput="vo(this);vfix(this)"><button type="button" class="small" onclick="st(1,'volS')">Testar</button></div></div>
<div class="vib"><label>Maximo <output id="o_volX"></output></label><div class="row"><input type="range" name="volX" min="5" max="100" step="5" oninput="vo(this);vfix(this)"><button type="button" class="small" onclick="st(4,'volX')">Testar</button></div></div>
<p class="sub">Acima de ~90% o alto-falante do board pode distorcer.</p>
</section>

<section><h2>Intensidade da vibracao</h2>
<p class="sub">PWM do motor em cada nivel. Mexa no controle e toque em <b>Testar</b> (vibra 2 s, sem salvar). Achou o ponto? <b>Salvar configuracoes</b>.</p>
<div class="vib"><label>Fraca <output id="o_vibW"></output></label><div class="row"><input type="range" name="vibW" min="0" max="100" step="5" oninput="vo(this)"><button type="button" class="small" onclick="vt('vibW')">Testar</button></div></div>
<div class="vib"><label>Media <output id="o_vibM"></output></label><div class="row"><input type="range" name="vibM" min="0" max="100" step="5" oninput="vo(this)"><button type="button" class="small" onclick="vt('vibM')">Testar</button></div></div>
<div class="vib"><label>Maxima <output id="o_vibX"></output></label><div class="row"><input type="range" name="vibX" min="0" max="100" step="5" oninput="vo(this)"><button type="button" class="small" onclick="vt('vibX')">Testar</button></div></div>
<p class="sub">No alarme: comeca na fraca (pulsando), passa para a media continua aos 2 min e para a maxima aos 5 min, sem passar do nivel escolhido em Vibracao. Se o motor nao girar numa intensidade baixa, suba o valor dela.</p>
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

<section><h2>Modo de operacao</h2>
<p class="sub" id="m_txt"></p>
<button type="button" id="m_btn" onclick="mode()">--</button>
<p class="sub"><b>Producao</b> = uso real: hora pela internet e o alarme que voce configurou. <b>Demo</b> = apresentacao: relogio vai para 07:29, alarme as 07:30 e tempos curtos. O alarme salvo nao se perde ao usar o demo.</p>
</section>

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
const $=s=>document.querySelector(s),F=$('#cfg');let loaded=false,demo=false;
const hm=m=>String(Math.floor(m/60)).padStart(2,'0')+':'+String(m%60).padStart(2,'0');
function toast(t){const e=$('#toast');e.textContent=t;e.style.display='block';clearTimeout(e._t);e._t=setTimeout(()=>e.style.display='none',3500)}
function esc(s){return String(s).replace(/[&<>"]/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;'}[c]))}
async function post(u,msg,body){try{const r=await fetch(u,{method:'POST',body:body||new URLSearchParams()});const j=await r.json().catch(()=>({}));
toast(r.ok?(msg||'OK'):(j.error||('Erro '+r.status)));setTimeout(refresh,800);return r.ok}catch(e){toast('Sem resposta do despertador');return false}}
let A=[],aLoaded=false;const DN=['D','S','T','Q','Q','S','S'],DF=['dom','seg','ter','qua','qui','sex','sab'];
function dtxt(d){if(d==0)return'Toca uma vez e se desativa';if(d==127)return'Todos os dias';if(d==62)return'Segunda a sexta';if(d==65)return'Sabado e domingo';
let o=[];for(let k=1;k<=7;k++){if(d>>(k%7)&1)o.push(DF[k%7])}return o.join(', ')}
function drawAlms(){$('#alms').innerHTML=A.length?A.map((a,i)=>'<div class="alm'+(a.en?'':' off')+'"><div class="row"><input type="time" value="'+hm(a.h*60+a.m)+'" onchange="setT('+i+',this.value)">'
+'<label class="chk"><input type="checkbox"'+(a.en?' checked':'')+' onchange="A['+i+'].en=this.checked;drawAlms()">Ativo</label>'
+'<button type="button" class="small" onclick="A.splice('+i+',1);drawAlms()">Remover</button></div><div class="days">'
+DN.map((n,d)=>'<span class="'+(a.d>>d&1?'on':'')+'" onclick="A['+i+'].d^='+(1<<d)+';drawAlms()">'+n+'</span>').join('')
+'</div><div class="sub">'+dtxt(a.d)+'</div></div>').join(''):'<div class="sub">Nenhum alarme.</div>'}
function setT(i,v){const p=v.split(':');if(p.length<2)return;A[i].h=+p[0];A[i].m=+p[1]}
function addAlm(){if(A.length>=6){toast('No maximo 6 alarmes');return}A.push({h:7,m:0,d:62,en:true});drawAlms()}
async function saveAlms(){const b=new URLSearchParams();b.set('list',A.map(a=>hm(a.h*60+a.m)+','+(a.en?1:0)+','+a.d).join(';'));
if(await post('/api/alarms','Alarmes salvos',b)){aLoaded=false}}
function fill(s){const a=s.alarm,n=s.night;F.ringMax.value=a.ringMax;document.querySelectorAll('.tchk').forEach(c=>c.checked=!!(a.mask>>c.value&1));F.diff.value=a.diff;
F.chal.value=a.chal;F.vib.value=a.vib;F.ramp.value=a.ramp;F.light.value=a.light;$('#lv').value=a.light+'%';
['vibW','vibM','vibX'].forEach((k,i)=>{F[k].value=a.vibD[i];vo(F[k])});F.volS.value=a.volS;F.volX.value=a.volX;vo(F.volS);vo(F.volX);F.wizIp.value=s.wiz.cfgIp;F.nightMode.value=n.mode;F.nightStart.value=hm(n.start);F.nightEnd.value=hm(n.end);F.tz.value=s.tz}
function show(s){const N=s.net,W=s.wiz;$('#hdr').textContent=N.state==='CONECTADO'?('Na rede '+N.ssid+' - '+N.ip):(N.ap?('Portal: '+N.apSsid):N.state);
$('#s_next').innerHTML=s.missed?'<span class="bad">'+esc(s.missed)+'</span>':(s.next?esc(s.next):'<span class="sub">nenhum ativo</span>');
$('#s_time').innerHTML=esc(['DOM','SEG','TER','QUA','QUI','SEX','SAB'][s.wday]||'')+' '+esc(s.time)+(s.ntp?' <span class="ok">(internet)</span>':' <span class="warn">(manual)</span>');
$('#s_net').innerHTML='<span class="'+(N.state==='CONECTADO'?'ok':'warn')+'">'+esc(N.state)+'</span>'+(N.ssid?' '+esc(N.ssid):'')+(N.ip&&N.state==='CONECTADO'?' - '+esc(N.ip):'');
$('#s_wiz').innerHTML='<span class="'+(W.status==='ONLINE'?'ok':'warn')+'">'+esc(W.status)+'</span>'+(W.ip?' '+esc(W.ip)+(W.fixed?' (fixo)':' (auto)'):'');
$('#s_night').textContent=(s.night.active?'modo noturno':'modo dia')+(s.sunrise?' - amanhecer em andamento':'');
$('#found').innerHTML=W.found.map(f=>'<div><span>'+esc(f.ip)+' <span class="sub">'+esc(f.mac)+'</span></span><button type="button" class="small" onclick="F.wizIp.value=\''+esc(f.ip)+'\';toast(\'Toque em Salvar para fixar\')">Usar</button></div>').join('');
$('#s_mode').innerHTML=s.demo?'<span class="warn">DEMO</span>':'<span class="ok">PRODUCAO</span>';
$('#m_txt').innerHTML='Agora: '+$('#s_mode').innerHTML;$('#m_btn').textContent=s.demo?'Mudar para PRODUCAO':'Mudar para DEMO';demo=s.demo;
$('#busy').style.display=s.alarm.busy?'block':'none';$('#fw').textContent='Firmware '+s.fw}
async function refresh(){try{const s=await (await fetch('/api/state')).json();if(!loaded){fill(s);loaded=true}
if(!aLoaded){A=s.alarms.map(x=>({h:x.h,m:x.m,d:x.d,en:x.en}));drawAlms();aLoaded=true}show(s)}catch(e){$('#hdr').textContent='sem conexao com o despertador'}}
async function save(e){e.preventDefault();let m=0;document.querySelectorAll('.tchk:checked').forEach(c=>m|=1<<c.value);
if(!m){toast('Marque pelo menos 1 tipo de desafio');return false}const b=new URLSearchParams(new FormData(F));b.set('chalMask',m);
if(await post('/api/save','Configuracoes salvas',b)){loaded=false}return false}
function vo(e){$('#o_'+e.name).value=e.value+'%'}
function vfix(e){if(+F.volS.value>+F.volX.value){if(e.name=='volS'){F.volX.value=F.volS.value;vo(F.volX)}else{F.volS.value=F.volX.value;vo(F.volS)}}}
function st(l,k){const b=new URLSearchParams();b.set('lvl',l);b.set('pct',F[k].value);post('/api/sound/test','Tocando '+F[k].value+'% por 2 s',b)}
function vt(k){const b=new URLSearchParams();b.set('duty',F[k].value);post('/api/vib/test','Vibrando '+F[k].value+'% por 2 s',b)}
async function mode(){const on=!demo;if(!confirm(on?'Ligar o DEMO? O relogio vai para 07:29 e o alarme toca as 07:30.':'Voltar para PRODUCAO? A hora volta a vir da internet e o alarme salvo volta.'))return;
const b=new URLSearchParams();b.set('demo',on?'1':'0');if(await post('/api/mode',on?'Modo DEMO':'Modo PRODUCAO',b)){loaded=false}}
async function wifi(e){e.preventDefault();const b=new URLSearchParams(new FormData(e.target));
await post('/api/wifi','Conectando... veja o IP na tela do despertador',b);return false}
async function scan(){$('#nets').innerHTML='<div class="sub">procurando...</div>';for(let i=0;i<15;i++){try{const j=await (await fetch('/api/scan')).json();
if(!j.scanning){$('#nets').innerHTML=j.nets.length?j.nets.map(n=>'<div><span>'+esc(n.ssid)+' <span class="sub">'+n.rssi+' dBm'+(n.open?' aberta':'')+'</span></span><button type="button" class="small" onclick="$(\'#ssid\').value=this.dataset.s" data-s="'+esc(n.ssid)+'">Usar</button></div>').join(''):'<div class="sub">nenhuma rede encontrada</div>';return}}catch(e){}
await new Promise(r=>setTimeout(r,1000))}$('#nets').innerHTML='<div class="sub">a busca demorou demais, tente de novo</div>'}
refresh();setInterval(refresh,4000);
</script></body></html>)HTML";

#endif /* PORTAL_PAGE_H */
