# Changelog of BUSMASTER-NG

All user-visible changes of BUSMASTER-NG, newest first. Numbers like #2 are issues of
[teoCLMR/busmaster-ng](https://github.com/teoCLMR/busmaster-ng/issues); "original issue 1213" refers to
[rbei-etas/busmaster](https://github.com/rbei-etas/busmaster/issues).

The history of the original BUSMASTER up to v3.2.x is in [docs/history](docs/history) (ETAS, not changed).

## 3.7.6 "Overdrive" - 2026-11-19

Bug fix release: IXXAT interfaces with the VCI V4 driver, IXXAT time stamps, and configurations of three drivers that
opened with the Simulation driver.

**Note on updating**
- Window positions and the last configuration are stored per version, so after updating from 3.7.5 load your
  configuration once (*File → Load*).

**Configuration**
- A configuration saved with the *VScom CAN-API*, *i-VIEW* or *ETAS ISOLAR-EVE* driver opened with the Simulation
  driver, because these three names were not recognised when it was loaded. It now opens with its own driver.

**Drivers**
- IXXAT VCI: the driver works with the VCI V4 driver of IXXAT / HMS. It refused it with "Driver selection failed. The
  file CAN_IXXAT_VCI.dll loads, but the driver could not be started ...", because it accepted only version 3 of the
  VCI library, and V4 reports version 4 (#16, reported by robomaniac; the same fix as in pull request 1189 of IXXAT for
  the original project) *(not tested with an IXXAT interface; the version check is tested against the installed VCI
  V4 driver)*.
- IXXAT VCI: the time stamps of the messages ran slow by up to some percent, because the length of a timer tick was
  cut to whole nanoseconds (27 instead of 27.78 ns with a 36 MHz clock); the error grew with the time since connect.
  The time is now computed without rounding (from original pull request 1189) *(not tested with an IXXAT interface)*.

## 3.7.5 "Overdrive" - 2026-11-14

Feature release: a new open source driver for the PEAK-System interfaces, on the PCAN-Basic API.

**Note on updating**
- Window positions and the last configuration are stored per version, so after updating from 3.6.0 load your
  configuration once (*File → Load*).

**Drivers**
- New CAN driver *PEAK PCAN-Basic* for the PEAK-System interfaces (USB, PCI, PC Card, LAN), with open source code on
  the PCAN-Basic API that comes with the PEAK device driver. It shows every PEAK channel of the PC in the hardware
  selection, with the speeds of the drop-down list and the PEAK special ones (33.3, 47.6, 83.3, 95.2 kbit/s). Under
  *Advanced* a channel can be put in listen-only mode: it only receives, without acknowledge or error frames, to watch
  a bus. A channel open in PCAN-View can be connected as well. Received messages carry the time stamp of the interface.
  If a channel cannot be started, the reason given by PEAK (for example the channel is used by another program) is
  written in the trace window. The hardware list is read in a moment and shows the firmware of each interface.
- The old PEAK driver (closed code from 2017) is now called *PEAK USB (Legacy)* and works as before; configurations
  saved with *PEAK USB* still open with it.
- The decoding of the error frames of the new driver (type of error and counters) is not tested yet: it needs a PEAK
  interface on a bus with errors.

## 3.6.0 "Overdrive" - 2026-11-14

Feature release: a Frequency (Hz) column in the Message and Transmit windows, a message list that can be sorted while
the bus is connected, messages that stop arriving shown in grey, repetition times that follow the clock, and updates
installed from inside BUSMASTER-NG.

**Note on updating**
- Window positions and the last configuration are stored per version, so after updating from 3.5.0 load your
  configuration once (*File → Load*).

**Message Window**
- With the Simulation driver the *Time* column could show the wrong time in *System Time* (the time since the PC was
  started instead of the time of day) after the first connect (#13). The reference time is now asked to the simulation
  engine when the connection is made, and a driver that does not provide it no longer leaves random values *(not
  tested yet)*.
- New *Frequency (Hz)* column (CAN): how often the messages with the same ID, channel and direction arrive, from the
  time between them, averaged over the last messages ("-" for the first one). In append mode each row keeps the value
  it had when it arrived. After the *Data Byte(s)* column, so a saved configuration keeps its columns; drag it where
  you like.
- Simulation driver: the time stamp of a message is taken when it is sent and not when the simulation engine hands it
  to the clients. Messages sent every millisecond used to show intervals such as 1 ms and then 20 ms, because the
  engine delivers them one at a time and a pause there bunched them together (the number sent per second was right);
  *Relative Time* and *Frequency (Hz)* are now regular. Interfaces with real hardware are not affected, they time
  stamp in the device.
- CAN: the list can be sorted while the bus is connected (it only worked when disconnected), and in the overwrite mode
  it stays sorted: click the header of *ID* (or *Message*, *Channel*, *Time*, *Tx/Rx*, *Msg Type*, *DLC*) once for
  ascending and again for descending, and an ID that appears later goes to its place. Rows that are equal in the
  column keep a fixed order (by ID, channel, direction). The sorting stays when the bus is connected again. In the
  append mode, which is a trace in order of arrival, it still sorts only the messages already there, when disconnected.
- CAN, LIN: the *Data Byte(s)* column opens narrower, enough for 8 bytes (its width came from a title padded with spaces;
  a width saved in a configuration is kept).
- CAN, overwrite mode: a message that stops arriving turns grey and its *Frequency (Hz)* shows "-", when nothing came
  for three times its period (at least 100 ms); it goes back to normal when it comes again. A message seen only once
  is never greyed, and on disconnecting the rows stay as they were.
- CAN: the *Frequency (Hz)* column can be sorted too (in the overwrite mode the list is sorted when the header is
  clicked and when an ID appears, the frequencies that change afterwards do not move the rows).
- Connecting with messages already set to repeat in the Transmit window sometimes failed with "Failed to start the
  hardware" (seen with Simulation): the Transmit window started sending before the hardware was started, kept the
  driver busy and the start gave up. The Transmit window now starts once the hardware has started, with every driver
  (with a real interface the first messages used to be sent to a controller not started yet), and the Simulation
  driver also waits its turn instead of giving up.
- CAN, sorted list (overwrite mode): the error frames kept their row after a sort (their row could take the place of a
  message, or show another message's data); the *Frequency (Hz)* sort is made with the frequencies held still (they
  changed during the sort and could leave it unsorted); *Clear* while connected no longer leaves rows pointing at the
  wrong message *(the error frames are not tested yet, they need a real interface)*.

**Transmit window**
- Messages sent with a repetition time (CAN and LIN) go out at the time you set: a message every 10 ms went out every
  20 ms or less often, because the repetition was counted in wake-ups of the transmit thread (a wait of 1 ms lasts one
  or two ticks of the system timer) and not in real time. It now follows the clock.
- The shortest repetition time depends on Windows: **1 ms (1000 Hz) on Windows 10 1803 or later and Windows 11**, which
  have a high resolution timer, **2 ms (500 Hz) on Windows 7** and earlier Windows 10, where a wait of 1 ms lasts 1 to
  2 ms. A shorter value is raised to the minimum, and the repetition time and the frequency shown in the list tell it.
  On the fast timer a message that is late goes out again for the periods it missed (up to three in a row), so the
  average rate holds.
- New *Frequency (Hz)* column next to *Repetition (ms)* (CAN and LIN): type the repetition time and the frequency is
  calculated, type the frequency (a comma is accepted as decimal separator) and the repetition time is calculated, in
  the range 1 ms to 60 s.

**Updates**
- *Help → Check for Updates* installs a newer version from inside BUSMASTER-NG: *Install now* downloads the
  installer (with a progress window and *Cancel*), checks it against the SHA-256 that GitHub gives for it, closes
  BUSMASTER-NG (with the usual question about saving the configuration) and installs the update without the wizard;
  BUSMASTER-NG starts again when it is done. Windows asks for administrator permission when BUSMASTER-NG is
  installed for all users, not when it is installed only for you. Portable copies (zip) still get the release page.
- *Help → Check at Startup* (off unless you turn it on): checks for a newer version every time BUSMASTER-NG starts,
  in the background, and speaks up only when there is one.

## 3.5.0 "Overdrive" - 2026-11-10

Feature release: a Hardware Selection dialog that is easier to use at high display scale, a baud rate list that
remembers the last speed, and a button to check for a new version.

**Hardware selection** (ETAS BOA, IntrepidCS neoVI, i-VIEW, IXXAT, Kvaser, MHS, NSI, Vector and VScom drivers)
- The columns of the lists scale with the display, so *Channel 1* is no longer cut off at high display scale.
- The baud rate is a drop-down list of the usual speeds (10 kbit/s to 1 Mbit/s); another value can still be typed.
  The last baud rate used with each interface is remembered for the next time, also when the configuration is not
  saved: a channel that is still at the default 500 kbit/s starts with it, and so does a channel you add to the
  list. Any other speed already set, for example by a loaded configuration, is kept.
- Tested with the VScom driver; the dialog is shared by the other drivers, which were not tried one by one.

**Help**
- *Help → Check for Updates* asks GitHub for the latest release and, when it is newer than the running version,
  opens its download page in the browser. Nothing is downloaded or installed by BUSMASTER-NG, and GitHub is
  contacted only when you click the button.

**Package**
- Every executable and DLL built from the BUSMASTER-NG sources says product *BUSMASTER-NG* and the version of the
  release in its file properties (*Properties → Details*); many still had the placeholders of the original project
  ("TODO: <Product name>", version 1.0.0.1) or no version at all. Needed for code signing.

**Note on updating**
- Window positions and the last configuration are stored per version, so after updating from 3.4.2 load your
  configuration once (*File → Load*). The remembered baud rates are not part of it and are kept.

## 3.4.2 "Overdrive" - 2026-10-30

Installer and packaging release: the program is the same as 3.4.1, with a clearer start-up message and the IXXAT
driver included.

**Downloads**
- An **installer** again, `BUSMASTER-NG_3.4.2_setup.exe`, for Windows 7 SP1, 10 and 11: for all users or only for
  you, with Start menu entries (BUSMASTER-NG, Format Converter, read-me), an optional desktop icon and an uninstaller.
  It installs the Visual C++ 2013 runtime when it is missing and, on Windows 7 only, the Universal C runtime.
- Two **portable zips** instead of one, with the same program: `_win32.zip` for Windows 10 / 11 (lighter: these
  versions of Windows already have the Universal C runtime) and `_win32_win7.zip` for Windows 7 SP1.
- In the zips the licenses and the change log are in a `Docs` folder, so the main folder is tidier. `Docs` also
  contains `THIRD-PARTY-NOTICES.txt` with the licenses of the bundled components (libxml2, zlib, DMGraph, ...) and the
  list of interface vendor libraries; build by-products (`.map` files) are no longer included.
- The **IXXAT VCI** driver (`CAN_IXXAT_VCI.dll`) is in the package; it was missing because it needs the SDK of the
  Ixxat VCI driver to be built. Not tested with real IXXAT hardware yet.

**Start-up**
- If the *Microsoft Visual C++ 2013 Redistributable (x86)* is not installed, BUSMASTER-NG says so, in Italian or
  English, and offers to open the Microsoft download page, instead of the bare Windows error "msvcr120.dll is
  missing".

**Note on updating**
- Window positions and the last configuration are stored per version, so after updating from 3.4.1 load your
  configuration once (*File → Load*).

**Project**
- The release packages are built by GitHub Actions from the tagged sources (see the README).

## 3.4.1 "Overdrive" - 2026-10-29

Bug fix release. Update recommended for everyone who uses the Simulation driver.

**Reliability**
- The program no longer hangs on the splash screen with the Simulation driver on a PC where BUSMASTER was never
  installed: the simulation engine `BusEmulation.exe` is registered for the current user when needed (no
  administrator rights), and if it still cannot be started a message says why (#7).

**Configuration**
- No "Do you want to save?" on exit when no configuration file is loaded (first start) and nothing was changed
  (#3).

**User interface**
- The main window no longer opens with its title bar above the top edge of the screen: it opens maximized the
  first time, then as it was when closed, and a restored window always lies inside a monitor (#8).
- The close button (X) of the Message Window hides it, like the *Message Window* button of the ribbon (#9).
- Message Window: the columns open wide enough for their titles at any display scale (they were measured with the
  wrong font and showed "Ti...", "Ch...").

**Note on updating**
- Window positions and the last configuration are stored per version, so after updating from 3.4.0 load your
  configuration once (*File → Load*).

## 3.4.0 "Overdrive" - 2026-10-28

Changes marked *(not tested yet)* are built but still waiting for the test listed in their issue; test reports are
welcome.

**Databases**
- Message window and Signal Watch show the value descriptions of CAN signals again ("Position control" instead of
  3) (#1).
- CAN database editor (#2):
  - factor and offset are kept with full precision: a factor such as 0.0000001 is no longer saved as 0, and
    editing a signal no longer cuts the offset to 2 decimals (original issues 932, 1248, 1308);
  - Signal Details: physical minimum and maximum can be typed with decimals; the raw range stored in the database
    follows (1296, 215);
  - no false "Maximum value cannot be less than minimum value" for unsigned hexadecimal values (1213);
  - the position of the signal (LSB, MSB and the start bit of a DBC file) is shown, and the Motorola error message
    explains the convention (1315);
  - Save As keeps a path with a dot in a folder name (1210);
  - the editor opens big enough, the message tree is visible and the window can be resized (1337).

**Node simulation**
- Works with a current MinGW (tested with WinLibs GCC 16.2.0, 32 bit); BUSMASTER-NG finds a `MinGW` folder next to
  `BUSMASTER.exe` and says clearly what is missing (#4). See the wiki page *Node simulation*.
- J1939 nodes: `SetTimeout` is declared with its right name (it was `SetTimout`), and the include guard of
  `Common.h` is fixed (original pull request 1136 by jogo-).

**Configuration**
- Open and Save dialogs remember the last folder for each kind of file (configuration, DBC, database, log, nodes,
  ...) instead of one folder for everything *(not tested yet, #3)*.
- The LIN driver and the visibility of the status bar are saved in the configuration (original issues 724, 1020,
  546) *(not tested yet, #3)*.
- No "Do you want to save?" on exit when nothing was changed (1004) *(not tested yet, #3)*.

**User interface**
- Replay window and node simulation output: readable font and size at high display scale.
- Start-up message boxes say BUSMASTER-NG.
- The round application button of the ribbon shows the BUSMASTER-NG logo, sharp at every display scale, instead of
  the old BUSMASTER bitmap.

**Replay**
- Log files are played back at their recorded speed: delays below 1 ms are added up instead of being rounded up
  to 1 ms each (original pull request 768 by PBjore, issue 1270); in cyclic replay with the recorded delays each
  message now waits its own delay, not the one of the previous message *(not tested yet, #5)*.

**Test automation**
- The Test Suite Executor sets each signal by name, not by position in the database, so the signals of a test
  setup are sent with the right values (original pull request 1116 by itiserik) *(not tested yet, #5)*.

**Diagnostics**
- The UDS window also shows the response as ASCII text (original pull request 1300 by saerpa) *(not tested yet,
  needs an ECU)*.

**Drivers**
- The Ixxat driver is built again (Ixxat VCI V4 SDK); not tested with an Ixxat interface.
- NSI: the driver is restored from the configuration, receives standard and extended frames, and connecting or
  disconnecting twice no longer fails (original pull request 1137 by MelissaA74, issue 1072); the project no longer
  needs a Kvaser library (pull requests 1143 and 1145 by jogo-) *(not tested yet, needs an NSI interface)*.
- PEAK PLIN-USB with one LIN channel works ("Listing hardware interfaces failed" before; original issue 1188, fix by
  janw-cz) *(not tested yet, needs a PLIN-USB)*.

**Package**
- The zip contains the license texts, the node simulation files and this changelog.

**Note on updating**
- Window positions and the last configuration are stored per version, so after updating from 3.3.0 load your
  configuration once (*File → Load*).

## 3.3.0 "Overdrive" - 2026-10-26

First release of BUSMASTER-NG, the community fork of BUSMASTER 3.2.2.

**DBC support**
- **Load DBC** (CAN tab, or Ctrl+Shift+M): DBC databases are converted and associated directly, several at once
  (original issue 586).
- DBC ↔ DBF converters fixed: decimal numbers on Italian / German Windows, large offsets and very small factors
  kept exactly, Motorola and 64-bit signals, value descriptions, files that hung or crashed the converter or came
  out empty. 26 issues of the original project fixed, partly fixed or explained: see
  [docs/dbc-support.md](docs/dbc-support.md).
- The **Format Converter** tool is back (it no longer compiled).

**User interface**
- Sharp display at 125–200% scale (DPI aware): splash, icons, message window, Transmit window.
- Transmit window: resizable with controls that follow it, readable check boxes, negative physical values.
- Dissociate Database: resizable, long paths readable.
- New splash screen, icon and About dialog.

**Reliability**
- A missing or damaged driver DLL no longer makes the program hang at start-up: a message says which file is
  missing and why.
- The trace window shows the right text.

**Under the hood**
- Built with Visual Studio 2022; a Windows 7 compatible toolset is used for the package.
- Configurations (`.cfx`) of BUSMASTER 3.2.x open unchanged.

**Known limitations**
- The database engine `DBManager.dll` is a closed binary of the original project: factor/offset with about 7
  significant digits, IEEE float signals shown as raw values, no CAN FD messages in databases, multiplexed signals
  always all shown.
- Japanese resources are not maintained; the help still has images of the original BUSMASTER.
