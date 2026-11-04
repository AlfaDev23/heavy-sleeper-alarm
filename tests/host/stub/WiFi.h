#pragma once
#include <Arduino.h>
struct IPAddress { const char* s="0.0.0.0"; struct S{const char* p; const char* c_str() const {return p;}}; S toString() const {return {s};} };
