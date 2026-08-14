# Feuille de route

## Fondations à consolider

- Ajouter une page d'état affichant version du jeu, statut de chaque wrapper et
  codes d'erreur du dézoom.
- Ajouter sauvegarde de sécurité, aperçu des changements et bouton d'annulation
  avant toute édition persistante.
- Créer une suite de smoke tests reproductibles et une matrice de tests en jeu.

## Éditeurs de données

- Fenêtre complète du groupe et des boîtes : création/suppression, espèce,
  niveau, sexe, shiny, nature, attaques, PP, HP, IV et EV.
- Inventaire complet par poche, ajout/suppression d'objets et quantités.
- Sexe du personnage, temps de jeu, badges et Pokédex complet
  (vu/capturé, mâle/femelle, shiny).
- Déblocage des destinations de vol et favoris de téléportation nommés.

## Exploration

- Accélération du jeu uniquement pendant le maintien d'une touche.
- Téléportation vers un point mémorisé avec contrôle de carte et coordonnées.
- Choix du Pokémon sauvage et de son niveau.
- Éclosion instantanée et CS effaçables.

## Combat et capture

- KO en un coup et multiplicateur de dégâts, limités explicitement au camp voulu.
- Taux de capture à 100 % et étude séparée de la capture d'un Pokémon adverse.
- Multiplicateur de taux shiny avec affichage de la probabilité effective.
- Objets consommables infinis (Poké Balls, soins, CT, baies) via les méthodes
  centrales de retrait, sans balayage mémoire.

## Ordre conseillé

1. Étendre les tests automatisés et les sauvegardes de sécurité autour du
   répartiteur RGSS central désormais en place.
2. Inventaire et groupe complets en lecture seule.
3. Édition transactionnelle avec validation et annulation.
4. Téléportation/vol et options d'exploration.
5. Hooks de combat/capture, testés séparément en simple et double combat.

Les fonctions de combat et de sauvegarde sont volontairement placées après les
fondations : une erreur y est plus susceptible de corrompre une partie ou de
casser une mécanique scénarisée.
