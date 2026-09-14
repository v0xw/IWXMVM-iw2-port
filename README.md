<div align="center">
  <br><br>
  <a href="https://github.com/reallyluckyy/IWXMVM">
    <img src="https://github.com/reallyluckyy/IWXMVM/assets/7430330/9463f12e-f919-49b9-b6ce-94037df9a181" width="50%">
  </a>
  <br><br>
</div>

## About

A port of [IWXMVM](https://github.com/reallyluckyy/IWXMVM) to **Call of Duty 2** (2005): a recording mod
featuring keyframeable campaths, a fully rewindable video editor-like timeline, a death animation picker, sun/fog/particles/DoF editor and built-in ProRes video capturing support.

This repository contains IWXMVM's game-agnostic core together with the CoD2 game module.

## Supported Game

| Game          | Notes |
| ------------- | ----- |
| Call of Duty 2 Multiplayer | v1.3 (with 1.4.6.8 [CoD2x](https://github.com/eyza-cod2/CoD2x)) only (`CoD2MP_s.exe`, hardcoded addresses, `.dm_1` demos). |

## Requirements

- Visual Studio 2022 (or newer)
- DirectX SDK Jun10 (to build) - sets `DXSDK_DIR`, providing `d3dx9.h`/`d3dx9.lib`
- DirectX End-User Runtime (Jun2010) (to run) - provides `d3dx9_43.dll`/`D3DCompiler_43.dll`, which neither Windows nor Call of Duty 2 ships; the mod will not load without it

## Building

First clone the repository:
```
git clone --recursive https://github.com/v0xw/IWXMVM-iw2-port.git
```
Then build the included solution file using Visual Studio.

## Running

Run the included launcher, then start Call of Duty 2 the way you normally do - the launcher waits for
`CoD2MP_s.exe` and injects `iw2.dll` once the game is up (it also works when the game is already running):

```
iw2launcher\bin\Win32\Release\iw2launcher.exe
```

Alternatively, inject `iw2.dll` (from `iw2\bin\Win32\Release\`) with the injector of your choice.
Nothing needs to be copied into the game installation: the DLL is injected straight from the build output.

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
