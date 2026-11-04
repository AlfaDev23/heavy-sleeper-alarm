/* fakes.cpp — Substitutos de tela, touch, UART, audio, Wi-Fi e lampada para o teste no PC. */
#include "src/config.h"
#include "src/ui.h"
#include "src/touch.h"
#include "src/comm.h"
#include "src/audio.h"
#include "src/net.h"
#include "src/wiz.h"
#include "src/portal.h"
#include "src/challenges.h"
unsigned long g_ms = 0; FakeSerial Serial; TFT_eSPI tft; XPT2046_Touchscreen touch;
// ---- ui
Screen g_screen = SCREEN_CLOCK; bool g_night=false; int g_rightSeen=0, g_wrongSeen=0, g_timeoutSeen=0; float g_hold=0;
void uiBegin(){} void uiSetBacklight(uint8_t){}
void uiSetNight(bool n){g_night=n;} bool uiIsNight(){return g_night;}
void uiDrawScreen(Screen s){g_screen=s;}
void uiClockTick(){} void uiNetworkTick(){}
void uiRingingUpdate(int,unsigned long,int,bool){}
void uiRingHoldProgress(float p){g_hold=p;}
void uiChallengeUpdate(unsigned long,unsigned long){ const Challenge&c=challengeCurrent(); if(!c.redraw) return; challengeClearRedraw();
  if(c.phase==CHP_RIGHT) g_rightSeen++; if(c.phase==CHP_WRONG){ g_wrongSeen++; if(c.timedOut) g_timeoutSeen++; } }
int g_action=ACT_NONE; int uiHitTest(int,int){return g_action;} void uiFlashAction(int){} void uiFlashUpdate(){}
void uiToast(const char*,uint16_t){} void uiDrawCalibrationPrompt(){}
// ---- touch
bool g_down=false; int g_tx=0,g_ty=0;
void touchBegin(){} void touchUpdate(){} bool touchAvailable(){return true;}
bool getTouchPoint(int&x,int&y){ if(!g_down) return false; x=g_tx;y=g_ty; return true;}
bool g_tapPending=false; bool touchTap(int&x,int&y){ if(!g_tapPending) return false; g_tapPending=false; x=1;y=1; return true;} bool touchLongPress(int&,int&){return false;} void touchFlush(){}
bool isTouched(int bx,int by,int bw,int bh,int tx,int ty){ return tx>=bx&&tx<bx+bw&&ty>=by&&ty<by+bh; }
// ---- comm / audio
const char* g_vib="OFF"; int g_audio=0;
void commBegin(){} void commPoll(){} void commSendLight(int){} void commSendVib(const char*m){g_vib=m;}
void commRequestStatus(){} void commSendAlarm(const char*){} bool commActuatorOnline(){return true;}
void audioBegin(){} void audioSetLevel(int l){g_audio=l;} int audioGetLevel(){return g_audio;} void stopAudio(){g_audio=0;}
// ---- net / portal
void netBegin(){} void netUpdate(){} NetState netState(){return NET_CONNECTED;} bool netConnected(){return true;}
void netOpenPortal(){} void netClosePortal(){} bool netApActive(){return false;}
void portalBegin(){} void portalUpdate(){}
// ---- wiz
bool g_wizOn=false; int g_dim=0,g_temp=0; int g_wizCmds=0;
void wizBegin(){} void wizUpdate(){}
void wizSetLight(int d,int t){g_wizOn=true;g_dim=d;g_temp=t;g_wizCmds++;}
void wizOff(){g_wizOn=false;g_wizCmds++;}
void wizTestBlink(){} void wizDiscover(){} const char* wizTargetIp(){return "1.2.3.4";}
