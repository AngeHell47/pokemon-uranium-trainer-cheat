# Audit de stabilité

Ce document sépare volontairement la revue statique, le test de démarrage et
la validation réelle en jeu. Une compilation réussie ne prouve pas à elle
seule le comportement d'un combat complet.

| Fonction | Protection actuelle | Risque ou limite connue | Validation requise en jeu |
|---|---|---|---|
| Démarrage direct | Marqueur éphémère par processus ; intro et écran de chargement headless ; branche `Continue` originale | En l'absence de sauvegarde valide, revient au menu de chargement normal | Sauvegarde unique/multiple, autosave plus récente, sauvegarde corrompue |
| Injection externe | Payload intégré, PID x86, mutex par processus, événement Ready + overlay exigés | Même niveau de privilèges requis | Refaire le smoke test après une évolution du bootstrap |
| Entrées overlay | Hooks limités au jeu au premier plan ; messages consommés/reroutés ; état physique masqué dans `Input.getstate` | Tester les boutons latéraux selon la souris | Glisser hors fenêtre, molette et boutons X1/X2 sur une session réelle |
| Pause inactive | Sauvegarde et restaure les octets d'origine ; vérifie `VirtualProtect` et vide le cache d'instructions | Signature propre à RGSS102E de cette version | Basculer ON/OFF puis changer plusieurs fois de fenêtre |
| God mode | HP/HP max réels ; `pbReduceHP` et les baisses directes sont annulés uniquement pour les battlers du joueur | Les effets secondaires d'une attaque peuvent toujours se déclencher | Combat simple/double, dégâts directs, poison, recul, drain, OFF en combat |
| PP infinis | Wrapper de `pbSetPP`, joueur uniquement ; couvre usage, Pressure, Grudge et Spite | L'activation remplit les PP réels, donc ce remplissage peut être sauvegardé | Attaque normale, Pressure/Spite, adversaire intact, OFF puis décrément |
| Météo | Getters virtuels ; la météo naturelle continue en arrière-plan et réapparaît sur OFF | Les constantes historiques sont incohérentes, le menu suit le renderer réel | Chaque type 0–8 sur une carte, combat et OFF |
| Heure | Wrapper unique de `pbGetTimeNow`, délégation native sur OFF ; caches jour/nuit invalidés uniquement au changement | Les événements dépendants de l'heure doivent être testés individuellement | 00 h, midi, 23 h 59, OFF, changement de carte |
| Argent | Setter natif, limite `999999`, lecture séparée des écritures | Modification volontairement sauvegardable | Valeurs 0/max, achats et sauvegarde |
| Sac | `pbStoreItem`/`pbDeleteItem`, limite native 99, ID+quantité figés dans une FIFO | L'élément sélectionné peut changer après suppression à 0 | 0, 1, 99, poche pleine, objet clé |
| Noclip | Lit directement l'état physique Windows de la touche configurable (`Ctrl` par défaut), surclasse temporairement `@through` pendant `passable?`, puis restaure l'état original | Les limites externes de carte restent actives ; `Inser` et `Échap` sont réservées par le menu lors de la saisie du raccourci | Murs, événements, portes, bords de carte, état scripted-through |
| Sans rencontres | Pilote le verrou natif `encounter_disabled` ; un garde de `pbSave` sérialise temporairement `false`, puis restaure l'état actif en mémoire | N'empêche pas les combats déclenchés par script | Herbes/grottes/surf, ON puis OFF, sauvegarde manuelle/auto avec l'option ON, ancienne sauvegarde contaminée |
| Vitesse joueur | Appliquée au dernier point avant le calcul de distance, `Game_Character#update_move`, avec retry temporisé jusqu'à acquittement | Les routes forcées gardent leur vitesse de script | Marche/course/surf/vélo/glace, toutes valeurs 1–8 |
| Dézoom | Dimensions logiques 4:3 + facteur inverse ; cache du sol et translation rapide des tuiles ; événements lointains en veille mais tous les sprites visibles actualisés | Le vide hors des limites d'une carte devient visible près d'un bord ; les cartes à très nombreux autotiles animés restent à qualifier | Carte réelle, déplacements/transferts, menu, combat, 100/133/187/200/300/400/500 % |
| Soigner équipe | Appelle `heal` sur chaque Pokémon du groupe | Action volontairement sauvegardable | Statuts, KO, œuf et groupe incomplet |
| Éditeur Pokémon | Transfert mémoire direct, FIFO immuable, nom 11 caractères, IV 0–31, EV 0–255 et total 510 | Version actuelle limitée au premier Pokémon ; toute édition est sauvegardable | Chaque champ, recalcul des stats, changement d'attaque |

## Validations réalisées

- Bouton de démarrage direct vérifié sur une vraie sauvegarde : aucune scène
  d'intro ou de sélection affichée, arrivée directe sur `Scene_Map`, overlay
  différé jusqu'à la carte et aucune boîte d'erreur après stabilisation. Un
  lancement normal sans marqueur affiche toujours l'intro complète.
- No-clip validé sur une vraie carte : franchissement uniquement pendant le
  maintien de la touche, changement par le bouton compact et prise en compte
  immédiate du nouveau raccourci.
- Les empreintes SHA-256 de `Uranium.rxdata` et
  `Uranium_autosave.rxdata` sont restées strictement identiques avant/après le
  smoke test de chargement direct.
- Build complet x86 final avec niveau d'avertissement `/W4` : aucune erreur ;
  payload et base des attaques correctement embarqués dans l'EXE.
- Injection du binaire final : événement Ready signalé, fenêtre
  `TrainerOverlay` présente, jeu réactif et exactement un payload chargé.
  Une seconde connexion a conservé ce nombre à un.
- Test dynamique des entrées : bouton gauche maintenu hors overlay lu à `1`
  par RGSS ; le même bouton sur l'overlay lu à `0` ; flèche Haut capturée par le
  menu lue à `0`.
- Dézoom synthétique sans chargement de sauvegarde : client `1024×768` avant,
  pendant et après ; logique `512×384` à 100 %, `1024×768` à 200 %, puis retour
  exact à `512×384`.
- Dézoom sur une vraie carte à 500 %, réglages utilisateur marche/course 7/6 :
  environ 55,5 FPS au repos et 50,8 FPS pendant 12 secondes de déplacement
  continu dans quatre directions. Le client est resté inchangé, sans couture
  horizontale ni PNJ figé au bord sur la capture finale ; mémoire privée
  observée à 173 Mo.
- Heure : midi, conversion 24 h vers 23 h 59 et retour à l'heure réelle sur OFF
  vérifiés dynamiquement.
- Les fichiers `Uranium.rxdata`, `Uranium_autosave.rxdata` et
  `GlobalSettings.rxdata` ont été restaurés puis comparés à leurs empreintes de
  référence après les essais.

Les combats réels n'ont pas été automatisés. HP, PP et météo doivent donc
encore être validés avec la matrice indiquée dans le tableau. Le dézoom a été
testé sur une vraie `Scene_Map`, mais les transitions combat/menu et les cartes
très chargées en autotiles animés restent à qualifier plus longuement.

## Corrections issues de l'audit

- Suppression du double chargement causé par une ancienne DLL proxy locale.
- Correction des libellés météo : 4 sable, 5 soleil, 7 blizzard.
- Suppression des écritures persistantes de HP, météo, noclip et rencontres.
- Correction des limites argent, sac, IV et EV.
- Séparation des files de lecture/écriture pour éviter qu'un rafraîchissement
  écrase une action utilisateur.
- Ajout de FIFO immuables pour les éditions rapides du sac et du Pokémon.
- Filtrage des clics et touches jusque dans le polling physique RGSS.
- Signal Ready strict, retry acquitté de la vitesse et prévention du double
  payload.
- Suppression de l'ancien zoom `BitBlt`, qui ne pouvait pas être installé dans
  cette version de RGSS.
- Suppression du fichier temporaire `partymon.txt` au profit d'un buffer mémoire.
- Application des opérations lourdes du dézoom seulement au relâchement du slider.
- Correction des coutures des tuiles de priorité à échelle fractionnaire par
  alignement des bornes physiques et chevauchement d'un pixel, inactif à 100 %.
- Suppression du `RGSSEval` de zoom exécuté depuis la pompe de messages : le
  bootstrap est désormais armé par un détour vérifié de `Graphics.update`, puis
  les changements sont consommés par `Scene_Map#update`.
- Plage du dézoom étendue à 100–500 % à la demande ; les valeurs extrêmes sont
  conservées même si elles révèlent le vide autour des cartes finies.
- Cache `CustomTilemap` recentré pour éviter un redessin complet à chaque pas,
  translation rapide des tuiles prioritaires et portée logique des événements
  ramenée à la fenêtre vanilla. Le rendu des PNJ visibles n'est jamais filtré.
- Migration de toutes les options du build vers un répartiteur
  `Graphics.update` unique ; `opt_ohk.cpp`, encore historique, reste exclu du
  payload.
- Remplacement de la lecture no-clip via `Input.getstate` par
  `GetAsyncKeyState`, avec bouton de raccourci configurable et persistance dans
  `trainer.ini`.
- Migration des anciennes sauvegardes où le trainer avait laissé
  `Game_System#encounter_disabled` actif : lorsque « Sans rencontres » est OFF,
  l'initialisation de l'option ou son passage sur OFF remet ce drapeau à
  `false`. Lorsque l'option est ON, le drapeau est actif en mémoire mais le
  garde de `pbSave` force uniquement sa valeur sérialisée à `false`, puis le
  restaure après la sauvegarde.

## Incident C0000005 du 14 août 2026

Le dump `Uranium.exe.40192.dmp` situe l'accès invalide dans
`RGSS102E.dll+0x717E6`, avec `RGSSEval` appelé depuis l'ancien chemin de zoom au
milieu de la pompe de messages de `Graphics.update`. Le problème était une
réentrée dans la VM Ruby, pas une corruption de sauvegarde.

Le payload n'évalue plus Ruby depuis `WH_CALLWNDPROC` ou `WH_GETMESSAGE`. Un
détour persistant, dont la signature, l'arité et l'enregistrement
`Graphics.update` sont validés pour le `RGSS102E.dll` de cette version, appelle
un registre borné de callbacks à la frontière native volontaire de Ruby. Une
garde de profondeur empêche toute réentrée si un script appelle indirectement
`Graphics.update`. Toutes les options effectivement compilées utilisent ce
répartiteur ; le prototype OHK historique n'est pas inclus dans le build.
