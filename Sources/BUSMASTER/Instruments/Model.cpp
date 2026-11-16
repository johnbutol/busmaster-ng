#include "stdafx.h"
#include "Model.h"

#include <libxml/parser.h>
#include <libxml/tree.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace Instruments
{
namespace
{
const char* const kTypeKeys[] = { "AnalogGauge", "Button", "TextBox", "CheckBox", "GroupBox", "Label", "Led",
                                  "NumericUpDown", "Picture", "TrackBar"
                                };
const char* const kTypeTitles[] = { "Analog gauge", "Button", "Text box", "Check box", "Group box", "Label",
                                    "LED", "Numeric up down", "Picture", "Track bar"
                                  };

std::vector<PropDef> Layout()
{
    return
    {
        { "Name", "Design", PropKind::Text, "", "Name of the control in the instrument", {} },
        { "Left", "Layout", PropKind::Integer, "0", "Distance from the left side of the panel, in pixels", {} },
        { "Top", "Layout", PropKind::Integer, "0", "Distance from the top of the panel, in pixels", {} },
        { "Width", "Layout", PropKind::Integer, "100", "Width in pixels", {} },
        { "Height", "Layout", PropKind::Integer, "24", "Height in pixels", {} },
    };
}

std::vector<PropDef> Appearance(const char* text, const char* back)
{
    return
    {
        { "Text", "Appearance", PropKind::Text, text, "Text shown by the control", {} },
        { "FontSize", "Appearance", PropKind::Integer, "9", "Size of the text, in points", {} },
        { "ForeColor", "Appearance", PropKind::Color, "#000000", "Color of the text", {} },
        { "BackColor", "Appearance", PropKind::Color, back, "Background color; none: the color of the panel", {} },
    };
}

std::vector<PropDef> Signal(const char* decimals)
{
    return
    {
        { "Variable", "Signal", PropKind::Variable, "", "Signal associated with the control (click ... to choose it)", {} },
        { "ValueType", "Signal", PropKind::Choice, "Physical", "Physical value (scaled, with its unit) or raw value of the signal", { "Physical", "Raw" } },
        { "Decimals", "Signal", PropKind::Integer, decimals, "Number of decimals shown", {} },
        { "Unit", "Signal", PropKind::Text, "", "Unit shown; empty: the unit of the database", {} },
    };
}

std::vector<PropDef> Join(std::initializer_list<std::vector<PropDef>> parts)
{
    std::vector<PropDef> result;
    for (auto& part : parts)
    {
        result.insert(result.end(), part.begin(), part.end());
    }
    return result;
}

std::vector<std::vector<PropDef>> MakeDefs()
{
    std::vector<std::vector<PropDef>> defs((size_t)ControlType::Count);
    defs[(int)ControlType::AnalogGauge] = Join({ Layout(), Appearance("", ""), Signal("0"),
    {
        { "Minimum", "Scale", PropKind::Number, "0", "Value at the start of the scale", {} },
        { "Maximum", "Scale", PropKind::Number, "100", "Value at the end of the scale", {} },
        { "MajorStep", "Scale", PropKind::Number, "10", "Step between two numbered marks", {} },
        { "MinorTicks", "Scale", PropKind::Integer, "4", "Number of small marks between two numbered marks", {} },
        { "ShowRanges", "Ranges", PropKind::Bool, "true", "Show the green, yellow and red ranges on the scale", {} },
        { "LowerRange", "Ranges", PropKind::Number, "60", "End of the green range, start of the yellow one", {} },
        { "UpperRange", "Ranges", PropKind::Number, "80", "End of the yellow range, start of the red one", {} },
        { "NeedleColor", "Appearance", PropKind::Color, "#C00000", "Color of the needle", {} },
        { "ShowValue", "Appearance", PropKind::Bool, "true", "Show the value in digits under the needle", {} },
    }
                                                 });
    defs[(int)ControlType::Button] = Join({ Layout(), Appearance("Button", ""), Signal("0"),
    {
        { "Mode", "Behavior", PropKind::Choice, "Momentary", "Momentary: pressed value while held down, released value when let go. Toggle: each click changes between the two values. Send: each click sends the pressed value.", { "Momentary", "Toggle", "Send" } },
        { "PressedValue", "Behavior", PropKind::Number, "1", "Value sent when the button is pressed", {} },
        { "ReleasedValue", "Behavior", PropKind::Number, "0", "Value sent when the button is released", {} },
    }
                                          });
    defs[(int)ControlType::TextBox] = Join({ Layout(), Appearance("", "#FFFFFF"), Signal("2"),
    {
        { "ReadOnly", "Behavior", PropKind::Bool, "false", "Only show the value; otherwise a value typed in the box (Enter) is sent", {} },
    }
                                           });
    defs[(int)ControlType::CheckBox] = Join({ Layout(), Appearance("Check box", ""), Signal("0"),
    {
        { "CheckedValue", "Behavior", PropKind::Number, "1", "Value sent when the box is checked", {} },
        { "UncheckedValue", "Behavior", PropKind::Number, "0", "Value sent when the box is cleared", {} },
    }
                                            });
    defs[(int)ControlType::GroupBox] = Join({ Layout(), Appearance("Group", "") });
    defs[(int)ControlType::Label] = Join({ Layout(), Appearance("Label", ""), Signal("2"),
    {
        { "Alignment", "Appearance", PropKind::Choice, "Left", "Alignment of the text", { "Left", "Center", "Right" } },
    }
                                         });
    defs[(int)ControlType::Led] = Join({ Layout(), Appearance("", ""), Signal("0"),
    {
        { "Threshold", "Behavior", PropKind::Number, "0.5", "The LED is on when the value is at least this", {} },
        { "OnColor", "Appearance", PropKind::Color, "#00D000", "Color of the LED when on", {} },
        { "OffColor", "Appearance", PropKind::Color, "#405040", "Color of the LED when off", {} },
        { "Shape", "Appearance", PropKind::Choice, "Round", "Shape of the LED", { "Round", "Square" } },
    }
                                       });
    defs[(int)ControlType::NumericUpDown] = Join({ Layout(), Appearance("", "#FFFFFF"), Signal("0"),
    {
        { "Minimum", "Behavior", PropKind::Number, "0", "Smallest value", {} },
        { "Maximum", "Behavior", PropKind::Number, "100", "Largest value", {} },
        { "Step", "Behavior", PropKind::Number, "1", "Change for one click on an arrow", {} },
    }
                                                 });
    defs[(int)ControlType::Picture] = Join({ Layout(),
    {
        { "Image", "Appearance", PropKind::Image, "", "Image shown (click ... to choose a file: it is stored in the instrument)", {} },
        { "SizeMode", "Appearance", PropKind::Choice, "Zoom", "Stretch: fill the control. Zoom: as large as possible, keeping the proportions. Center: actual size.", { "Stretch", "Zoom", "Center" } },
        { "BackColor", "Appearance", PropKind::Color, "", "Background color; none: the color of the panel", {} },
    }
                                           });
    defs[(int)ControlType::TrackBar] = Join({ Layout(), Appearance("", ""), Signal("0"),
    {
        { "Minimum", "Behavior", PropKind::Number, "0", "Value at the start of the bar", {} },
        { "Maximum", "Behavior", PropKind::Number, "100", "Value at the end of the bar", {} },
        { "Step", "Behavior", PropKind::Number, "1", "The values are rounded to this step", {} },
        { "TickStep", "Appearance", PropKind::Number, "10", "Step between two marks; 0: no marks", {} },
        { "Orientation", "Appearance", PropKind::Choice, "Horizontal", "Direction of the bar", { "Horizontal", "Vertical" } },
        { "ShowValue", "Appearance", PropKind::Bool, "true", "Show the value", {} },
    }
                                            });
    return defs;
}

const std::vector<std::vector<PropDef>>& AllDefs()
{
    static const std::vector<std::vector<PropDef>> defs = MakeDefs();
    return defs;
}

std::string GetAttr(xmlNodePtr node, const char* name)
{
    xmlChar* value = xmlGetProp(node, BAD_CAST name);
    std::string result;
    if (value != nullptr)
    {
        result = (const char*)value;
        xmlFree(value);
    }
    return result;
}

void ReadProperties(xmlNodePtr node, std::map<std::string, std::string>& props)
{
    for (xmlNodePtr child = node->children; child != nullptr; child = child->next)
    {
        if (child->type == XML_ELEMENT_NODE && !xmlStrcmp(child->name, BAD_CAST "Property"))
        {
            props[GetAttr(child, "Name")] = GetAttr(child, "Value");
        }
    }
}

bool ReadControl(xmlNodePtr node, Control& control)
{
    ControlType type;
    if (!TypeFromKey(GetAttr(node, "Type"), type))
    {
        return false;
    }
    control = Control(type);
    std::map<std::string, std::string> props;
    ReadProperties(node, props);
    for (auto& prop : props)
    {
        control.mProps[prop.first] = prop.second;
    }
    return true;
}

void WriteControl(xmlNodePtr parent, const Control& control)
{
    xmlNodePtr node = xmlNewChild(parent, nullptr, BAD_CAST "Control", nullptr);
    xmlNewProp(node, BAD_CAST "Type", BAD_CAST TypeKey(control.mType));
    // in the order of the definitions (the property window), not alphabetically
    for (auto& def : PropDefs(control.mType))
    {
        xmlNodePtr child = xmlNewChild(node, nullptr, BAD_CAST "Property", nullptr);
        xmlNewProp(child, BAD_CAST "Name", BAD_CAST def.mName);
        xmlNewProp(child, BAD_CAST "Value", BAD_CAST control.Get(def.mName).c_str());
    }
}

std::string DocToString(xmlDocPtr doc)
{
    xmlChar* buffer = nullptr;
    int size = 0;
    xmlDocDumpFormatMemoryEnc(doc, &buffer, &size, "UTF-8", 1);
    std::string text;
    if (buffer != nullptr)
    {
        text.assign((const char*)buffer, size);
        xmlFree(buffer);
    }
    return text;
}

// The files are UTF-8; BUSMASTER works with the ANSI code page
std::string AnsiToUtf8(const std::string& text)
{
    if (text.empty())
    {
        return text;
    }
    int wide = MultiByteToWideChar(CP_ACP, 0, text.c_str(), (int)text.size(), nullptr, 0);
    std::wstring w(wide, L'\0');
    MultiByteToWideChar(CP_ACP, 0, text.c_str(), (int)text.size(), &w[0], wide);
    int size = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), wide, nullptr, 0, nullptr, nullptr);
    std::string result(size, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), wide, &result[0], size, nullptr, nullptr);
    return result;
}

std::string Utf8ToAnsi(const std::string& text)
{
    if (text.empty())
    {
        return text;
    }
    int wide = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), (int)text.size(), nullptr, 0);
    std::wstring w(wide, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.c_str(), (int)text.size(), &w[0], wide);
    int size = WideCharToMultiByte(CP_ACP, 0, w.c_str(), wide, nullptr, 0, nullptr, nullptr);
    std::string result(size, '\0');
    WideCharToMultiByte(CP_ACP, 0, w.c_str(), wide, &result[0], size, nullptr, nullptr);
    return result;
}

void ToUtf8(std::map<std::string, std::string>& props)
{
    for (auto& prop : props)
    {
        prop.second = AnsiToUtf8(prop.second);
    }
}

void ToAnsi(std::map<std::string, std::string>& props)
{
    for (auto& prop : props)
    {
        prop.second = Utf8ToAnsi(prop.second);
    }
}
}

const char* TypeKey(ControlType type)
{
    return kTypeKeys[(int)type];
}

const char* TypeTitle(ControlType type)
{
    return kTypeTitles[(int)type];
}

bool TypeFromKey(const std::string& key, ControlType& type)
{
    for (int i = 0; i < (int)ControlType::Count; i++)
    {
        if (key == kTypeKeys[i])
        {
            type = (ControlType)i;
            return true;
        }
    }
    return false;
}

const std::vector<PropDef>& PropDefs(ControlType type)
{
    return AllDefs()[(int)type];
}

const PropDef* FindPropDef(ControlType type, const std::string& name)
{
    for (auto& def : PropDefs(type))
    {
        if (name == def.mName)
        {
            return &def;
        }
    }
    return nullptr;
}

const std::vector<PropDef>& PanelPropDefs()
{
    static const std::vector<PropDef> defs =
    {
        { "Title", "Panel", PropKind::Text, "Instrument", "Title of the instrument window", {} },
        { "Width", "Panel", PropKind::Integer, "400", "Width of the panel, in pixels", {} },
        { "Height", "Panel", PropKind::Integer, "300", "Height of the panel, in pixels", {} },
        { "BackColor", "Panel", PropKind::Color, "#F0F0F0", "Background color of the panel", {} },
    };
    return defs;
}

bool TypeSends(ControlType type)
{
    switch (type)
    {
        case ControlType::Button:
        case ControlType::TextBox:
        case ControlType::CheckBox:
        case ControlType::NumericUpDown:
        case ControlType::TrackBar:
            return true;
        default:
            return false;
    }
}

bool TypeHasVariable(ControlType type)
{
    return FindPropDef(type, "Variable") != nullptr;
}

void DefaultSize(ControlType type, int& width, int& height)
{
    switch (type)
    {
        case ControlType::AnalogGauge:
            width = 180;
            height = 180;
            break;
        case ControlType::Button:
            width = 90;
            height = 28;
            break;
        case ControlType::TextBox:
        case ControlType::NumericUpDown:
            width = 100;
            height = 24;
            break;
        case ControlType::CheckBox:
            width = 110;
            height = 22;
            break;
        case ControlType::GroupBox:
            width = 200;
            height = 140;
            break;
        case ControlType::Label:
            width = 100;
            height = 20;
            break;
        case ControlType::Led:
            width = 28;
            height = 28;
            break;
        case ControlType::Picture:
            width = 120;
            height = 90;
            break;
        case ControlType::TrackBar:
            width = 200;
            height = 48;
            break;
        default:
            width = 100;
            height = 24;
            break;
    }
}

Control::Control(ControlType type)
    : mType(type)
{
    for (auto& def : PropDefs(type))
    {
        mProps[def.mName] = def.mDefault;
    }
    int w, h;
    DefaultSize(type, w, h);
    SetInt("Width", w);
    SetInt("Height", h);
}

std::string Control::Get(const std::string& name) const
{
    auto it = mProps.find(name);
    if (it != mProps.end())
    {
        return it->second;
    }
    const PropDef* def = FindPropDef(mType, name);
    return def != nullptr ? def->mDefault : "";
}

void Control::Set(const std::string& name, const std::string& value)
{
    mProps[name] = value;
}

int Control::GetInt(const std::string& name) const
{
    return atoi(Get(name).c_str());
}

double Control::GetNumber(const std::string& name) const
{
    return ToNumber(Get(name));
}

_locale_t NumberLocale()
{
    static _locale_t locale = _create_locale(LC_NUMERIC, "C");
    return locale;
}

double ToNumber(const std::string& text)
{
    double value = 0;
    if (!ParseNumber(text, value))
    {
        std::string t = text;
        std::replace(t.begin(), t.end(), ',', '.');
        value = _strtod_l(t.c_str(), nullptr, NumberLocale());
    }
    return value;
}

bool ParseNumber(const std::string& text, double& value)
{
    std::string t = text;
    const char* const blanks = " \t";
    t.erase(0, t.find_first_not_of(blanks));
    t.erase(t.find_last_not_of(blanks) + 1);
    std::replace(t.begin(), t.end(), ',', '.');
    char* end = nullptr;
    value = _strtod_l(t.c_str(), &end, NumberLocale());
    return !t.empty() && end != nullptr && *end == '\0';
}

std::string FormatNumber(double value, int decimals)
{
    char text[64];
    if (decimals < 0)
    {
        _sprintf_s_l(text, sizeof(text), "%.10g", NumberLocale(), value);
    }
    else
    {
        _sprintf_s_l(text, sizeof(text), "%.*f", NumberLocale(), std::min(decimals, 15), value);
    }
    return text;
}

bool Control::GetBool(const std::string& name) const
{
    std::string value = Get(name);
    return value == "true" || value == "1";
}

void Control::SetInt(const std::string& name, int value)
{
    Set(name, std::to_string(value));
}

void Control::SetRect(int x, int y, int w, int h)
{
    SetInt("Left", x);
    SetInt("Top", y);
    SetInt("Width", std::max(w, 4));
    SetInt("Height", std::max(h, 4));
}

Instrument::Instrument()
{
    for (auto& def : PanelPropDefs())
    {
        mPanel[def.mName] = def.mDefault;
    }
}

std::string Instrument::GetPanel(const std::string& name) const
{
    auto it = mPanel.find(name);
    return it != mPanel.end() ? it->second : "";
}

int Instrument::PanelWidth() const
{
    return std::max(50, atoi(GetPanel("Width").c_str()));
}

int Instrument::PanelHeight() const
{
    return std::max(30, atoi(GetPanel("Height").c_str()));
}

std::string Instrument::NewName(ControlType type) const
{
    std::string base = TypeKey(type);
    for (int n = 1;; n++)
    {
        std::string name = base + std::to_string(n);
        bool used = false;
        for (auto& control : mControls)
        {
            if (control.Get("Name") == name)
            {
                used = true;
                break;
            }
        }
        if (!used)
        {
            return name;
        }
    }
}

std::string Instrument::ToXml() const
{
    xmlDocPtr doc = xmlNewDoc(BAD_CAST "1.0");
    xmlNodePtr root = xmlNewNode(nullptr, BAD_CAST "BusmasterInstrument");
    xmlDocSetRootElement(doc, root);
    xmlNewProp(root, BAD_CAST "version", BAD_CAST "1");
    xmlNodePtr panel = xmlNewChild(root, nullptr, BAD_CAST "Panel", nullptr);
    std::map<std::string, std::string> panelProps = mPanel;
    ToUtf8(panelProps);
    for (auto& def : PanelPropDefs())
    {
        xmlNewProp(panel, BAD_CAST def.mName, BAD_CAST panelProps[def.mName].c_str());
    }
    for (auto control : mControls)
    {
        ToUtf8(control.mProps);
        WriteControl(root, control);
    }
    std::string text = DocToString(doc);
    xmlFreeDoc(doc);
    return text;
}

std::string Instrument::ControlsToXml(const std::vector<Control>& controls)
{
    Instrument instrument;
    instrument.mControls = controls;
    return instrument.ToXml();
}

bool Instrument::FromXml(const std::string& xml, std::string& error)
{
    xmlDocPtr doc = xmlReadMemory(xml.c_str(), (int)xml.size(), "instrument.xml", nullptr, XML_PARSE_NONET | XML_PARSE_HUGE);
    if (doc == nullptr)
    {
        error = "The file is not valid XML.";
        return false;
    }
    xmlNodePtr root = xmlDocGetRootElement(doc);
    if (root == nullptr || xmlStrcmp(root->name, BAD_CAST "BusmasterInstrument"))
    {
        xmlFreeDoc(doc);
        error = "The file is not a BUSMASTER instrument.";
        return false;
    }
    Instrument result;
    for (xmlNodePtr node = root->children; node != nullptr; node = node->next)
    {
        if (node->type != XML_ELEMENT_NODE)
        {
            continue;
        }
        if (!xmlStrcmp(node->name, BAD_CAST "Panel"))
        {
            for (auto& def : PanelPropDefs())
            {
                xmlChar* value = xmlGetProp(node, BAD_CAST def.mName);
                if (value != nullptr)
                {
                    result.mPanel[def.mName] = Utf8ToAnsi((const char*)value);
                    xmlFree(value);
                }
            }
        }
        else if (!xmlStrcmp(node->name, BAD_CAST "Control"))
        {
            Control control;
            if (ReadControl(node, control))
            {
                ToAnsi(control.mProps);
                result.mControls.push_back(control);
            }
        }
    }
    xmlFreeDoc(doc);
    *this = result;
    return true;
}

bool Instrument::ControlsFromXml(const std::string& xml, std::vector<Control>& controls)
{
    Instrument instrument;
    std::string error;
    if (!instrument.FromXml(xml, error))
    {
        return false;
    }
    controls = instrument.mControls;
    return true;
}

bool Instrument::Load(const std::string& path, std::string& error)
{
    std::ifstream file(path, std::ios::binary);
    if (!file)
    {
        error = "Cannot open " + path;
        return false;
    }
    std::stringstream text;
    text << file.rdbuf();
    return FromXml(text.str(), error);
}

bool Instrument::Save(const std::string& path, std::string& error) const
{
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file)
    {
        error = "Cannot write " + path;
        return false;
    }
    file << ToXml();
    if (!file)
    {
        error = "Cannot write " + path;
        return false;
    }
    return true;
}

bool ParseColor(const std::string& text, unsigned long& color)
{
    if (text.size() != 7 || text[0] != '#')
    {
        return false;
    }
    unsigned long rgb = strtoul(text.c_str() + 1, nullptr, 16);
    color = RGB((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF);
    return true;
}

std::string ColorText(unsigned long color)
{
    char text[8];
    sprintf_s(text, "#%02X%02X%02X", GetRValue(color), GetGValue(color), GetBValue(color));
    return text;
}

std::string Base64Encode(const std::vector<unsigned char>& data)
{
    static const char table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve((data.size() + 2) / 3 * 4);
    size_t i = 0;
    for (; i + 2 < data.size(); i += 3)
    {
        unsigned value = (data[i] << 16) | (data[i + 1] << 8) | data[i + 2];
        out += table[(value >> 18) & 63];
        out += table[(value >> 12) & 63];
        out += table[(value >> 6) & 63];
        out += table[value & 63];
    }
    if (i < data.size())
    {
        unsigned value = data[i] << 16;
        if (i + 1 < data.size())
        {
            value |= data[i + 1] << 8;
        }
        out += table[(value >> 18) & 63];
        out += table[(value >> 12) & 63];
        out += (i + 1 < data.size()) ? table[(value >> 6) & 63] : '=';
        out += '=';
    }
    return out;
}

std::vector<unsigned char> Base64Decode(const std::string& text)
{
    std::vector<unsigned char> out;
    unsigned value = 0;
    int bits = -8;
    for (char c : text)
    {
        int digit;
        if (c >= 'A' && c <= 'Z')
        {
            digit = c - 'A';
        }
        else if (c >= 'a' && c <= 'z')
        {
            digit = c - 'a' + 26;
        }
        else if (c >= '0' && c <= '9')
        {
            digit = c - '0' + 52;
        }
        else if (c == '+')
        {
            digit = 62;
        }
        else if (c == '/')
        {
            digit = 63;
        }
        else
        {
            continue;       // padding, line breaks
        }
        value = (value << 6) | digit;
        bits += 6;
        if (bits >= 0)
        {
            out.push_back((unsigned char)((value >> bits) & 0xFF));
            bits -= 8;
        }
    }
    return out;
}

std::string VariableLabel(const std::string& path)
{
    // SIGNAL:CAN:CHANNEL0:<database>:<message>:<signal>
    std::vector<std::string> parts;
    std::stringstream stream(path);
    std::string part;
    while (std::getline(stream, part, ':'))
    {
        parts.push_back(part);
    }
    if (parts.size() < 6)
    {
        return path;
    }
    int channel = atoi(parts[2].c_str() + (parts[2].size() > 7 ? 7 : parts[2].size())) + 1;
    return parts[4] + "." + parts[5] + " (" + parts[1] + " " + std::to_string(channel) + ")";
}
}
