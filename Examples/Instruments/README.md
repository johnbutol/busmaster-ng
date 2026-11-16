# Instruments example

- `InstrumentsDemo.dbc`: three messages, `EngineData` (0x100), `Lights` (0x200) and `Control` (0x300).
- `InstrumentsDemo.bminst`: an instrument for it, two gauges, LEDs and labels that show the signals, and a button,
  a track bar, a text box, a numeric up down and check boxes that send them.
- `make_demo.py`: writes `InstrumentsDemo.bminst` again (`python make_demo.py`).

To try it:

1. *CAN → Load DBC*: `InstrumentsDemo.dbc`. It is converted to `InstrumentsDemo.dbf` next to it and associated on
   channel 1.
2. *Instruments → Import*: `InstrumentsDemo.bminst`.
3. Select the *Simulation* driver and *Connect*: the frames sent by the controls on the right come back to the gauges
   and LEDs on the left.

The instrument finds its signals by the name BUSMASTER gives the database, `InstrumentsDemodbf`: keep the name of
the DBC. See [docs/instruments.md](../../docs/instruments.md).
