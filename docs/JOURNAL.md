# Journal des essais

Une entrée par session. Noter le commit de Cascadia, l'hôte, ce qui a marché et ce qui a coincé.

## 2026-11-08

- iPad : iOS 9.3.5, jailbreak Carbon réussi après quelques essais (page qui se recharge).
- Untether iocaste installé depuis `https://lukezgd.github.io/repo`, jailbreak conservé après redémarrage.
- OpenSSH installé, connexion depuis Windows avec `HostKeyAlgorithms +ssh-rsa`.
- Hôte de la soirée : Mac Apple Silicon, Docker Desktop installé.
- Hôte finalement : Ubuntu 26.04 en dual boot (PC MSI, Ubuntu à placer en tête dans « UEFI Hard Disk Drive BBS Priorities »).

## 2026-11-09
- Hôte : Ubuntu 26.04, Cascadia 705fd31, noyau précompilé a5-2026.11.04-e06bdc3
- Boot en RAM sans bureau, par kDFU : OK
- SSH refusé au début : umask 0002 d'Ubuntu, /root en 775 dans l'overlay, dropbear refuse.
  Correction : `umask 022` dans ~/.bashrc, puis `./cascadia a5 build`.
- Tactile, 2 cœurs, Wi-Fi (192.168.68.96) : OK. RAM libre : 406 Mo.
- Calibration du tactile : `./cascadia a5 mtcal`.
- Squelette de l'appli LVGL (v9.2.2) : vérifié en rendu hors écran, pas encore sur l'iPad.
- Essais kDFU réussis / tentés : _à remplir_
- Page de test et transition glissée (carte `panel.test`) essayées sur l'iPad :
  - `build-ipad/` contenait un `.o` à 0 octet (`lv_draw_arc.c.o`, build précédent interrompu) :
    CMake ne l'a pas recompilé et l'édition de liens a échoué (`undefined reference to lv_draw_arc`).
    Correction : supprimer tout `build-ipad/` (il appartenait à root, créé par le conteneur : `docker run ... rm -rf build-ipad`) puis recompiler à neuf.
  - Après ce correctif, l'appli s'est bloquée en silence après plusieurs allers-retours sur la page de test (boutons et écran figés, rien dans `panel.log` ni dans `dmesg`) :
    cause trouvée : `lv_sysmon_show_performance()`/`hide_performance()` ne sont pas prévues pour être rappelées à chaque passage — chaque appel recrée étiquette, minuteur et rappel d'affichage sans jamais détruire les précédents (fuite). Corrigé en créant l'étiquette une seule fois et en basculant seulement son indicateur masqué.
  - Le glissement du doigt pour revenir ne déclenchait jamais le retour : dans LVGL v9.2.2, un objet avec un parent hérite par défaut du bit « le geste remonte » (`LV_OBJ_FLAG_GESTURE_BUBBLE`), donc le geste détecté sur `page_test` remontait jusqu'à l'écran (qui n'a pas de gestionnaire) sans jamais atteindre le callback posé sur la page. Corrigé en retirant ce bit sur `page_test`.
  - Entrelacement visible pendant le glissement : le pilote `/dev/fb0` de LVGL écrit directement dans la mémoire affichée, sans tampon matériel double ni attente du balayage ; avec un tampon partiel de 60 lignes (`LV_LINUX_FBDEV_BUFFER_SIZE`), une zone pleine hauteur comme le glissement se recopiait en ~17 écritures séparées, chacune pouvant être coupée par le balayage. Passé à un tampon pleine hauteur (1024 lignes, ~3 Mo) : une seule écriture par zone modifiée, entrelacement quasi disparu.
  - Détecteur de signal fatal ajouté dans `main.c` (SIGSEGV/SIGABRT/SIGBUS/SIGFPE/SIGILL consignés dans `panel.log` avant de sortir), au cas où un autre blocage silencieux se reproduise.
  - Après ces trois correctifs : glissement fonctionnel, entrelacement quasi disparu, aucun blocage après de nombreux allers-retours.
