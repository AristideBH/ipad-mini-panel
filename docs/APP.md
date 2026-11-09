# Application LVGL

Un seul code, deux cibles :

| Cible | Où | Affichage | Entrée |
|---|---|---|---|
| `sim` | le PC | fenêtre SDL 768 × 1024 | souris |
| `fbdev` | l'iPad sous Linux | `/dev/fb0` | tactile `/dev/input/event0` |

LVGL est épinglé en **v9.2.2** et téléchargé à la compilation. La configuration est dans `app/lv_conf.h`.

## Ce que fait l'appli

- Heure et date en haut.
- Une grille de cartes, une par entité de `HA_CARDS`, sur deux colonnes. Elle défile si les cartes dépassent l'écran (16 au plus).
- Une ligne d'état en bas : mode démo, connecté, injoignable, jeton refusé.

| Domaine | Carte | Au toucher |
|---|---|---|
| `light`, `switch`, `fan`, `input_boolean`, `automation` | bascule, Allumé / Éteint | `homeassistant.toggle` |
| `cover` | bascule, Ouvert / Fermé | `cover.toggle` |
| `sensor`, `binary_sensor`, autres | valeur et unité | rien |
| `scene`, `script` | lancement | `scene.turn_on`, `script.turn_on` |
| `button`, `input_button` | lancement | `button.press`, `input_button.press` |

Une entité qui n'existe pas dans Home Assistant affiche « Entité inconnue » en rouge.

Sans `HA_HOST`, l'appli tourne en **mode démo** avec des valeurs simulées. Pratique pour travailler l'interface sans Home Assistant.

Le client passe par l'API REST en HTTP simple sur le réseau local, avec un jeton longue durée. Il relève les états toutes les 3 secondes dans un thread à part, donc l'interface ne se fige jamais sur le réseau. Une requête perdue est retentée une fois, et « injoignable » ne s'affiche qu'après 3 relevés ratés d'affilée.

## Réglages

```sh
cp app/panel.env.example app/panel.env
```

Puis remplir `HA_HOST`, `HA_TOKEN` et `HA_CARDS`. `HA_HOST` est l'adresse locale de Home Assistant, sans `http://` : l'appli ne parle pas HTTPS. Le jeton se crée dans Home Assistant, Profil, Sécurité, Jetons d'accès longue durée. `app/panel.env` reste hors de git.

## Développer sur le PC

```sh
sudo apt install -y build-essential cmake libsdl2-dev   # une fois
scripts/app-sim.sh
```

## Compiler pour l'iPad

La compilation se fait dans un conteneur Alpine ARMv7 émulé par QEMU, et produit un binaire statique : il ne dépend d'aucune bibliothèque de l'image de l'iPad.

```sh
sudo apt install -y qemu-user-binfmt   # une fois (qemu-user-static avant Ubuntu 26.04)
docker run --rm --platform linux/arm/v7 alpine:3.22 uname -m   # doit afficher armv7l
scripts/app-build-ipad.sh
```

Si la vérification affiche `exec format error`, Docker ne voit pas l'émulation ARM. L'activer avec `docker run --privileged --rm tonistiigi/binfmt --install arm`.

La première fois, LVGL se compile sous émulation : compter plusieurs minutes.

## Lancer sur l'iPad

L'iPad est sous Linux et joignable par `./cascadia a5 ssh`.

```sh
scripts/app-deploy.sh          # au premier plan, Ctrl+C pour quitter
scripts/app-deploy.sh --bg     # en arrière-plan
scripts/app-deploy.sh --stop   # arrêter
```

Modifier `panel.env` ne demande pas de recompiler : `scripts/app-deploy.sh --bg` le renvoie et relance l'appli. Le script arrête aussi toute instance précédente, et l'appli refuse de démarrer si une autre tourne déjà.

Le script arrête le clavier à l'écran (`fbkeyboard`), qui dessine dans le même framebuffer. L'appli met la console en mode graphique pendant qu'elle tourne et la rend en quittant.

## Limites actuelles

- **Polices** : Montserrat en Latin-1 (accents français, €, guillemets) aux tailles 16, 20, 28 et 48, générées dans `app/src/fonts/` par `scripts/gen-fonts.sh`. Un caractère hors de ce jeu (emoji, alphabets non latins) ne s'affiche pas.
- **Rien ne survit au redémarrage de l'iPad** : l'appli et `panel.env` sont en RAM. Les intégrer à l'image passera par le dossier `keep/` de Cascadia.
- **Rendu logiciel** sur le framebuffer. Le GPU pourra venir ensuite, via SDL ou Wayland.

## Vérifié

- Compilation de la cible `fbdev` en x86_64, statique.
- Rendu hors écran, en mode démo et contre un faux serveur Home Assistant : noms accentués, nom imposé, volet, entité inconnue.
- Appuis : le bon service est appelé selon le domaine, un appui sur un capteur ne fait rien.
- Sur l'iPad, par l'utilisateur : compilation ARM, affichage, tactile, bascule d'une lumière.
