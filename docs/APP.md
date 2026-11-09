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
| `panel` | page de l'appli, sans Home Assistant | ouvre la page (`panel.test` : page de test) |

Une entité qui n'existe pas dans Home Assistant affiche « Entité inconnue » en rouge.

Sans `HA_HOST`, l'appli tourne en **mode démo** avec des valeurs simulées. Pratique pour travailler l'interface sans Home Assistant.

Le client passe par l'API REST en HTTP simple sur le réseau local, avec un jeton longue durée. Il relève les états toutes les 3 secondes dans un thread à part, donc l'interface ne se fige jamais sur le réseau. Une requête perdue est retentée une fois, et « injoignable » ne s'affiche qu'après 3 relevés ratés d'affilée.

## Page de test et transitions

`panel.test=Test animation` dans `HA_CARDS` ajoute une carte qui ouvre une page de test. On en revient par le bouton Accueil de l'iPad, le bouton Retour, ou un glissement vers la droite.

Le passage d'une page à l'autre glisse en 340 ms, avec une parallaxe et une courbe proche de celle d'iOS, et vise 60 images/s (`LV_DEF_REFR_PERIOD` à 16 ms). Pour tenir ce rythme sur l'A5 sans GPU, chaque page est photographiée une fois au départ (`lv_snapshot`, en XRGB8888), puis seules les deux photos se déplacent. LVGL recopie une image XRGB8888 opaque par `memcpy` ligne à ligne : pendant le mouvement, aucune carte ni aucun texte n'est redessiné.

La page de test affiche le compteur de LVGL (images/s et charge du processeur) et le résultat de la dernière transition. Chaque transition est aussi notée dans `panel.log` :

```
ui: transition aller, 21 images en 351 ms, soit 59 i/s
```

Mesuré hors écran sur un PC : 57 à 59 i/s. Le chiffre sur l'iPad reste à relever.

## Boutons de l'iPad

L'appli reprend les boutons à `p105-keys`, le programme de Cascadia qui les gère d'habitude. Tous arrivent sur le périphérique `gpio-keys` (`/dev/input/event1`).

| Bouton | Action |
|---|---|
| Volume + / − | luminosité en 32 pas, maintenir pour continuer |
| Marche | éteint ou rallume l'écran ; éteint, le tactile est ignoré |
| Accueil | rallume l'écran ; sur une sous-page, revient à l'accueil ; sinon remonte la grille |
| Interrupteur latéral | mode nuit en position « muet » (orange visible) : luminosité au minimum, écran voilé ; l'autre position rend la luminosité de jour |

`scripts/app-deploy.sh` arrête `p105-keys` avant de lancer l'appli, et `--stop` le relance en rallumant l'écran. Si `p105-keys` tourne encore, l'appli ne peut pas prendre les boutons : elle l'écrit dans `panel.log` et continue sans eux. Dans le simulateur, les boutons n'existent pas.

## Page Réglages (disposition des cartes)

Un bouton engrenage en haut à droite de l'écran principal ouvre une page qui liste les cartes de `HA_CARDS` : icône (touche pour changer parmi un petit jeu fixe), flèches pour réordonner, œil pour masquer/afficher sur la grille principale. Pas d'ajout de nouvelle entité ici, ni de nom personnalisé : ça reste dans `HA_CARDS` et `panel.env`, côté hôte.

Chaque changement est sauvegardé dans `app/panel-tiles.json`, via un petit service HTTP sur l'hôte (`scripts/config-server.sh`, Python 3 seul, sans dépendance) — sans lui, l'appli garde simplement l'ordre et l'affichage de `HA_CARDS` tels quels, rien de plus :

```sh
scripts/config-server.sh                 # écoute sur :8787
CONFIG_HOST=10.55.0.1 CONFIG_PORT=8787 scripts/app-deploy.sh --bg   # sur l'iPad : .1, l'hôte du câble
```

Dans le simulateur sur le PC, les deux tournent en local : `CONFIG_HOST=127.0.0.1 scripts/app-sim.sh`.

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
- Boutons : logique testée avec de faux événements et un faux rétroéclairage (luminosité, écran éteint, accueil, mode nuit, arrêt). Pas encore sur l'iPad.
- Sur l'iPad, par l'utilisateur : compilation ARM, affichage, tactile, bascule d'une lumière.
