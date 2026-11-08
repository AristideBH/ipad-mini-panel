# Limites connues

## Côté Cascadia

- **Démarrage tethered.** Chaque coupure ramène sur iOS. L'hôte doit relancer Linux.
- **Pas de stockage interne.** Linux ne peut pas encore écrire sur la mémoire de l'iPad. En RAM, seul ce qui est intégré à l'image survit à un redémarrage. Le NFS sur l'hôte sert pendant le développement.
- **`reboot` éteint** au lieu de redémarrer.
- **512 Mo de RAM.** L'interface doit rester légère.

## Rencontré sur Ubuntu

- **SSH refusé, « Permission denied (publickey) ».** Ubuntu crée les fichiers avec un umask `0002` : l'overlay de Cascadia sort avec `/root` en `drwxrwxr-x`, et dropbear refuse toute clé si le dossier personnel est modifiable par le groupe. Correction : `umask 022` dans `~/.bashrc`, puis `./cascadia a5 build` et un nouveau boot. À signaler chez Cascadia.
- **`newgrp docker`** ne vaut que pour le terminal courant. Préférer un redémarrage après l'ajout au groupe `docker`.

## Retours d'un autre utilisateur

Un iPad mini 1 tourne déjà en écran de statut permanent, en iOS 9.3.5, kDFU et RAM ([issue #1](https://github.com/rzxvx/Project-Cascadia/issues/1)). Il est jugé stable, avec trois réserves :

- Débrancher puis rebrancher le câble pendant que Linux tourne coupe la liaison USB. Non résolu. Un redémarrage par le watchdog est à l'essai.
- kloader depuis iOS 9.3.5 a atteint le DFU 3 fois sur 6. En cas d'échec, l'écran reste noir et il faut marche + accueil.
- `--skip-pwn` après un kDFU fait à la main envoie l'iBEC chiffré par défaut, et l'iPad reste noir. Il faut l'image `.plain`.

## À vérifier

- Cascadia et Legacy iOS Kit sur un Pi Zero 2 W comme hôte.
- La fiabilité du kDFU sur cet iPad, sur une dizaine d'essais.
- LVGL accéléré par le GPU sans X (via Wayland ou SDL), après une première version sur le framebuffer.
