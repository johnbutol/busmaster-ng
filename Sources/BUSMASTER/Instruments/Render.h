/*
 * BUSMASTER-NG Instruments: drawing of the controls, the same in the editor and in BUSMASTER.
 */
#pragma once

#include "Model.h"

#include <string>

namespace Gdiplus
{
class Graphics;
}

namespace Instruments
{
// What a control shows at run time
struct ControlState
{
    bool mHasValue = false;     // a value was received or set
    double mValue = 0.0;
    std::string mUnit;          // unit of the database, used when the control has none
    bool mPressed = false;      // button held down
    bool mHover = false;
};

void DrawControl(Gdiplus::Graphics& graphics, const Control& control, const ControlState& state, bool design);
void DrawPanelBackground(Gdiplus::Graphics& graphics, const Instrument& instrument, int width, int height);
// A small picture of a control type for the toolbox
void DrawToolIcon(Gdiplus::Graphics& graphics, ControlType type, float x, float y, float size);

// The value as text, with the decimals of the control; withUnit adds the unit
std::string FormatValue(const Control& control, const ControlState& state, bool withUnit);
std::string ControlUnit(const Control& control, const ControlState& state);

// Run time geometry, relative to the panel
// track bar: the value at a point
double TrackBarValueAt(const Control& control, int x, int y);
// numeric up down: +1 on the up arrow, -1 on the down arrow, 0 elsewhere
int UpDownArrowAt(const Control& control, int x, int y);
// rounds a value to the step of the control and keeps it between its minimum and maximum
double ClampToControl(const Control& control, double value);

std::wstring Widen(const std::string& text);

// frees the decoded pictures (before GDI+ is shut down)
void ClearPictureCache();
}
