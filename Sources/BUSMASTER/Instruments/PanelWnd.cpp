#include "stdafx.h"
#include "PanelWnd.h"
#include "Host.h"

#include <algorithm>
#include <cmath>

using namespace Gdiplus;
using namespace VariableManager;

namespace Instruments
{
namespace
{
const UINT_PTR kTimerValues = 1;
const UINT kValuesPeriod = 50;          // ms between two updates of the shown values
const UINT kMenuEdit = 1, kMenuReload = 2, kMenuClose = 3;
const char* const kPanelClass = "BusmasterInstrumentPanel";
}

// ---- in place editor of a text box or numeric up down
BEGIN_MESSAGE_MAP(InplaceEdit, CEdit)
    ON_WM_KEYDOWN()
    ON_WM_KILLFOCUS()
    ON_WM_GETDLGCODE()
END_MESSAGE_MAP()

void InplaceEdit::OnKeyDown(UINT nChar, UINT nRepCnt, UINT nFlags)
{
    if ((nChar == VK_RETURN || nChar == VK_ESCAPE) && !mFinished)
    {
        mFinished = true;
        if (mDone)
        {
            mDone(nChar == VK_RETURN);
        }
        return;
    }
    CEdit::OnKeyDown(nChar, nRepCnt, nFlags);
}

void InplaceEdit::OnKillFocus(CWnd* pNewWnd)
{
    CEdit::OnKillFocus(pNewWnd);
    if (!mFinished)
    {
        mFinished = true;
        if (mDone)
        {
            mDone(false);
        }
    }
}

UINT InplaceEdit::OnGetDlgCode()
{
    return DLGC_WANTALLKEYS;
}

// ---- the panel
BEGIN_MESSAGE_MAP(PanelWnd, CWnd)
    ON_WM_PAINT()
    ON_WM_ERASEBKGND()
    ON_WM_LBUTTONDOWN()
    ON_WM_LBUTTONUP()
    ON_WM_LBUTTONDBLCLK()
    ON_WM_MOUSEMOVE()
    ON_MESSAGE(WM_MOUSELEAVE, OnMouseLeave)
    ON_WM_MOUSEWHEEL()
    ON_WM_CONTEXTMENU()
    ON_WM_TIMER()
    ON_WM_CLOSE()
    ON_WM_MOUSEACTIVATE()
    ON_WM_CAPTURECHANGED()
END_MESSAGE_MAP()

PanelWnd::PanelWnd()
{
}

PanelWnd::~PanelWnd()
{
    Unregister();
}

bool PanelWnd::Open(const std::string& file, std::string& error)
{
    Instrument instrument;
    if (!instrument.Load(file, error))
    {
        return false;
    }
    mFile = file;
    mInstrument = instrument;
    mStates.assign(mInstrument.mControls.size(), ControlState());
    Host* host = GetHost();
    for (size_t i = 0; i < mInstrument.mControls.size(); i++)
    {
        std::string path = mInstrument.mControls[i].Get("Variable");
        if (!path.empty())
        {
            mStates[i].mUnit = host->UnitOf(path);
        }
    }
    return true;
}

bool PanelWnd::CreatePanel(HWND parent, const RECT* placement)
{
    WNDCLASS wc = {};
    if (!GetClassInfo(AfxGetInstanceHandle(), kPanelClass, &wc))
    {
        wc.style = CS_DBLCLKS;
        wc.lpfnWndProc = ::DefWindowProc;
        wc.hInstance = AfxGetInstanceHandle();
        wc.hCursor = ::LoadCursor(nullptr, IDC_ARROW);
        wc.lpszClassName = kPanelClass;
        AfxRegisterClass(&wc);
    }
    CRect rect(20, 20, 20 + mInstrument.PanelWidth(), 20 + mInstrument.PanelHeight());
    DWORD style = WS_CHILD | WS_CAPTION | WS_SYSMENU | WS_CLIPSIBLINGS | WS_CLIPCHILDREN;
    DWORD exStyle = WS_EX_TOOLWINDOW | WS_EX_WINDOWEDGE;
    AdjustWindowRectEx(&rect, style, FALSE, exStyle);
    if (placement != nullptr)
    {
        rect.OffsetRect(placement->left - rect.left, placement->top - rect.top);
    }
    if (!CreateEx(exStyle, kPanelClass, mInstrument.GetPanel("Title").c_str(), style, rect, CWnd::FromHandle(parent), 0))
    {
        return false;
    }
    FitWindow();
    SetWindowPos(&CWnd::wndTop, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
    Register();
    SetTimer(kTimerValues, kValuesPeriod, nullptr);
    return true;
}

void PanelWnd::FitWindow()
{
    CRect client(0, 0, mInstrument.PanelWidth(), mInstrument.PanelHeight());
    AdjustWindowRectEx(&client, GetStyle(), FALSE, GetExStyle());
    SetWindowPos(nullptr, 0, 0, client.Width(), client.Height(), SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
}

void PanelWnd::Register()
{
    IVariableLayer* variables = GetHost()->Variables();
    std::set<std::string> paths;
    for (auto& control : mInstrument.mControls)
    {
        std::string path = control.Get("Variable");
        if (!path.empty())
        {
            paths.insert(path);
        }
    }
    mUnresolved.clear();
    for (auto path : paths)
    {
        if (mRegistered.count(path) != 0)
        {
            continue;
        }
        if (variables != nullptr && variables->isVariableExists(path) && variables->registerVariable(path, this, false, true))
        {
            mRegistered.insert(path);
        }
        else
        {
            mUnresolved.insert(path);
        }
    }
}

void PanelWnd::RetryRegistration()
{
    if (!mUnresolved.empty())
    {
        Host* host = GetHost();
        Register();
        for (size_t i = 0; i < mInstrument.mControls.size(); i++)
        {
            std::string path = mInstrument.mControls[i].Get("Variable");
            if (!path.empty() && mStates[i].mUnit.empty())
            {
                mStates[i].mUnit = host->UnitOf(path);
            }
        }
    }
}

void PanelWnd::Rebind()
{
    Unregister();
    Register();
    Host* host = GetHost();
    for (size_t i = 0; i < mInstrument.mControls.size(); i++)
    {
        std::string path = mInstrument.mControls[i].Get("Variable");
        mStates[i].mUnit = path.empty() ? "" : host->UnitOf(path);
    }
    Invalidate(FALSE);
}

void PanelWnd::Unregister()
{
    IVariableLayer* variables = GetHost() != nullptr ? GetHost()->Variables() : nullptr;
    if (variables != nullptr)
    {
        for (auto path : mRegistered)
        {
            variables->UnRegisterVariable(path, this);
        }
    }
    mRegistered.clear();
}

void PanelWnd::Reload()
{
    std::string error;
    Unregister();
    Instrument instrument;
    if (!instrument.Load(mFile, error))
    {
        AfxMessageBox(error.c_str(), MB_ICONWARNING);
        return;
    }
    Open(mFile, error);
    SetWindowText(mInstrument.GetPanel("Title").c_str());
    FitWindow();
    Register();
    Invalidate();
}

CRect PanelWnd::Placement()
{
    CRect rect;
    GetWindowRect(&rect);
    GetParent()->ScreenToClient(&rect);
    return rect;
}

void PanelWnd::OnVariableChange(VariableData* data)
{
    if (data == nullptr)
    {
        return;
    }
    Incoming value;
    value.mPhysical = data->mPhysicalValue;
    value.mRaw = data->mValue.mRawValueType == Long ? (double)data->mValue.LongValue : (double)data->mValue.ULongValue;
    std::lock_guard<std::mutex> guard(mLock);
    mIncoming[data->mVariablePath] = value;
}

void PanelWnd::TakeIncoming()
{
    std::map<std::string, Incoming> incoming;
    {
        std::lock_guard<std::mutex> guard(mLock);
        incoming.swap(mIncoming);
    }
    if (incoming.empty())
    {
        return;
    }
    for (size_t i = 0; i < mInstrument.mControls.size(); i++)
    {
        const Control& control = mInstrument.mControls[i];
        auto it = incoming.find(control.Get("Variable"));
        if (it == incoming.end() || (int)i == mPressed || (int)i == mEditing)
        {
            continue;       // the user is changing this one
        }
        double value = control.Get("ValueType") == "Raw" ? it->second.mRaw : it->second.mPhysical;
        if (!mStates[i].mHasValue || mStates[i].mValue != value)
        {
            mStates[i].mHasValue = true;
            mStates[i].mValue = value;
            CRect rect(control.X(), control.Y(), control.X() + control.W(), control.Y() + control.H());
            InvalidateRect(rect, FALSE);
        }
    }
}

void PanelWnd::SetValue(int index, double value, bool send)
{
    if (index < 0 || index >= (int)mStates.size())
    {
        return;
    }
    const Control& control = mInstrument.mControls[index];
    bool changed = !mStates[index].mHasValue || mStates[index].mValue != value;
    mStates[index].mHasValue = true;
    mStates[index].mValue = value;
    CRect rect(control.X(), control.Y(), control.X() + control.W(), control.Y() + control.H());
    InvalidateRect(rect, FALSE);
    std::string path = control.Get("Variable");
    if (send && !path.empty())
    {
        GetHost()->Send(path, value, control.Get("ValueType") != "Raw");
    }
    // the other controls of the same signal show the new value
    if (changed && !path.empty())
    {
        for (size_t i = 0; i < mInstrument.mControls.size(); i++)
        {
            const Control& other = mInstrument.mControls[i];
            if ((int)i != index && other.Get("Variable") == path && other.Get("ValueType") == control.Get("ValueType"))
            {
                mStates[i].mHasValue = true;
                mStates[i].mValue = value;
                InvalidateRect(CRect(other.X(), other.Y(), other.X() + other.W(), other.Y() + other.H()), FALSE);
            }
        }
    }
}

int PanelWnd::HitTest(CPoint point) const
{
    for (int i = (int)mInstrument.mControls.size() - 1; i >= 0; i--)
    {
        const Control& c = mInstrument.mControls[i];
        if (c.mType == ControlType::GroupBox || c.mType == ControlType::Picture)
        {
            continue;       // a frame or a picture does not take the clicks of the controls on it
        }
        if (point.x >= c.X() && point.x < c.X() + c.W() && point.y >= c.Y() && point.y < c.Y() + c.H())
        {
            return i;
        }
    }
    return -1;
}

void PanelWnd::OnPaint()
{
    CPaintDC dc(this);
    CRect client;
    GetClientRect(&client);
    CDC memory;
    memory.CreateCompatibleDC(&dc);
    CBitmap bitmap;
    bitmap.CreateCompatibleBitmap(&dc, std::max(1, client.Width()), std::max(1, client.Height()));
    CBitmap* old = memory.SelectObject(&bitmap);
    {
        Graphics g(memory.GetSafeHdc());
        g.SetSmoothingMode(SmoothingModeAntiAlias);
        g.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);
        g.SetPageUnit(UnitPixel);
        DrawPanelBackground(g, mInstrument, client.Width(), client.Height());
        for (size_t i = 0; i < mInstrument.mControls.size(); i++)
        {
            if ((int)i == mEditing)
            {
                continue;
            }
            ControlState state = mStates[i];
            state.mHover = (int)i == mHover;
            state.mPressed = (int)i == mPressed && mInstrument.mControls[i].mType == ControlType::Button;
            DrawControl(g, mInstrument.mControls[i], state, false);
        }
    }
    dc.BitBlt(0, 0, client.Width(), client.Height(), &memory, 0, 0, SRCCOPY);
    memory.SelectObject(old);
}

BOOL PanelWnd::OnEraseBkgnd(CDC*)
{
    return TRUE;
}

void PanelWnd::OnLButtonDown(UINT, CPoint point)
{
    SetWindowPos(&CWnd::wndTop, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
    int index = HitTest(point);
    if (index < 0)
    {
        return;
    }
    const Control& c = mInstrument.mControls[index];
    switch (c.mType)
    {
        case ControlType::Button:
            mPressed = index;
            SetCapture();
            if (c.Get("Mode") != "Toggle")
            {
                SetValue(index, c.GetNumber("PressedValue"), true);
            }
            else
            {
                InvalidateRect(CRect(c.X(), c.Y(), c.X() + c.W(), c.Y() + c.H()), FALSE);
            }
            break;
        case ControlType::CheckBox:
        {
            bool checked = mStates[index].mHasValue && fabs(mStates[index].mValue - c.GetNumber("CheckedValue")) < 1e-9;
            SetValue(index, checked ? c.GetNumber("UncheckedValue") : c.GetNumber("CheckedValue"), true);
            break;
        }
        case ControlType::NumericUpDown:
        {
            int direction = UpDownArrowAt(c, point.x, point.y);
            if (direction != 0)
            {
                double value = mStates[index].mHasValue ? mStates[index].mValue : ClampToControl(c, 0.0);
                SetValue(index, ClampToControl(c, value + direction * c.GetNumber("Step")), true);
            }
            else
            {
                BeginEdit(index);
            }
            break;
        }
        case ControlType::TrackBar:
            mPressed = index;
            SetCapture();
            SetValue(index, TrackBarValueAt(c, point.x, point.y), true);
            break;
        case ControlType::TextBox:
            if (!c.GetBool("ReadOnly"))
            {
                BeginEdit(index);
            }
            break;
        default:
            break;
    }
}

void PanelWnd::OnLButtonDblClk(UINT nFlags, CPoint point)
{
    // a double click is two clicks for the arrows and the check boxes
    OnLButtonDown(nFlags, point);
}

void PanelWnd::OnLButtonUp(UINT, CPoint point)
{
    int index = mPressed;
    if (index < 0)
    {
        return;
    }
    mPressed = -1;
    if (GetCapture() == this)
    {
        ReleaseCapture();
    }
    const Control& c = mInstrument.mControls[index];
    if (c.mType == ControlType::Button)
    {
        std::string mode = c.Get("Mode");
        bool inside = point.x >= c.X() && point.x < c.X() + c.W() && point.y >= c.Y() && point.y < c.Y() + c.H();
        if (mode == "Momentary")
        {
            SetValue(index, c.GetNumber("ReleasedValue"), true);
        }
        else if (mode == "Toggle" && inside)
        {
            bool pressed = mStates[index].mHasValue && fabs(mStates[index].mValue - c.GetNumber("PressedValue")) < 1e-9;
            SetValue(index, pressed ? c.GetNumber("ReleasedValue") : c.GetNumber("PressedValue"), true);
        }
        InvalidateRect(CRect(c.X(), c.Y(), c.X() + c.W(), c.Y() + c.H()), FALSE);
    }
}

void PanelWnd::OnCaptureChanged(CWnd* pWnd)
{
    // the capture is lost (another window, Alt+Tab): a momentary button is released
    if (mPressed >= 0 && pWnd != this)
    {
        int index = mPressed;
        mPressed = -1;
        const Control& c = mInstrument.mControls[index];
        if (c.mType == ControlType::Button && c.Get("Mode") == "Momentary")
        {
            SetValue(index, c.GetNumber("ReleasedValue"), true);
        }
        Invalidate(FALSE);
    }
    CWnd::OnCaptureChanged(pWnd);
}

void PanelWnd::OnMouseMove(UINT, CPoint point)
{
    if (!mTracking)
    {
        TRACKMOUSEEVENT track = { sizeof(track), TME_LEAVE, m_hWnd, 0 };
        TrackMouseEvent(&track);
        mTracking = true;
    }
    if (mPressed >= 0)
    {
        const Control& c = mInstrument.mControls[mPressed];
        if (c.mType == ControlType::TrackBar)
        {
            double value = TrackBarValueAt(c, point.x, point.y);
            if (!mStates[mPressed].mHasValue || value != mStates[mPressed].mValue)
            {
                SetValue(mPressed, value, true);
            }
        }
        return;
    }
    int hover = HitTest(point);
    if (hover >= 0 && !TypeSends(mInstrument.mControls[hover].mType))
    {
        hover = -1;
    }
    if (hover != mHover)
    {
        auto invalidate = [this](int i)
        {
            if (i >= 0)
            {
                const Control& c = mInstrument.mControls[i];
                InvalidateRect(CRect(c.X(), c.Y(), c.X() + c.W(), c.Y() + c.H()), FALSE);
            }
        };
        invalidate(mHover);
        mHover = hover;
        invalidate(mHover);
    }
}

LRESULT PanelWnd::OnMouseLeave(WPARAM, LPARAM)
{
    mTracking = false;
    if (mHover >= 0)
    {
        mHover = -1;
        Invalidate(FALSE);
    }
    return 0;
}

BOOL PanelWnd::OnMouseWheel(UINT, short zDelta, CPoint pt)
{
    ScreenToClient(&pt);
    int index = HitTest(pt);
    if (index >= 0)
    {
        const Control& c = mInstrument.mControls[index];
        if (c.mType == ControlType::NumericUpDown || c.mType == ControlType::TrackBar)
        {
            double step = c.GetNumber("Step");
            if (step <= 0)
            {
                step = (c.GetNumber("Maximum") - c.GetNumber("Minimum")) / 100;
            }
            double value = mStates[index].mHasValue ? mStates[index].mValue : ClampToControl(c, 0.0);
            SetValue(index, ClampToControl(c, value + (zDelta > 0 ? step : -step)), true);
            return TRUE;
        }
    }
    return FALSE;
}

void PanelWnd::BeginEdit(int index)
{
    EndEdit(false);
    const Control& c = mInstrument.mControls[index];
    mEditing = index;
    int right = c.mType == ControlType::NumericUpDown ? std::min(18, c.W() / 2) + 1 : 0;
    CRect rect(c.X() + 1, c.Y() + 1, c.X() + c.W() - 1 - right, c.Y() + c.H() - 1);
    if (mEdit.GetSafeHwnd() == nullptr)
    {
        mEdit.Create(WS_CHILD | ES_AUTOHSCROLL, rect, this, 100);
    }
    mEditFont.DeleteObject();
    CClientDC dc(this);
    int size = std::max(6, c.GetInt("FontSize"));
    mEditFont.CreateFont(-MulDiv(size, dc.GetDeviceCaps(LOGPIXELSY), 72), 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0,
                         CLEARTYPE_QUALITY, 0, "Segoe UI");
    mEdit.SetFont(&mEditFont);
    mEdit.MoveWindow(rect);
    std::string text = mStates[index].mHasValue ? FormatValue(c, mStates[index], false) : c.Get("Text");
    mEdit.SetWindowText(text.c_str());
    mEdit.mFinished = false;
    mEdit.mDone = [this](bool commit)
    {
        // after the message of the edit control: it is hidden then
        PostMessage(WM_TIMER, commit ? 3 : 2, 0);
    };
    mEdit.ShowWindow(SW_SHOW);
    mEdit.SetFocus();
    mEdit.SetSel(0, -1);
    Invalidate(FALSE);
}

void PanelWnd::EndEdit(bool commit)
{
    if (mEditing < 0)
    {
        return;
    }
    int index = mEditing;
    mEditing = -1;
    if (commit && mEdit.GetSafeHwnd() != nullptr)
    {
        CString text;
        mEdit.GetWindowText(text);
        double value = 0;
        if (ParseNumber((const char*)text, value))
        {
            const Control& c = mInstrument.mControls[index];
            if (c.mType == ControlType::NumericUpDown)
            {
                value = ClampToControl(c, value);
            }
            SetValue(index, value, true);
        }
        else
        {
            MessageBeep(MB_ICONWARNING);
        }
    }
    if (mEdit.GetSafeHwnd() != nullptr)
    {
        mEdit.mFinished = true;
        mEdit.ShowWindow(SW_HIDE);
    }
    Invalidate(FALSE);
}

void PanelWnd::OnTimer(UINT_PTR nIDEvent)
{
    if (nIDEvent == kTimerValues)
    {
        TakeIncoming();
        // a database associated after the instrument was opened: try its signals again every few seconds
        if (!mUnresolved.empty() && ++mRetryCount >= 60)
        {
            mRetryCount = 0;
            RetryRegistration();
        }
    }
    else if (nIDEvent == 2 || nIDEvent == 3)
    {
        EndEdit(nIDEvent == 3);
    }
}

void PanelWnd::OnContextMenu(CWnd*, CPoint point)
{
    CMenu menu;
    menu.CreatePopupMenu();
    menu.AppendMenu(MF_STRING, kMenuEdit, "&Edit in the Instrument Editor");
    menu.AppendMenu(MF_STRING, kMenuReload, "&Reload");
    menu.AppendMenu(MF_SEPARATOR);
    menu.AppendMenu(MF_STRING, kMenuClose, "&Close");
    if (point.x == -1 && point.y == -1)
    {
        point = CPoint(10, 10);
        ClientToScreen(&point);
    }
    UINT command = menu.TrackPopupMenu(TPM_RETURNCMD | TPM_RIGHTBUTTON, point.x, point.y, this);
    switch (command)
    {
        case kMenuEdit:
            GetHost()->OpenEditor(mFile);
            break;
        case kMenuReload:
            GetHost()->ReloadPanel(this);
            break;
        case kMenuClose:
            GetHost()->ClosePanel(this);
            break;
        default:
            break;
    }
}

void PanelWnd::OnClose()
{
    GetHost()->ClosePanel(this);
}

void PanelWnd::PostNcDestroy()
{
    delete this;
}

int PanelWnd::OnMouseActivate(CWnd*, UINT, UINT)
{
    SetWindowPos(&CWnd::wndTop, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
    return MA_ACTIVATE;
}
}
