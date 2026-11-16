/*
 * BUSMASTER-NG Instruments: the model of an instrument (a panel of controls) and its file.
 *
 * An instrument is a panel with controls (gauge, button, LED...). Each control has a type, a place and a list of
 * properties, kept as text (name -> value). The properties each type has are described by PropDef lists, which give
 * the property window of the editor and the defaults. A control can be associated with a signal through a variable
 * path of the variable layer of BUSMASTER: SIGNAL:<bus>:CHANNEL<n>:<database>:<message>:<signal>.
 *
 * File (XML, extension .bminst):
 *   <BusmasterInstrument version="1">
 *     <Panel Title="Engine" Width="400" Height="300" BackColor="#F0F0F0"/>
 *     <Control Type="AnalogGauge">
 *       <Property Name="Name" Value="Gauge1"/> ...
 *     </Control>
 *   </BusmasterInstrument>
 */
#pragma once

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace Instruments
{
enum class ControlType
{
    AnalogGauge,
    Button,
    TextBox,
    CheckBox,
    GroupBox,
    Label,
    Led,
    NumericUpDown,
    Picture,
    TrackBar,
    Count
};

enum class PropKind
{
    Text,
    Integer,
    Number,
    Bool,
    Color,          // "#RRGGBB", or "" for none (transparent / default)
    Choice,         // one of PropDef::mChoices
    Variable,       // a variable path, chosen in the signal selection dialog
    Image,          // an image embedded in the file (base64)
};

struct PropDef
{
    const char* mName;          // key in the file
    const char* mCategory;      // group in the property window
    PropKind mKind;
    const char* mDefault;
    const char* mDescription;
    std::vector<std::string> mChoices;
};

// The name used in files and the name shown to the user
const char* TypeKey(ControlType type);
const char* TypeTitle(ControlType type);
bool TypeFromKey(const std::string& key, ControlType& type);

// The properties of a type, in the order of the property window (position and size first)
const std::vector<PropDef>& PropDefs(ControlType type);
const PropDef* FindPropDef(ControlType type, const std::string& name);
const std::vector<PropDef>& PanelPropDefs();

// Controls that send a value when the user acts on them; the others only show values
bool TypeSends(ControlType type);
// Controls that can be associated with a signal
bool TypeHasVariable(ControlType type);
// The default size of a new control
void DefaultSize(ControlType type, int& width, int& height);

class Control
{
public:
    ControlType mType = ControlType::Label;
    std::map<std::string, std::string> mProps;

    explicit Control(ControlType type = ControlType::Label);

    std::string Get(const std::string& name) const;
    void Set(const std::string& name, const std::string& value);
    int GetInt(const std::string& name) const;
    double GetNumber(const std::string& name) const;
    bool GetBool(const std::string& name) const;
    void SetInt(const std::string& name, int value);

    // place on the panel, in pixels
    int X() const { return GetInt("Left"); }
    int Y() const { return GetInt("Top"); }
    int W() const { return GetInt("Width"); }
    int H() const { return GetInt("Height"); }
    void SetRect(int x, int y, int w, int h);
};

class Instrument
{
public:
    std::map<std::string, std::string> mPanel;      // Title, Width, Height, BackColor
    std::vector<Control> mControls;                 // in drawing order: the last one is on top

    Instrument();
    std::string GetPanel(const std::string& name) const;
    int PanelWidth() const;
    int PanelHeight() const;

    // A name not used yet by any control: Gauge1, Gauge2...
    std::string NewName(ControlType type) const;

    // XML text of the whole instrument, or of some of its controls (for the clipboard)
    std::string ToXml() const;
    static std::string ControlsToXml(const std::vector<Control>& controls);
    bool FromXml(const std::string& xml, std::string& error);
    static bool ControlsFromXml(const std::string& xml, std::vector<Control>& controls);

    bool Load(const std::string& path, std::string& error);
    bool Save(const std::string& path, std::string& error) const;
};

// Numbers in the files and on the screen always have a point, whatever the language of Windows (BUSMASTER sets
// the C locale to the one of the user, where atof("0.5") can be 0)
_locale_t NumberLocale();
double ToNumber(const std::string& text);                       // 0 if not a number
bool ParseNumber(const std::string& text, double& value);       // the whole text; a comma is taken as a point
std::string FormatNumber(double value, int decimals = -1);      // -1: as short as possible (%g)

// Color helpers: "#RRGGBB" <-> COLORREF; an empty text is "none"
bool ParseColor(const std::string& text, unsigned long& color);
std::string ColorText(unsigned long color);

// base64 for the embedded images
std::string Base64Encode(const std::vector<unsigned char>& data);
std::vector<unsigned char> Base64Decode(const std::string& text);

// A short label of a variable path for the user: "EngineData.EngineSpeed (CAN 1)"
std::string VariableLabel(const std::string& path);
}
