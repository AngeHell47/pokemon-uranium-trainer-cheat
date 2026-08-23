# Pokémon Uranium External Trainer

Un trainer externe pour Pokémon Uranium. Il s’attache à une partie déjà lancée et ouvre son menu directement dans le jeu.

## Aperçu

![Aperçu du trainer](Trainer%20externe/preview.png)

## Lancer le trainer

1. Lance Pokémon Uranium normalement.
2. Ouvre `UraniumTrainer.exe`.
3. Sélectionne le processus `Uranium.exe`.
4. Clique sur **Attacher au processus**.

Le menu apparaît automatiquement dans le jeu. Utilise la touche `Insert` pour l’afficher ou le masquer.

> Si le jeu est exécuté en administrateur, lance aussi le trainer en administrateur.

## Fonctionnalités

- God mode, PP et objets infinis, capture garantie et one-hit KO.
- Modification de l’argent, de l’inventaire, de l’équipe, des boîtes PC et du profil du dresseur.
- Contrôle de la vitesse, du déplacement, des rencontres, de l’heure, de la météo et de la mini-carte.
- Éditeurs de Pokémon, d’inventaire et de dresseur directement depuis le menu.

## Langues

L’interface du trainer est disponible en anglais, français et espagnol. Utilise les drapeaux en haut à droite du menu pour changer de langue.

## Compiler

Visual Studio 2022 avec les outils **Desktop C++ x86** est nécessaire.

```bat
cd "Trainer externe"
build.bat
```

L’exécutable compilé est disponible dans `Trainer externe/UraniumTrainer.exe`.
