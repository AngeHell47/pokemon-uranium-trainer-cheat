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

### Préparation du God mode total

Le verrouillage complet des PV est installé dans le `Scripts.rxdata` déchiffré
du jeu afin d'intercepter aussi le poison, la confusion, le recul et les mises à
zéro forcées. Le dépôt ne distribue aucune donnée du jeu. À partir d'une copie
extraite correspondant à Uranium 1.2/1.3 :

```bat
py -3 -m pip install -r tools\requirements-godmode.txt
py -3 tools\patch_godmode_scripts.py ^
  "C:\chemin\extrait\Data\Scripts.rxdata" ^
  "C:\chemin\Uranium\Data\Scripts.rxdata"
```

Place ensuite tout le dossier `Data` extrait à côté de `Uranium.exe` et
conserve l'archive `Uranium.rgssad` sous un autre nom, par exemple
`Uranium.rgssad.original`, pour que RGSS charge ces données. L'archive reste
ainsi restaurable. Le chemin de `trainer.ini` est déduit automatiquement de la
destination ; `--trainer-ini` permet de le préciser si nécessaire.

## Compilation

Prérequis : Visual Studio 2022 avec les outils Desktop C++ x86.

Lancer :

```bat
cd "Trainer externe"
build.bat
```

Le binaire généré se trouve dans `Trainer externe/UraniumTrainer.exe`.

L'EXE contient également la base des attaques nécessaire à l'éditeur Pokémon ;
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
- Vitesse globale x1 à x5, active pendant le maintien d'une touche configurable
  ou en permanence sans touche (comportement par défaut). Les ticks RGSS sont accélérés tandis que le
  rendu adapte sa cadence jusqu'à 120 FPS au maximum, sans falsifier l'horloge
  Windows. Ce plafond affiche davantage de positions intermédiaires pendant les
  déplacements sans multiplier inutilement le rendu.
- Argent et deux fenêtres autonomes : inventaire complet par poche/catalogue,
  plus équipe et boîtes avec création, suppression et édition détaillée des
  Pokémon (PV, IV/EV, nature, objet tenu, attaques, PP et provenance).
- Dézoom logique de la carte en conservant la taille physique de la fenêtre.
- Zoom avant/arrière directement en jeu avec la molette de la souris.

Les clics reçus par l'overlay sont bloqués avant d'atteindre le jeu. Les limites
de validation et les essais restant à effectuer en situation réelle sont
détaillés dans [l'audit de stabilité](docs/STABILITY_AUDIT.md).

## Organisation

- `Trainer externe/` : sélecteur de processus, injection et empaquetage.
- `Launcher DLL/` : payload, overlay et implémentation des options.
- `tools/` : générateur du correctif God mode, sans données du jeu.
- `docs/` : architecture, audit de stabilité, feuille de route et
  [points d'accroche Ruby](docs/GAME_SCRIPT_HOOKS.md) pour les futures options.

Ce dépôt ne contient ni le jeu, ni ses données propriétaires, ni les artefacts
de compilation.
