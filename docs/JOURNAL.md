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
- Bascule optimiste (lumières/prises) vraiment instantanée : l'écran n'attendait le minuteur à 500 ms pour afficher le retour déjà basculé dans les données, et le relevé HA déclenché juste après l'appui écrasait ce retour avec l'ancien état avant que le filet de sécurité (`press_mask`) ne le protège — il était effacé avant ce relevé-là au lieu d'après. Corrigé : rafraîchissement immédiat après l'appui, `press_mask` effacé après le relevé qui suit directement.
- Recherche puis construction d'une Phase 1 pour des cartes configurables sur l'iPad (ordre, icône, masquage ; pas d'ajout d'entité ni de nom personnalisé pour l'instant) :
  - Persistance choisie : un petit service HTTP sur l'hôte (`scripts/config-server.sh`, Python 3 seul), plutôt qu'une racine NFS Cascadia (existe pour `a5` mais change le mode de boot, coût CPU mesuré plus élevé que la RAM) ou un module HA (mélangerait l'état d'affichage du panneau avec l'installation domotique réelle). Tourne sur l'hôte tethered (dev : le PC ; à terme, la petite carte prévue dans `KNOWN-ISSUES.md`), déjà une dépendance du projet.
  - Nouveau module `app/src/tiles.c`/`tiles.h` : client HTTP vers ce service, petit analyseur JSON pour `{"tiles":[...]}`, table de clés sémantiques d'icônes (`ampoule`, `prise`…) vers les symboles LVGL.
  - `ha.c` superpose ordre/icône/masquage lus au démarrage sur les cartes de `HA_CARDS`, et sauvegarde en arrière-plan (thread existant) à chaque édition.
  - Nouvelle page Réglages dans `ui.c` (même mécanisme de transition que la page de test, généralisé à une troisième page) : icône (touche pour changer), flèches haut/bas, œil masquer/afficher.
  - Bug trouvé en testant dans le simulateur : le tampon de requête HTTP de `tiles.c` (256 octets) contenait l'en-tête *et* le corps JSON complet — trop petit dès que la liste de cartes dépasse quelques entrées, `POST /tiles` échouait en silence alors que `GET /tiles` (corps vide) marchait. Corrigé en écrivant l'en-tête et le corps séparément.
  - Validé de bout en bout dans le simulateur (pas sur l'iPad cette fois) : édition → sauvegarde → relecture après redémarrage de l'appli, fidèle.
- Phase 2 : ajouter/retirer des entités depuis l'appli (toujours testé en simulateur, pas sur l'iPad) :
  - `ha_browse()` interroge HA via `/api/template` (une requête par domaine actionnable, gabarit à une seule boucle par appel pour éviter les virgules entre boucles imbriquées, une entité par ligne en sortie plutôt qu'un tableau JSON). Testé contre une installation réelle avec un jeton partagé temporairement par l'utilisateur, jamais commité : 436 entités au total, dont 316 capteurs à eux seuls (exclus du navigateur, voir `APP.md`).
  - Tourne dans le thread existant, pas sur l'appelant : corrigé au passage un bug où le réveil par signal ne regardait que `press_mask`, pas `tiles_dirty`, ce qui retardait une sauvegarde jusqu'à `POLL_MS` (3 s) au lieu d'être immédiate.
  - Plantage trouvé en testant (grâce au détecteur de signal ajouté plus tôt : `panel: signal fatal 11`) : `lv_label_set_text()` appelé sur le bouton « Ajouter » lui-même plutôt que sur l'étiquette à l'intérieur — confusion de type d'objet LVGL, corruption mémoire. Corrigé en gardant un pointeur séparé vers l'étiquette.
  - Validé de bout en bout contre la vraie installation : navigateur, ajout, retrait, persistance.
- Indicateur i/s-CPU de LVGL désactivé (pour l'instant) : `lv_display_create()` l'affichait tout seul dès que `LV_USE_PERF_MONITOR` est à 1 dans `lv_conf.h`, indépendamment de l'appel applicatif déjà retiré — la bascule manuelle de l'étiquette ne servait donc à rien, le réglage est la vraie commande.
- Premier essai des cartes configurables sur l'iPad réel (après le câble, Wi-Fi non nécessaire : le service de config écoute sur l'adresse de l'hôte vue depuis l'iPad, 10.55.0.1) :
  - Glissement, Réglages, Ajouter (contre la vraie installation HA), retrait : tout fonctionne.
  - En testant la persistance (relancer l'appli sans toucher à HA_CARDS) : une carte ajoutée depuis la page Réglages ne revenait pas. Cause : `apply_tile_prefs()` ne faisait que réordonner/masquer les cartes déjà présentes dans HA_CARDS, sans jamais reconstruire celles du fichier de préférences qui n'y figurent pas. Corrigé en reconstruisant directement ces cartes-là (même logique que `ha_tile_add()`).
  - Revalidé sur l'iPad après correctif : la carte ajoutée revient bien après redémarrage de l'appli.
