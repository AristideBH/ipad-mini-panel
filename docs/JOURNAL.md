# Journal des essais

Une entrée par session. Noter le commit de Cascadia, l'hôte, ce qui a marché et ce qui a coincé.

## 2026-11-08

- iPad : iOS 9.3.5, jailbreak Carbon réussi après quelques essais (page qui se recharge).
- Untether iocaste installé depuis `https://lukezgd.github.io/repo`, jailbreak conservé après redémarrage.
- OpenSSH installé, connexion depuis Windows avec `HostKeyAlgorithms +ssh-rsa`.
- Hôte de la soirée : Mac Apple Silicon, Docker Desktop installé.
- Commit Cascadia : _à remplir_
- Premier boot Linux : _à remplir_
- Essais kDFU réussis / tentés : _à remplir_

## 2026-11-09
- Hôte : Ubuntu 26.04, Cascadia 705fd31, noyau précompilé a5-2026.11.04-e06bdc3
- Boot en RAM sans bureau, par kDFU : OK
- SSH refusé au début : umask 0002 d'Ubuntu, /root en 775 dans l'overlay, dropbear refuse.
  Correction : `umask 022` dans ~/.bashrc, puis `./cascadia a5 build`.
- Tactile, 2 cœurs, Wi-Fi (192.168.68.96) : OK. RAM libre : 406 Mo.
