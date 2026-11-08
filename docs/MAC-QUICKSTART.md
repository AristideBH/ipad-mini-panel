# Premier boot depuis un Mac Apple Silicon

Objectif de la soirée : compiler Cascadia et obtenir un shell Linux sur l'iPad. Rien n'est écrit sur l'iPad, un appui long sur marche et accueil ramène toujours sous iOS.

Le Mac sur puce Apple est l'hôte le mieux testé par Cascadia. Tout ce qui est fait ici se refera à l'identique sous Ubuntu, puisque la compilation passe par Docker.

> Le dépôt Cascadia a été réorganisé le 4 novembre 2026 (arborescence `devices/a5`). Les commandes ci-dessous suivent cette version, mais `./cascadia a5` reste la référence : il affiche la liste exacte des sous-commandes. En cas de doute, c'est lui qui a raison.

## Avant de commencer

- iPad sous iOS 9.3.5, jailbreak permanent, OpenSSH installé ([jailbreak/README.md](../jailbreak/README.md))
- iPad chargé, verrouillage automatique sur **Jamais** pendant les essais
- Câble Lightning de bonne qualité, branché directement sur le Mac (pas de hub)
- Environ 20 Go libres sur le Mac

## 1. Installer les outils

### Docker Desktop

Installé. Il doit être lancé et tourner pendant toute la compilation.

```sh
docker info >/dev/null && echo "Docker OK"
```

### Outils en ligne de commande

git, Python 3 et rsync sont souvent déjà là. Sinon, les outils Xcode les fournissent :

```sh
xcode-select --install
```

### Legacy iOS Kit

Il fournit `kloader`, `primepwn` et `irecovery`, que Cascadia appelle.

1. Télécharger l'archive **macOS** sur la [page des releases](https://github.com/LukeZGD/Legacy-iOS-Kit/releases/latest).
2. La décompresser, par exemple dans `~/Tools/Legacy-iOS-Kit`.
3. Si macOS bloque le lancement (« développeur non identifié ») :

```sh
xattr -dr com.apple.quarantine ~/Tools/Legacy-iOS-Kit
```

### Clé SSH ed25519

Linux sur l'iPad n'accepte que la connexion par clé. Cascadia intègre ta clé publique dans l'image au moment de la compilation.

```sh
ls ~/.ssh/id_ed25519.pub || ssh-keygen -t ed25519
```

## 2. Récupérer l'IPSW

Il ne sert qu'à fabriquer la chaîne de boot. L'iPad reste en 9.3.5.

- Fichier : `iPad2,5_8.4.1_12H321_Restore.ipsw`, 1,41 Go
- Lien : [ipswdl.com](https://ipswdl.com/ipsw/iPad2,5/12H321) (le fichier vient des serveurs d'Apple)

Le ranger dans `firmware/`, puis vérifier qu'il est intact :

```sh
./scripts/check-ipsw.sh firmware/iPad2,5_8.4.1_12H321_Restore.ipsw
```

L'empreinte attendue est `365d4a8e9ea9613c6bdb6db35cba0331580f7ae7` (SHA-1). Cascadia refuse un autre fichier, car un correctif vise une adresse précise dans l'iBEC de cette version.

## 3. Cloner Cascadia

Depuis la racine de ce dépôt :

```sh
git clone https://github.com/rzxvx/Project-Cascadia.git cascadia
cd cascadia
./cascadia a5
```

Noter dans le [journal](JOURNAL.md) le commit utilisé :

```sh
git rev-parse --short HEAD
```

## 4. Vérifier la machine

```sh
./cascadia a5 doctor
```

Il liste ce qui manque. Le corriger avant d'aller plus loin. Docker doit être joignable.

## 5. Récupérer les données propres à l'iPad

Deux fichiers ne sortent pas de l'IPSW et viennent de ton iPad.

### Calibration du tactile

Sans elle, Linux démarre mais le tactile ne répond pas. Elle se lit depuis iOS jailbreaké avec l'outil `mtdump` de Cascadia, par USB via `iproxy` (fourni par usbmuxd). La procédure exacte est dans `devices/a5/docs/CASCADIA-CHEATSHEET.md`. Le fichier obtenu va dans :

```
devices/a5/build/keep/lib/firmware/mtcal.bin
```

Si `./cascadia a5 install` propose de s'en charger, le laisser faire.

### Adresse Wi-Fi

Réglages, Général, Informations, **Adresse Wi-Fi**. Sans elle, Cascadia utilise une adresse générique, ce qui marche aussi. Avec elle, le routeur reconnaît l'iPad. Elle se passe à la compilation par la variable `WIFI_MAC`.

## 6. Lancer l'installation

```sh
./cascadia a5 install
```

Elle déroule les étapes une à une, et chacune peut aussi se lancer seule :

| Étape | Rôle |
|---|---|
| `kernel` | récupère Linux, épinglé sur la v6.12 |
| `rootfs` | construit l'Alpine, avec dropbear et ta clé SSH |
| `firmware` | extrait et patche iBSS et iBEC depuis l'IPSW |
| `build` | assemble le noyau, le device tree et le bundle de boot |
| `flash` | met l'iPad en DFU et envoie la chaîne de boot |

La première compilation prend du temps, surtout le noyau.

## 7. Démarrer l'iPad sous Linux

L'iPad est branché en USB, allumé sous iOS, jailbreak actif. On utilise la voie kDFU :

```sh
./cascadia a5 flash --kdfu
```

`kloader` passe l'iPad en DFU depuis iOS, puis l'hôte envoie la chaîne de boot. Après le flash, le Mac prend l'adresse `10.55.0.1` sur l'interface USB et attend que l'iPad réponde.

**Résultat attendu :** une console Linux sur l'écran de l'iPad, puis :

```sh
ssh root@10.55.0.2
```

Si `ssh` reste bloqué sans erreur, l'interface réseau USB n'est pas configurée. Relancer l'étape réseau seule :

```sh
./cascadia a5 link
```

## 8. Vérifier le matériel

Depuis le shell sur l'iPad :

```sh
cat /proc/cpuinfo | grep processor     # deux cœurs attendus
ls /dev/fb0 /dev/input/event0          # écran et tactile
wifi connect "NOM_DU_RESEAU" "MOT_DE_PASSE"
ip addr show wlan0
```

Toucher l'écran pour voir si la console réagit.

## 9. Revenir à iOS

Maintenir **marche + accueil** jusqu'au redémarrage. Ou `poweroff` sur l'iPad, puis le bouton marche. Attention, `reboot` éteint l'iPad au lieu de le redémarrer.

## Si ça coince

| Symptôme | Cause probable | Que faire |
|---|---|---|
| L'iPad reste noir après le kDFU | kloader a échoué (environ une fois sur deux chez un autre utilisateur) | marche + accueil, retour sous iOS, relancer |
| Envoi à 100 %, puis écran noir | image iBEC chiffrée envoyée après un kDFU | laisser `--kdfu` choisir l'image, ne pas forcer `--skip-pwn` sans l'image `.plain` |
| `ssh` bloqué sans message | lien réseau USB absent | `./cascadia a5 link` |
| Le tactile ne répond pas | calibration absente | refaire l'étape 5, recompiler |
| Câble débranché pendant que Linux tourne, plus de réseau | bug connu, non résolu | retour sous iOS et nouveau boot |
| Docker introuvable | Docker Desktop fermé | le lancer, relancer la commande |

## Après ce soir

Noter dans le [journal](JOURNAL.md) le commit de Cascadia, ce qui a marché, et combien d'essais le kDFU a demandés. Ce dernier chiffre décidera si le Pico vaut la peine d'être acheté.
