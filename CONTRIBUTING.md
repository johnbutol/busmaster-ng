# Contributing to BUSMASTER-NG

Thank you for helping to keep BUSMASTER alive. Bug reports, test results on real hardware, fixes and small
improvements are all welcome. *(Italiano: vedi in fondo.)*

BUSMASTER-NG is a community fork maintained by **bigt92** (GitHub: **teoCLMR**) in his spare time, so answers
may take a while. It is **not affiliated with ETAS GmbH or Robert Bosch GmbH**.

Everyone taking part follows the [Code of Conduct](CODE_OF_CONDUCT.md). Security problems are reported privately,
as described in the [security policy](SECURITY.md).

## Reporting a problem

Open an issue and use the *Bug report* form. The more of these you can tell us, the faster it gets fixed:

- the **version** (*Help -> About*, for example `Ver 3.3.0`) and whether it is the standard or the Windows 7 build;
- **Windows version** and the **display scale** (100%, 150%, 200%, ...): this fork is DPI aware and most visual
  problems depend on it;
- the **interface and driver** you use (PEAK, Vector, Kvaser, ETAS, ... or the *Simulation* driver) and its version;
- what you did, what you expected and what happened. A screenshot helps a lot;
- **whether it also happens in BUSMASTER 3.2.2.** If it does, it is an old bug of the original project, which is
  still good to know and fix;
- if the program hangs or crashes: the entry of the *Application Error* / *Application Hang* event in the Windows
  Event Viewer and, if there is one, the dump file the program writes (look for a `DumpFiles` folder next to the
  executable).

Please do not attach files that contain confidential data (logs, databases, configurations of customers).

## Changing the code

1. Fork the repository and create a branch from `master`.
2. Build it as described in the [README](README.md#building) (Visual Studio 2022 with C++ and MFC). If you touch
   something that is also part of the Windows 7 build, see *Windows 7 build* in the README.
3. Keep your change **small and focused**: one problem per pull request. Describe what was wrong, what you changed
   and how you tested it (which Windows, which display scale, which hardware or the *Simulation* driver).
4. Respect the existing style of the file you edit. The project uses spaces (the *astyle* profile of the original
   project, `Sources/BUSMASTER/astyle.txt`, uses the ANSI brace style and converts tabs) and, in most files,
   **CRLF line endings**: do not convert whole files.
5. **Do not remove the original copyright notices** of ETAS GmbH and Robert Bosch Engineering and Business
   Solutions Limited from the source files, and keep new files under the same license.
6. Do not commit build output (`Release`, `Debug`, `.obj`, `.pch`, ...). The `.gitignore` covers the usual ones,
   including `BUSMASTER.tlb`, which the build generates.

Things we are especially glad to receive:

- **tests on real hardware** and with other Windows versions, in particular Windows 7;
- fixes for the **bugs inherited from BUSMASTER 3.2.2** (hangs and crashes, fixed-size windows, labels cut at high
  display scale, ...);
- updated **drivers and libraries** of the interface vendors, and translations.

## Keeping the changelog up to date

Every change that users notice gets a line in [CHANGELOG.md](CHANGELOG.md), under **Unreleased**, in the same
commit or pull request: what changed for the user, with the issue numbers. Mark it *(not tested yet)* until the test
listed in its issue is done. At a release the *Unreleased* section becomes the new version (with its date) and its
text is used for the release page on GitHub. Do not edit the files in `docs/history`: they are the history
of the original BUSMASTER.

## Keeping the wiki up to date

The [wiki](https://github.com/teoCLMR/busmaster-ng/wiki) is the user guide, in **English and Italian**. When a change
alters what users see or do (a new command, a different message, a new requirement or limit), update the related
wiki pages **in both languages** in the same release. Each English page links to its Italian twin at the top; keep
their content aligned. Menu and command names stay in English, as in the program.

## License of your contribution

BUSMASTER-NG is released under the **GNU Lesser General Public License v3 or later** (see [LICENSE](LICENSE)).
By sending a contribution you agree that it is released under the same license.

## Italiano

Grazie per l'aiuto! Puoi scrivere segnalazioni e pull request **anche in italiano**.

- **Segnalazioni**: usa il modulo *Bug report* e indica versione (*Help -> About*), versione di Windows e
  **scala dello schermo**, interfaccia e driver usati, i passi per riprodurre il problema e, se puoi, se succede
  anche in BUSMASTER 3.2.2.
- **Codice**: fai un fork, parti da `master`, tieni la modifica piccola e descrivi come l'hai provata. Mantieni lo
  stile del file (spesso con fine riga CRLF) e **non togliere le note di copyright originali** di ETAS e Bosch.
- **Changelog**: ogni modifica che l'utente nota va in [CHANGELOG.md](CHANGELOG.md), sotto *Unreleased*, nello
  stesso commit; finché non è provata si segna *(not tested yet)*. I file in `docs/history` sono la
  storia del BUSMASTER originale e non si modificano.
- **Wiki**: è la guida per gli utenti, in inglese e in italiano. Se una modifica cambia ciò che l'utente vede o fa,
  aggiorna le pagine del wiki collegate **in entrambe le lingue**, nella stessa release.
- **Licenza**: i contributi sono rilasciati sotto la LGPL v3 o successiva, come il resto del progetto.
