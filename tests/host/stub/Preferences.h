#pragma once
#include <Arduino.h>
#include <map>
#include <string>
struct Preferences {
  std::map<std::string,long> i; std::map<std::string,std::string> s;
  bool begin(const char*,bool){return true;}
  int getInt(const char*k,int d){ return i.count(k)?i[k]:d; } void putInt(const char*k,int v){i[k]=v;}
  bool getBool(const char*k,bool d){ return i.count(k)?i[k]:d; } void putBool(const char*k,bool v){i[k]=v;}
  bool isKey(const char*k){ return i.count(k)||s.count(k); }
  size_t getString(const char*k,char*o,size_t n){ strncpy(o,s[k].c_str(),n-1); o[n-1]=0; return 1;}
  void putString(const char*k,const char*v){s[k]=v;}
  size_t getBytesLength(const char*){return 0;} void getBytes(const char*,void*,size_t){} void putBytes(const char*,const void*,size_t){}
  void remove(const char*){}
};
