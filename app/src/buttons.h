/* Boutons de l'iPad (gpio-keys) : luminosité, écran éteint, accueil, mode nuit.
 *
 *   volume + / -          luminosité, en 32 pas ; maintenu, continue
 *   marche                écran éteint / rallumé ; éteint, le tactile est ignoré
 *   accueil               rallume l'écran, sinon ui_home()
 *   interrupteur latéral  mode nuit en position « muet » (orange visible)
 *
 * L'appli prend les boutons en exclusivité (EVIOCGRAB) : p105-keys, le
 * programme de Cascadia qui les gère d'habitude, doit être arrêté avant
 * (scripts/app-deploy.sh s'en charge). Sans effet dans le simulateur. */
#ifndef BUTTONS_H
#define BUTTONS_H

#include "lvgl.h"

/* touch : le périphérique tactile, désactivé pendant que l'écran est éteint. */
void buttons_start(lv_indev_t * touch);
/* Rallume l'écran et le rétroéclairage, à appeler en quittant. */
void buttons_stop(void);

#endif
