#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
IMAGE="${DSM_BUILD_IMAGE:-dsm-builder}"
CROSS="${CROSS_PREFIX:-aarch64-linux-gnu-}"
VERSION="$(python3 -c 'import json;print(json.load(open("module.json"))["version"])')"
[ -f vendor/schwung/plugin_api_v1.h ] || { echo "vendor header missing -- run scripts/vendor-header.sh update" >&2; exit 1; }
docker build -t "$IMAGE" -f scripts/Dockerfile .
docker run --rm -v "$PWD":/w -w /w "$IMAGE" bash -lc '
  set -euo pipefail
  mkdir -p build/pack
  '"$CROSS"'gcc -O2 -Wall -Wextra -fPIC -shared -I vendor/schwung \
      -o build/pack/dsp.so src/schwung_dubstarmoonicon.c src/suborg.c -lm -lpthread
  '"$CROSS"'strip --strip-unneeded build/pack/dsp.so
  cp module.json help.json LICENSE build/pack/
'
echo "built build/pack/"; ls -la build/pack/

# Stage under a top-level module directory so the manager's catalog install
# (extract into modules/sound_generators/) produces modules/sound_generators/dubstarmoonicon/.
rm -rf dist/dubstarmoonicon
mkdir -p dist/dubstarmoonicon
cp build/pack/* dist/dubstarmoonicon/
(cd dist && tar -czvf dubstarmoonicon-module.tar.gz dubstarmoonicon/)

TARBALL="build/dubstarmoonicon-module.tar.gz"
mv dist/dubstarmoonicon-module.tar.gz "$TARBALL"
echo "packaged $TARBALL (v$VERSION)"
tar -tzf "$TARBALL"
