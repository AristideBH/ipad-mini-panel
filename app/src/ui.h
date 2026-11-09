#ifndef UI_H
#define UI_H

#include <stdbool.h>

/* Construit l'écran principal et lance son rafraîchissement. */
void ui_create(void);

/* Bouton accueil : remonte la grille en haut. */
void ui_home(void);

/* Mode nuit : assombrit tout l'écran (la luminosité est réglée à part). */
void ui_set_night(bool on);

/* Mode édition de la grille principale (déplacer/redimensionner les
 * cartes) : bascule, déclenché aussi par un appui long sur Accueil. */
void ui_toggle_edit(void);

#endif
