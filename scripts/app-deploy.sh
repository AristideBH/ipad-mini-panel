#!/bin/sh
# Envoie l'appli sur l'iPad (sous Linux, par le câble) et la lance.
#   scripts/app-deploy.sh         envoie et lance au premier plan (Ctrl+C pour quitter)
#   scripts/app-deploy.sh --bg    envoie et lance en arrière-plan
#   scripts/app-deploy.sh --stop  arrête l'appli et rend la main à la console
set -eu
cd "$(dirname "$0")/../app"

IPAD="${IPAD:-root@10.55.0.2}"
SSH="ssh -o StrictHostKeyChecking=accept-new $IPAD"

# Arrête toutes les instances, et vérifie : deux instances qui dessinent sur le
# même framebuffer se recouvrent à l'écran.
STOP='killall panel 2>/dev/null; for i in 1 2 3 4 5; do pidof panel >/dev/null || exit 0; sleep 0.2; done; killall -9 panel 2>/dev/null; sleep 0.2; ! pidof panel >/dev/null'

if [ "${1:-}" = "--stop" ]; then
    $SSH "$STOP" || { echo "panel tourne encore sur l'iPad" >&2; exit 1; }
    exit 0
fi

[ -x build-ipad/panel ] || { echo "Pas de build-ipad/panel : lancer scripts/app-build-ipad.sh" >&2; exit 1; }

# Pas de scp : dropbear n'a pas forcément de serveur sftp. Un cat par ssh suffit.
$SSH "$STOP" || { echo "Impossible d'arrêter l'ancienne instance sur l'iPad" >&2; exit 1; }
$SSH 'cat > /root/panel.new && chmod +x /root/panel.new && mv /root/panel.new /root/panel' < build-ipad/panel
if [ -f panel.env ]; then
    $SSH 'cat > /root/panel.env' < panel.env
fi

# Le clavier à l'écran dessine aussi dans le framebuffer : on l'arrête.
RUN='pkill fbkeyboard 2>/dev/null || true; [ -f /root/panel.env ] && { set -a; . /root/panel.env; set +a; }; exec /root/panel'

if [ "${1:-}" = "--bg" ]; then
    $SSH "nohup sh -c '$RUN' >/root/panel.log 2>&1 &"
    echo "Lancée en arrière-plan. Journal : ssh $IPAD cat /root/panel.log"
else
    exec ssh -t -o StrictHostKeyChecking=accept-new "$IPAD" "$RUN"
fi
