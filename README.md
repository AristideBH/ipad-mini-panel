# ipad-mini-panel

Linux sur un iPad mini 1 (A1432, Wi-Fi) avec [Project Cascadia](https://github.com/rzxvx/Project-Cascadia), puis une interface LVGL pour en faire un panneau mural relié à Home Assistant.

iOS reste installé et intact. Linux démarre en RAM depuis un iOS 9.3.5 jailbreaké, par la voie kDFU, sans matériel en plus. Une coupure ramène sur iOS, et l'hôte relance Linux.

## Où en est le projet

| Étape | État |
|---|---|
| Jailbreak permanent (Carbon + iocaste) | fait |
| OpenSSH sur l'iPad | fait |
| Hôte Ubuntu 26.04 en dual boot | fait |
| Premier boot de Linux (kDFU, RAM, sans bureau) | fait : SSH, tactile, Wi-Fi, 2 cœurs |
| Application LVGL | squelette, voir [docs/APP.md](docs/APP.md) |
| Image Alpine avec démarrage auto de l'appli | à faire |
| Pi Zero 2 W comme hôte permanent | à évaluer |

## Comment ça démarre

```
iPad sous iOS 9.3.5 (jailbreak permanent)
        │  l'hôte lance kloader par SSH → kDFU
        ▼
chaîne de boot patchée (iBSS → iBEC → bundle → loader)
        ▼
Linux 6.12 + Alpine, en RAM
        ▼
appli LVGL sur /dev/fb0, tactile sur /dev/input/event0, Wi-Fi vers Home Assistant
```

## Structure

```
ipad-mini-panel/
├── cascadia/            Project Cascadia, cloné ici (hors git, voir plus bas)
├── firmware/            IPSW et fichiers tirés de l'iPad (hors git)
├── host/
│   ├── macos/           mise en place du Mac, config SSH
│   ├── ubuntu/          mise en place de l'hôte Ubuntu
│   └── pizero/          relance automatique du boot, plus tard
├── jailbreak/           ce qui a été fait sur l'iPad et comment le refaire
├── image/               ajouts à l'image Alpine : paquets, services, Wi-Fi
├── app/                 application LVGL
│   ├── src/             code de l'interface
│   ├── sim/             simulateur SDL pour le PC
│   └── target/          build pour l'iPad
├── scripts/             vérifications et commandes du quotidien
└── docs/                procédures et journal des essais
```

## Ce qui reste hors de git

L'IPSW, la chaîne de boot générée, les firmwares, la calibration du tactile, l'adresse Wi-Fi de l'iPad et tous les produits de build. Le `.gitignore` les exclut. Apple ne permet pas de redistribuer ses fichiers, et Cascadia suit la même règle.

Cascadia est cloné dans `cascadia/` sans être suivi. Une fois le premier boot validé, on le passera en sous-module épinglé sur le commit qui marche.

## Documents

- [docs/MAC-QUICKSTART.md](docs/MAC-QUICKSTART.md) : premier boot, écrit pour un Mac, valable sous Ubuntu à quelques détails près
- [docs/APP.md](docs/APP.md) : l'application LVGL, simulateur et déploiement
- [jailbreak/README.md](jailbreak/README.md) : le jailbreak de l'iPad, étape par étape
- [docs/JOURNAL.md](docs/JOURNAL.md) : journal des essais
- [docs/KNOWN-ISSUES.md](docs/KNOWN-ISSUES.md) : limites et retours d'autres utilisateurs

## Sources

- [Project Cascadia](https://github.com/rzxvx/Project-Cascadia)
- [Legacy iOS Kit](https://github.com/LukeZGD/Legacy-iOS-Kit)
- [iocaste](https://github.com/staturnzz/iocaste)
- [Guide Carbon, ios.cfw.guide](https://ios.cfw.guide/using-carbon/)
