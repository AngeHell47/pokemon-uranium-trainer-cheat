# Architecture du trainer externe

## Vue d'ensemble

`UraniumTrainer.exe` est une application Windows x86 autonome. Elle liste les
processus compatibles, extrait son payload DLL embarqué dans le dossier
temporaire de Windows, puis le charge dans le processus RGSS sélectionné.

Le bouton de lancement direct crée `Uranium.exe` avec un marqueur de ligne de
commande propre à cette instance, injecte le payload dès que RGSS est chargé,
puis attend l'arrivée sur la carte. Le payload remplace la scène d'intro par
une scène vide et rend l'écran de chargement headless. Il ne simule aucun clic :
la branche `Continue` originale d'Uranium effectue encore le choix de la
sauvegarde par défaut, les contrôles d'intégrité, les migrations et la
désérialisation. Sans ce marqueur, un lancement normal reste inchangé.

Le payload crée une fenêtre d'overlay indépendante au-dessus du jeu. Les
fonctions Ruby/RGSS ne sont jamais appelées depuis le thread de l'interface :
les demandes sont mises en attente dans des structures atomiques ou des FIFO,
puis consommées par leurs wrappers Ruby sur le thread RGSS.

```text
UraniumTrainer.exe
  -> sélection du PID x86/RGSS
  -> extraction du payload versionné dans %TEMP%
  -> LoadLibraryW dans Uranium.exe
     -> démarrage direct headless éventuel
     -> overlay TrainerOverlay après l'arrivée sur la carte
     -> commandes atomiques / FIFO
     -> consommation dans les wrappers Ruby du jeu
```

Un mutex nommé par PID empêche une double initialisation. Le lanceur ne déclare
la connexion réussie qu'après le signal de l'événement `Ready` **et** la
présence de la fenêtre d'overlay. Une deuxième connexion réutilise donc
l'instance prête sans charger un second payload. Aucun `version.dll` de trainer
ne doit rester à côté de `Uranium.exe`.

## Principes de sûreté

- Les options réversibles utilisent des wrappers Ruby idempotents qui délèguent
  immédiatement à l'implémentation d'origine quand elles sont désactivées.
- HP, météo, noclip et blocage des rencontres ne modifient pas les objets
  sérialisés de la sauvegarde.
- Le god mode conserve les HP réels et annule seulement leurs diminutions en
  combat ; il ne remplace plus les accesseurs par un affichage 999/999.
- PP infinis remplit les PP à l'activation et empêche ensuite les diminutions
  des Pokémon possédés par le joueur. Les PP remplis peuvent naturellement
  être sauvegardés si le joueur sauvegarde ensuite sa partie.
- Les écritures volontaires (argent, sac, Pokémon) passent autant que possible
  par les méthodes du jeu et respectent ses limites.
- Les éditions du sac et du Pokémon utilisent des files FIFO de commandes
  immuables. Un rafraîchissement ou un second clic ne peut donc pas mélanger la
  cible et la valeur d'une commande précédente.
- Les ressources nécessaires au trainer, dont la base des attaques, sont
  embarquées dans le payload ; un fichier externe valide peut les remplacer
  pour le développement.

## Entrées de l'overlay

Les hooks souris et clavier bas niveau reroutent les interactions vers
l'overlay seulement lorsque le jeu est au premier plan. Pokémon Uranium lit
aussi directement l'état physique via `GetAsyncKeyState` : un wrapper
idempotent de `Input.getstate` masque donc les boutons et touches appartenant à
l'overlay. Il est programmé avant le signal `Ready` et installé au premier
safe point, avant que l'image RGSS correspondante ne sonde `Input`. À la
fermeture du menu ou à la perte de focus, les drapeaux natifs sont remis à zéro
immédiatement.

Le point de passage RGSS commun est installé avant les hooks bas niveau de
l'overlay. Leur thread entre ensuite immédiatement dans sa boucle de messages,
ce qui évite un retrait silencieux par `LowLevelHooksTimeout`.

## Dézoom

Le dézoom ne redimensionne pas la fenêtre et ne fait pas d'étirement GDI. Il
augmente les dimensions logiques 4:3 de `Graphics` et réduit le facteur de
`Sprite_Resizer` dans la proportion inverse. La surface physique conserve donc
exactement sa largeur et sa hauteur, tandis que la carte affiche davantage de
tuiles autour du personnage.

Les spritesets de carte sont recréés lors d'une validation du slider, pas à
chaque pixel parcouru par la souris. Les écrans synchrones (menu et combat) sont
temporairement rendus à 100 %, puis le dézoom est réappliqué au retour sur la
carte.

Lorsque le pointeur se trouve dans la zone cliente du jeu, chaque cran de
molette change le dézoom de 30 points, dans les mêmes bornes que le slider
(haut pour zoomer, bas pour dézoomer). Le hook souris met seulement la demande
en file ; l'overlay applique ensuite le réglage afin de garder le hook bas
niveau non bloquant.

Le bootstrap Ruby du zoom n'est pas exécuté depuis la pompe de messages. Un
répartiteur commun pose un détour persistant sur la méthode native
`Graphics.update`, après validation stricte du PE, de son prologue et de son
enregistrement Ruby. Toutes les options compilées évaluent désormais Ruby à
cette frontière C sûre, jamais depuis `WH_CALLWNDPROC` ou `WH_GETMESSAGE`.
Les changements de zoom suivants sont publiés atomiquement puis consommés par
`Scene_Map#update`.

`CustomTilemap` construit les décors prioritaires avec un sprite par tuile. À
une échelle fractionnaire, leurs arrondis indépendants ouvraient des coutures
d'un pixel. Le trainer force désormais les positions et étend ces sprites d'un
pixel physique ; ce chevauchement est désactivé à 100 %. Le cache du sol garde
une marge symétrique et les sprites prioritaires sont translatés sans être
recréés à chaque pas. Enfin, seule la logique des événements très lointains est
mise en veille : les sprites visibles restent tous actualisés, ce qui empêche
les PNJ de rester accrochés au bord de l'écran.

La plage va de 100 à 500 %. Les zones noires qui peuvent rester près d'un bord
sont le vide réel hors des limites finies de la carte, et non un
redimensionnement de fenêtre.

## Compilation

Depuis une invite Visual Studio x86, ou simplement avec Visual Studio 2022
Community installé :

```bat
cd "Trainer externe"
build.bat
```

La sortie utilisable est `Trainer externe\UraniumTrainer.exe`. Les DLL et
ressources intermédiaires du dossier `build` ne sont pas à distribuer.
