# Jailbreak de l'iPad

Fait le 8 novembre 2026 sur l'iPad mini A1432 en iOS 9.3.5. Le jailbreak survit aux redémarrages.

Le jailbreak sert à deux choses pour ce projet : lancer le kDFU depuis iOS, et lire la calibration du tactile.

## Ce qui a été utilisé

- **Carbon**, jailbreak lancé depuis Safari, sans ordinateur ni compte Apple. Couvre tous les appareils 32 bits de iOS 8.0 à 9.3.6.
- **iocaste untether**, qui rend le jailbreak permanent sur 9.3.5 et 9.3.6. Il n'est pas installé par Carbon et se trouve dans le dépôt Cydia de LukeZGD.
- **OpenSSH**, depuis Cydia.

## Refaire la procédure

1. iPad chargé, Wi-Fi actif, verrouillage automatique sur **Jamais**.
2. Dans Safari, ouvrir `http://carbon.sep.lol` (en http) et appuyer sur **Run**.
   - Si la page affiche « Un problème est survenu » et se recharge, fermer Safari complètement et réessayer. Plusieurs essais sont normaux.
3. Une fois Cydia sur l'écran d'accueil, **ne pas redémarrer**.
4. Dans Cydia, Sources, Modifier, Ajouter : `https://lukezgd.github.io/repo`.
5. Rechercher `iocaste untether`, installer, confirmer.
   - **Ne pas quitter Cydia** pendant l'installation, jusqu'à 5 minutes. L'interrompre bloque l'iPad au démarrage et oblige à le restaurer.
6. Accepter le redémarrage. Cydia doit toujours s'ouvrir après.
7. Installer `OpenSSH` depuis Cydia.

Dans la recherche Cydia, les autres paquets « untether » (Pangu, TaiG, evasi0n…) visent d'autres versions d'iOS. Ne pas les installer.

## Connexion SSH

L'iPad ne propose que des clés `ssh-rsa` et `ssh-dss`, désactivées par défaut dans les clients récents. Voir [host/macos/ssh_config.example](../host/macos/ssh_config.example).

Mot de passe par défaut : `alpine`. À changer avec `passwd` une fois le boot Linux validé. Tant qu'il reste `alpine`, n'importe qui sur le réseau local peut prendre la main sur l'iPad.

## Sources

- [Guide Carbon, ios.cfw.guide](https://ios.cfw.guide/using-carbon/)
- [iocaste](https://github.com/staturnzz/iocaste)
