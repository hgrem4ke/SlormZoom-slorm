# SlormZoom for The Slormancer

Lets the camera zoom out further than the game allows. The game stops at 75% in its Zoom setting;
SlormZoom lets you go down to 0%, and you can change the limit live in game.

The game keeps its zoom limit in a variable (`max_zoom`): SlormZoom raises it a few times per
second. The game's executable is never changed.

## Installation

You need [Aurie Framework](https://github.com/AurieFramework/Aurie) and
[YYToolkit](https://github.com/AurieFramework/YYToolkit) **v5.0.0c** already installed in the game.

Copy `x64/Release/SlormZoom.dll` into `The Slormancer\mods\Aurie\`.

To change the settings with the mouse in game, also install
[ModMenu](https://github.com/hgrem4ke/ModMenu-slorm). Without it, SlormZoom still works with the
`SlormZoom.ini` file.

## Settings

**In game, with the mouse** (with ModMenu): Options menu → **M** button → **SLORMZOOM** tile.
Then zoom with the mouse wheel or the game's own Zoom setting.

**Outside the game**: the `SlormZoom.ini` file is created next to the DLL on first launch.

| Setting | Values |
|---------|--------|
| Enabled | 1 = zoom further out than the game allows, 0 = the game's own limit |
| MaxZoom | furthest zoom-out: 1.25 = 75% (the game's own), 1.5 = 50%, 1.75 = 25%, 1.9 = 10%, 2.0 = 0% |
| Debug | 1 = detailed messages in the console |

## Building

Visual Studio 2022 or newer, with "Desktop development with C++". Open `SlormZoom.vcxproj` and build
**Release / x64**. The Aurie, YYToolkit v5.0.0c and ModMenu headers are in `include/`.

## License

AGPL-3.0, see [LICENSE](LICENSE). The `include/` folder contains the Aurie and YYToolkit headers
(AGPL-3.0, AurieFramework).

Unofficial mod, not affiliated with the game's developers.
