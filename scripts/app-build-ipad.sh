#!/bin/sh
# Compile l'appli pour l'iPad : ARMv7, binaire statique (musl), dans un
# conteneur Alpine ARM émulé par QEMU. Le binaire ne dépend de rien sur l'iPad.
#
# Prérequis Ubuntu, une seule fois :
#   sudo apt install -y qemu-user-static
# (enregistre l'émulation ARM auprès du noyau, Docker s'en sert ensuite)
set -eu
cd "$(dirname "$0")/../app"

docker run --rm --platform linux/arm/v7 \
    -e HOST_UID="$(id -u)" -e HOST_GID="$(id -g)" \
    -v "$PWD":/src -w /src alpine:3.22 sh -euc '
        apk add --no-cache build-base cmake git linux-headers >/dev/null
        cmake -S . -B build-ipad -DPANEL_TARGET=fbdev -DPANEL_STATIC=ON >/dev/null
        cmake --build build-ipad -j"$(nproc)"
        strip build-ipad/panel
        chown -R "$HOST_UID:$HOST_GID" build-ipad
    '

file build-ipad/panel 2>/dev/null || ls -la build-ipad/panel
