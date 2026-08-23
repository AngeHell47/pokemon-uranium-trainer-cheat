# Pokémon Uranium External Trainer

A standalone trainer for Pokémon Uranium. It attaches to a game that is already running and opens its menu directly in-game.

## Preview

![Trainer preview](Trainer%20externe/preview.png)

## How to use

1. Start Pokémon Uranium normally.
2. Open `UraniumTrainer.exe`.
3. Select the `Uranium.exe` process.
4. Click **Attach to process**.

The menu opens automatically in the game. Press `Insert` to show or hide it.

> If the game is running as administrator, run the trainer as administrator too.

## Features

- God mode, infinite PP and items, guaranteed capture, and one-hit KOs.
- Money, inventory, party, PC box, and trainer-profile editing.
- Speed, movement, encounters, time, weather, and minimap controls.
- In-game Pokémon, inventory, and trainer editors.

## Languages

The trainer interface is available in English, French, and Spanish. Use the flags in the top-right corner of the menu to change the language.

## Build

Visual Studio 2022 with the **Desktop C++ x86** tools is required.

```bat
cd "Trainer externe"
build.bat
```

The compiled executable is available at `Trainer externe/UraniumTrainer.exe`.
