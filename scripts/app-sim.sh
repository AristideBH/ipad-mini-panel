#!/bin/sh
# Compile et lance l'appli dans une fenêtre SDL sur le PC (768 x 1024).
# Prérequis Ubuntu : sudo apt install -y build-essential cmake libsdl2-dev
# Les variables HA_* de app/panel.env sont prises si le fichier existe.
set -eu
cd "$(dirname "$0")/../app"

cmake -S . -B build-sim -DPANEL_TARGET=sim >/dev/null
cmake --build build-sim -j"$(nproc)"

if [ -f panel.env ]; then
    set -a
    . ./panel.env
    set +a
fi
exec ./build-sim/panel
