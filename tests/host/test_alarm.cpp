/*
 * test_alarm.cpp — Simula no PC uma manha inteira do Display: rampa da
 * lampada, tocar, segurar DESLIGAR, errar, tempo esgotado, escalada,
 * acerto, soneca, modo noturno e retorno automatico ao relogio.
 *
 * Usa a logica REAL (heavy_sleeper_display.ino, challenges, clock, night,
 * storage) com tela, touch, Wi-Fi e lampada falsos (fakes.cpp).
 * Rodar: tests/host/run.sh
 */
#ifndef TEST_CHAL_TYPE
#define TEST_CHAL_TYPE 0
#endif
#include "src/config.h"
#include "src/clock.h"
#include "src/storage.h"
#include "src/challenges.h"
#include "src/ui.h"
extern bool g_tapPending; extern int g_action;
static void tap(int a){ g_action=a; g_tapPending=true; }
extern unsigned long g_ms; extern Screen g_screen; extern bool g_night,g_wizOn,g_down; extern int g_dim,g_temp,g_audio,g_tx,g_ty,g_rightSeen,g_wrongSeen,g_timeoutSeen; extern const char* g_vib;
void setup(); void loop(); bool appAlarmBusy();
static int fails=0;
#define CHECK(c,msg) do{ if(c) printf("  ok   %s\n",msg); else {printf("  FAIL %s\n",msg); fails++;} }while(0)
static void run(unsigned long ms){ unsigned long end=g_ms+ms; while(g_ms<end){ g_ms+=5; loop(); } }
static void hold(bool d){ g_down=d; g_tx=DISMISS_X+50; g_ty=DISMISS_Y+40; }
static void solve(){ const Challenge&c=challengeCurrent(); 
  // espera fases SHOW (memoria/reacao) terminarem
  int guard=0; while(c.phase==CHP_SHOW && guard++<20000){ g_ms+=5; loop(); }
  for(int i=c.entered;i<c.expectedLen;i++) challengeTouchIndex(c.expected[i]); }
static void wrong(){ const Challenge&c=challengeCurrent(); int guard=0; while(c.phase==CHP_SHOW && guard++<20000){ g_ms+=5; loop(); }
  int bad=(c.expected[c.entered]+1)%c.optCount; challengeTouchIndex(bad); }
int main(){
  setup();
  settings.almHour=7; settings.almMin=0; settings.almEnabled=true; settings.rampMin=10; settings.lightPct=80;
  settings.chalTimeS=60; settings.diffType=TEST_CHAL_TYPE; settings.vibMode=VIB_MODE_STRONG; settings.nightMode=NIGHT_AUTO;
  rtc.setTime(6,49,0); rtc.lastTickMs=g_ms;
  printf("A) nascer do sol\n");
  run(30000); CHECK(!g_wizOn,"06:49:30 lampada ainda apagada");
  run(62000); CHECK(g_wizOn && g_dim<=15 && g_temp<=2500, "06:50:32 acendeu fraca e quente (32 s de rampa)");
  printf("     dim=%d temp=%d\n",g_dim,g_temp);
  run(5*60000); printf("     06:55:32 dim=%d temp=%d\n",g_dim,g_temp); CHECK(g_dim>40&&g_dim<50,"metade da rampa ~45%");
  run(4*60000+20000); printf("     06:59:52 dim=%d temp=%d\n",g_dim,g_temp); CHECK(g_dim>=78,"quase no maximo antes de tocar");
  run(10000);
  printf("B) alarme toca e segurar DESLIGAR\n");
  CHECK(g_screen==SCREEN_ALARM_RINGING && g_audio==1,"07:00 tocando, som nivel 1");
  CHECK(g_wizOn && g_dim==80 && g_temp==6500,"luz no maximo configurado");
  hold(true); run(1000); hold(false); run(500);
  CHECK(g_screen==SCREEN_ALARM_RINGING,"segurar 1 s nao desliga");
  hold(true); run(2100);
  CHECK(g_screen==SCREEN_ALARM_ACTIVE && g_audio==0 && !strcmp(g_vib,"OFF"),"segurar 2 s: desafio em silencio");
  CHECK(g_wizOn,"luz continua acesa no desafio");
  hold(false); run(200);
  printf("C) errar volta a tocar\n");
  int lvl0=challengeCurrent().level; wrong(); run(2000);
  CHECK(g_wrongSeen==1,"tela ERROU foi desenhada");
  CHECK(g_screen==SCREEN_ALARM_RINGING && g_audio>=1,"voltou a tocar");
  hold(true); run(2100); hold(false); run(100);
  CHECK(challengeCurrent().level==lvl0+1,"desafio subiu 1 nivel");
  printf("D) tempo esgotado\n");
  int w0=g_wrongSeen; run(60500); CHECK(g_timeoutSeen==1 || (settings.diffType==CHAL_REACTION && g_wrongSeen>w0),"tela TEMPO ESGOTADO (ou ERROU pela janela da Reacao)");
  run(2000); CHECK(g_screen==SCREEN_ALARM_RINGING,"tempo acabou: toca de novo");
  printf("E) escalada so conta tempo tocando\n");
  // tempo tocando ate aqui ~ 3.6 s + ... ; o desafio durou >60 s e nao pode ter subido o estagio
  CHECK(g_audio==1,"ainda estagio 1 apesar de >60 s de desafio");
  run(2*60000+1000); CHECK(g_audio==2,"2 min tocando -> estagio 2");
  printf("F) acertar encerra e deixa a luz acesa\n");
  hold(true); run(2100); hold(false); run(100);
  solve(); run(2000);
  CHECK(g_rightSeen==1,"tela CORRETO! desenhada");
  CHECK(g_screen==SCREEN_CLOCK && !appAlarmBusy(),"voltou ao relogio, alarme encerrado");
  CHECK(g_wizOn && g_audio==0,"luz acesa, som desligado");
  printf("G) soneca apaga a luz e retoca\n");
  rtc.setTime(8,0,0); settings.almHour=8; settings.almMin=2; // proximo alarme 08:02
  run(130000); CHECK(g_screen==SCREEN_ALARM_RINGING,"08:02 tocou");
  tap(ACT_SNOOZE); run(100);
  CHECK(!g_wizOn && g_screen==SCREEN_CLOCK,"soneca: luz apagada, relogio");
  run(5*60000+1000); CHECK(g_screen==SCREEN_ALARM_RINGING && g_wizOn,"soneca venceu: toca com luz");
  hold(true); run(2100); hold(false); solve(); run(2000);
  printf("H) cancelar rampa no meio apaga\n");
  rtc.setTime(9,0,0); settings.almHour=9; settings.almMin=5; settings.rampMin=10; run(3000);
  CHECK(g_wizOn,"dentro da rampa: lampada ligada (estado anterior ou rampa)");
  settings.almEnabled=false; run(1500); CHECK(!g_wizOn,"alarme desativado no meio: lampada apagada");
  printf("I) modo noturno\n");
  settings.almEnabled=false; rtc.setTime(21,59,57); run(1500); CHECK(!g_night && g_screen==SCREEN_CLOCK,"21:59 tema normal");
  run(3000); CHECK(g_night,"22:00 entrou no modo noturno");
  tap(ACT_NIGHT_PEEK); run(100); CHECK(!g_night,"toque: espia o tema normal");
  run(11200); CHECK(g_night,"10 s depois volta ao noturno");
  rtc.setTime(5,59,58); run(3000); CHECK(!g_night,"06:00 saiu do modo noturno");
  settings.nightMode=NIGHT_FORCE_NIGHT; run(1100); CHECK(g_night,"modo NOITE forcado");
  settings.nightMode=NIGHT_FORCE_DAY; run(1100); CHECK(!g_night,"modo DIA forcado");
  printf("J) tela de ajuste esquecida volta ao relogio\n");
  tap(ACT_GO_NIGHT_CFG); run(100); CHECK(g_screen==SCREEN_NIGHT_CONFIG,"abriu ajustes");
  run(61000); CHECK(g_screen==SCREEN_CLOCK,"1 min sem toque: relogio");
  printf("\n%s (%d falhas)\n", fails?"FALHOU":"TUDO OK", fails);
  return fails;
  return 0;
}
