#!/bin/bash
# Compila e roda a simulacao do alarme no PC (precisa de g++).
# Roda uma vez para cada tipo de desafio (0=MAT 1=MEM 2=SEQ 3=REA).
set -e
cd "$(dirname "$0")"
rm -rf src build && mkdir -p src build
cp ../../heavy_sleeper_display/*.h ../../heavy_sleeper_display/{clock,challenges,night,storage}.cpp src/
cp ../../heavy_sleeper_display/heavy_sleeper_display.ino src/main.cpp
sed -i 's|^#define DEMO_MODE|// #define DEMO_MODE|' src/config.h   # tempos reais
fail=0
for t in 0 1 2 3; do
  g++ -std=gnu++17 -Istub -Isrc -include Arduino.h -DTEST_CHAL_TYPE=$t -o build/t$t \
      test_alarm.cpp fakes.cpp src/main.cpp src/clock.cpp src/challenges.cpp src/night.cpp src/storage.cpp
  if [ "$t" = 0 ]; then ./build/t$t || fail=1; else echo "tipo $t: $(./build/t$t | tail -1)" || fail=1; ./build/t$t >/dev/null || fail=1; fi
done
rm -rf src build
exit $fail
