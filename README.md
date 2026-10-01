# DLSS 5 Setup Helper

Source code for the installer published on Nexus Mods as
[DLSS 5 Setup Helper](https://www.nexusmods.com/site/mods/2224) *(Universal Tools)*.

It installs DLSS 5 Neural Rendering into single-player games: it checks whether a
game already has DLSS and sets up either OptiScaler DLSS-NR, or ReShade with
DLSS5-Feeder and the RenoDX add-on. It finds games on Steam, Epic and Xbox, or
you can point it at any folder. It refuses to install where it finds anti-cheat.
One-click Restore puts the game back as it was.

Free and non-commercial. No income of any kind is taken from it.

## Why this repository exists

The release archive contains an unsigned executable and a nested archive, so
automated safety checks flag it. This repository exists so anyone - including
Nexus Mods staff - can read the code and build it themselves instead of trusting
a binary.

## What is here

| File | What it is |
|---|---|
| `main.cpp` | The whole program. Single file, plain Win32 C++, no frameworks. |
| `strings_en.tsv` | String table used to produce the English build from the Korean source. |
| `make_en.ps1` | Substitutes the strings and writes `main_en.cpp`. |
| `build.bat` | Builds the Korean executable. |
| `build_en.bat` | Runs `make_en.ps1`, then builds the English executable. |
| `app.rc`, `app.ico` | Application icon. |
| `DLSS5_Setup.exe.manifest` | Requests administrator rights. |

The release archive also contains a `data\` folder of third-party files
(OptiScaler, ReShade, RenoDX, DLSS5-Feeder, NVIDIA). Those are **not** in this
repository - they belong to their own authors and are listed with their sources
and licences on the Nexus mod page.

## How to build

Requires Visual Studio with the C++ desktop workload (the build scripts call
`vcvars64.bat`; edit the path in the `.bat` files if your install differs).

Korean build:

```
build.bat
```

English build:

```
build_en.bat
```

Each prints `EXITCODE=0` on success and produces `DLSS5_Setup.exe` or
`DLSS5_Setup_EN.exe`. The compile line is plain `cl` with `/O2 /MT`, linking only
against Windows system libraries:

```
user32 gdi32 comctl32 shell32 ole32 urlmon advapi32 shlwapi comdlg32 version
```

There are no external dependencies and nothing is downloaded at build time.

A built executable needs the release `data\` folder beside it to do anything; on
its own it will report that its files are missing.

## What the program does to your system

- Writes dll and settings files into the folder that holds the game executable,
  and records what it wrote to `_DLSS5_installed.txt` there.
- Backs up anything it replaces to `_DLSS5_backup\` in the same folder.
- Writes `DLSS5_log.txt` next to itself, and
  `%LOCALAPPDATA%\DLSS5Setup\neural_path.txt`.
- Reads the registry for Steam, Epic, Xbox and Ubisoft install paths and for
  graphics driver information. It does not write to the registry.
- Needs administrator rights because game folders are usually under
  `C:\Program Files`.

## Credits

The installer bundles work by other authors. Full attribution, versions, sources
and licences are on the Nexus mod page.

- OptiScaler - Dagherbou, cdozdil and OptiScaler contributors
- RenoDX - clshortfuse and RenoDX contributors
- DLSS5-Feeder - Jean-Laurent ROUZIES
- ReShade - crosire
- reshade-shaders - the individual shader authors
- NVIDIA - DLSS and the neural rendering model

Permission to bundle the RenoDX add-on and the NVIDIA files was given by
velasquez3589 on 18 September 2026.
