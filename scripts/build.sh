#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
IMAGE="${DSM_BUILD_IMAGE:-dsm-builder}"
CROSS="${CROSS_PREFIX:-aarch64-linux-gnu-}"
[ -f vendor/schwung/plugin_api_v1.h ] || { echo "vendor header missing -- run scripts/vendor-header.sh update" >&2; exit 1; }
docker build -t "$IMAGE" -f scripts/Dockerfile .
docker run --rm -v "$PWD":/w -w /w "$IMAGE" bash -lc '
  set -euo pipefail
  mkdir -p build/pack
  '"$CROSS"'gcc -O2 -Wall -Wextra -fPIC -shared -I vendor/schwung \
      -o build/pack/dsp.so src/schwung_dubstarmoonicon.c src/suborg.c -lm -lpthread
  '"$CROSS"'strip --strip-unneeded build/pack/dsp.so
  cp module.json help.json build/pack/
'
echo "built build/pack/"; ls -la build/pack/
