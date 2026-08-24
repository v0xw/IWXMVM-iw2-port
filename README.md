<div align="center">
  <br><br>
  <a href="https://github.com/reallyluckyy/IWXMVM">
    <img src="https://github.com/reallyluckyy/IWXMVM/assets/7430330/9463f12e-f919-49b9-b6ce-94037df9a181" width="50%">
  </a>
  <br><br>
</div>

## About

A port of [IWXMVM](https://github.com/reallyluckyy/IWXMVM) to **Call of Duty 2** (2005): a recording mod
featuring keyframeable campaths, a fully rewindable video editor-like timeline, kill markers detected at
demo load time, a bone camera and built-in ProRes video capturing support.

This repository contains IWXMVM's game-agnostic core together with the CoD2 game module. It is not part
of upstream IWXMVM; see [Credits](#credits).

## Supported Game

| Game          | Notes |
| ------------- | ----- |
| Call of Duty 2 Multiplayer | v1.3 only (`CoD2MP_s.exe`, hardcoded addresses, `.dm_1` demos). Works alongside [CoD2x](https://github.com/eyza-cod2/CoD2x). |

## Requirements

- Visual Studio 2022 (or newer)
- DirectX SDK Jun10 is optional: when `DXSDK_DIR` is not set, the copy of Microsoft's
  [Microsoft.DXSDK.D3DX](https://www.nuget.org/packages/Microsoft.DXSDK.D3DX) package under `core/third-party/d3dx` is used

## Building

First clone the repository:
```
git clone --recursive https://github.com/v0xw/IWXMVM-iw2-port.git
```
Then build the included solution file using Visual Studio.

## Running

Start the game and inject `iw2.dll` (from `iw2\bin\Win32\Release\`) with the injector of your choice, or
use the included development launcher, which starts the game and injects in one step:

```
iw2launcher\bin\Win32\Release\iw2launcher.exe --launch --windowed 1920x1080
```

Nothing needs to be copied into the game installation: the launcher injects the DLL straight from the
build output, and `iw2.dll` loads `D3DCompiler_43.dll` from its own directory (staged there by the build).

## Project Structure

The project is structured into the following sub-projects:
- [`core`](core/) contains the core mod logic (from upstream IWXMVM, with generic additions)
- [`iw2`](iw2/) contains the game-specific bindings for Call of Duty 2
- [`iw2launcher`](iw2launcher/) is a small development launcher/injector for CoD2

## Credits

- [IWXMVM](https://github.com/reallyluckyy/IWXMVM) by reallyluckyy and contributors - this project is
  built on IWXMVM's core and modeled on its CoD4/MW3 game modules. Core changes are kept generic and
  compatible with the upstream modules.
- [CoD2x](https://github.com/eyza-cod2/CoD2x) - the reverse-engineered symbol tables and struct layouts
  of the 1.3 binaries were invaluable for the CoD2 module.
