"""Writes InstrumentsDemo.bminst, the demo instrument for InstrumentsDemo.dbc (python make_demo.py).

The signals are found by the name BUSMASTER gives the database: the name of the converted file without its dot,
"InstrumentsDemodbf", on CAN channel 1 (CHANNEL0).
"""
import os
from xml.sax.saxutils import quoteattr

DB = 'SIGNAL:CAN:CHANNEL0:InstrumentsDemodbf:'


def control(kind, name, x, y, w, h, **props):
    props = dict(Name=name, Left=x, Top=y, Width=w, Height=h, **props)
    lines = ['  <Control Type="%s">' % kind]
    for key, value in props.items():
        if isinstance(value, bool):
            value = 'true' if value else 'false'
        lines.append('    <Property Name=%s Value=%s/>' % (quoteattr(key), quoteattr(str(value))))
    lines.append('  </Control>')
    return '\n'.join(lines)


controls = [
    control('GroupBox', 'Dashboard', 8, 8, 424, 318, Text='Dashboard (received)'),
    control('GroupBox', 'Commands', 440, 8, 232, 318, Text='Commands (sent)'),
    control('AnalogGauge', 'Speed', 18, 30, 200, 200, Text='Speed', Variable=DB + 'EngineData:VehicleSpeed',
            Minimum=0, Maximum=250, MajorStep=50, MinorTicks=4, LowerRange=130, UpperRange=200),
    control('AnalogGauge', 'Rpm', 222, 30, 200, 200, Text='Engine', Variable=DB + 'EngineData:EngineSpeed',
            Minimum=0, Maximum=8000, MajorStep=1000, MinorTicks=1, LowerRange=5000, UpperRange=6500),
    control('Label', 'Coolant', 24, 240, 190, 22, Text='Coolant', Variable=DB + 'EngineData:CoolantTemp',
            Decimals=0),
    control('Label', 'ThrottleShown', 228, 240, 190, 22, Text='Throttle', Variable=DB + 'EngineData:Throttle',
            Decimals=1),
    control('Led', 'HeadLightLed', 24, 280, 130, 26, Text='Head light', Variable=DB + 'Lights:HeadLight',
            OnColor='#FFD800', OffColor='#505040'),
    control('Led', 'HazardLed', 160, 280, 130, 26, Text='Hazard', Variable=DB + 'Lights:Hazard',
            OnColor='#FF8000', OffColor='#504030'),
    control('Led', 'EngineLed', 296, 280, 130, 26, Text='Engine on', Variable=DB + 'Control:EngineOn'),
    control('Button', 'EngineButton', 456, 34, 200, 32, Text='Engine on / off', Variable=DB + 'Control:EngineOn',
            Mode='Toggle', PressedValue=1, ReleasedValue=0),
    control('Label', 'TargetLabel', 456, 78, 200, 20, Text='Target speed'),
    control('TrackBar', 'TargetSpeed', 452, 98, 208, 48, Variable=DB + 'Control:TargetSpeed', Minimum=0,
            Maximum=250, Step=5, TickStep=50),
    control('Label', 'RpmLabel', 456, 156, 110, 22, Text='Engine speed'),
    control('TextBox', 'RpmBox', 566, 156, 90, 24, Variable=DB + 'EngineData:EngineSpeed', Decimals=0),
    control('Label', 'ThrottleLabel', 456, 190, 110, 22, Text='Throttle (%)'),
    control('NumericUpDown', 'Throttle', 566, 190, 90, 24, Variable=DB + 'EngineData:Throttle', Minimum=0,
            Maximum=100, Step=5),
    control('CheckBox', 'HeadLight', 456, 228, 200, 24, Text='Head light', Variable=DB + 'Lights:HeadLight'),
    control('CheckBox', 'Hazard', 456, 256, 200, 24, Text='Hazard', Variable=DB + 'Lights:Hazard'),
    control('Button', 'Horn', 456, 288, 200, 28, Text='Hazard while held', Variable=DB + 'Lights:Hazard',
            Mode='Momentary', PressedValue=1, ReleasedValue=0),
]

text = '\n'.join([
    '<?xml version="1.0" encoding="UTF-8"?>',
    '<BusmasterInstrument version="1">',
    '  <Panel Title="Instruments demo" Width="680" Height="334" BackColor="#F0F0F0"/>',
] + controls + ['</BusmasterInstrument>', ''])
path = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'InstrumentsDemo.bminst')
with open(path, 'w', encoding='utf-8', newline='\n') as handle:
    handle.write(text)
print('written', path)
