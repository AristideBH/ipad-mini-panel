# ipad-mini-panel : contexte pour Claude Code

Linux (Project Cascadia) sur un iPad mini 1 (A1432), pour en faire un panneau mural Home Assistant avec une appli LVGL. L'utilisateur parle français : répondre en français.

## Machines

- **Hôte** : ce PC, Ubuntu 26.04. Compile, démarre l'iPad, déploie l'appli.
- **iPad sous Linux** : `root@10.55.0.2` par le câble USB. Alpine en RAM, rien n'est écrit sur la mémoire de l'iPad.
- **iPad sous iOS 9.3.5** : jailbreaké (Carbon + iocaste), OpenSSH, mot de passe root `alpine`.

## Arborescence

- `cascadia/` : clone de Project Cascadia (hors git). Commandes : `./cascadia a5` les liste.
- `firmware/` : IPSW 8.4.1 (hors git), lié depuis `cascadia/`.
- `app/` : appli LVGL v9.2.2, cibles `sim` (SDL) et `fbdev` (iPad).
- `scripts/` : `app-sim.sh`, `app-build-ipad.sh`, `app-deploy.sh`, `gen-fonts.sh`, `check-ipsw.sh`.
- `docs/` : `APP.md`, `JOURNAL.md` (à tenir à jour), `KNOWN-ISSUES.md`.

## Cycle courant

```sh
scripts/app-sim.sh                 # interface sur le PC
scripts/app-build-ipad.sh          # binaire ARMv7 statique
scripts/app-deploy.sh --bg         # envoi et lancement sur l'iPad
ssh root@10.55.0.2 cat /root/panel.log
```

Redémarrer l'iPad sous Linux, quand il est revenu sous iOS : `cd cascadia && ./cascadia a5 flash --kdfu`.

## Ce que seul l'utilisateur peut faire

- Maintenir marche + accueil pour revenir sous iOS (après un crash, un kDFU raté, ou avant `mtcal` et `flash`).
- Regarder l'écran et toucher la dalle. Lui demander ce qu'il voit plutôt que supposer.

## Règles

- Ne jamais écrire sur la NAND de l'iPad, ne rien installer sous iOS sans demander.
- Ne jamais committer l'IPSW, la chaîne de boot, les firmwares, `mtcal.bin`, `app/panel.env` ni le jeton Home Assistant.
- `umask 022` obligatoire avant `./cascadia a5 build` (voir `docs/KNOWN-ISSUES.md`).
- Noter chaque session dans `docs/JOURNAL.md`.
