# Pokémon Uranium External Trainer

Trainer Windows x86 externe pour la version RGSS1 de Pokémon Uranium.

L'application permet de sélectionner le processus du jeu puis charge un
payload intégré qui affiche l'overlay et pilote les fonctions RGSS dans le bon
thread. Aucun `version.dll` ne doit être copié à côté du jeu.

## Utilisation

1. Lancer Pokémon Uranium et attendre l'ouverture de sa fenêtre.
2. Lancer `UraniumTrainer.exe`.
3. Sélectionner le processus marqué `[Jeu]`.
4. Cliquer sur **Connecter**.
5. Utiliser `Inser` pour masquer ou réafficher l'overlay.

Le jeu et le trainer doivent fonctionner au même niveau de privilèges. Si le
jeu est administrateur, le trainer doit l'être également.

## Compilation

Prérequis : Visual Studio 2022 avec les outils Desktop C++ x86.

Lancer :

```bat
cd "Trainer externe"
build.bat
```

Le binaire généré se trouve dans `Trainer externe/UraniumTrainer.exe`.

L'EXE contient également la base des attaques nécessaire au panneau Pokémon ;
aucun fichier `moves.txt` ni DLL auxiliaire n'est requis à l'utilisation.

## Fonctions actuelles

- HP à 999 et PP infinis pour les Pokémon du joueur.
- Météo et heure forcées, réversibles sans modifier leur état sauvegardé.
- Noclip, blocage des rencontres, soin de l'équipe et vitesses de déplacement.
- Argent, quantité de l'objet sélectionné et édition du premier Pokémon.
- Dézoom logique de la carte en conservant la taille physique de la fenêtre.

Les clics reçus par l'overlay sont bloqués avant d'atteindre le jeu. Les limites
de validation et les essais restant à effectuer en situation réelle sont
détaillés dans [l'audit de stabilité](docs/STABILITY_AUDIT.md).

## Organisation

- `Trainer externe/` : sélecteur de processus, injection et empaquetage.
- `Launcher DLL/` : payload, overlay et implémentation des options.
- `docs/` : notes d'architecture, audit de stabilité et pistes futures.

Ce dépôt ne contient ni le jeu, ni ses données propriétaires, ni les artefacts
de compilation.
