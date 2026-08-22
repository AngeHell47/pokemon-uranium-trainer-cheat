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

Les boutons "Gerer tous les Pokemon" et "Gerer tout l'inventaire" ouvrent des
fenetres autonomes. La premiere couvre l'equipe et les boites, y compris la
creation/suppression et les attaques/PP/IV/EV. La seconde affiche toutes les
poches et le catalogue complet pour ajouter, retirer ou fixer une quantite.
"Gerer le dresseur" ouvre une troisieme fenetre pour modifier le pseudo, le
sexe, le temps de jeu et chacun des huit badges. Cliquez sur Appliquer, puis
sauvegardez dans le jeu pour rendre ces changements permanents.

No-clip : active l'option, puis maintiens la touche affichee dans le petit
bouton (CTRL par defaut). Clique sur ce bouton et presse une autre touche pour
changer le raccourci. Presse Echap, Retour arriere ou Suppr pour n'assigner
aucune touche : le no-clip reste alors actif en permanence tant que l'option
est active. Le choix est memorise dans trainer.ini.

Vitesse globale : choisis un multiplicateur x1 a x5 puis active l'option.
Sans touche assignee (reglage par defaut), l'acceleration reste permanente.
Clique sur le petit bouton de touche pour choisir un raccourci a maintenir ;
Echap, Retour arriere ou Suppr restaure le mode sans touche.

KO en un coup met les Pokemon adverses KO des le premier degat inflige, y
compris pendant les combats de dresseurs. Objets infinis empeche la
consommation des Pokeballs, soins, CT et baies ; les objets-clefs restent
volontairement exclus afin de ne pas bloquer les scripts du scenario.

"Voler depuis n'importe ou" ouvre directement la carte de destinations,
sans verifier si le personnage se trouve dehors. "Capturer dresseurs" active
le chemin de capture specialise d'Uranium : une Ball peut alors retirer et
ajouter a vos boites un Pokemon adverse de dresseur. Combinez-le avec
"Capture 100%" pour une capture garantie.

Le curseur "Multiplicateur de degats" regle les degats de vos Pokemon de x1
a x100. Il ne modifie ni les degats recus, ni les degats de recul/allies.

Le chargement direct ne modifie aucun reglage persistant : lancer Uranium.exe
normalement conserve l'intro habituelle.

Le fichier EXE contient le payload necessaire : il n'est pas necessaire de
copier version.dll dans le dossier du jeu. Le payload est extrait dans le
dossier temporaire de Windows au moment de la connexion.

Si le jeu est lance en administrateur, lance egalement le trainer en
administrateur.

Compilation : lancer build.bat avec Visual Studio 2022 Community et les
outils Desktop C++ x86 installes.
