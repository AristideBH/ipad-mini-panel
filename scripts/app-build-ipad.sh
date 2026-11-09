#!/bin/sh
# Compile l'appli pour l'iPad : ARMv7, binaire statique (musl), dans un
# conteneur Alpine ARM émulé par QEMU. Le binaire ne dépend de rien sur l'iPad.
#
# Prérequis Ubuntu, une seule fois :
#   sudo apt install -y qemu-user-binfmt      (qemu-user-static avant Ubuntu 26.04)
# Vérification : docker run --rm --platform linux/arm/v7 alpine:3.22 uname -m  -> armv7l
# Si « exec format error » : docker run --privileged --rm tonistiigi/binfmt --install arm
set -eu

# Docker inaccessible alors qu'on est inscrit au groupe : la session n'a pas
# encore pris le groupe en compte (il faut redémarrer). En attendant, on se
# relance à travers le groupe docker.
if ! docker info >/dev/null 2>&1; then
    if getent group docker | grep -qw "$(id -un)" && [ -z "${PANEL_SG:-}" ]; then
        echo "Docker inaccessible depuis cette session, relance via « sg docker » (redémarrer le PC règle ça pour de bon)." >&2
        PANEL_SG=1 exec sg docker -c "PANEL_SG=1 '$(readlink -f "$0")'"
    fi
    echo "Docker inaccessible. Vérifier : getent group docker ; sudo systemctl status docker" >&2
    exit 1
fi

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
