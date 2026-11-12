#!/usr/bin/env bash
# Run inside the Linux builder with gcc/g++-multilib and qemu-user installed.
# 32-bit object sizes match the target; snapshots need a separate, larger pool.
set -euo pipefail
MODULE="${PROSPECTOR_ROOT:-/workspaces/prospector-local-module}"
FW="${PROSPECTOR_FIRMWARE:-/workspaces/zmk}"
OUT="$FW/build/operator-host-cache/build/input-modes-32"
cmake -S "$MODULE/tools/operator_host" -B "$OUT" \
  -DCMAKE_C_FLAGS=-m32 -DCMAKE_CXX_FLAGS=-m32 -DCMAKE_EXE_LINKER_FLAGS=-m32 \
  -DPROSPECTOR_ROOT="$MODULE" -DLVGL_DIR="$FW/modules/lib/gui/lvgl" \
  -DPROSPECTOR_HOST_ALL_LAYOUTS=ON -DPROSPECTOR_HOST_INPUT_MODES=ON \
  -DPROSPECTOR_HOST_STUDIO_USB=ON -DPROSPECTOR_HOST_LEFT_RECOVERY=OFF \
  -DOPERATOR_LAYER_COUNT=9 -DPROSPECTOR_HOST_HEAP_BYTES=65536
cmake --build "$OUT" --parallel 4
qemu-i386 "$OUT/operator_renderer" --device-tests
qemu-i386 "$OUT/operator_renderer" --peripheral-tests
