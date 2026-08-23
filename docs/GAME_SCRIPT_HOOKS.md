# Points d'accroche des scripts du jeu

Ce document rassemble les observations faites sur les scripts Ruby extraits de
Pokémon Uranium 1.2/1.3 utilisés par cette installation. Il sert de base aux
prochaines fonctions du trainer ; ce n'est pas une promesse de compatibilité
avec une autre révision du jeu.

Tous les wrappers doivent continuer à être installés par le répartiteur
`Graphics.update`. Une fonction qui ouvre un écran synchrone doit masquer
l'overlay, vérifier que la scène courante l'autorise et accepter les appels
imbriqués à `Graphics.update` sans réentrer dans les callbacks du trainer.

## Fonctions maintenant confirmées

- Capture garantie : `PokeBattle_Battle#pbThrowPokeBall` consulte
  `BallHandlers.isUnconditional?` après les refus de cible, de Dresseur et de
  rareté nulle. Étendre ce prédicat donne un comportement de Master Ball sans
  recopier toute la méthode de capture.
- Éclosion : le gestionnaire `Events.onStepTaken` décrémente `eggsteps`, puis
  appelle `pbHatch`. Un getter virtuel à 1 conserve l'animation et ne modifie
  pas le compteur réel si l'option est désactivée avant le prochain pas.
- CS effaçables : le corps natif de `PokemonSummary#pbStartForgetScreen` est
  reposé en mémoire avec une condition supplémentaire autour de son unique
  refus. Le helper `pbIsHiddenMove?` est également réinstallé, sans affecter
  l'utilisation des CS sur la carte ni les objets HM. Les deux méthodes sont
  installées une fois après acquittement de `PokemonSummary`, puis uniquement
  mises à jour lors d'une bascule explicite de l'option. Le drapeau `$DEBUG`,
  que le jeu consulte dans ce refus précis, est également
  forcé pendant l'activation et remis à faux à la désactivation.

## Éditeurs persistants

### Inventaire complet

L'instance active est `$PokemonBag`. La dernière classe chargée se trouve dans
`BW_Bag` et conserve les poches dans `@pockets`. Les écritures doivent rester
basées sur `pbStoreItem`, `pbDeleteItem`, `pbCanStore?` et `pbQuantity` pour
respecter les limites propres à chaque poche.

La fenêtre externe prend désormais un instantané immuable de toutes les poches,
sépare lecture et écriture, déduplique les IDs puis applique des commandes
ciblées. Le
wrapper global de `pbDeleteItem` n'est pas une bonne implémentation des objets
infinis : vente, dépôt, don et mise à la poubelle passent aussi par cette
méthode. Il faudra entourer les chemins de consommation (`pbConsumeItemInBattle`
et les handlers d'utilisation hors combat) avec une portée explicite.

### Groupe et boîtes

Le groupe est disponible via `$Trainer.party` et le stockage via
`$PokemonStorage`. Les écrans natifs utilisent `PokemonStorageScreen`, mais la
fenêtre externe complète lit un snapshot et ne garde jamais de pointeur Ruby
vivant côté interface. Création et suppression valident notamment le groupe
non vide, la taille maximale 6, l'emplacement libre, l'espèce, le niveau et le
recalcul des statistiques.

Avant toute édition en masse, ajouter une sauvegarde de sécurité, un aperçu des
changements et une commande d'annulation. Les fichiers principal et autosave ne
doivent jamais être réécrits directement par le trainer pendant que le jeu les
utilise.

### Identité, temps, badges et Pokédex

- Le modèle de personnage ne se résume pas à une variable de sexe.
  `pbChangePlayer(id)` synchronise `trainertype`, le sprite, `playerID` et
  `metaID`. Les scripts de sélection utilisent notamment les IDs 0, 1 et 2.
  Les pronoms sont stockés séparément dans `$Trainer.pronouns`.
- Le temps affiché et sauvegardé vaut `$totalPlayTime + (Time.now.to_i -
  $loadTime)`. La sauvegarde écrit `"T#{totalsec}"`. Un éditeur doit donc
  réancrer les deux variables, pas modifier `Graphics.frame_count`.
- Les badges sont le tableau booléen `$Trainer.badges`; `numbadges` en compte
  les valeurs vraies. Ils influencent aussi les CS, les boosts de statistiques
  et le niveau d'obéissance, ce qui impose un aperçu explicite.
- Le Pokédex combine `$Trainer.seen`, `$Trainer.owned`, `formseen`,
  `formlastseen` et `seenShiny`. Remplir seulement `seen`/`owned` laisserait les
  variantes mâle, femelle, formes et shiny incohérentes.

## Exploration

### Vol et téléportation

Les destinations de vol sont filtrées par
`$PokemonGlobal.visitedMaps[healspot[0]]`, puis par
`REGIONMAPFLIGHTRESTRICTIONS`. Débloquer toutes les destinations peut soit
remplir `visitedMaps` de façon persistante, soit virtualiser uniquement le test
de visite. La seconde approche est réversible mais ne doit pas contourner les
restrictions scénarisées sans une option distincte.

Un favori de téléportation doit enregistrer au minimum l'ID de carte, `x`, `y`
et la direction. Avant le transfert, vérifier que la carte existe, que les
coordonnées sont dans ses limites, que la tuile est praticable et qu'aucun
combat, message ou interpréteur d'événement n'est actif. Utiliser ensuite la
transition native via `$game_temp.player_new_map_id`, `player_new_x` et
`player_new_y` plutôt qu'une écriture directe dans `$game_player`.

### Rencontres, niveaux et shiny

- Les rencontres aléatoires passent par
  `$PokemonEncounters.pbEncounteredPokemon`, qui retourne `[species, level]`.
  Les combats explicitement scénarisés appellent parfois `pbWildBattle`
  directement et doivent rester hors portée par défaut.
- `pbGenerateWildPokemon(species, level)` est le dernier point commun de
  création d'un Pokémon sauvage. Il applique ensuite objets tenus, Charme
  Chroma, Pokérus, Joli Sourire, Pression et Synchro avant de déclencher
  `Events.onWildPokemonCreate`.
- Le test shiny natif est `d < SHINYPOKEMONCHANCE`, avec une constante à 64 sur
  65536 dans cette version, soit 1/1024 avant les relances. Un multiplicateur
  devrait agir uniquement lors de la création sauvage et afficher la
  probabilité effective avec les relances, sans rendre shiny les Pokémon déjà
  possédés ni ceux des Dresseurs.

Le trainer peut maintenant remplacer le résultat de la **prochaine** rencontre
aléatoire par une espèce choisie (ID Pokédex 1 à 800) et fixer son niveau. Le
taux shiny est un dénominateur `1/N` réglable de 1 à 1024 : il est tiré après
la création complète du Pokémon sauvage et ajuste son PID pour respecter ce
taux. Hors activation, le slider revient sur la valeur native 1/1024.

Les deux points d'accroche (`$PokemonEncounters` et
`Events.onWildPokemonCreate`) doivent désormais confirmer leur installation.
Le bootstrap réessaie seulement tant que l'un manque ; une fois acquitté, il
n'exécute plus `RGSSEval` périodiquement et les réglages utilisent les globales
Ruby déjà installées.

### PC accessible partout

`StorageSystemPC#access` ouvre directement l'interface de boîtes, tandis que
`pbPokeCenterPC` affiche le menu complet des PC enregistrés. La première cible
est plus prévisible. L'action devra être refusée hors `Scene_Map`, pendant un
événement ou un transfert, fermer temporairement l'overlay, puis restaurer son
état après la fermeture de l'écran.

Le trainer propose désormais cette action depuis la carte : il ferme l'overlay
avant d'appeler `StorageSystemPC#access` et laisse intact le jeu lorsqu'une
transition ou un événement est actif. Le déblocage des destinations de vol
remplit `visitedMaps` pour les cartes déclarées dans `MapInfos.rxdata` ; ce
choix est persistant dès la prochaine sauvegarde. La complétion du Pokédex
utilise `setSeen` et `setOwned` pour chaque espèce, ses deux sexes et une
variante shiny, plutôt que d'écrire partiellement les tableaux du dresseur.

## Combat et vitesse

Le God mode ne falsifie pas `isFainted?` et ne restaure pas les PV à chaque
image. Le payload réinstalle dynamiquement trois petites méthodes natives avec
un garde optionnel, sans modifier `Scripts.rxdata` :

- `PokeBattle_Battler#pbReduceHP` et son writer `hp=` bloquent les dégâts
  résiduels, le recul et les sacrifices forcés pour les battlers du joueur ;
- `PokeBattle_Pokemon#hp=` protège l'objet de l'équipe lui-même, notamment
  contre le poison hors combat et les écritures directes.

Les mêmes gardes sont posés sur les singletons des Pokémon du groupe et des
battlers actifs afin de couvrir les classes internes remplacées tardivement par
Uranium.

Les protections consultent `$__uranium_trainer_hp_lock` et prouvent la
propriété via `pbOwnedByPlayer?` ou l'identité dans `$Trainer.party`. Le payload
natif synchronise cette globale avec le bouton et `trainer.ini`. Les wrappers
sont recréés à chaque injection, puis reposés périodiquement en mémoire pour
résister aux chargements tardifs. Ils disparaissent à la fermeture du jeu et ne
maintiennent jamais artificiellement un Pokémon KO dans le combat.

Le prototype historique `opt_ohk.cpp` et l'ancien `opt_speedhack.cpp` restent
exclus du payload. Ils ne doivent pas être réactivés tels quels : le premier
n'a pas encore la matrice de validation simple/double combat, et le second pose
un détour de cinq octets dans `timeGetTime` sans décodage d'instructions ni
protocole de retrait équivalent au répartiteur RGSS.

La vitesse globale active est implémentée séparément dans `opt_gamespeed.cpp`.
Elle conserve plusieurs ticks logiques par image lorsque nécessaire, virtualise
`Graphics.frame_count` et `Graphics.frame_rate`, puis augmente progressivement
la cadence de rendu avec un plafond absolu de 120 FPS. Cela expose davantage de
positions intermédiaires pendant les déplacements sans rendre cinq fois plus
d'images. Elle ne modifie ni `timeGetTime` ni la cadence du processus entier.
Les méthodes Ruby entourant le rendu, notamment les transitions spéciales,
continuent d'être mises à jour à chaque tick logique.

Pour KO en un coup ou un multiplicateur de dégâts reçu, réutiliser ces points
centraux et conserver la preuve du camp avec `pbOwnedByPlayer?`, y compris en
combat double.
