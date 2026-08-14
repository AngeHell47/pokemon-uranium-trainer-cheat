TRAINER EXTERNE - POKEMON URANIUM
=================================

Utilisation :
1. Lance UraniumTrainer.exe.
2. Clique sur "Lancer le jeu + chargement direct" pour supprimer l'intro et
   charger immediatement la sauvegarde par defaut, sans clic simule.
3. Pour un jeu deja ouvert normalement, selectionne Uranium.exe (marque [Jeu])
   puis clique sur "Connecter au jeu selectionne".
4. Le menu s'ouvre automatiquement au-dessus du jeu.
5. La touche Inser masque ou reaffiche le menu.

No-clip : active l'option, puis maintiens la touche affichee dans le petit
bouton (CTRL par defaut). Clique sur ce bouton et presse une autre touche pour
changer le raccourci. Presse Echap, Retour arriere ou Suppr pour n'assigner
aucune touche : le no-clip reste alors actif en permanence tant que l'option
est active. Le choix est memorise dans trainer.ini.

Le chargement direct ne modifie aucun reglage persistant : lancer Uranium.exe
normalement conserve l'intro habituelle.

Le fichier EXE contient le payload necessaire : il n'est pas necessaire de
copier version.dll dans le dossier du jeu. Le payload est extrait dans le
dossier temporaire de Windows au moment de la connexion.

Si le jeu est lance en administrateur, lance egalement le trainer en
administrateur.

Compilation : lancer build.bat avec Visual Studio 2022 Community et les
outils Desktop C++ x86 installes.
