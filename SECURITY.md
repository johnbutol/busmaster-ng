# Security policy

## Supported versions

Only the **latest release** of BUSMASTER-NG gets fixes, security fixes included. Please update before reporting a
problem. BUSMASTER 3.2.2 and earlier versions of the original project (rbei-etas/busmaster) are not maintained.

## Reporting a vulnerability

Please **do not open a public issue** for a security problem. Report it privately with the
[Report a vulnerability](https://github.com/teoCLMR/busmaster-ng/security/advisories/new) form of GitHub
(*Security* tab of the repository). Only the maintainer can read it. English or Italian.

Tell us the version (*Help -> About*), the Windows version and how to reproduce the problem: for example a
crafted configuration, database, log or DBC file that makes the program crash or run code.

BUSMASTER-NG is maintained in spare time: we try to answer within two weeks. When a fix is released, the
advisory is published and you are credited, unless you prefer not to be.

## Scope

BUSMASTER-NG is a desktop program that reads files and talks to bus interfaces; it opens no network service of its
own. Problems in the drivers and libraries of the interface vendors (PEAK, Vector, Kvaser, ...) belong to their
vendors; we will update the bundled copies when they publish a fix.

---

*Italiano:* non aprire issue pubbliche per problemi di sicurezza: usa il modulo privato
[Report a vulnerability](https://github.com/teoCLMR/busmaster-ng/security/advisories/new). Viene corretta solo
l'ultima versione.
