/*
 * BUSMASTER-NG Instruments: the Instrument Editor.
 *
 * A window of its own with a toolbox (the control types: drag one onto the panel, or double click it), the panel
 * of the instrument being edited (several instruments in tabs), and the property window of the selected controls.
 * Menus File, Edit (undo, redo, clipboard), View, Format (alignment, size, spacing, order) as in the add-on
 * BUSMASTER had, and the same file format as the instruments BUSMASTER shows.
 */
#pragma once

#include "Model.h"

#include <memory>
#include <string>
#include <vector>

namespace Instruments
{
class EditorFrame;

// the commands of the menus of the editor
enum Command : UINT
{
    ID_FIRST = 33000,
    ID_NEW = ID_FIRST, ID_OPEN, ID_SAVE, ID_SAVEAS, ID_CLOSE, ID_CLOSEALL, ID_CLOSEOTHERS, ID_EXIT,
    ID_UNDO, ID_REDO, ID_CUT, ID_COPY, ID_PASTE, ID_DELETE, ID_SELECTALL, ID_CHOOSESIGNAL,
    ID_VIEW_PROPS, ID_VIEW_TOOLBOX, ID_VIEW_SNAP,
    ID_ALIGN_LEFT, ID_ALIGN_CENTER, ID_ALIGN_RIGHT, ID_ALIGN_TOP, ID_ALIGN_MIDDLE, ID_ALIGN_BOTTOM, ID_ALIGN_GRID,
    ID_SIZE_WIDTH, ID_SIZE_HEIGHT, ID_SIZE_BOTH,
    ID_HSPACE_EQUAL, ID_HSPACE_INC, ID_HSPACE_DEC, ID_HSPACE_REMOVE,
    ID_VSPACE_EQUAL, ID_VSPACE_INC, ID_VSPACE_DEC, ID_VSPACE_REMOVE,
    ID_CENTER_H, ID_CENTER_V, ID_FRONT, ID_BACK,
    ID_ABOUT,
    ID_LAST
};

// One instrument being edited
class Document
{
public:
    Instrument mInstrument;
    std::string mPath;                  // "" until saved
    std::string mUntitledName;
    bool mModified = false;
    std::vector<int> mSelection;        // indices in mInstrument.mControls; the first is the reference of Format
    std::vector<std::string> mUndo;     // XML of the instrument before each change
    std::vector<std::string> mRedo;
    CPoint mScroll = CPoint(0, 0);

    std::string Title() const;
    void PushUndo();                    // call before a change
    bool IsSelected(int index) const;
    std::vector<Control> SelectedControls() const;
};

class Toolbox : public CWnd
{
public:
    EditorFrame* mFrame = nullptr;
    BOOL Create(CWnd* parent, UINT id);
protected:
    afx_msg void OnPaint();
    afx_msg BOOL OnEraseBkgnd(CDC*);
    afx_msg void OnLButtonDown(UINT, CPoint);
    afx_msg void OnLButtonUp(UINT, CPoint);
    afx_msg void OnLButtonDblClk(UINT, CPoint);
    afx_msg void OnMouseMove(UINT, CPoint);
    afx_msg LRESULT OnMouseLeave(WPARAM, LPARAM);
    afx_msg BOOL OnSetCursor(CWnd*, UINT, UINT);
    DECLARE_MESSAGE_MAP()
private:
    int ItemAt(CPoint point) const;
    int mHover = -1;
    int mDragging = -1;
    CPoint mDragStart;
    bool mDragStarted = false;
    bool mTracking = false;
};

class Canvas : public CWnd
{
public:
    EditorFrame* mFrame = nullptr;
    BOOL Create(CWnd* parent, UINT id);
    void UpdateScrollBars();
    // a control of the toolbox dropped at a point of the screen
    void Drop(ControlType type, CPoint screen);
    bool IsOverCanvas(CPoint screen);
protected:
    afx_msg void OnPaint();
    afx_msg BOOL OnEraseBkgnd(CDC*);
    afx_msg void OnSize(UINT, int, int);
    afx_msg void OnHScroll(UINT, UINT, CScrollBar*);
    afx_msg void OnVScroll(UINT, UINT, CScrollBar*);
    afx_msg BOOL OnMouseWheel(UINT, short, CPoint);
    afx_msg void OnLButtonDown(UINT, CPoint);
    afx_msg void OnLButtonUp(UINT, CPoint);
    afx_msg void OnLButtonDblClk(UINT, CPoint);
    afx_msg void OnMouseMove(UINT, CPoint);
    afx_msg void OnContextMenu(CWnd*, CPoint);
    afx_msg void OnKeyDown(UINT, UINT, UINT);
    afx_msg UINT OnGetDlgCode();
    afx_msg BOOL OnSetCursor(CWnd*, UINT, UINT);
    afx_msg void OnCaptureChanged(CWnd*);
    DECLARE_MESSAGE_MAP()
private:
    enum class Drag { None, Move, Resize, Band, Panel };
    CPoint Origin() const;                  // where the panel starts in the client area
    CPoint ToPanel(CPoint client) const;
    int HitControl(CPoint panel) const;
    int HitHandle(CPoint panel) const;      // handle of the single selected control, -1 if none
    bool HitPanelHandle(CPoint panel) const;
    CRect HandleRect(const CRect& rect, int handle) const;

    Drag mDrag = Drag::None;
    CPoint mDragStart;                      // panel coordinates
    int mHandle = -1;
    std::vector<CRect> mStartRects;         // of the selected controls when the drag started
    CSize mStartPanel;
    bool mChanged = false;
    bool mUndoPushed = false;
    CRect mBand;
};

class PropertyGrid : public CMFCPropertyGridCtrl
{
public:
    EditorFrame* mFrame = nullptr;
};

class EditorFrame : public CFrameWnd
{
public:
    EditorFrame();
    virtual ~EditorFrame();

    static EditorFrame* Open(HWND owner);           // creates the window, or brings the existing one forward
    static EditorFrame* Current();
    void OpenFile(const std::string& path);
    void NewDocument();
    // BUSMASTER closes: offers to save the changes (no cancel) and closes
    void CloseForExit();

    Document* Doc();                                // the active instrument, never null while the window exists
    void SetDirty();                                // the instrument changed: redraw, title, property window
    void SelectionChanged();
    void RefreshPropertyValues();                   // positions changed: values only
    void ShowStatus();

    // operations used by the canvas and the toolbox
    void AddControl(ControlType type, CPoint panelPoint, bool centered);
    void DeleteSelection();
    void CopySelection();
    void Paste();
    void ChooseVariable(int index);
    void ChooseImage(int index);
    void BringToFront();
    void SendToBack();
    void ExecuteCommand(UINT id);

    int mGrid = 8;
    Canvas mCanvas;

protected:
    BOOL PreCreateWindow(CREATESTRUCT& cs) override;
    BOOL OnCommand(WPARAM wParam, LPARAM lParam) override;
    BOOL PreTranslateMessage(MSG* pMsg) override;
    void RecalcLayout(BOOL bNotify = TRUE) override;
    afx_msg int OnCreate(LPCREATESTRUCT lpCreateStruct);
    afx_msg void OnClose();
    afx_msg void OnDestroy();
    afx_msg void OnInitMenuPopup(CMenu* pPopupMenu, UINT nIndex, BOOL bSysMenu);
    afx_msg void OnTabChange(NMHDR*, LRESULT*);
    afx_msg LRESULT OnPropertyChanged(WPARAM, LPARAM);
    afx_msg void OnGetMinMaxInfo(MINMAXINFO* lpMMI);
    DECLARE_MESSAGE_MAP()

private:
    void BuildMenu();
    void BuildPropertyGrid();
    void UpdateTabs();
    void Activate(int index);
    bool CloseDocument(int index);          // asks to save; false if the user cancels
    bool SaveDocument(Document* doc, bool saveAs);
    bool CanExecute(UINT id);
    void Undo();
    void Redo();
    void Align(UINT id);
    void SameSize(UINT id);
    void Spacing(UINT id);
    void CenterInForm(bool horizontal);
    void SelectAll();
    void ShowAbout();

    std::vector<std::unique_ptr<Document>> mDocs;
    int mActive = -1;
    int mUntitledCount = 0;
    CTabCtrl mTabs;
    Toolbox mToolbox;
    PropertyGrid mProps;
    CStatusBar mStatus;
    CMenu mMenu;
    CFont mFont;
    HACCEL mAccel = nullptr;
    bool mShowToolbox = true;
    bool mShowProps = true;
    bool mBuildingGrid = false;
};

// Messages for the windows of the editor go through its PreTranslateMessage (keys, property window), which
// BUSMASTER does not call for the windows of a plugin: a message hook of the thread does it.
void InstallMessageHook();
void RemoveMessageHook();
}
