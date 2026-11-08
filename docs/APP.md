# Application LVGL

Un seul code, deux cibles :

| Cible | Où | Affichage | Entrée |
|---|---|---|---|
| `sim` | le PC | fenêtre SDL 768 × 1024 | souris |
| `fbdev` | l'iPad sous Linux | `/dev/fb0` | tactile `/dev/input/event0` |

LVGL est épinglé en **v9.2.2** et téléchargé à la compilation. La configuration est dans `app/lv_conf.h`.

## Ce que fait le squelette

- Heure et date en haut.
- Une carte **lumière** : un appui bascule l'entité dans Home Assistant.
- Une carte **capteur** : valeur et unité.
- Une ligne d'état en bas : mode démo, Home Assistant joignable ou non.

Sans `HA_HOST`, l'appli tourne en **mode démo** avec des valeurs simulées. Pratique pour travailler l'interface sans Home Assistant.

Le client Home Assistant passe par l'API REST en HTTP simple sur le réseau local, avec un jeton longue durée. Il relève les états toutes les 3 secondes dans un thread à part, donc l'interface ne se fige jamais sur le réseau.

## Réglages

```sh
cp app/panel.env.example app/panel.env
```

Puis remplir `HA_HOST`, `HA_TOKEN`, `HA_LIGHT` et `HA_SENSOR`. Le jeton se crée dans Home Assistant, Profil, Sécurité, Jetons d'accès longue durée. `app/panel.env` reste hors de git.

## Développer sur le PC

```sh
sudo apt install -y build-essential cmake libsdl2-dev   # une fois
scripts/app-sim.sh
```

## Compiler pour l'iPad

La compilation se fait dans un conteneur Alpine ARMv7 émulé par QEMU, et produit un binaire statique : il ne dépend d'aucune bibliothèque de l'image de l'iPad.

```sh
sudo apt install -y qemu-user-static   # une fois
scripts/app-build-ipad.sh
```

La première fois, LVGL se compile sous émulation : compter plusieurs minutes.

## Lancer sur l'iPad

L'iPad est sous Linux et joignable par `./cascadia a5 ssh`.

```sh
scripts/app-deploy.sh          # au premier plan, Ctrl+C pour quitter
scripts/app-deploy.sh --bg     # en arrière-plan
scripts/app-deploy.sh --stop   # arrêter
```

Le script arrête le clavier à l'écran (`fbkeyboard`), qui dessine dans le même framebuffer. L'appli met la console en mode graphique pendant qu'elle tourne et la rend en quittant.

## Limites actuelles

- **Pas d'accents à l'écran.** Les polices intégrées de LVGL s'arrêtent à l'ASCII, plus le signe degré. Prochaine étape : générer une police Latin-1 avec `lv_font_conv`.
- **Rien ne survit au redémarrage de l'iPad** : l'appli et `panel.env` sont en RAM. Les intégrer à l'image passera par le dossier `keep/` de Cascadia.
- **Rendu logiciel** sur le framebuffer. Le GPU pourra venir ensuite, via SDL ou Wayland.

## Vérifié

- Compilation de la cible `fbdev` en x86_64, statique.
- Rendu hors écran de l'interface, en mode démo et contre un faux serveur Home Assistant (lecture des états, du nom, de l'unité, jeton envoyé).

Pas encore vérifié : le simulateur SDL, la compilation ARM et le tactile sur l'iPad.
