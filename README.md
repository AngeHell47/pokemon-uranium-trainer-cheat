# Pokémon Uranium External Trainer

A standalone 32-bit Windows trainer for the RGSS1 version of Pokémon Uranium.
It lets you launch the game directly or attach to an existing game process,
then injects its embedded overlay safely into the game's RGSS thread. No
`version.dll` needs to be copied next to the game.

## Using the trainer

1. Run `UraniumTrainer.exe`.
2. Click **Launch game + load save directly** to skip the intro and open the default save.
3. To attach to a game already running, select the process labelled `[Game]` and click **Connect to selected game**.
4. Press `Insert` to hide or show the overlay, or click the close button in its title bar.

The launcher closes after a successful injection and initialized overlay. It stays open and displays the error if anything fails. Direct launch is temporary: it does not save a startup preference or change normal launches of `Uranium.exe`.

Run the trainer at the same privilege level as the game. If the game runs as administrator, run the trainer as administrator too.

## Overlay and language

The trainer opens in English by default. Use the United States, French or Spanish flag in the top-right of the **Uranium Trainer** title bar to switch the overlay language. The selected language is saved in `trainer.ini`.

The compact main window is organized into six tabs: **Player**, **Battle**, **Encounters**, **World**, **Display** and **Settings**. Each page uses short cards instead of one continuous feature list. The **Settings** tab lets you replace the global `Insert` show/hide shortcut, restore its default value, or use **Stop Trainer** to close the trainer UI and remove its active hooks for the current game session. The overlay and its Pokémon, inventory and trainer editors remain visible and interactive when Uranium loses focus or is minimized. After its initial placement, the overlay keeps the position chosen by the user.

The **Global speed** control keeps its x1–x5 slider; the separate duplicate multiplier row has been removed. The **Pokemon ID** control displays the selected value followed by the corresponding Pokémon name; the name is read from the game data.

## Features

- Direct game launch and save loading without simulated input.
- God mode that preserves real HP, infinite PP, one-hit KOs, damage multiplier, guaranteed catches, trainer catches, instant egg hatching, removable HMs and infinite items.
- Configurable noclip, wild-encounter controls, wild level and shiny chance.
- Forced time and weather that can be disabled without altering saved state.
- Global speed x1–x5, plus configurable walking, running, surfing and cycling speeds.
- One-click restoration of the default walking, running, surfing and cycling speeds.
- Party and PC Pokémon editor, full inventory editor and trainer-profile editor.
- Money, map zoom-out, mouse-wheel zoom and a configurable minimap.

Overlay clicks are intercepted before they reach the game.

## Building

Requirements: Visual Studio 2022 with the Desktop C++ x86 tools.

```bat
cd "Trainer externe"
build.bat
```

The resulting executable is `Trainer externe/UraniumTrainer.exe`. It embeds the payload and the move database, so neither `moves.txt` nor an auxiliary DLL is required when using it.

## Project layout

- `Trainer externe/` — process picker, injector and packaging.
- `Launcher DLL/` — injected payload, overlay and trainer options.
- `tools/` — God mode patch generator without game data.
- `docs/` — [architecture](docs/ARCHITECTURE.md), stability audit, roadmap and [Ruby hook notes](docs/GAME_SCRIPT_HOOKS.md).

This repository does not include the game, proprietary game data or build artifacts.
