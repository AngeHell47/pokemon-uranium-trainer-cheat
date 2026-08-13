# Architecture du trainer externe

## Vue d'ensemble

`UraniumTrainer.exe` est une application Windows x86 autonome. Elle liste les
processus compatibles, extrait son payload DLL embarqué dans le dossier
temporaire de Windows, puis le charge dans le processus RGSS sélectionné.

Le payload crée une fenêtre d'overlay indépendante au-dessus du jeu. Les
fonctions Ruby/RGSS ne sont jamais appelées depuis le thread de l'interface :
les demandes sont mises en attente avec des variables atomiques, réveillent le
thread de fenêtre du jeu, puis sont exécutées depuis des hooks attachés au
thread RGSS.

```text
UraniumTrainer.exe
  -> sélection du PID x86/RGSS
  -> extraction du payload versionné dans %TEMP%
  -> LoadLibraryW dans Uranium.exe
     -> overlay TrainerOverlay
     -> commandes atomiques
     -> exécution RGSSEval sur le thread RGSS
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
idempotent de `Input.getstate`, installé et acquitté avant le signal `Ready`,
masque donc les boutons et touches appartenant à l'overlay. À la fermeture du
menu ou à la perte de focus, les drapeaux natifs sont remis à zéro
immédiatement.

Les hooks du thread RGSS sont installés avant les hooks bas niveau. Cela évite
que Windows retire silencieusement ces derniers via `LowLevelHooksTimeout`
pendant un éventuel retry de chargement des scripts.

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

## Compilation

Depuis une invite Visual Studio x86, ou simplement avec Visual Studio 2022
Community installé :

```bat
cd "Trainer externe"
build.bat
```

La sortie utilisable est `Trainer externe\UraniumTrainer.exe`. Les DLL et
ressources intermédiaires du dossier `build` ne sont pas à distribuer.
