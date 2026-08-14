# Pokémon Uranium External Trainer

Trainer Windows x86 externe pour la version RGSS1 de Pokémon Uranium.

L'application permet de sélectionner le processus du jeu puis charge un
payload intégré qui affiche l'overlay et pilote les fonctions RGSS dans le bon
thread. Aucun `version.dll` ne doit être copié à côté du jeu.

## Utilisation

1. Lancer `UraniumTrainer.exe`.
2. Cliquer sur **Lancer le jeu + chargement direct** pour supprimer l'intro et
   arriver directement sur la sauvegarde par défaut.
3. Pour un jeu déjà ouvert normalement, sélectionner le processus marqué
   `[Jeu]` puis cliquer sur **Connecter au jeu sélectionné**.
4. Utiliser `Inser` pour masquer ou réafficher l'overlay.

Une fois l'injection confirmée et l'overlay initialisé, le launcher se ferme
automatiquement. En cas d'échec, il reste ouvert et affiche le message d'erreur.

Le lancement direct est éphémère : il n'écrit aucun réglage persistant et ne
change pas le comportement d'un démarrage normal de `Uranium.exe`.

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

- Démarrage direct sur la sauvegarde, sans intro ni simulation de saisie.
- God mode à HP réels et PP infinis pour les Pokémon du joueur.
- Capture garantie des Pokémon sauvages capturables, quelle que soit la Ball.
- Éclosion des œufs au prochain pas, avec animation et données natives.
- CS effaçables dans l'écran natif de remplacement d'une attaque.
- Météo et heure forcées, réversibles sans modifier leur état sauvegardé.
- Noclip pendant le maintien d'une touche configurable (`Ctrl` par défaut), ou
  actif en permanence si aucune touche n'est assignée, blocage des rencontres,
  soin de l'équipe et vitesses de déplacement.
- Argent, quantité de l'objet sélectionné et édition du premier Pokémon.
- Dézoom logique de la carte en conservant la taille physique de la fenêtre.
- Zoom avant/arrière directement en jeu avec la molette de la souris.

Les clics reçus par l'overlay sont bloqués avant d'atteindre le jeu. Les limites
de validation et les essais restant à effectuer en situation réelle sont
détaillés dans [l'audit de stabilité](docs/STABILITY_AUDIT.md).

## Organisation

- `Trainer externe/` : sélecteur de processus, injection et empaquetage.
- `Launcher DLL/` : payload, overlay et implémentation des options.
- `docs/` : notes d'architecture, audit de stabilité et pistes futures.

Ce dépôt ne contient ni le jeu, ni ses données propriétaires, ni les artefacts
de compilation.
