#include "stdafx.h"
#include "Editor.h"
#include "Host.h"
#include "Render.h"
#include "VariableDlg.h"

#include <algorithm>
#include <fstream>
#include <functional>
#include <sstream>

using namespace Gdiplus;

namespace Instruments
{
namespace
{
const UINT kTabsId = 100, kToolboxId = 101, kCanvasId = 102, kPropsId = 103;
const int kToolboxWidth = 170;
const int kPropsWidth = 320;
const char* const kFilter = "BUSMASTER instruments (*.bminst)|*.bminst|All files (*.*)|*.*||";
const char* const kClipboardFormat = "BUSMASTER Instrument Controls";

EditorFrame* gEditor = nullptr;
HHOOK gHook = nullptr;

// ---- property window: a property with a button that opens a dialog (signal, image, color)
class ButtonProperty : public CMFCPropertyGridProperty
{
public:
    std::string mValue;                                         // the real value, the shown text may differ
    std::function<bool(std::string&)> mChoose;                  // opens the dialog; true if the value changed
    std::function<std::string(const std::string&)> mDisplay;    // text shown for a value
    bool mSwatch = false;                                       // a color: shows a sample

    ButtonProperty(const CString& name, const std::string& value, LPCTSTR description, bool editable)
        : CMFCPropertyGridProperty(name, COleVariant(""), description), mValue(value)
    {
        m_dwFlags = 0x0002;     // AFX_PROP_HAS_BUTTON
        AllowEdit(editable ? TRUE : FALSE);
    }

    BOOL HasButton() const override
    {
        return TRUE;
    }

    void SetRealValue(const std::string& value)
    {
        mValue = value;
        CMFCPropertyGridProperty::SetValue(COleVariant(mDisplay ? mDisplay(value).c_str() : value.c_str()));
    }

    void OnClickButton(CPoint) override
    {
        std::string value = mValue;
        if (mChoose && mChoose(value))
        {
            SetRealValue(value);
            m_pWndList->OnPropertyChanged(this);
        }
    }

    void OnDrawValue(CDC* pDC, CRect rect) override
    {
        unsigned long color;
        if (mSwatch && ParseColor(mValue, color))
        {
            CRect sample(rect.left + 2, rect.top + 2, rect.left + 2 + rect.Height() - 4, rect.bottom - 2);
            pDC->FillSolidRect(sample, color);
            pDC->Draw3dRect(sample, RGB(80, 80, 80), RGB(80, 80, 80));
            rect.left = sample.right + 2;
        }
        CMFCPropertyGridProperty::OnDrawValue(pDC, rect);
    }
};

std::string VariantText(const COleVariant& value)
{
    COleVariant copy(value);
    try
    {
        copy.ChangeType(VT_BSTR);
    }
    catch (...)
    {
        return "";
    }
    return std::string(CString(copy.bstrVal));
}

std::string FileName(const std::string& path)
{
    size_t slash = path.find_last_of("\\/");
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

// the text box being typed in on an instrument panel in BUSMASTER
bool IsPanelEdit(HWND hwnd)
{
    char name[64] = "";
    HWND parent = ::GetParent(hwnd);
    return parent != nullptr && ::GetClassName(parent, name, sizeof(name)) > 0 && strcmp(name, "BusmasterInstrumentPanel") == 0;
}

LRESULT CALLBACK GetMessageHook(int code, WPARAM wParam, LPARAM lParam)
{
    MSG* msg = (MSG*)lParam;
    bool keyboard = code >= 0 && wParam == PM_REMOVE && msg->message >= WM_KEYFIRST && msg->message <= WM_KEYLAST && msg->hwnd != nullptr;
    if (keyboard && IsPanelEdit(msg->hwnd))
    {
        // Enter and Escape go to the text box, not to the accelerators of BUSMASTER
        ::TranslateMessage(msg);
        ::DispatchMessage(msg);
        msg->message = WM_NULL;
    }
    else if (keyboard && gEditor != nullptr)
    {
        if (gEditor->GetSafeHwnd() != nullptr && (msg->hwnd == gEditor->m_hWnd || ::IsChild(gEditor->m_hWnd, msg->hwnd)))
        {
            AFX_MANAGE_STATE(AfxGetStaticModuleState());
            // the editor takes its keys: they must not reach BUSMASTER (its accelerators, key handlers of nodes)
            if (!CWnd::WalkPreTranslateTree(gEditor->m_hWnd, msg))
            {
                ::TranslateMessage(msg);
                ::DispatchMessage(msg);
            }
            msg->message = WM_NULL;
        }
    }
    return CallNextHookEx(gHook, code, wParam, lParam);
}
}

void InstallMessageHook()
{
    if (gHook == nullptr)
    {
        gHook = SetWindowsHookEx(WH_GETMESSAGE, GetMessageHook, nullptr, GetCurrentThreadId());
    }
}

void RemoveMessageHook()
{
    if (gHook != nullptr)
    {
        UnhookWindowsHookEx(gHook);
        gHook = nullptr;
    }
}

// ---- Document
std::string Document::Title() const
{
    std::string name = mPath.empty() ? mUntitledName : FileName(mPath);
    return mModified ? name + " *" : name;
}

void Document::PushUndo()
{
    mUndo.push_back(mInstrument.ToXml());
    if (mUndo.size() > 100)
    {
        mUndo.erase(mUndo.begin());
    }
    mRedo.clear();
    mModified = true;
}

bool Document::IsSelected(int index) const
{
    return std::find(mSelection.begin(), mSelection.end(), index) != mSelection.end();
}

std::vector<Control> Document::SelectedControls() const
{
    std::vector<int> sorted = mSelection;
    std::sort(sorted.begin(), sorted.end());
    std::vector<Control> result;
    for (int index : sorted)
    {
        result.push_back(mInstrument.mControls[index]);
    }
    return result;
}

// ---- EditorFrame
BEGIN_MESSAGE_MAP(EditorFrame, CFrameWnd)
    ON_WM_CREATE()
    ON_WM_CLOSE()
    ON_WM_DESTROY()
    ON_WM_INITMENUPOPUP()
    ON_WM_GETMINMAXINFO()
    ON_NOTIFY(TCN_SELCHANGE, kTabsId, OnTabChange)
    ON_REGISTERED_MESSAGE(AFX_WM_PROPERTY_CHANGED, OnPropertyChanged)
END_MESSAGE_MAP()

EditorFrame::EditorFrame()
{
    m_bAutoMenuEnable = FALSE;
}

EditorFrame::~EditorFrame()
{
    if (mAccel != nullptr)
    {
        DestroyAcceleratorTable(mAccel);
    }
    if (gEditor == this)
    {
        gEditor = nullptr;
    }
}

EditorFrame* EditorFrame::Current()
{
    return gEditor;
}

EditorFrame* EditorFrame::Open(HWND owner)
{
    if (gEditor != nullptr)
    {
        if (gEditor->IsIconic())
        {
            gEditor->ShowWindow(SW_RESTORE);
        }
        gEditor->SetForegroundWindow();
        return gEditor;
    }
    EditorFrame* frame = new EditorFrame();
    CRect rect(80, 60, 1180, 800);
    if (!frame->CreateEx(WS_EX_APPWINDOW, AfxRegisterWndClass(CS_DBLCLKS, ::LoadCursor(nullptr, IDC_ARROW), nullptr,
                         AfxGetApp()->LoadIcon(IDI_INSTRUMENTS)),
                         "BUSMASTER Instrument Editor", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, rect, CWnd::FromHandle(owner), 0))
    {
        return nullptr;
    }
    gEditor = frame;
    InstallMessageHook();
    frame->NewDocument();
    frame->ShowWindow(SW_SHOW);
    frame->UpdateWindow();
    return frame;
}

BOOL EditorFrame::PreCreateWindow(CREATESTRUCT& cs)
{
    if (!CFrameWnd::PreCreateWindow(cs))
    {
        return FALSE;
    }
    cs.dwExStyle &= ~WS_EX_CLIENTEDGE;
    return TRUE;
}

int EditorFrame::OnCreate(LPCREATESTRUCT lpCreateStruct)
{
    if (CFrameWnd::OnCreate(lpCreateStruct) == -1)
    {
        return -1;
    }
    NONCLIENTMETRICS metrics = { sizeof(metrics) };
    SystemParametersInfo(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0);
    mFont.CreateFontIndirect(&metrics.lfMessageFont);

    BuildMenu();
    static UINT indicators[] = { ID_SEPARATOR };
    mStatus.Create(this);
    mStatus.SetIndicators(indicators, 1);

    mTabs.Create(WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | TCS_FOCUSNEVER, CRect(0, 0, 10, 10), this, kTabsId);
    mTabs.SetFont(&mFont);
    mToolbox.mFrame = this;
    mToolbox.Create(this, kToolboxId);
    mCanvas.mFrame = this;
    mCanvas.Create(this, kCanvasId);
    mProps.mFrame = this;
    mProps.Create(WS_CHILD | WS_VISIBLE | WS_BORDER | WS_TABSTOP, CRect(0, 0, 10, 10), this, kPropsId);
    mProps.EnableHeaderCtrl(FALSE);
    mProps.EnableDescriptionArea(TRUE);
    mProps.SetVSDotNetLook(TRUE);
    mProps.MarkModifiedProperties(FALSE);
    mProps.SetFont(&mFont);
    return 0;
}

void EditorFrame::BuildMenu()
{
    CMenu file, edit, view, format, help, align, size, hspace, vspace, center, order;
    file.CreatePopupMenu();
    file.AppendMenu(MF_STRING, ID_NEW, "&New\tCtrl+N");
    file.AppendMenu(MF_STRING, ID_OPEN, "&Open...\tCtrl+O");
    file.AppendMenu(MF_STRING, ID_SAVE, "&Save\tCtrl+S");
    file.AppendMenu(MF_STRING, ID_SAVEAS, "Save &As...");
    file.AppendMenu(MF_SEPARATOR);
    file.AppendMenu(MF_STRING, ID_CLOSE, "&Close\tCtrl+F4");
    file.AppendMenu(MF_STRING, ID_CLOSEALL, "Close A&ll");
    file.AppendMenu(MF_STRING, ID_CLOSEOTHERS, "Close All &But This One");
    file.AppendMenu(MF_SEPARATOR);
    file.AppendMenu(MF_STRING, ID_EXIT, "E&xit");

    edit.CreatePopupMenu();
    edit.AppendMenu(MF_STRING, ID_UNDO, "&Undo\tCtrl+Z");
    edit.AppendMenu(MF_STRING, ID_REDO, "&Redo\tCtrl+Y");
    edit.AppendMenu(MF_SEPARATOR);
    edit.AppendMenu(MF_STRING, ID_CUT, "Cu&t\tCtrl+X");
    edit.AppendMenu(MF_STRING, ID_COPY, "&Copy\tCtrl+C");
    edit.AppendMenu(MF_STRING, ID_PASTE, "&Paste\tCtrl+V");
    edit.AppendMenu(MF_STRING, ID_DELETE, "&Delete\tDel");
    edit.AppendMenu(MF_SEPARATOR);
    edit.AppendMenu(MF_STRING, ID_SELECTALL, "Select &All\tCtrl+A");
    edit.AppendMenu(MF_SEPARATOR);
    edit.AppendMenu(MF_STRING, ID_CHOOSESIGNAL, "Choose &Signal...\tCtrl+L");

    view.CreatePopupMenu();
    view.AppendMenu(MF_STRING, ID_VIEW_PROPS, "&Property Window\tF4");
    view.AppendMenu(MF_STRING, ID_VIEW_TOOLBOX, "&Toolbox\tCtrl+Alt+X");
    view.AppendMenu(MF_SEPARATOR);
    view.AppendMenu(MF_STRING, ID_VIEW_SNAP, "&Snap to Grid");

    align.CreatePopupMenu();
    align.AppendMenu(MF_STRING, ID_ALIGN_LEFT, "&Lefts");
    align.AppendMenu(MF_STRING, ID_ALIGN_CENTER, "&Centers");
    align.AppendMenu(MF_STRING, ID_ALIGN_RIGHT, "&Rights");
    align.AppendMenu(MF_SEPARATOR);
    align.AppendMenu(MF_STRING, ID_ALIGN_TOP, "&Tops");
    align.AppendMenu(MF_STRING, ID_ALIGN_MIDDLE, "&Middles");
    align.AppendMenu(MF_STRING, ID_ALIGN_BOTTOM, "&Bottoms");
    align.AppendMenu(MF_SEPARATOR);
    align.AppendMenu(MF_STRING, ID_ALIGN_GRID, "to &Grid");
    size.CreatePopupMenu();
    size.AppendMenu(MF_STRING, ID_SIZE_WIDTH, "&Width");
    size.AppendMenu(MF_STRING, ID_SIZE_HEIGHT, "&Height");
    size.AppendMenu(MF_STRING, ID_SIZE_BOTH, "&Both");
    hspace.CreatePopupMenu();
    hspace.AppendMenu(MF_STRING, ID_HSPACE_EQUAL, "Make &Equal");
    hspace.AppendMenu(MF_STRING, ID_HSPACE_INC, "&Increase");
    hspace.AppendMenu(MF_STRING, ID_HSPACE_DEC, "&Decrease");
    hspace.AppendMenu(MF_STRING, ID_HSPACE_REMOVE, "&Remove");
    vspace.CreatePopupMenu();
    vspace.AppendMenu(MF_STRING, ID_VSPACE_EQUAL, "Make &Equal");
    vspace.AppendMenu(MF_STRING, ID_VSPACE_INC, "&Increase");
    vspace.AppendMenu(MF_STRING, ID_VSPACE_DEC, "&Decrease");
    vspace.AppendMenu(MF_STRING, ID_VSPACE_REMOVE, "&Remove");
    center.CreatePopupMenu();
    center.AppendMenu(MF_STRING, ID_CENTER_H, "&Horizontally");
    center.AppendMenu(MF_STRING, ID_CENTER_V, "&Vertically");
    order.CreatePopupMenu();
    order.AppendMenu(MF_STRING, ID_FRONT, "&Bring to Front");
    order.AppendMenu(MF_STRING, ID_BACK, "&Send to Back");

    format.CreatePopupMenu();
    format.AppendMenu(MF_POPUP, (UINT_PTR)align.Detach(), "&Align");
    format.AppendMenu(MF_POPUP, (UINT_PTR)size.Detach(), "Make Same &Size");
    format.AppendMenu(MF_SEPARATOR);
    format.AppendMenu(MF_POPUP, (UINT_PTR)hspace.Detach(), "&Horizontal Spacing");
    format.AppendMenu(MF_POPUP, (UINT_PTR)vspace.Detach(), "&Vertical Spacing");
    format.AppendMenu(MF_SEPARATOR);
    format.AppendMenu(MF_POPUP, (UINT_PTR)center.Detach(), "&Center in Form");
    format.AppendMenu(MF_SEPARATOR);
    format.AppendMenu(MF_POPUP, (UINT_PTR)order.Detach(), "&Order");

    help.CreatePopupMenu();
    help.AppendMenu(MF_STRING, ID_ABOUT, "&About the Instrument Editor");

    mMenu.CreateMenu();
    mMenu.AppendMenu(MF_POPUP, (UINT_PTR)file.Detach(), "&File");
    mMenu.AppendMenu(MF_POPUP, (UINT_PTR)edit.Detach(), "&Edit");
    mMenu.AppendMenu(MF_POPUP, (UINT_PTR)view.Detach(), "&View");
    mMenu.AppendMenu(MF_POPUP, (UINT_PTR)format.Detach(), "F&ormat");
    mMenu.AppendMenu(MF_POPUP, (UINT_PTR)help.Detach(), "&Help");
    SetMenu(&mMenu);

    ACCEL keys[] =
    {
        { FVIRTKEY | FCONTROL, 'N', (WORD)ID_NEW },
        { FVIRTKEY | FCONTROL, 'O', (WORD)ID_OPEN },
        { FVIRTKEY | FCONTROL, 'S', (WORD)ID_SAVE },
        { FVIRTKEY | FCONTROL, VK_F4, (WORD)ID_CLOSE },
        { FVIRTKEY, VK_F4, (WORD)ID_VIEW_PROPS },
        { FVIRTKEY | FCONTROL | FALT, 'X', (WORD)ID_VIEW_TOOLBOX },
        { FVIRTKEY | FCONTROL, 'Z', (WORD)ID_UNDO },
        { FVIRTKEY | FCONTROL, 'Y', (WORD)ID_REDO },
        { FVIRTKEY | FCONTROL, 'X', (WORD)ID_CUT },
        { FVIRTKEY | FCONTROL, 'C', (WORD)ID_COPY },
        { FVIRTKEY | FCONTROL, 'V', (WORD)ID_PASTE },
        { FVIRTKEY, VK_DELETE, (WORD)ID_DELETE },
        { FVIRTKEY | FCONTROL, 'A', (WORD)ID_SELECTALL },
        { FVIRTKEY | FCONTROL, 'L', (WORD)ID_CHOOSESIGNAL },
    };
    mAccel = CreateAcceleratorTable(keys, _countof(keys));
}

BOOL EditorFrame::PreTranslateMessage(MSG* pMsg)
{
    if (pMsg->message >= WM_KEYFIRST && pMsg->message <= WM_KEYLAST && mAccel != nullptr)
    {
        // the edit commands are for the panel; in the property window the keys go to the text being typed
        HWND focus = ::GetFocus();
        bool inProperties = focus != nullptr && (focus == mProps.m_hWnd || ::IsChild(mProps.m_hWnd, focus));
        if (inProperties && pMsg->message == WM_KEYDOWN)
        {
            bool control = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
            WPARAM key = pMsg->wParam;
            bool editKey = key == VK_DELETE || (control && (key == 'Z' || key == 'Y' || key == 'X' || key == 'C' || key == 'V' || key == 'A'));
            if (editKey && focus != mProps.m_hWnd)
            {
                return CFrameWnd::PreTranslateMessage(pMsg);
            }
        }
        if (::TranslateAccelerator(m_hWnd, mAccel, pMsg))
        {
            return TRUE;
        }
    }
    return CFrameWnd::PreTranslateMessage(pMsg);
}

void EditorFrame::RecalcLayout(BOOL bNotify)
{
    CFrameWnd::RecalcLayout(bNotify);
    if (mTabs.GetSafeHwnd() == nullptr)
    {
        return;
    }
    CRect area;
    RepositionBars(AFX_IDW_CONTROLBAR_FIRST, AFX_IDW_CONTROLBAR_LAST, AFX_IDW_PANE_FIRST, reposQuery, &area);
    int left = area.left, right = area.right;
    mToolbox.ShowWindow(mShowToolbox ? SW_SHOW : SW_HIDE);
    mProps.ShowWindow(mShowProps ? SW_SHOW : SW_HIDE);
    if (mShowToolbox)
    {
        mToolbox.MoveWindow(left, area.top, kToolboxWidth, area.Height());
        left += kToolboxWidth;
    }
    if (mShowProps)
    {
        int width = std::min(kPropsWidth, std::max(150, (right - left) / 2));
        mProps.MoveWindow(right - width, area.top, width, area.Height());
        right -= width;
    }
    CRect tabs(left, area.top, right, area.top + 26);
    mTabs.MoveWindow(tabs);
    CRect display = tabs;
    mTabs.AdjustRect(FALSE, &display);
    int top = std::max(display.top, (LONG)area.top + 24);
    mCanvas.MoveWindow(left, top, std::max(0, right - left), std::max(0, (int)area.bottom - top));
}

void EditorFrame::OnGetMinMaxInfo(MINMAXINFO* lpMMI)
{
    lpMMI->ptMinTrackSize = CPoint(600, 400);
}

Document* EditorFrame::Doc()
{
    if (mActive < 0 || mActive >= (int)mDocs.size())
    {
        return nullptr;
    }
    return mDocs[mActive].get();
}

void EditorFrame::NewDocument()
{
    auto doc = std::make_unique<Document>();
    doc->mUntitledName = "Instrument" + std::to_string(++mUntitledCount);
    doc->mInstrument.mPanel["Title"] = doc->mUntitledName;
    mDocs.push_back(std::move(doc));
    UpdateTabs();
    Activate((int)mDocs.size() - 1);
}

void EditorFrame::OpenFile(const std::string& path)
{
    for (size_t i = 0; i < mDocs.size(); i++)
    {
        if (_stricmp(mDocs[i]->mPath.c_str(), path.c_str()) == 0)
        {
            Activate((int)i);
            return;
        }
    }
    auto doc = std::make_unique<Document>();
    std::string error;
    if (!doc->mInstrument.Load(path, error))
    {
        MessageBox(error.c_str(), "Instrument Editor", MB_ICONWARNING);
        return;
    }
    doc->mPath = path;
    // an empty untitled instrument (the one the editor starts with) is replaced
    Document* current = Doc();
    if (current != nullptr && current->mPath.empty() && !current->mModified && current->mInstrument.mControls.empty())
    {
        mDocs[mActive] = std::move(doc);
        UpdateTabs();
        Activate(mActive);
        return;
    }
    mDocs.push_back(std::move(doc));
    UpdateTabs();
    Activate((int)mDocs.size() - 1);
}

void EditorFrame::UpdateTabs()
{
    mTabs.DeleteAllItems();
    for (size_t i = 0; i < mDocs.size(); i++)
    {
        mTabs.InsertItem((int)i, mDocs[i]->Title().c_str());
    }
    if (mActive >= 0 && mActive < (int)mDocs.size())
    {
        mTabs.SetCurSel(mActive);
    }
}

void EditorFrame::Activate(int index)
{
    if (index < 0 || index >= (int)mDocs.size())
    {
        return;
    }
    mActive = index;
    mTabs.SetCurSel(index);
    mCanvas.UpdateScrollBars();
    mCanvas.Invalidate();
    SelectionChanged();
    std::string title = "BUSMASTER Instrument Editor - " + Doc()->Title();
    SetWindowText(title.c_str());
}

void EditorFrame::OnTabChange(NMHDR*, LRESULT* pResult)
{
    Activate(mTabs.GetCurSel());
    *pResult = 0;
}

void EditorFrame::SetDirty()
{
    Document* doc = Doc();
    if (doc == nullptr)
    {
        return;
    }
    doc->mModified = true;
    // the selection keeps only existing controls
    doc->mSelection.erase(std::remove_if(doc->mSelection.begin(), doc->mSelection.end(), [doc](int i)
    {
        return i < 0 || i >= (int)doc->mInstrument.mControls.size();
    }), doc->mSelection.end());
    TCITEM item = {};
    item.mask = TCIF_TEXT;
    std::string tab = doc->Title();
    item.pszText = (LPSTR)tab.c_str();
    mTabs.SetItem(mActive, &item);
    std::string title = "BUSMASTER Instrument Editor - " + tab;
    SetWindowText(title.c_str());
    mCanvas.UpdateScrollBars();
    mCanvas.Invalidate(FALSE);
    ShowStatus();
}

void EditorFrame::SelectionChanged()
{
    BuildPropertyGrid();
    mCanvas.Invalidate(FALSE);
    ShowStatus();
}

void EditorFrame::ShowStatus()
{
    Document* doc = Doc();
    if (doc == nullptr || mStatus.GetSafeHwnd() == nullptr)
    {
        return;
    }
    std::string text;
    if (doc->mSelection.empty())
    {
        text = "Panel " + std::to_string(doc->mInstrument.PanelWidth()) + " x " + std::to_string(doc->mInstrument.PanelHeight()) +
               ", " + std::to_string(doc->mInstrument.mControls.size()) + " controls";
    }
    else
    {
        const Control& c = doc->mInstrument.mControls[doc->mSelection.front()];
        text = c.Get("Name") + " (" + TypeTitle(c.mType) + ")  at " + std::to_string(c.X()) + ", " + std::to_string(c.Y()) +
               "  size " + std::to_string(c.W()) + " x " + std::to_string(c.H());
        if (!c.Get("Variable").empty())
        {
            text += "  signal " + VariableLabel(c.Get("Variable"));
        }
        if (doc->mSelection.size() > 1)
        {
            text += "  (" + std::to_string(doc->mSelection.size()) + " selected)";
        }
    }
    mStatus.SetPaneText(0, text.c_str());
}

// ---- property window
void EditorFrame::BuildPropertyGrid()
{
    if (mProps.GetSafeHwnd() == nullptr)
    {
        return;
    }
    mBuildingGrid = true;
    mProps.SetRedraw(FALSE);
    mProps.RemoveAll();
    Document* doc = Doc();
    if (doc != nullptr)
    {
        bool panel = doc->mSelection.empty();
        const Control* control = panel ? nullptr : &doc->mInstrument.mControls[doc->mSelection.front()];
        const std::vector<PropDef>& defs = panel ? PanelPropDefs() : PropDefs(control->mType);
        std::map<std::string, CMFCPropertyGridProperty*> groups;
        std::vector<std::string> order;
        for (auto& def : defs)
        {
            std::string value = panel ? doc->mInstrument.GetPanel(def.mName) : control->Get(def.mName);
            CMFCPropertyGridProperty* prop = nullptr;
            switch (def.mKind)
            {
                case PropKind::Integer:
                    prop = new CMFCPropertyGridProperty(def.mName, COleVariant((long)atol(value.c_str()), VT_I4), def.mDescription);
                    break;
                case PropKind::Bool:
                    prop = new CMFCPropertyGridProperty(def.mName, COleVariant((short)(value == "true" ? VARIANT_TRUE : VARIANT_FALSE), VT_BOOL),
                                                        def.mDescription);
                    break;
                case PropKind::Choice:
                {
                    prop = new CMFCPropertyGridProperty(def.mName, COleVariant(value.c_str()), def.mDescription);
                    for (auto& choice : def.mChoices)
                    {
                        prop->AddOption(choice.c_str());
                    }
                    prop->AllowEdit(FALSE);
                    break;
                }
                case PropKind::Color:
                {
                    auto button = new ButtonProperty(def.mName, value, def.mDescription, true);
                    button->mSwatch = true;
                    button->mDisplay = [](const std::string& v)
                    {
                        return v.empty() ? std::string("None") : v;
                    };
                    button->mChoose = [this](std::string& v)
                    {
                        unsigned long color = RGB(255, 255, 255);
                        ParseColor(v, color);
                        CColorDialog dialog(color, CC_FULLOPEN, this);
                        if (dialog.DoModal() != IDOK)
                        {
                            return false;
                        }
                        v = ColorText(dialog.GetColor());
                        return true;
                    };
                    button->SetRealValue(value);
                    prop = button;
                    break;
                }
                case PropKind::Variable:
                {
                    auto button = new ButtonProperty(def.mName, value, def.mDescription, false);
                    button->mDisplay = [](const std::string& v)
                    {
                        return v.empty() ? std::string("(none)") : VariableLabel(v);
                    };
                    button->mChoose = [this](std::string& v)
                    {
                        VariableDlg dialog(v, this);
                        if (dialog.DoModal() != IDOK)
                        {
                            return false;
                        }
                        v = dialog.mPath;
                        return true;
                    };
                    button->SetRealValue(value);
                    prop = button;
                    break;
                }
                case PropKind::Image:
                {
                    auto button = new ButtonProperty(def.mName, value, def.mDescription, false);
                    button->mDisplay = [](const std::string& v)
                    {
                        if (v.empty())
                        {
                            return std::string("(none)");
                        }
                        return "(image, " + std::to_string(v.size() * 3 / 4 / 1024 + 1) + " kB)";
                    };
                    button->mChoose = [this](std::string& v)
                    {
                        CFileDialog dialog(TRUE, nullptr, nullptr, OFN_FILEMUSTEXIST | OFN_HIDEREADONLY,
                                           "Images (*.png;*.jpg;*.jpeg;*.bmp;*.gif)|*.png;*.jpg;*.jpeg;*.bmp;*.gif|All files (*.*)|*.*||", this);
                        if (dialog.DoModal() != IDOK)
                        {
                            return false;
                        }
                        std::ifstream file((const char*)dialog.GetPathName(), std::ios::binary);
                        std::vector<unsigned char> data((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
                        if (data.empty())
                        {
                            return false;
                        }
                        v = Base64Encode(data);
                        return true;
                    };
                    button->SetRealValue(value);
                    prop = button;
                    break;
                }
                default:        // Text, Number
                    prop = new CMFCPropertyGridProperty(def.mName, COleVariant(value.c_str()), def.mDescription);
                    break;
            }
            prop->SetData((DWORD_PTR)&def);
            if (groups.find(def.mCategory) == groups.end())
            {
                groups[def.mCategory] = new CMFCPropertyGridProperty(def.mCategory);
                order.push_back(def.mCategory);
            }
            groups[def.mCategory]->AddSubItem(prop);
        }
        for (auto& name : order)
        {
            mProps.AddProperty(groups[name]);
        }
    }
    mProps.SetRedraw(TRUE);
    mProps.Invalidate();
    mBuildingGrid = false;
}

void EditorFrame::RefreshPropertyValues()
{
    Document* doc = Doc();
    if (doc == nullptr || doc->mSelection.empty() || mProps.GetSafeHwnd() == nullptr)
    {
        BuildPropertyGrid();
        return;
    }
    const Control& c = doc->mInstrument.mControls[doc->mSelection.front()];
    mBuildingGrid = true;
    for (int g = 0; g < mProps.GetPropertyCount(); g++)
    {
        CMFCPropertyGridProperty* group = mProps.GetProperty(g);
        for (int i = 0; i < group->GetSubItemsCount(); i++)
        {
            CMFCPropertyGridProperty* prop = group->GetSubItem(i);
            const PropDef* def = (const PropDef*)prop->GetData();
            if (def != nullptr && def->mKind == PropKind::Integer)
            {
                prop->SetValue(COleVariant((long)c.GetInt(def->mName), VT_I4));
            }
        }
    }
    mBuildingGrid = false;
}

LRESULT EditorFrame::OnPropertyChanged(WPARAM, LPARAM lParam)
{
    if (mBuildingGrid)
    {
        return 0;
    }
    CMFCPropertyGridProperty* prop = (CMFCPropertyGridProperty*)lParam;
    Document* doc = Doc();
    const PropDef* def = prop != nullptr ? (const PropDef*)prop->GetData() : nullptr;
    if (doc == nullptr || def == nullptr)
    {
        return 0;
    }
    std::string value;
    bool valid = true;
    switch (def->mKind)
    {
        case PropKind::Integer:
        {
            COleVariant v = prop->GetValue();
            try
            {
                v.ChangeType(VT_I4);
                value = std::to_string(v.lVal);
            }
            catch (...)
            {
                valid = false;
            }
            break;
        }
        case PropKind::Bool:
            value = prop->GetValue().boolVal != VARIANT_FALSE ? "true" : "false";
            break;
        case PropKind::Number:
        {
            double number;
            valid = ParseNumber(VariantText(prop->GetValue()), number);
            value = FormatNumber(number);
            break;
        }
        case PropKind::Color:
        {
            auto button = static_cast<ButtonProperty*>(prop);
            std::string text = VariantText(prop->GetValue());
            unsigned long color;
            if (text.empty() || _stricmp(text.c_str(), "none") == 0)
            {
                value = "";
            }
            else if (ParseColor(text, color))
            {
                value = ColorText(color);
            }
            else if (ParseColor(button->mValue, color) && text == button->mValue)
            {
                value = button->mValue;
            }
            else
            {
                valid = false;
            }
            if (valid)
            {
                mBuildingGrid = true;
                button->SetRealValue(value);
                mBuildingGrid = false;
            }
            break;
        }
        case PropKind::Variable:
        case PropKind::Image:
            value = static_cast<ButtonProperty*>(prop)->mValue;
            break;
        default:
            value = VariantText(prop->GetValue());
            break;
    }
    if (!valid)
    {
        MessageBeep(MB_ICONWARNING);
        BuildPropertyGrid();
        return 0;
    }
    if (doc->mSelection.empty())
    {
        if (doc->mInstrument.GetPanel(def->mName) == value)
        {
            return 0;
        }
        doc->PushUndo();
        doc->mInstrument.mPanel[def->mName] = value;
    }
    else
    {
        doc->PushUndo();
        for (int index : doc->mSelection)
        {
            Control& c = doc->mInstrument.mControls[index];
            if (FindPropDef(c.mType, def->mName) != nullptr)
            {
                c.Set(def->mName, value);
            }
        }
    }
    SetDirty();
    return 0;
}

// ---- commands
BOOL EditorFrame::OnCommand(WPARAM wParam, LPARAM lParam)
{
    UINT id = LOWORD(wParam);
    if (id >= ID_FIRST && id < ID_LAST)
    {
        if (CanExecute(id))
        {
            ExecuteCommand(id);
        }
        return TRUE;
    }
    return CFrameWnd::OnCommand(wParam, lParam);
}

bool EditorFrame::CanExecute(UINT id)
{
    Document* doc = Doc();
    size_t selected = doc != nullptr ? doc->mSelection.size() : 0;
    switch (id)
    {
        case ID_SAVE:
        case ID_SAVEAS:
        case ID_CLOSE:
        case ID_CLOSEALL:
        case ID_SELECTALL:
            return doc != nullptr;
        case ID_CLOSEOTHERS:
            return mDocs.size() > 1;
        case ID_UNDO:
            return doc != nullptr && !doc->mUndo.empty();
        case ID_REDO:
            return doc != nullptr && !doc->mRedo.empty();
        case ID_CUT:
        case ID_COPY:
        case ID_DELETE:
        case ID_FRONT:
        case ID_BACK:
        case ID_CENTER_H:
        case ID_CENTER_V:
        case ID_ALIGN_GRID:
            return selected > 0;
        case ID_PASTE:
            return doc != nullptr && IsClipboardFormatAvailable(RegisterClipboardFormat(kClipboardFormat));
        case ID_CHOOSESIGNAL:
            return selected == 1 && TypeHasVariable(doc->mInstrument.mControls[doc->mSelection.front()].mType);
        case ID_ALIGN_LEFT:
        case ID_ALIGN_CENTER:
        case ID_ALIGN_RIGHT:
        case ID_ALIGN_TOP:
        case ID_ALIGN_MIDDLE:
        case ID_ALIGN_BOTTOM:
        case ID_SIZE_WIDTH:
        case ID_SIZE_HEIGHT:
        case ID_SIZE_BOTH:
        case ID_HSPACE_INC:
        case ID_HSPACE_DEC:
        case ID_HSPACE_REMOVE:
        case ID_VSPACE_INC:
        case ID_VSPACE_DEC:
        case ID_VSPACE_REMOVE:
            return selected > 1;
        case ID_HSPACE_EQUAL:
        case ID_VSPACE_EQUAL:
            return selected > 2;
        default:
            return true;
    }
}

void EditorFrame::OnInitMenuPopup(CMenu* pPopupMenu, UINT, BOOL bSysMenu)
{
    if (bSysMenu)
    {
        return;
    }
    for (int i = 0; i < (int)pPopupMenu->GetMenuItemCount(); i++)
    {
        UINT id = pPopupMenu->GetMenuItemID(i);
        if (id >= ID_FIRST && id < ID_LAST)
        {
            pPopupMenu->EnableMenuItem(id, MF_BYCOMMAND | (CanExecute(id) ? MF_ENABLED : MF_GRAYED));
        }
    }
    pPopupMenu->CheckMenuItem(ID_VIEW_PROPS, MF_BYCOMMAND | (mShowProps ? MF_CHECKED : MF_UNCHECKED));
    pPopupMenu->CheckMenuItem(ID_VIEW_TOOLBOX, MF_BYCOMMAND | (mShowToolbox ? MF_CHECKED : MF_UNCHECKED));
    pPopupMenu->CheckMenuItem(ID_VIEW_SNAP, MF_BYCOMMAND | (mGrid > 1 ? MF_CHECKED : MF_UNCHECKED));
}

void EditorFrame::ExecuteCommand(UINT id)
{
    Document* doc = Doc();
    switch (id)
    {
        case ID_NEW:
            NewDocument();
            break;
        case ID_OPEN:
        {
            CFileDialog dialog(TRUE, "bminst", nullptr, OFN_FILEMUSTEXIST | OFN_HIDEREADONLY | OFN_ALLOWMULTISELECT, kFilter, this);
            if (dialog.DoModal() == IDOK)
            {
                POSITION pos = dialog.GetStartPosition();
                while (pos != nullptr)
                {
                    OpenFile((const char*)dialog.GetNextPathName(pos));
                }
            }
            break;
        }
        case ID_SAVE:
            SaveDocument(doc, false);
            break;
        case ID_SAVEAS:
            SaveDocument(doc, true);
            break;
        case ID_CLOSE:
            CloseDocument(mActive);
            break;
        case ID_CLOSEALL:
            while (!mDocs.empty())
            {
                if (!CloseDocument((int)mDocs.size() - 1))
                {
                    break;
                }
            }
            break;
        case ID_CLOSEOTHERS:
        {
            Document* keep = doc;
            for (int i = (int)mDocs.size() - 1; i >= 0; i--)
            {
                if (mDocs[i].get() != keep && !CloseDocument(i))
                {
                    break;
                }
            }
            break;
        }
        case ID_EXIT:
            PostMessage(WM_CLOSE);
            break;
        case ID_UNDO:
            Undo();
            break;
        case ID_REDO:
            Redo();
            break;
        case ID_CUT:
            CopySelection();
            DeleteSelection();
            break;
        case ID_COPY:
            CopySelection();
            break;
        case ID_PASTE:
            Paste();
            break;
        case ID_DELETE:
            DeleteSelection();
            break;
        case ID_SELECTALL:
            SelectAll();
            break;
        case ID_CHOOSESIGNAL:
            ChooseVariable(doc->mSelection.front());
            break;
        case ID_VIEW_PROPS:
            mShowProps = !mShowProps;
            RecalcLayout();
            break;
        case ID_VIEW_TOOLBOX:
            mShowToolbox = !mShowToolbox;
            RecalcLayout();
            break;
        case ID_VIEW_SNAP:
            mGrid = mGrid > 1 ? 1 : 8;
            mCanvas.Invalidate(FALSE);
            break;
        case ID_ALIGN_LEFT:
        case ID_ALIGN_CENTER:
        case ID_ALIGN_RIGHT:
        case ID_ALIGN_TOP:
        case ID_ALIGN_MIDDLE:
        case ID_ALIGN_BOTTOM:
        case ID_ALIGN_GRID:
            Align(id);
            break;
        case ID_SIZE_WIDTH:
        case ID_SIZE_HEIGHT:
        case ID_SIZE_BOTH:
            SameSize(id);
            break;
        case ID_HSPACE_EQUAL:
        case ID_HSPACE_INC:
        case ID_HSPACE_DEC:
        case ID_HSPACE_REMOVE:
        case ID_VSPACE_EQUAL:
        case ID_VSPACE_INC:
        case ID_VSPACE_DEC:
        case ID_VSPACE_REMOVE:
            Spacing(id);
            break;
        case ID_CENTER_H:
            CenterInForm(true);
            break;
        case ID_CENTER_V:
            CenterInForm(false);
            break;
        case ID_FRONT:
            BringToFront();
            break;
        case ID_BACK:
            SendToBack();
            break;
        case ID_ABOUT:
            ShowAbout();
            break;
        default:
            break;
    }
}

bool EditorFrame::SaveDocument(Document* doc, bool saveAs)
{
    if (doc == nullptr)
    {
        return false;
    }
    std::string path = doc->mPath;
    if (saveAs || path.empty())
    {
        std::string name = doc->mPath.empty() ? doc->mUntitledName + ".bminst" : FileName(doc->mPath);
        CFileDialog dialog(FALSE, "bminst", name.c_str(), OFN_OVERWRITEPROMPT | OFN_HIDEREADONLY, kFilter, this);
        if (dialog.DoModal() != IDOK)
        {
            return false;
        }
        path = (const char*)dialog.GetPathName();
    }
    std::string error;
    if (!doc->mInstrument.Save(path, error))
    {
        MessageBox(error.c_str(), "Instrument Editor", MB_ICONWARNING);
        return false;
    }
    doc->mPath = path;
    doc->mModified = false;
    UpdateTabs();
    std::string title = "BUSMASTER Instrument Editor - " + Doc()->Title();
    SetWindowText(title.c_str());
    GetHost()->FileSaved(path);
    return true;
}

bool EditorFrame::CloseDocument(int index)
{
    if (index < 0 || index >= (int)mDocs.size())
    {
        return true;
    }
    Document* doc = mDocs[index].get();
    if (doc->mModified)
    {
        Activate(index);
        std::string question = "Save the changes of " + (doc->mPath.empty() ? doc->mUntitledName : FileName(doc->mPath)) + "?";
        int answer = MessageBox(question.c_str(), "Instrument Editor", MB_YESNOCANCEL | MB_ICONQUESTION);
        if (answer == IDCANCEL || (answer == IDYES && !SaveDocument(doc, false)))
        {
            return false;
        }
    }
    mDocs.erase(mDocs.begin() + index);
    if (mDocs.empty())
    {
        mActive = -1;
        NewDocument();
        return true;
    }
    if (mActive >= (int)mDocs.size())
    {
        mActive = (int)mDocs.size() - 1;
    }
    else if (index < mActive)
    {
        mActive--;
    }
    UpdateTabs();
    Activate(mActive);
    return true;
}

void EditorFrame::OnClose()
{
    for (int i = (int)mDocs.size() - 1; i >= 0; i--)
    {
        Document* doc = mDocs[i].get();
        if (doc->mModified)
        {
            Activate(i);
            std::string question = "Save the changes of " + (doc->mPath.empty() ? doc->mUntitledName : FileName(doc->mPath)) + "?";
            int answer = MessageBox(question.c_str(), "Instrument Editor", MB_YESNOCANCEL | MB_ICONQUESTION);
            if (answer == IDCANCEL || (answer == IDYES && !SaveDocument(doc, false)))
            {
                return;
            }
        }
    }
    DestroyWindow();
}

void EditorFrame::CloseForExit()
{
    for (int i = (int)mDocs.size() - 1; i >= 0; i--)
    {
        Document* doc = mDocs[i].get();
        if (doc->mModified)
        {
            Activate(i);
            std::string question = "BUSMASTER closes. Save the changes of " +
                                   (doc->mPath.empty() ? doc->mUntitledName : FileName(doc->mPath)) + "?";
            if (MessageBox(question.c_str(), "Instrument Editor", MB_YESNO | MB_ICONQUESTION) == IDYES)
            {
                SaveDocument(doc, false);
            }
        }
    }
    DestroyWindow();
}

void EditorFrame::OnDestroy()
{
    if (gEditor == this)
    {
        gEditor = nullptr;
    }
    // the main window of BUSMASTER comes back in front
    CWnd* owner = GetOwner();
    CFrameWnd::OnDestroy();
    if (owner != nullptr && owner->GetSafeHwnd() != nullptr)
    {
        owner->SetForegroundWindow();
    }
}

void EditorFrame::Undo()
{
    Document* doc = Doc();
    if (doc == nullptr || doc->mUndo.empty())
    {
        return;
    }
    doc->mRedo.push_back(doc->mInstrument.ToXml());
    std::string error;
    doc->mInstrument.FromXml(doc->mUndo.back(), error);
    doc->mUndo.pop_back();
    doc->mSelection.clear();
    doc->mModified = true;
    SetDirty();
    SelectionChanged();
}

void EditorFrame::Redo()
{
    Document* doc = Doc();
    if (doc == nullptr || doc->mRedo.empty())
    {
        return;
    }
    doc->mUndo.push_back(doc->mInstrument.ToXml());
    std::string error;
    doc->mInstrument.FromXml(doc->mRedo.back(), error);
    doc->mRedo.pop_back();
    doc->mSelection.clear();
    doc->mModified = true;
    SetDirty();
    SelectionChanged();
}

void EditorFrame::AddControl(ControlType type, CPoint panelPoint, bool centered)
{
    Document* doc = Doc();
    if (doc == nullptr)
    {
        return;
    }
    doc->PushUndo();
    Control control(type);
    control.Set("Name", doc->mInstrument.NewName(type));
    if (type == ControlType::Label || type == ControlType::Button || type == ControlType::CheckBox || type == ControlType::GroupBox)
    {
        control.Set("Text", control.Get("Name"));
    }
    int x = panelPoint.x, y = panelPoint.y;
    if (centered)
    {
        x -= control.W() / 2;
        y -= control.H() / 2;
    }
    if (mGrid > 1)
    {
        x = (x + mGrid / 2) / mGrid * mGrid;
        y = (y + mGrid / 2) / mGrid * mGrid;
    }
    x = std::max(0, std::min(x, doc->mInstrument.PanelWidth() - 8));
    y = std::max(0, std::min(y, doc->mInstrument.PanelHeight() - 8));
    control.SetRect(x, y, control.W(), control.H());
    // a group box goes under the controls already there
    if (type == ControlType::GroupBox)
    {
        doc->mInstrument.mControls.insert(doc->mInstrument.mControls.begin(), control);
        doc->mSelection = { 0 };
    }
    else
    {
        doc->mInstrument.mControls.push_back(control);
        doc->mSelection = { (int)doc->mInstrument.mControls.size() - 1 };
    }
    SetDirty();
    SelectionChanged();
    mCanvas.SetFocus();
}

void EditorFrame::DeleteSelection()
{
    Document* doc = Doc();
    if (doc == nullptr || doc->mSelection.empty())
    {
        return;
    }
    doc->PushUndo();
    std::vector<int> sorted = doc->mSelection;
    std::sort(sorted.rbegin(), sorted.rend());
    for (int index : sorted)
    {
        doc->mInstrument.mControls.erase(doc->mInstrument.mControls.begin() + index);
    }
    doc->mSelection.clear();
    SetDirty();
    SelectionChanged();
}

void EditorFrame::CopySelection()
{
    Document* doc = Doc();
    if (doc == nullptr || doc->mSelection.empty())
    {
        return;
    }
    std::string xml = Instrument::ControlsToXml(doc->SelectedControls());
    if (!OpenClipboard())
    {
        return;
    }
    EmptyClipboard();
    auto put = [](UINT format, const std::string& text)
    {
        HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, text.size() + 1);
        if (memory != nullptr)
        {
            memcpy(GlobalLock(memory), text.c_str(), text.size() + 1);
            GlobalUnlock(memory);
            SetClipboardData(format, memory);
        }
    };
    put(RegisterClipboardFormat(kClipboardFormat), xml);
    put(CF_TEXT, xml);
    CloseClipboard();
}

void EditorFrame::Paste()
{
    Document* doc = Doc();
    if (doc == nullptr || !OpenClipboard())
    {
        return;
    }
    std::string xml;
    HANDLE data = GetClipboardData(RegisterClipboardFormat(kClipboardFormat));
    if (data != nullptr)
    {
        const char* text = (const char*)GlobalLock(data);
        if (text != nullptr)
        {
            xml = text;
        }
        GlobalUnlock(data);
    }
    CloseClipboard();
    std::vector<Control> controls;
    if (xml.empty() || !Instrument::ControlsFromXml(xml, controls) || controls.empty())
    {
        return;
    }
    doc->PushUndo();
    doc->mSelection.clear();
    for (auto& control : controls)
    {
        // a copy on top of its original is moved aside
        bool overlaps = false;
        for (auto& existing : doc->mInstrument.mControls)
        {
            if (existing.X() == control.X() && existing.Y() == control.Y())
            {
                overlaps = true;
                break;
            }
        }
        if (overlaps)
        {
            control.SetRect(control.X() + 16, control.Y() + 16, control.W(), control.H());
        }
        bool nameUsed = false;
        for (auto& existing : doc->mInstrument.mControls)
        {
            if (existing.Get("Name") == control.Get("Name"))
            {
                nameUsed = true;
                break;
            }
        }
        if (nameUsed)
        {
            control.Set("Name", doc->mInstrument.NewName(control.mType));
        }
        doc->mInstrument.mControls.push_back(control);
        doc->mSelection.push_back((int)doc->mInstrument.mControls.size() - 1);
    }
    SetDirty();
    SelectionChanged();
}

void EditorFrame::SelectAll()
{
    Document* doc = Doc();
    if (doc == nullptr)
    {
        return;
    }
    doc->mSelection.clear();
    for (int i = 0; i < (int)doc->mInstrument.mControls.size(); i++)
    {
        doc->mSelection.push_back(i);
    }
    SelectionChanged();
}

void EditorFrame::ChooseVariable(int index)
{
    Document* doc = Doc();
    if (doc == nullptr || index < 0 || index >= (int)doc->mInstrument.mControls.size())
    {
        return;
    }
    Control& control = doc->mInstrument.mControls[index];
    if (!TypeHasVariable(control.mType))
    {
        return;
    }
    VariableDlg dialog(control.Get("Variable"), this);
    if (dialog.DoModal() == IDOK && dialog.mPath != control.Get("Variable"))
    {
        doc->PushUndo();
        doc->mInstrument.mControls[index].Set("Variable", dialog.mPath);
        SetDirty();
        BuildPropertyGrid();
    }
}

void EditorFrame::ChooseImage(int index)
{
    Document* doc = Doc();
    if (doc == nullptr || index < 0 || index >= (int)doc->mInstrument.mControls.size())
    {
        return;
    }
    CFileDialog dialog(TRUE, nullptr, nullptr, OFN_FILEMUSTEXIST | OFN_HIDEREADONLY,
                       "Images (*.png;*.jpg;*.jpeg;*.bmp;*.gif)|*.png;*.jpg;*.jpeg;*.bmp;*.gif|All files (*.*)|*.*||", this);
    if (dialog.DoModal() != IDOK)
    {
        return;
    }
    std::ifstream file((const char*)dialog.GetPathName(), std::ios::binary);
    std::vector<unsigned char> data((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    if (data.empty())
    {
        return;
    }
    doc->PushUndo();
    doc->mInstrument.mControls[index].Set("Image", Base64Encode(data));
    SetDirty();
    BuildPropertyGrid();
}

void EditorFrame::BringToFront()
{
    Document* doc = Doc();
    if (doc == nullptr || doc->mSelection.empty())
    {
        return;
    }
    doc->PushUndo();
    std::vector<Control> moved = doc->SelectedControls();
    std::vector<Control> rest;
    for (int i = 0; i < (int)doc->mInstrument.mControls.size(); i++)
    {
        if (!doc->IsSelected(i))
        {
            rest.push_back(doc->mInstrument.mControls[i]);
        }
    }
    doc->mSelection.clear();
    for (auto& control : moved)
    {
        rest.push_back(control);
        doc->mSelection.push_back((int)rest.size() - 1);
    }
    doc->mInstrument.mControls = rest;
    SetDirty();
    SelectionChanged();
}

void EditorFrame::SendToBack()
{
    Document* doc = Doc();
    if (doc == nullptr || doc->mSelection.empty())
    {
        return;
    }
    doc->PushUndo();
    std::vector<Control> result = doc->SelectedControls();
    size_t count = result.size();
    for (int i = 0; i < (int)doc->mInstrument.mControls.size(); i++)
    {
        if (!doc->IsSelected(i))
        {
            result.push_back(doc->mInstrument.mControls[i]);
        }
    }
    doc->mInstrument.mControls = result;
    doc->mSelection.clear();
    for (size_t i = 0; i < count; i++)
    {
        doc->mSelection.push_back((int)i);
    }
    SetDirty();
    SelectionChanged();
}

void EditorFrame::Align(UINT id)
{
    Document* doc = Doc();
    if (doc == nullptr || doc->mSelection.empty())
    {
        return;
    }
    doc->PushUndo();
    const Control reference = doc->mInstrument.mControls[doc->mSelection.front()];
    for (int index : doc->mSelection)
    {
        Control& c = doc->mInstrument.mControls[index];
        int x = c.X(), y = c.Y();
        switch (id)
        {
            case ID_ALIGN_LEFT:
                x = reference.X();
                break;
            case ID_ALIGN_CENTER:
                x = reference.X() + reference.W() / 2 - c.W() / 2;
                break;
            case ID_ALIGN_RIGHT:
                x = reference.X() + reference.W() - c.W();
                break;
            case ID_ALIGN_TOP:
                y = reference.Y();
                break;
            case ID_ALIGN_MIDDLE:
                y = reference.Y() + reference.H() / 2 - c.H() / 2;
                break;
            case ID_ALIGN_BOTTOM:
                y = reference.Y() + reference.H() - c.H();
                break;
            case ID_ALIGN_GRID:
            {
                int grid = std::max(mGrid, 8);
                x = (x + grid / 2) / grid * grid;
                y = (y + grid / 2) / grid * grid;
                break;
            }
            default:
                break;
        }
        c.SetRect(x, y, c.W(), c.H());
    }
    SetDirty();
    RefreshPropertyValues();
}

void EditorFrame::SameSize(UINT id)
{
    Document* doc = Doc();
    if (doc == nullptr || doc->mSelection.size() < 2)
    {
        return;
    }
    doc->PushUndo();
    const Control reference = doc->mInstrument.mControls[doc->mSelection.front()];
    for (int index : doc->mSelection)
    {
        Control& c = doc->mInstrument.mControls[index];
        int w = id == ID_SIZE_HEIGHT ? c.W() : reference.W();
        int h = id == ID_SIZE_WIDTH ? c.H() : reference.H();
        c.SetRect(c.X(), c.Y(), w, h);
    }
    SetDirty();
    RefreshPropertyValues();
}

void EditorFrame::Spacing(UINT id)
{
    Document* doc = Doc();
    if (doc == nullptr || doc->mSelection.size() < 2)
    {
        return;
    }
    bool horizontal = id == ID_HSPACE_EQUAL || id == ID_HSPACE_INC || id == ID_HSPACE_DEC || id == ID_HSPACE_REMOVE;
    std::vector<int> order = doc->mSelection;
    auto& controls = doc->mInstrument.mControls;
    std::sort(order.begin(), order.end(), [&](int a, int b)
    {
        return horizontal ? controls[a].X() < controls[b].X() : controls[a].Y() < controls[b].Y();
    });
    auto pos = [&](int i)
    {
        return horizontal ? controls[i].X() : controls[i].Y();
    };
    auto len = [&](int i)
    {
        return horizontal ? controls[i].W() : controls[i].H();
    };
    auto move = [&](int i, int value)
    {
        Control& c = controls[i];
        if (horizontal)
        {
            c.SetRect(value, c.Y(), c.W(), c.H());
        }
        else
        {
            c.SetRect(c.X(), value, c.W(), c.H());
        }
    };
    doc->PushUndo();
    int step = std::max(mGrid, 4);
    if (id == ID_HSPACE_EQUAL || id == ID_VSPACE_EQUAL)
    {
        int first = pos(order.front());
        int last = pos(order.back()) + len(order.back());
        int total = 0;
        for (int i : order)
        {
            total += len(i);
        }
        double gap = (double)(last - first - total) / (order.size() - 1);
        double at = first;
        for (int i : order)
        {
            move(i, (int)floor(at + 0.5));
            at += len(i) + gap;
        }
    }
    else
    {
        for (size_t k = 1; k < order.size(); k++)
        {
            int i = order[k];
            int gap = pos(i) - (pos(order[k - 1]) + len(order[k - 1]));
            if (id == ID_HSPACE_INC || id == ID_VSPACE_INC)
            {
                gap += step;
            }
            else if (id == ID_HSPACE_DEC || id == ID_VSPACE_DEC)
            {
                gap = std::max(0, gap - step);
            }
            else
            {
                gap = 0;
            }
            int previousEnd = pos(order[k - 1]) + len(order[k - 1]);
            move(i, previousEnd + gap);
        }
    }
    SetDirty();
    RefreshPropertyValues();
}

void EditorFrame::CenterInForm(bool horizontal)
{
    Document* doc = Doc();
    if (doc == nullptr || doc->mSelection.empty())
    {
        return;
    }
    auto& controls = doc->mInstrument.mControls;
    CRect bounds;
    bool first = true;
    for (int i : doc->mSelection)
    {
        CRect r(controls[i].X(), controls[i].Y(), controls[i].X() + controls[i].W(), controls[i].Y() + controls[i].H());
        if (first)
        {
            bounds = r;
            first = false;
        }
        else
        {
            bounds.UnionRect(bounds, r);
        }
    }
    doc->PushUndo();
    int dx = horizontal ? (doc->mInstrument.PanelWidth() - bounds.Width()) / 2 - bounds.left : 0;
    int dy = horizontal ? 0 : (doc->mInstrument.PanelHeight() - bounds.Height()) / 2 - bounds.top;
    for (int i : doc->mSelection)
    {
        controls[i].SetRect(controls[i].X() + dx, controls[i].Y() + dy, controls[i].W(), controls[i].H());
    }
    SetDirty();
    RefreshPropertyValues();
}

void EditorFrame::ShowAbout()
{
    MessageBox("BUSMASTER Instrument Editor\n\n"
               "Draw an instrument: drag controls from the toolbox onto the panel, set their properties and associate "
               "them with signals of the databases of BUSMASTER (Edit > Choose Signal, or the Variable property).\n\n"
               "Save it, then load it in BUSMASTER with Instruments > Import. The gauges, LEDs, labels and read-only "
               "text boxes show the values received; the buttons, check boxes, numeric up downs, track bars and text "
               "boxes send their signal in a frame when they are used.\n\n"
               "Part of BUSMASTER-NG, LGPL 3.",
               "About the Instrument Editor", MB_ICONINFORMATION);
}
}
