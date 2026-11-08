#!/bin/sh
# Vérifie que l'IPSW est bien celui attendu par Cascadia (iPad2,5, iOS 8.4.1, 12H321).
set -eu

EXPECTED="365d4a8e9ea9613c6bdb6db35cba0331580f7ae7"
FILE="${1:-firmware/iPad2,5_8.4.1_12H321_Restore.ipsw}"

if [ ! -f "$FILE" ]; then
    echo "Fichier introuvable : $FILE" >&2
    echo "Téléchargement : https://ipswdl.com/ipsw/iPad2,5/12H321" >&2
    exit 1
fi

if command -v shasum >/dev/null 2>&1; then
    ACTUAL=$(shasum -a 1 "$FILE" | cut -d' ' -f1)
else
    ACTUAL=$(sha1sum "$FILE" | cut -d' ' -f1)
fi

if [ "$ACTUAL" = "$EXPECTED" ]; then
    echo "IPSW OK"
else
    echo "Empreinte différente, fichier corrompu ou mauvaise version." >&2
    echo "  attendue : $EXPECTED" >&2
    echo "  obtenue  : $ACTUAL" >&2
    exit 1
fi
