#!/bin/sh
# Petit service HTTP qui garde la disposition des cartes (ordre, icône,
# masquage) éditée depuis la page Réglages de l'appli, dans
# app/panel-tiles.json. L'iPad le lit au démarrage et y écrit à chaque
# modification (voir app/src/tiles.c). Sans lui, l'appli garde simplement
# l'ordre de HA_CARDS, sans rien à sauvegarder de durable.
#
#   scripts/config-server.sh            écoute sur 0.0.0.0:8787
#   PORT=9000 scripts/config-server.sh  un autre port
set -eu
cd "$(dirname "$0")/.."
PANEL_TILES_FILE="$PWD/app/panel-tiles.json" PANEL_TILES_PORT="${PORT:-8787}" \
    exec python3 -I - <<'PY'
import http.server, json, os, sys

path = os.environ["PANEL_TILES_FILE"]
port = int(os.environ["PANEL_TILES_PORT"])
empty = {"version": 1, "tiles": []}

class Handler(http.server.BaseHTTPRequestHandler):
    def _send(self, code, body=b""):
        self.send_response(code)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        if body:
            self.wfile.write(body)

    def do_GET(self):
        if self.path != "/tiles":
            return self._send(404)
        if os.path.exists(path):
            with open(path, "rb") as f:
                self._send(200, f.read())
        else:
            self._send(200, json.dumps(empty).encode())

    def do_POST(self):
        if self.path != "/tiles":
            return self._send(404)
        n = int(self.headers.get("Content-Length", 0))
        body = self.rfile.read(n) if n else b""
        try:
            json.loads(body)   # valide seulement : on réécrit tel que reçu
        except ValueError:
            return self._send(400)
        tmp = path + ".tmp"
        with open(tmp, "wb") as f:
            f.write(body)
        os.replace(tmp, path)   # remplacement atomique
        self._send(200)

    def log_message(self, fmt, *args):
        sys.stderr.write("config-server: " + (fmt % args) + "\n")

print(f"config-server: écoute sur :{port}, fichier {path}", file=sys.stderr)
http.server.HTTPServer(("0.0.0.0", port), Handler).serve_forever()
PY
