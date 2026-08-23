POKEMON URANIUM EXTERNAL TRAINER
================================

Usage:
1. Run UraniumTrainer.exe.
2. Click "Launch game + load save directly" to skip the intro and load the default save without simulated input.
3. To attach to a game already running, select Uranium.exe (labelled [Game]) and click "Connect to selected game".
4. The menu opens automatically over the game.
5. Press Insert to hide or show the menu, or click the close button in its title bar.

English is the default language. Click the US, French or Spanish flag in the overlay title bar to change the interface language. The preference is saved to trainer.ini.

The main interface is divided into Player, Battle, Encounters, World, Display and Settings tabs. Settings lets you replace the global Insert show/hide shortcut with another key, restore the default, or use "Stop & Unload" to cleanly detach the trainer from the current game process. The interface remains visible and interactive when Uranium is not the active window or is minimized, and keeps the position where you moved it.

"Manage all Pokemon" opens a party and PC-box editor, including creation, deletion, moves, PP, IVs and EVs. "Manage inventory" opens an editor for every bag pocket and the complete catalog. "Manage trainer" lets you edit the name, gender, play time and all eight badges. Click Apply, then save in the game to keep profile changes.

No-clip: enable the option, then hold the key shown in its small button (Ctrl by default). Click that button and press another key to change the shortcut. Press Escape, Backspace or Delete to clear the shortcut; noclip then remains active while the option is enabled. The selection is saved in trainer.ini.

Global speed: choose x1 to x5 on the slider built into the Global speed row, then enable the option. Without a shortcut (the default), acceleration stays on permanently. Click the key button to choose a hold shortcut; Escape, Backspace or Delete restores the no-shortcut mode. The separate duplicate multiplier row has been removed.

One-hit KO defeats opposing Pokémon on the first damaging hit, including in trainer battles. Infinite items prevents Poké Balls, healing items, TMs and berries from being consumed; key items are intentionally excluded so story scripts are not blocked.

"Fly from anywhere" opens the destination map without checking whether the player is outdoors. "Catch trainers" enables Uranium's special trainer-catch path: a Ball can remove an opponent's Pokémon and add it to your PC boxes. Combine it with "Capture 100%" for guaranteed catches.

The Damage multiplier slider changes damage dealt by your Pokémon from x1 to x100. It does not change received, recoil or ally damage.

Direct loading does not change persistent settings; launching Uranium.exe normally keeps the regular intro. The executable embeds the required payload, which is extracted to the Windows temporary directory while connecting; do not copy version.dll into the game folder.

If the game runs as administrator, run the trainer as administrator too.

Build with build.bat using Visual Studio 2022 Community and the Desktop C++ x86 tools.
