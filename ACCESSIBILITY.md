# Accessibility

BUSMASTER-NG wants to be usable by everyone who works with CAN and LIN buses. It is a Windows desktop program
(MFC) inherited from BUSMASTER 3.2.2, so its accessibility is mostly what Windows controls give, and we improve it
step by step.

## What works

- **Display scale**: the program is DPI aware and sharp at 125-200% scale, so text can be made bigger with the
  Windows setting *Display -> Scale*. Windows that were fixed in size in BUSMASTER 3.2.2 are being made resizable.
- **Keyboard**: menus, ribbon and dialogs can be used with the keyboard (Alt, Tab, arrow keys), and the main
  commands have shortcuts, for example Ctrl+Shift+M for *Load DBC*.
- **Documentation**: the user guide in the [wiki](https://github.com/teoCLMR/busmaster-ng/wiki), in English and
  Italian, is plain text and works with screen readers and browser zoom. So does the
  [project page](https://teoclmr.github.io/busmaster-ng/).

## Known limitations

- Not tested with **screen readers** (Narrator, NVDA, JAWS). Standard controls are probably read; the message
  window, graphs and other custom-drawn views probably are not.
- Some views use **colour** to show state (for example errors or changed values) without another sign.
- **High contrast** themes are not tested; some icons and custom-drawn views may be hard to see.
- The built-in help still has images of the original BUSMASTER, without text alternatives.

## Reporting a barrier

If something stops you from using BUSMASTER-NG, please
[open an issue](https://github.com/teoCLMR/busmaster-ng/issues/new/choose) (English or Italian) and tell us what
you were trying to do, which assistive technology you use and your Windows version. Accessibility problems are
treated as bugs.
