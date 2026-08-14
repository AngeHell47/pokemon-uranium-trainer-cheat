# Feuille de route

## Fondations à consolider

- Ajouter une page d'état affichant version du jeu, statut de chaque wrapper et
  codes d'erreur du dézoom.
- Ajouter sauvegarde de sécurité, aperçu des changements et bouton d'annulation
  avant toute édition persistante.
- Créer une suite de smoke tests reproductibles et une matrice de tests en jeu.

## Éditeurs de données

- Les fenêtres complètes du groupe/boîtes et de l'inventaire par poche sont
  disponibles, avec création/suppression et commandes ciblées immuables.
- Ajouter un aperçu transactionnel global et une annulation multi-champs avant
  les éditions de masse.
- Sexe du personnage, temps de jeu, badges et Pokédex complet
  (vu/capturé, mâle/femelle, shiny).
- Déblocage des destinations de vol et favoris de téléportation nommés.

## Exploration

- Accélération du jeu uniquement pendant le maintien d'une touche.
- Téléportation vers un point mémorisé avec contrôle de carte et coordonnées.
- Choix du Pokémon sauvage et de son niveau.
- L'éclosion au prochain pas et les CS effaçables sont disponibles.

## Combat et capture

- KO en un coup et multiplicateur de dégâts, limités explicitement au camp voulu.
- Étudier séparément la capture d'un Pokémon adverse ; la capture sauvage à
  100 % est disponible sans contourner les refus de scénario.
- Multiplicateur de taux shiny avec affichage de la probabilité effective.
- Objets consommables infinis (Poké Balls, soins, CT, baies) via les méthodes
  centrales de retrait, sans balayage mémoire.

## Ordre conseillé

1. Étendre les tests automatisés et les sauvegardes de sécurité autour du
   répartiteur RGSS central désormais en place.
2. Étendre les tests réels de l'inventaire et du groupe/boîtes complets.
3. Ajouter prévisualisation transactionnelle et annulation multi-champs.
4. Téléportation/vol et options d'exploration.
5. Hooks de combat/capture, testés séparément en simple et double combat.

Les fonctions de combat et de sauvegarde sont volontairement placées après les
fondations : une erreur y est plus susceptible de corrompre une partie ou de
casser une mécanique scénarisée.
