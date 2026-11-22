#pragma once
#include <Arduino.h>
#include <map>
#include <string>
struct Preferences {
  std::map<std::string,long> i; std::map<std::string,std::string> s, b;
  bool begin(const char*,bool){return true;}
  int getInt(const char*k,int d){ return i.count(k)?i[k]:d; } void putInt(const char*k,int v){i[k]=v;}
  bool getBool(const char*k,bool d){ return i.count(k)?i[k]:d; } void putBool(const char*k,bool v){i[k]=v;}
  bool isKey(const char*k){ return i.count(k)||s.count(k)||b.count(k); }
  size_t getString(const char*k,char*o,size_t n){ strncpy(o,s[k].c_str(),n-1); o[n-1]=0; return 1;}
  void putString(const char*k,const char*v){s[k]=v;}
  size_t getBytesLength(const char*k){ return b.count(k)?b[k].size():0; }
  size_t getBytes(const char*k,void*o,size_t n){ if(!b.count(k)) return 0; size_t c=b[k].size()<n?b[k].size():n; memcpy(o,b[k].data(),c); return c; }
  size_t putBytes(const char*k,const void*v,size_t n){ b[k].assign((const char*)v,n); return n; }
  void remove(const char*k){ i.erase(k); s.erase(k); b.erase(k); }
};
