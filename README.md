# BUSMASTER-NG

![BUSMASTER-NG Overdrive](docs/social-preview.png)

[![License: LGPL v3](https://img.shields.io/github/license/teoCLMR/busmaster-ng)](LICENSE)
[![Last commit](https://img.shields.io/github/last-commit/teoCLMR/busmaster-ng)](https://github.com/teoCLMR/busmaster-ng/commits/master)
[![Issues](https://img.shields.io/github/issues/teoCLMR/busmaster-ng)](https://github.com/teoCLMR/busmaster-ng/issues)
![Platform: Windows](https://img.shields.io/badge/platform-Windows-0078D4)
![Version 3.5.0](https://img.shields.io/badge/version-3.5.0-2ecca0)
![Built with Visual Studio 2022](https://img.shields.io/badge/built%20with-Visual%20Studio%202022-5C2D91)
![Buses: CAN, LIN](https://img.shields.io/badge/buses-CAN%20%C2%B7%20LIN-0a5c5e)

**BUSMASTER-NG** (*Next Generation*, code name **Overdrive**) is a community-maintained fork of
[BUSMASTER](https://github.com/rbei-etas/busmaster), the open source tool for monitoring, analysing and
simulating CAN and LIN bus systems. (The original also had FlexRay support through ETAS BOA; that part is not
built in BUSMASTER-NG.)

The original project has not been updated since 2017 (last release 3.2.2). BUSMASTER-NG keeps it alive:
modern build toolchain, bug fixes and maintenance.

> BUSMASTER-NG is an independent community project. It is **not affiliated with, endorsed by or
> supported by ETAS GmbH or Robert Bosch GmbH**. BUSMASTER is the work of its original authors; their
> copyright notices are preserved in the source files.

## Download

Get the ready-to-use program from [**Releases**](https://github.com/teoCLMR/busmaster-ng/releases). From 3.4.2
every release has three files, with the same program inside:

| File | For | Notes |
|---|---|---|
| `BUSMASTER-NG_X.Y.Z_setup.exe` | Windows 7 SP1, 10, 11 | Installer, for all users or only for you. It adds the Microsoft runtimes that are missing (Visual C++ 2013, and on Windows 7 the Universal C runtime). |
| `BUSMASTER-NG_X.Y.Z_win32.zip` | Windows 10, 11 | Portable: extract the folder anywhere and start `BUSMASTER.exe`, no setup. |
| `BUSMASTER-NG_X.Y.Z_win32_win7.zip` | Windows 7 SP1 | Portable, with the Universal C runtime that Windows 7 lacks. |

The portable zips need the *Microsoft Visual C++ 2013 Redistributable (x86)*: most PCs already have it, and if it is
missing BUSMASTER-NG says so and offers to open the Microsoft download page. Every version also needs the driver of
your CAN / LIN interface.

User guide, in English and Italian: the [**wiki**](https://github.com/teoCLMR/busmaster-ng/wiki).
Project page: [teoclmr.github.io/busmaster-ng](https://teoclmr.github.io/busmaster-ng/).

What changed in each version, and what is coming in the next one: [**CHANGELOG.md**](CHANGELOG.md).
The release notes, change log and read-me of the original BUSMASTER up to v3.2.x are kept in
[docs/history](docs/history).

## Maintainer

- Developer name: **bigt92**
- GitHub: **teoCLMR**

## What is different from BUSMASTER 3.2.2

- Versions from **3.3.0**, product name BUSMASTER-NG, new splash screen, icon and installer artwork
  (the ETAS / Bosch logos are no longer used).
- Built with **Visual Studio 2022** (MSVC v143 toolset) and the Windows 10 SDK instead of Visual Studio 2013.
- Windows XP support removed.
- User data and settings are kept apart from earlier versions (folder and registry key named after the version),
  so BUSMASTER-NG can be installed next to BUSMASTER 3.2.2.
- The About dialog points to the original project and credits its authors.
- Configuration files (`.cfx`) of BUSMASTER 3.2.x open in BUSMASTER-NG (tested with real configurations).
- **DBC databases can be loaded directly**: *CAN → Load DBC* (or Ctrl+Shift+M) converts the DBC to a BUSMASTER
  database next to it and associates it, several files at once if you like. Configurations keep pointing to the
  `.dbf`, so they stay readable by BUSMASTER 3.2.x.
- **DBC ↔ DBF converters fixed**: numbers no longer depend on the language of Windows (on an Italian or German PC
  a factor of 0.5 became 0), factors and offsets keep all their digits (1e-7, -2105540607), 64-bit and Motorola
  signals convert correctly in both directions, and files that used to hang or crash the converter or come out
  empty now convert. Details and the upstream issues they close: [docs/dbc-support.md](docs/dbc-support.md).
- The **Format Converter** tool is built again (with Visual Studio 2022 it no longer compiled, so it was missing).
- An **installer** again (the original had one up to 3.2.2), next to two portable zips, and a clear message instead
  of "msvcr120.dll is missing" when the Visual C++ 2013 runtime is not installed.
- The release packages are built on **GitHub Actions** from the tagged sources, the **IXXAT VCI** driver included.
- The Message window and Signal Watch show the **value descriptions** of CAN signals again
  (`Position control` instead of `3`), lost since BUSMASTER 3.0.

## Known limitations

- The **Japanese** language resources are not maintained or tested in this fork. They were already out of date
  with the code in BUSMASTER 3.2.2 (for example the Japanese About dialog lacks controls that the code expects),
  and have not been brought in line with the NG branding.
- The **Windows 7** build is tested on a clean Windows 7 PC only for start-up and the Simulation driver so far.
- The images inside the help file (screenshots, logos) still come from the original BUSMASTER help.
- The database engine `DBManager.dll` is a closed binary of the original project. It keeps factors and offsets with
  about 7 significant digits, shows IEEE float/double signals as raw integers, refuses a database with CAN FD messages
  (more than 8 data bytes: on a CAN FD bus such frames are sent and received, but shown only as raw bytes) and shows every multiplexed signal whatever the multiplexer value. See
  [docs/dbc-support.md](docs/dbc-support.md).

## Requirements

- Windows 10 or 11, or **Windows 7 SP1** (start-up and Simulation driver tested). All release packages contain the
  *Windows 7 build* (see below); a build made with the newest Visual C++ 2022 toolset runs on Windows 10 or later
  only.
- On every Windows version the *Microsoft Visual C++ 2013 Redistributable (x86)* must be installed. The prebuilt
  components inherited from BUSMASTER (`libxml2.dll`, `DBManager.dll`, the PEAK driver `CAN_PEAK_USB.dll`) are
  built with that runtime. The installer adds it when it is missing; `BUSMASTER.exe` checks it at start-up and, if
  it is missing, says which package to install instead of the bare Windows error about `msvcr120.dll`.
- The C++ / MFC 14.38 runtime is delivered next to the executable. The Universal CRT is part of Windows 10 and
  later; for Windows 7 it is in the `_win7` zip and the installer copies it only there.
- The vendor driver of your CAN / LIN interface (PEAK, Vector, Kvaser, ETAS, ...), installed separately.

## Building

Install Visual Studio 2022 (Community is enough) with:

- the *Desktop development with C++* workload,
- *C++ MFC for latest v143 build tools (x86 & x64)*.

Then run `Sources\build.bat`, or open `Sources\Kernel\BusmasterKernel.sln` and
`Sources\BUSMASTER\BUSMASTER.sln` in Visual Studio and build the *Release* configuration.
The output is written to `Sources\BUSMASTER\BIN\Release`.

### Windows 7 build

The Visual Studio 2022 runtime from toolset 14.40 on needs Windows 8 or later (for example the C++ standard library
imports `GetSystemTimePreciseAsFileTime` and `CreateFile2`). For Windows 7 build with the older 14.38 toolset
(Visual Studio 2022 17.8), selected by a switch in `Sources\Directory.Build.props`.

Requirements on the build PC: the Visual Studio components *MSVC v143 ... (v14.38-17.8)* and the matching
*C++ ATL* and *C++ MFC* (v14.38) build tools.

1. Clean build with the switch (the toolset changes, so objects of the default build cannot be reused):

   ```
   MSBuild Sources\Kernel\BusmasterKernel.sln       /t:Rebuild /p:Configuration=Release /p:BusmasterWin7=true
   MSBuild Sources\BUSMASTER\BUSMASTER.sln          /t:Rebuild /p:Configuration=Release /p:BusmasterWin7=true
   ```

   `BusEmulation` (COM registration needs an administrator prompt) and `CAN_IXXAT_VCI` (vendor SDK) may fail; if
   `BUSMASTER.exe` was skipped because of that, build the project alone with
   `/t:Build /p:BuildProjectReferences=false /p:SolutionDir=<full path of Sources\BUSMASTER with a trailing />`.
   **Do the same for `CAN_STUB\CAN_STUB.vcxproj`** (the "Simulation" driver): it depends on `BusEmulation`, so the
   solution skips it after the clean has deleted `CAN_STUB.dll`. Without that DLL the application hangs at start-up
   with an empty trace window. Check that `BIN\Release\CAN_STUB.dll` exists before starting the program.
   A clean of the Application project deletes the tracked `BIN\Release\BUSMASTER.exe.manifest`; restore it with
   `git checkout -- Sources/BUSMASTER/BIN/Release/BUSMASTER.exe.manifest` before building the Application.

2. Assemble a self-contained folder (the output of step 1 is in `Sources\BUSMASTER\BIN\Release`).
   `Tools\make-release.ps1` does this step for you (see [Release packages](#release-packages)); by hand: copy that folder,
   without the intermediate files (`*.pch`, `*.ilk`, `*.pdb`, `*.exp`, `*.lib`), and add next to `BUSMASTER.exe`:
   - `msvcp140.dll`, `vcruntime140.dll`, `concrt140.dll` from `VC\Redist\MSVC\14.38.33130\x86\Microsoft.VC143.CRT`;
   - `mfc140.dll`, `mfc140u.dll`, `mfcm140.dll`, `mfcm140u.dll` from `...\Microsoft.VC143.MFC`;
   - all DLLs of `C:\Program Files (x86)\Windows Kits\10\Redist\ucrt\DLLs\x86` (the Universal CRT for
     application-local deployment, with `ucrtbase.dll` and the `api-ms-win-*` files).

3. On the Windows 7 PC install Windows 7 SP1 and the Visual C++ 2013 Redistributable (x86), then run the folder.

The result is checked with `dumpbin /imports`: none of the 36 compiled binaries, nor the 14.38 C++ / MFC runtime,
imports a function that is missing on Windows 7. On a clean Windows 7 PC the 3.4.1 package starts and the
Simulation driver connects and sends messages; the other functions are not tested there yet.
The default build and the Windows 7 build write to the same `BIN\Release` folder: do a clean build whenever you
switch between them.

Known build notes:

- `CAN_IXXAT_VCI` needs the SDK of the Ixxat VCI V4 driver (`vcinpl.h`): install the driver package
  *VCI V4 Windows 11, 10* from the [HMS general downloads](https://www.hms-networks.com/support/general-downloads)
  with its SDK, which goes to `C:\Program Files\HMS\Ixxat VCI\sdk`. Without it that driver is skipped.
- The post-build step of `BusEmulation` registers a COM server and needs an elevated (administrator) prompt.
- LDF Editor and LDF Viewer additionally need Qt 5.
- The PEAK driver (`CAN_PEAK_USB.dll`), `DBManager.dll`, `libxml2.dll` and the other vendor libraries in
  `BIN\Release` are prebuilt binaries inherited from BUSMASTER 3.2.2 (the PEAK driver has no sources in the
  repository), so they are not rebuilt with the new toolset.

### Release packages

The packages of a release are built by GitHub Actions ([release.yml](.github/workflows/release.yml)):

1. `Tools\set-version.ps1 -Version X.Y.Z` writes the version into the sources (version resource, About, logs,
   splash screen, README badge, bug report form).
2. The *Unreleased* section of [CHANGELOG.md](CHANGELOG.md) becomes `## X.Y.Z "Code name" - date`; commit both.
3. Push a tag `vX.Y.Z`. The workflow builds the Windows 7 build (toolset 14.38, IXXAT driver included), runs
   `make-release.ps1` and creates a **draft** release with the three packages and the CHANGELOG section as text.
   Check it on GitHub and publish it.

The workflow can also be started by hand (*Actions → Release → Run workflow*): it then only builds the packages, as an
artifact of the run. It installs on the runner the Visual Studio 14.38 components, WinLibs MinGW, Inno Setup, the
Visual C++ 2013 Redistributable to bundle and the SDK of the Ixxat VCI driver (from the HMS download link written in
the workflow: update it when HMS publishes a new VCI version).

To build the packages on your PC instead: build the Windows 7 build as above, then

```
Tools\make-release.ps1 -Version X.Y.Z -MinGW <MinGW folder> -Installer
```

writes to `release-out` the two zips, the installer (with [Inno Setup 6](https://jrsoftware.org/isinfo.php) and
`Tools\installer.iss`) and their SHA-256. The installer bundles the Visual C++ 2013 Redistributable from
`Tools\redist\vcredist2013_x86.exe` (not in the repository: download it from https://aka.ms/highdpimfc2013x86enu),
or from `-VCRedist2013 <file>`.

## Are the files signed?

No. BUSMASTER-NG is a free community project and has no code signing certificate yet, so the installer and the
programs are not signed. Windows may warn about them:

- **"Windows protected your PC"** (SmartScreen) when you start the installer or `BUSMASTER.exe`: click *More info*,
  then *Run anyway*. The warning becomes rarer as more people download the same release.
- Some browsers say the file "is not commonly downloaded": choose *Keep*.

To be sure a file is the one published on GitHub, compare its SHA-256 with the one shown next to the file on the
[release page](https://github.com/teoCLMR/busmaster-ng/releases/latest) (`sha256:...`). In PowerShell:

```
Get-FileHash .\BUSMASTER-NG_3.4.2_setup.exe
```

Download only from the GitHub release page: the packages are built by GitHub Actions from the tagged sources (see
[Release packages](#release-packages)).

The [Release workflow](.github/workflows/release.yml) can already sign with [SignPath](https://about.signpath.io/)
(configurations in [.signpath](.signpath)); it stays off until the project has a certificate.

Privacy: this program does not transfer any information to other networked systems unless the user asks for it. (The
network interfaces i-VIEW and ISOLAR-EVE talk only to the device or virtual ECU configured by the user.) No
telemetry, no update check.

## Hall of Madness

> *Every project needs a few stories. This one needs more than a few.*

BUSMASTER was last released in 2017. Everybody moved on. **One stubborn madman did not.** A short, honest list of
what it took to bring it back to life:

- **88 projects, one big jump.** From Visual Studio 2013 to Visual Studio 2022, with Windows XP left on the side of
  the road.
- **`About.ico` was an empty file.** Zero bytes, in the original repository. It has an icon now.
- **The help file refused to compile because of a single `%20`.** A help compiler from another era crashed with
  *"floating point not loaded"* and left half a file behind. The culprit was a space written as `%20` in the table
  of contents. Fixed, rebuilt, back online.
- **A 2011 MFC program at 200% DPI.** Tiny fonts, a Transmit window locked to exactly 860x655 pixels, tables that
  did not line up. Not anymore.
- **Windows 7, because someone still needs it.** The new C++ runtime imports two Windows 8 functions, so a second
  build with the older 14.38 toolset exists just for Windows 7 users. Checked with `dumpbin`, then on a real
  Windows 7 PC: it starts.
- **A close button that closed nothing, for 16 years.** The function behind the X of the Message Window was written
  in 2010, and left empty. Clicking it did exactly that: nothing. It hides the window now.
- **A factor of 0.0000001, saved as 0.** The database editor rounded small factors until they vanished, and cut every
  offset to 2 decimals. A signal could quietly turn into a constant.
- **DBC files that did not like Italians.** On Italian or German Windows the decimal comma of the system was used to
  read the DBC numbers, so 0.5 became 0. Now a dot is a dot everywhere.
- **`SetTimout`.** A J1939 function declared with a typo in 2012. A fix was offered in 2018 and never merged. It is
  `SetTimeout` now.
- **Our own contribution to the original project.** Issue 1338 of the abandoned repository, opened by us by mistake
  and titled, after a quick edit, *"Opened by mistake, please ignore"*. Nobody is perfect, madmen included.
- **The signature.** *Made with stubbornness by **bigt92** (teoCLMR). Yes, it was me.*

*Next madmen:* fix a bug, add a feature, test a driver on real hardware, and your name goes here.

### Thanks

- **[CLIMER](https://www.climergroup.com)**, for always backing my crazy ideas, this one included. BUSMASTER-NG is a
  community project, not a CLIMER product: please report problems here on GitHub.
- **Night Man (Davide Clivio)**, friend and partner.
- **Briz (Fabio Merenda)**, friend and partner.
- **Salvatore Torsello** and **Nicolò Campanini**, former collaborators.

### Italiano

> *Ogni progetto ha qualche storia da raccontare. Questo ne ha parecchie.*

BUSMASTER è stato rilasciato l'ultima volta nel 2017. Tutti sono passati ad altro. **Un testardo pazzo no.** Ecco,
senza abbellimenti, cosa è servito per riportarlo in vita:

- **88 progetti, un grande salto.** Da Visual Studio 2013 a Visual Studio 2022, con Windows XP lasciato sul ciglio
  della strada.
- **`About.ico` era un file vuoto.** Zero byte, nel repository originale. Ora ha un'icona.
- **La guida non voleva compilarsi per colpa di un solo `%20`.** Un compilatore di guide di un'altra epoca andava in
  crash con *"floating point not loaded"* lasciando un file a metà. Il colpevole era uno spazio scritto come `%20`
  nel sommario. Sistemato, ricompilata, di nuovo online.
- **Un programma MFC del 2011 a 200% di DPI.** Caratteri minuscoli, una Transmit window bloccata esattamente a
  860x655 pixel, tabelle non allineate. Non più.
- **Windows 7, perché c'è ancora chi lo usa.** Il nuovo runtime C++ importa due funzioni di Windows 8, quindi esiste
  una seconda build con il vecchio toolset 14.38 solo per chi usa Windows 7. Controllata con `dumpbin`, poi su un vero
  PC Windows 7: parte.
- **Un pulsante di chiusura che non chiudeva niente, per 16 anni.** La funzione dietro la X della Message Window è
  stata scritta nel 2010, e lasciata vuota. Cliccarla faceva esattamente quello: niente. Ora nasconde la finestra.
- **Un fattore di 0,0000001, salvato come 0.** L'editor dei database arrotondava i fattori piccoli fino a farli
  sparire, e tagliava ogni offset a 2 decimali. Un segnale poteva diventare una costante senza dire niente.
- **I DBC non andavano d'accordo con gli italiani.** Su Windows italiano o tedesco i numeri dei DBC venivano letti con
  la virgola decimale di sistema, e 0.5 diventava 0. Ora un punto è un punto ovunque.
- **`SetTimout`.** Una funzione J1939 dichiarata con un errore di battitura nel 2012. Una correzione è stata proposta
  nel 2018 e mai accettata. Ora si chiama `SetTimeout`.
- **Il nostro contributo al progetto originale.** La issue 1338 del repository abbandonato, aperta da noi per sbaglio
  e intitolata, dopo una rapida modifica, *"Opened by mistake, please ignore"*. Nessuno è perfetto, nemmeno i pazzi.
- **La firma.** *Fatto con testardaggine da **bigt92** (teoCLMR). Sì, sono stato io.*

*I prossimi pazzi:* correggi un bug, aggiungi una funzione, prova un driver su hardware vero, e il tuo nome finisce qui.

### Ringraziamenti

- **[CLIMER](https://www.climergroup.com)**, per aver sempre sostenuto le mie idee folli, questa compresa.
  BUSMASTER-NG è un progetto della community, non un prodotto CLIMER: segnala i problemi qui su GitHub.
- **Night Man (Davide Clivio)**, amico e socio.
- **Briz (Fabio Merenda)**, amico e socio.
- **Salvatore Torsello** e **Nicolò Campanini**, ex collaboratori.

## License

BUSMASTER-NG is distributed under the **GNU Lesser General Public License, version 3 or later**
(SPDX: `LGPL-3.0-or-later`), like the original project.

- `LICENSE` (identical to `COPYING.LESSER.txt`) is the text of the LGPL v3.
- `COPYING.txt` is the GNU GPL v3, whose terms the LGPL v3 builds on.
- The original copyright notices of ETAS GmbH and Robert Bosch Engineering and Business Solutions Limited are
  kept in the source files; the changes of BUSMASTER-NG are released under the same license.
- Third-party components keep their own licenses; see [Tools/third-party-notices.txt](Tools/third-party-notices.txt)
  (`Docs\THIRD-PARTY-NOTICES.txt` in the packages) and section 7 of `docs/history/Readme.txt`.
