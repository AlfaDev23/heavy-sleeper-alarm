#pragma once
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <cstdarg>
#include <algorithm>
extern unsigned long g_ms;
inline unsigned long millis(){ return g_ms; }
#define F(x) x
#define PROGMEM
#define LOW 0
#define HIGH 1
#define OUTPUT 1
#define INPUT 0
template<class T,class L,class H> T constrain(T x,L lo,H hi){ return x<(T)lo?(T)lo:(x>(T)hi?(T)hi:x); }
using std::min; using std::max;
inline long random(long lo,long hi){ return lo + (rand() % (hi-lo)); }
inline long random(long hi){ return rand()%hi; }
inline void randomSeed(unsigned long s){ srand(s); }
inline void pinMode(int,int){} inline void digitalWrite(int,int){} inline int analogRead(int){return 0;}
struct FakeSerial { bool quiet=true;
  void printf(const char* f,...){ if(quiet)return; va_list a; va_start(a,f); vprintf(f,a); va_end(a);} 
  void println(const char* s){ if(!quiet) puts(s);} void print(const char*){} void begin(...){} };
extern FakeSerial Serial;
