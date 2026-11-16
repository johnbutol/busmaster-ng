#include "stdafx.h"
#include "Editor.h"
#include "Render.h"

#include <algorithm>

using namespace Gdiplus;

namespace Instruments
{
namespace
{
const int kMargin = 16;         // around the panel in the canvas
const int kHandle = 7;          // size of the sizing handles
const int kToolHeader = 26;
const int kToolRow = 30;
const char* const kCanvasClass = "BusmasterInstrumentCanvas";
const char* const kToolboxClass = "BusmasterInstrumentToolbox";

void RegisterClass(const char* name, UINT style)
{
    WNDCLASS wc = {};
    if (!GetClassInfo(AfxGetInstanceHandle(), name, &wc))
    {
        wc.style = style;
        wc.lpfnWndProc = ::DefWindowProc;
        wc.hInstance = AfxGetInstanceHandle();
        wc.hCursor = ::LoadCursor(nullptr, IDC_ARROW);
        wc.lpszClassName = name;
        AfxRegisterClass(&wc);
    }
}

CRect RectOf(const Control& c)
{
    return CRect(c.X(), c.Y(), c.X() + c.W(), c.Y() + c.H());
}

int Snap(int value, int grid)
{
    if (grid <= 1)
    {
        return value;
    }
    return (int)floor((double)value / grid + 0.5) * grid;
}
}

// ---- Canvas
BEGIN_MESSAGE_MAP(Canvas, CWnd)
    ON_WM_PAINT()
    ON_WM_ERASEBKGND()
    ON_WM_SIZE()
    ON_WM_HSCROLL()
    ON_WM_VSCROLL()
    ON_WM_MOUSEWHEEL()
    ON_WM_LBUTTONDOWN()
    ON_WM_LBUTTONUP()
    ON_WM_LBUTTONDBLCLK()
    ON_WM_MOUSEMOVE()
    ON_WM_CONTEXTMENU()
    ON_WM_KEYDOWN()
    ON_WM_GETDLGCODE()
    ON_WM_SETCURSOR()
    ON_WM_CAPTURECHANGED()
END_MESSAGE_MAP()

BOOL Canvas::Create(CWnd* parent, UINT id)
{
    RegisterClass(kCanvasClass, CS_DBLCLKS);
    return CWnd::CreateEx(WS_EX_CLIENTEDGE, kCanvasClass, "", WS_CHILD | WS_VISIBLE | WS_HSCROLL | WS_VSCROLL | WS_TABSTOP,
                          CRect(0, 0, 10, 10), parent, id);
}

CPoint Canvas::Origin() const
{
    Document* doc = mFrame->Doc();
    CPoint scroll = doc != nullptr ? doc->mScroll : CPoint(0, 0);
    return CPoint(kMargin - scroll.x, kMargin - scroll.y);
}

CPoint Canvas::ToPanel(CPoint client) const
{
    return client - Origin();
}

void Canvas::UpdateScrollBars()
{
    Document* doc = mFrame->Doc();
    if (doc == nullptr || GetSafeHwnd() == nullptr)
    {
        return;
    }
    CRect client;
    GetClientRect(&client);
    int width = doc->mInstrument.PanelWidth() + 2 * kMargin + kHandle;
    int height = doc->mInstrument.PanelHeight() + 2 * kMargin + kHandle;
    doc->mScroll.x = std::max(0, std::min((int)doc->mScroll.x, width - client.Width()));
    doc->mScroll.y = std::max(0, std::min((int)doc->mScroll.y, height - client.Height()));
    SCROLLINFO info = { sizeof(info), SIF_ALL | SIF_DISABLENOSCROLL };
    info.nMin = 0;
    info.nMax = width - 1;
    info.nPage = client.Width();
    info.nPos = doc->mScroll.x;
    SetScrollInfo(SB_HORZ, &info);
    info.nMax = height - 1;
    info.nPage = client.Height();
    info.nPos = doc->mScroll.y;
    SetScrollInfo(SB_VERT, &info);
}

void Canvas::OnSize(UINT nType, int cx, int cy)
{
    CWnd::OnSize(nType, cx, cy);
    UpdateScrollBars();
}

void Canvas::OnHScroll(UINT nSBCode, UINT nPos, CScrollBar*)
{
    Document* doc = mFrame->Doc();
    if (doc == nullptr)
    {
        return;
    }
    CRect client;
    GetClientRect(&client);
    int x = doc->mScroll.x;
    switch (nSBCode)
    {
        case SB_LINELEFT:
            x -= 16;
            break;
        case SB_LINERIGHT:
            x += 16;
            break;
        case SB_PAGELEFT:
            x -= client.Width();
            break;
        case SB_PAGERIGHT:
            x += client.Width();
            break;
        case SB_THUMBTRACK:
        case SB_THUMBPOSITION:
            x = nPos;
            break;
        default:
            break;
    }
    doc->mScroll.x = x;
    UpdateScrollBars();
    Invalidate(FALSE);
}

void Canvas::OnVScroll(UINT nSBCode, UINT nPos, CScrollBar*)
{
    Document* doc = mFrame->Doc();
    if (doc == nullptr)
    {
        return;
    }
    CRect client;
    GetClientRect(&client);
    int y = doc->mScroll.y;
    switch (nSBCode)
    {
        case SB_LINEUP:
            y -= 16;
            break;
        case SB_LINEDOWN:
            y += 16;
            break;
        case SB_PAGEUP:
            y -= client.Height();
            break;
        case SB_PAGEDOWN:
            y += client.Height();
            break;
        case SB_THUMBTRACK:
        case SB_THUMBPOSITION:
            y = nPos;
            break;
        default:
            break;
    }
    doc->mScroll.y = y;
    UpdateScrollBars();
    Invalidate(FALSE);
}

BOOL Canvas::OnMouseWheel(UINT nFlags, short zDelta, CPoint)
{
    OnVScroll(zDelta > 0 ? SB_LINEUP : SB_LINEDOWN, 0, nullptr);
    if (nFlags & MK_SHIFT)
    {
        OnHScroll(zDelta > 0 ? SB_LINELEFT : SB_LINERIGHT, 0, nullptr);
    }
    return TRUE;
}

BOOL Canvas::OnEraseBkgnd(CDC*)
{
    return TRUE;
}

CRect Canvas::HandleRect(const CRect& r, int handle) const
{
    int cx = (r.left + r.right) / 2, cy = (r.top + r.bottom) / 2;
    CPoint centers[8] =
    {
        CPoint(r.left, r.top), CPoint(cx, r.top), CPoint(r.right, r.top), CPoint(r.right, cy),
        CPoint(r.right, r.bottom), CPoint(cx, r.bottom), CPoint(r.left, r.bottom), CPoint(r.left, cy)
    };
    CPoint p = centers[handle];
    return CRect(p.x - kHandle / 2, p.y - kHandle / 2, p.x + kHandle / 2 + 1, p.y + kHandle / 2 + 1);
}

int Canvas::HitControl(CPoint p) const
{
    Document* doc = mFrame->Doc();
    for (int i = (int)doc->mInstrument.mControls.size() - 1; i >= 0; i--)
    {
        if (RectOf(doc->mInstrument.mControls[i]).PtInRect(p))
        {
            return i;
        }
    }
    return -1;
}

int Canvas::HitHandle(CPoint p) const
{
    Document* doc = mFrame->Doc();
    if (doc->mSelection.size() != 1)
    {
        return -1;
    }
    CRect r = RectOf(doc->mInstrument.mControls[doc->mSelection.front()]);
    for (int h = 0; h < 8; h++)
    {
        CRect handle = HandleRect(r, h);
        handle.InflateRect(1, 1);
        if (handle.PtInRect(p))
        {
            return h;
        }
    }
    return -1;
}

bool Canvas::HitPanelHandle(CPoint p) const
{
    Document* doc = mFrame->Doc();
    CRect panel(0, 0, doc->mInstrument.PanelWidth(), doc->mInstrument.PanelHeight());
    CRect handle = HandleRect(panel, 4);
    handle.InflateRect(2, 2);
    return handle.PtInRect(p) != FALSE;
}

void Canvas::OnPaint()
{
    CPaintDC dc(this);
    Document* doc = mFrame->Doc();
    CRect client;
    GetClientRect(&client);
    CDC memory;
    memory.CreateCompatibleDC(&dc);
    CBitmap bitmap;
    bitmap.CreateCompatibleBitmap(&dc, std::max(1, client.Width()), std::max(1, client.Height()));
    CBitmap* old = memory.SelectObject(&bitmap);
    memory.FillSolidRect(client, RGB(214, 219, 233));
    if (doc != nullptr)
    {
        const Instrument& instrument = doc->mInstrument;
        CPoint origin = Origin();
        int pw = instrument.PanelWidth(), ph = instrument.PanelHeight();
        {
            Graphics g(memory.GetSafeHdc());
            g.SetPageUnit(UnitPixel);
            g.TranslateTransform((REAL)origin.x, (REAL)origin.y);
            // shadow, panel, grid
            SolidBrush shadow(Color(60, 0, 0, 0));
            g.FillRectangle(&shadow, 4, 4, pw, ph);
            g.SetClip(Rect(0, 0, pw, ph));
            DrawPanelBackground(g, instrument, pw, ph);
            if (mFrame->mGrid > 1)
            {
                SolidBrush dot(Color(70, 0, 0, 0));
                for (int y = 0; y < ph; y += mFrame->mGrid)
                {
                    for (int x = 0; x < pw; x += mFrame->mGrid)
                    {
                        g.FillRectangle(&dot, x, y, 1, 1);
                    }
                }
            }
            g.SetSmoothingMode(SmoothingModeAntiAlias);
            g.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);
            ControlState state;
            for (auto& control : instrument.mControls)
            {
                ControlState shown = state;
                // a few controls show a value in the editor, to see how they look
                if (control.mType == ControlType::AnalogGauge || control.mType == ControlType::TrackBar)
                {
                    shown.mHasValue = true;
                    double minimum = control.GetNumber("Minimum"), maximum = control.GetNumber("Maximum");
                    shown.mValue = minimum + (maximum - minimum) * 0.4;
                }
                DrawControl(g, control, shown, true);
            }
            g.ResetClip();
        }
        // selection
        CPen dashed(PS_DOT, 1, RGB(0, 0, 0));
        CBrush white(RGB(255, 255, 255)), black(RGB(30, 30, 30));
        CPen frame(PS_SOLID, 1, RGB(30, 30, 30));
        memory.SetBkMode(TRANSPARENT);
        for (size_t k = 0; k < doc->mSelection.size(); k++)
        {
            CRect r = RectOf(instrument.mControls[doc->mSelection[k]]);
            r.OffsetRect(origin);
            CRect outline = r;
            outline.InflateRect(1, 1);
            CPen* oldPen = memory.SelectObject(&dashed);
            CBrush* oldBrush = (CBrush*)memory.SelectStockObject(NULL_BRUSH);
            memory.Rectangle(outline);
            memory.SelectObject(&frame);
            bool primary = k == 0;
            memory.SelectObject(primary ? &white : &black);
            for (int h = 0; h < 8; h++)
            {
                if (doc->mSelection.size() > 1 && h % 2 == 1)
                {
                    continue;   // corners only when several are selected
                }
                CRect handle = HandleRect(CRect(r.left, r.top, r.right, r.bottom), h);
                memory.Rectangle(handle);
            }
            memory.SelectObject(oldPen);
            memory.SelectObject(oldBrush);
        }
        // the handle that sizes the panel
        {
            CRect handle = HandleRect(CRect(origin.x, origin.y, origin.x + pw, origin.y + ph), 4);
            CPen* oldPen = memory.SelectObject(&frame);
            CBrush* oldBrush = memory.SelectObject(&white);
            memory.Rectangle(handle);
            memory.SelectObject(oldPen);
            memory.SelectObject(oldBrush);
        }
        if (mDrag == Drag::Band)
        {
            CRect band = mBand;
            band.NormalizeRect();
            band.OffsetRect(origin);
            memory.DrawFocusRect(band);
        }
    }
    dc.BitBlt(0, 0, client.Width(), client.Height(), &memory, 0, 0, SRCCOPY);
    memory.SelectObject(old);
}

void Canvas::OnLButtonDown(UINT nFlags, CPoint point)
{
    SetFocus();
    Document* doc = mFrame->Doc();
    if (doc == nullptr)
    {
        return;
    }
    CPoint p = ToPanel(point);
    mDragStart = p;
    mChanged = false;
    mUndoPushed = false;
    mHandle = HitHandle(p);
    bool additive = (nFlags & (MK_CONTROL | MK_SHIFT)) != 0;
    if (mHandle >= 0)
    {
        mDrag = Drag::Resize;
    }
    else if (HitPanelHandle(p))
    {
        mDrag = Drag::Panel;
        mStartPanel = CSize(doc->mInstrument.PanelWidth(), doc->mInstrument.PanelHeight());
    }
    else
    {
        int index = HitControl(p);
        if (index >= 0)
        {
            auto it = std::find(doc->mSelection.begin(), doc->mSelection.end(), index);
            if (additive)
            {
                if (it != doc->mSelection.end())
                {
                    doc->mSelection.erase(it);
                    mFrame->SelectionChanged();
                    mDrag = Drag::None;
                    return;
                }
                doc->mSelection.insert(doc->mSelection.begin(), index);
            }
            else if (it == doc->mSelection.end())
            {
                doc->mSelection = { index };
            }
            else
            {
                // the clicked one becomes the reference of Format
                doc->mSelection.erase(it);
                doc->mSelection.insert(doc->mSelection.begin(), index);
            }
            mDrag = Drag::Move;
        }
        else
        {
            if (!additive)
            {
                doc->mSelection.clear();
            }
            mDrag = Drag::Band;
            mBand = CRect(p, p);
        }
        mFrame->SelectionChanged();
    }
    mStartRects.clear();
    for (int index : doc->mSelection)
    {
        mStartRects.push_back(RectOf(doc->mInstrument.mControls[index]));
    }
    SetCapture();
}

void Canvas::OnMouseMove(UINT, CPoint point)
{
    Document* doc = mFrame->Doc();
    if (doc == nullptr || mDrag == Drag::None || GetCapture() != this)
    {
        return;
    }
    CPoint p = ToPanel(point);
    int grid = (GetKeyState(VK_MENU) & 0x8000) ? 1 : mFrame->mGrid;
    CSize delta = p - mDragStart;
    if (!mChanged && abs(delta.cx) < 3 && abs(delta.cy) < 3 && mDrag != Drag::Band)
    {
        return;     // not a drag yet
    }
    auto pushUndo = [this, doc]()
    {
        if (!mUndoPushed)
        {
            doc->PushUndo();
            mUndoPushed = true;
        }
    };
    switch (mDrag)
    {
        case Drag::Move:
        {
            if (mStartRects.empty())
            {
                return;
            }
            // the reference control lands on the grid, the others keep their place relative to it
            CPoint target(Snap(mStartRects[0].left + delta.cx, grid), Snap(mStartRects[0].top + delta.cy, grid));
            CSize move = target - mStartRects[0].TopLeft();
            pushUndo();
            for (size_t k = 0; k < doc->mSelection.size() && k < mStartRects.size(); k++)
            {
                CRect r = mStartRects[k];
                r.OffsetRect(move);
                doc->mInstrument.mControls[doc->mSelection[k]].SetRect(r.left, r.top, r.Width(), r.Height());
            }
            mChanged = true;
            break;
        }
        case Drag::Resize:
        {
            if (mStartRects.empty())
            {
                return;
            }
            CRect r = mStartRects[0];
            int h = mHandle;
            if (h == 0 || h == 6 || h == 7)
            {
                r.left = std::min(Snap(r.left + delta.cx, grid), (int)r.right - 4);
            }
            if (h == 2 || h == 3 || h == 4)
            {
                r.right = std::max(Snap(r.right + delta.cx, grid), (int)r.left + 4);
            }
            if (h == 0 || h == 1 || h == 2)
            {
                r.top = std::min(Snap(r.top + delta.cy, grid), (int)r.bottom - 4);
            }
            if (h == 4 || h == 5 || h == 6)
            {
                r.bottom = std::max(Snap(r.bottom + delta.cy, grid), (int)r.top + 4);
            }
            pushUndo();
            doc->mInstrument.mControls[doc->mSelection.front()].SetRect(r.left, r.top, r.Width(), r.Height());
            mChanged = true;
            break;
        }
        case Drag::Panel:
        {
            int w = std::max(50, Snap(mStartPanel.cx + delta.cx, grid));
            int h = std::max(30, Snap(mStartPanel.cy + delta.cy, grid));
            pushUndo();
            doc->mInstrument.mPanel["Width"] = std::to_string(w);
            doc->mInstrument.mPanel["Height"] = std::to_string(h);
            mChanged = true;
            UpdateScrollBars();
            break;
        }
        case Drag::Band:
        {
            mBand = CRect(mDragStart, p);
            CRect band = mBand;
            band.NormalizeRect();
            std::vector<int> selection;
            for (int i = 0; i < (int)doc->mInstrument.mControls.size(); i++)
            {
                CRect r = RectOf(doc->mInstrument.mControls[i]);
                CRect common;
                if (common.IntersectRect(band, r))
                {
                    selection.push_back(i);
                }
            }
            doc->mSelection = selection;
            break;
        }
        default:
            break;
    }
    Invalidate(FALSE);
    mFrame->ShowStatus();
}

void Canvas::OnLButtonUp(UINT, CPoint)
{
    if (GetCapture() == this)
    {
        ReleaseCapture();
    }
    Drag drag = mDrag;
    mDrag = Drag::None;
    if (mChanged)
    {
        mFrame->SetDirty();
        mFrame->RefreshPropertyValues();
        if (drag == Drag::Panel)
        {
            mFrame->SelectionChanged();
        }
    }
    if (drag == Drag::Band)
    {
        mFrame->SelectionChanged();
    }
    Invalidate(FALSE);
}

void Canvas::OnCaptureChanged(CWnd* pWnd)
{
    if (pWnd != this && mDrag != Drag::None)
    {
        mDrag = Drag::None;
        if (mChanged)
        {
            mFrame->SetDirty();
            mFrame->RefreshPropertyValues();
        }
        Invalidate(FALSE);
    }
    CWnd::OnCaptureChanged(pWnd);
}

void Canvas::OnLButtonDblClk(UINT, CPoint point)
{
    Document* doc = mFrame->Doc();
    if (doc == nullptr)
    {
        return;
    }
    int index = HitControl(ToPanel(point));
    if (index < 0)
    {
        return;
    }
    ControlType type = doc->mInstrument.mControls[index].mType;
    if (TypeHasVariable(type))
    {
        mFrame->ChooseVariable(index);
    }
    else if (type == ControlType::Picture)
    {
        mFrame->ChooseImage(index);
    }
}

void Canvas::OnContextMenu(CWnd*, CPoint screen)
{
    Document* doc = mFrame->Doc();
    if (doc == nullptr)
    {
        return;
    }
    if (screen.x != -1 || screen.y != -1)
    {
        CPoint client = screen;
        ScreenToClient(&client);
        int index = HitControl(ToPanel(client));
        if (index >= 0 && !doc->IsSelected(index))
        {
            doc->mSelection = { index };
            mFrame->SelectionChanged();
        }
    }
    else
    {
        screen = CPoint(20, 20);
        ClientToScreen(&screen);
    }
    bool one = doc->mSelection.size() == 1;
    bool any = !doc->mSelection.empty();
    ControlType type = one ? doc->mInstrument.mControls[doc->mSelection.front()].mType : ControlType::Label;
    CMenu menu;
    menu.CreatePopupMenu();
    menu.AppendMenu(MF_STRING | (any ? 0 : MF_GRAYED), ID_CUT, "Cu&t");
    menu.AppendMenu(MF_STRING | (any ? 0 : MF_GRAYED), ID_COPY, "&Copy");
    menu.AppendMenu(MF_STRING, ID_PASTE, "&Paste");
    menu.AppendMenu(MF_STRING | (any ? 0 : MF_GRAYED), ID_DELETE, "&Delete");
    menu.AppendMenu(MF_SEPARATOR);
    menu.AppendMenu(MF_STRING | (one && TypeHasVariable(type) ? 0 : MF_GRAYED), ID_CHOOSESIGNAL, "Choose &Signal...");
    if (one && type == ControlType::Picture)
    {
        menu.AppendMenu(MF_STRING, ID_LAST, "Choose &Image...");
    }
    menu.AppendMenu(MF_SEPARATOR);
    menu.AppendMenu(MF_STRING | (any ? 0 : MF_GRAYED), ID_FRONT, "&Bring to Front");
    menu.AppendMenu(MF_STRING | (any ? 0 : MF_GRAYED), ID_BACK, "Send to Bac&k");
    UINT command = menu.TrackPopupMenu(TPM_RETURNCMD | TPM_RIGHTBUTTON, screen.x, screen.y, this);
    if (command == ID_LAST)
    {
        mFrame->ChooseImage(doc->mSelection.front());
    }
    else if (command != 0)
    {
        mFrame->SendMessage(WM_COMMAND, command);
    }
}

UINT Canvas::OnGetDlgCode()
{
    return DLGC_WANTARROWS | DLGC_WANTCHARS;
}

void Canvas::OnKeyDown(UINT nChar, UINT nRepCnt, UINT nFlags)
{
    Document* doc = mFrame->Doc();
    if (doc == nullptr)
    {
        return;
    }
    int dx = 0, dy = 0;
    switch (nChar)
    {
        case VK_LEFT:
            dx = -1;
            break;
        case VK_RIGHT:
            dx = 1;
            break;
        case VK_UP:
            dy = -1;
            break;
        case VK_DOWN:
            dy = 1;
            break;
        case VK_ESCAPE:
            doc->mSelection.clear();
            mFrame->SelectionChanged();
            return;
        case VK_TAB:
        {
            int count = (int)doc->mInstrument.mControls.size();
            if (count > 0)
            {
                int current = doc->mSelection.empty() ? -1 : doc->mSelection.front();
                bool back = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
                int next = back ? (current <= 0 ? count - 1 : current - 1) : (current + 1) % count;
                doc->mSelection = { next };
                mFrame->SelectionChanged();
            }
            return;
        }
        default:
            CWnd::OnKeyDown(nChar, nRepCnt, nFlags);
            return;
    }
    if (doc->mSelection.empty())
    {
        return;
    }
    bool resize = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
    int step = (GetKeyState(VK_CONTROL) & 0x8000) ? std::max(mFrame->mGrid, 8) : 1;
    doc->PushUndo();
    for (int index : doc->mSelection)
    {
        Control& c = doc->mInstrument.mControls[index];
        if (resize)
        {
            c.SetRect(c.X(), c.Y(), c.W() + dx * step, c.H() + dy * step);
        }
        else
        {
            c.SetRect(c.X() + dx * step, c.Y() + dy * step, c.W(), c.H());
        }
    }
    mFrame->SetDirty();
    mFrame->RefreshPropertyValues();
}

BOOL Canvas::OnSetCursor(CWnd* pWnd, UINT nHitTest, UINT message)
{
    Document* doc = mFrame->Doc();
    if (nHitTest != HTCLIENT || doc == nullptr)
    {
        return CWnd::OnSetCursor(pWnd, nHitTest, message);
    }
    CPoint point;
    GetCursorPos(&point);
    ScreenToClient(&point);
    CPoint p = ToPanel(point);
    LPCTSTR cursor = IDC_ARROW;
    int handle = HitHandle(p);
    if (handle >= 0)
    {
        static const LPCTSTR cursors[8] = { IDC_SIZENWSE, IDC_SIZENS, IDC_SIZENESW, IDC_SIZEWE, IDC_SIZENWSE, IDC_SIZENS, IDC_SIZENESW, IDC_SIZEWE };
        cursor = cursors[handle];
    }
    else if (HitPanelHandle(p))
    {
        cursor = IDC_SIZENWSE;
    }
    else if (HitControl(p) >= 0)
    {
        cursor = IDC_SIZEALL;
    }
    ::SetCursor(::LoadCursor(nullptr, cursor));
    return TRUE;
}

bool Canvas::IsOverCanvas(CPoint screen)
{
    CWnd* under = WindowFromPoint(screen);
    return under == this;
}

void Canvas::Drop(ControlType type, CPoint screen)
{
    CPoint client = screen;
    ScreenToClient(&client);
    mFrame->AddControl(type, ToPanel(client), true);
}

// ---- Toolbox
BEGIN_MESSAGE_MAP(Toolbox, CWnd)
    ON_WM_PAINT()
    ON_WM_ERASEBKGND()
    ON_WM_LBUTTONDOWN()
    ON_WM_LBUTTONUP()
    ON_WM_LBUTTONDBLCLK()
    ON_WM_MOUSEMOVE()
    ON_MESSAGE(WM_MOUSELEAVE, OnMouseLeave)
    ON_WM_SETCURSOR()
END_MESSAGE_MAP()

BOOL Toolbox::Create(CWnd* parent, UINT id)
{
    RegisterClass(kToolboxClass, CS_DBLCLKS);
    return CWnd::CreateEx(WS_EX_CLIENTEDGE, kToolboxClass, "", WS_CHILD | WS_VISIBLE, CRect(0, 0, 10, 10), parent, id);
}

int Toolbox::ItemAt(CPoint point) const
{
    if (point.y < kToolHeader)
    {
        return -1;
    }
    int index = (point.y - kToolHeader) / kToolRow;
    return index < (int)ControlType::Count ? index : -1;
}

BOOL Toolbox::OnEraseBkgnd(CDC*)
{
    return TRUE;
}

void Toolbox::OnPaint()
{
    CPaintDC dc(this);
    CRect client;
    GetClientRect(&client);
    CDC memory;
    memory.CreateCompatibleDC(&dc);
    CBitmap bitmap;
    bitmap.CreateCompatibleBitmap(&dc, std::max(1, client.Width()), std::max(1, client.Height()));
    CBitmap* old = memory.SelectObject(&bitmap);
    memory.FillSolidRect(client, GetSysColor(COLOR_WINDOW));
    CFont* oldFont = memory.SelectObject(GetParent()->GetFont() != nullptr ? GetParent()->GetFont() : CFont::FromHandle((HFONT)GetStockObject(DEFAULT_GUI_FONT)));
    memory.SetBkMode(TRANSPARENT);
    CRect header(0, 0, client.right, kToolHeader);
    memory.FillSolidRect(header, RGB(230, 232, 240));
    memory.SetTextColor(RGB(40, 40, 40));
    CRect headerText = header;
    headerText.left += 8;
    memory.DrawText("Toolbox", headerText, DT_SINGLELINE | DT_VCENTER | DT_LEFT);
    {
        Graphics g(memory.GetSafeHdc());
        g.SetSmoothingMode(SmoothingModeAntiAlias);
        g.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);
        for (int i = 0; i < (int)ControlType::Count; i++)
        {
            int top = kToolHeader + i * kToolRow;
            if (i == mHover || i == mDragging)
            {
                SolidBrush hover(Color(255, 220, 230, 248));
                Pen border(Color(255, 150, 180, 230), 1);
                g.FillRectangle(&hover, 2, top + 1, client.Width() - 5, kToolRow - 3);
                g.DrawRectangle(&border, 2, top + 1, client.Width() - 5, kToolRow - 3);
            }
            DrawToolIcon(g, (ControlType)i, 8, (REAL)top + 4, 22);
        }
    }
    memory.SetTextColor(RGB(0, 0, 0));
    for (int i = 0; i < (int)ControlType::Count; i++)
    {
        CRect text(38, kToolHeader + i * kToolRow, client.right - 4, kToolHeader + (i + 1) * kToolRow);
        memory.DrawText(TypeTitle((ControlType)i), text, DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_END_ELLIPSIS);
    }
    CRect hint(8, kToolHeader + (int)ControlType::Count * kToolRow + 8, client.right - 6, client.bottom - 4);
    memory.SetTextColor(RGB(110, 110, 110));
    memory.DrawText("Drag a control onto the panel, or double click it.", hint, DT_WORDBREAK | DT_LEFT);
    memory.SelectObject(oldFont);
    dc.BitBlt(0, 0, client.Width(), client.Height(), &memory, 0, 0, SRCCOPY);
    memory.SelectObject(old);
}

void Toolbox::OnLButtonDown(UINT, CPoint point)
{
    mDragging = ItemAt(point);
    mDragStart = point;
    mDragStarted = false;
    if (mDragging >= 0)
    {
        SetCapture();
        Invalidate(FALSE);
    }
}

void Toolbox::OnMouseMove(UINT, CPoint point)
{
    if (!mTracking)
    {
        TRACKMOUSEEVENT track = { sizeof(track), TME_LEAVE, m_hWnd, 0 };
        TrackMouseEvent(&track);
        mTracking = true;
    }
    if (mDragging >= 0 && GetCapture() == this)
    {
        if (!mDragStarted && (abs(point.x - mDragStart.x) > 4 || abs(point.y - mDragStart.y) > 4))
        {
            mDragStarted = true;
        }
        if (mDragStarted)
        {
            CPoint screen = point;
            ClientToScreen(&screen);
            ::SetCursor(::LoadCursor(nullptr, mFrame->mCanvas.IsOverCanvas(screen) ? IDC_CROSS : IDC_NO));
        }
        return;
    }
    int hover = ItemAt(point);
    if (hover != mHover)
    {
        mHover = hover;
        Invalidate(FALSE);
    }
}

void Toolbox::OnLButtonUp(UINT, CPoint point)
{
    int dragging = mDragging;
    bool started = mDragStarted;
    mDragging = -1;
    mDragStarted = false;
    if (GetCapture() == this)
    {
        ReleaseCapture();
    }
    Invalidate(FALSE);
    if (dragging >= 0 && started)
    {
        CPoint screen = point;
        ClientToScreen(&screen);
        if (mFrame->mCanvas.IsOverCanvas(screen))
        {
            mFrame->mCanvas.Drop((ControlType)dragging, screen);
        }
    }
}

void Toolbox::OnLButtonDblClk(UINT, CPoint point)
{
    int index = ItemAt(point);
    Document* doc = mFrame->Doc();
    if (index < 0 || doc == nullptr)
    {
        return;
    }
    int n = (int)doc->mInstrument.mControls.size() % 8;
    mFrame->AddControl((ControlType)index, CPoint(16 + n * 16, 16 + n * 16), false);
}

LRESULT Toolbox::OnMouseLeave(WPARAM, LPARAM)
{
    mTracking = false;
    if (mHover >= 0)
    {
        mHover = -1;
        Invalidate(FALSE);
    }
    return 0;
}

BOOL Toolbox::OnSetCursor(CWnd* pWnd, UINT nHitTest, UINT message)
{
    if (mDragStarted)
    {
        return TRUE;
    }
    return CWnd::OnSetCursor(pWnd, nHitTest, message);
}
}
