/*
 * BUSMASTER-NG Instruments: an instrument at run time, in the client area of BUSMASTER.
 *
 * The controls associated with a signal show its value when a frame with it is received (the variable layer of
 * BUSMASTER calls OnVariableChange from its read thread; the values are taken over by a timer of the window). The
 * button, check box, numeric up down, track bar and text box send a frame with the value of their signal when the
 * user acts on them.
 */
#pragma once

#include "Model.h"
#include "Render.h"
#include "IVariableLayer.h"

#include <map>
#include <mutex>
#include <set>
#include <string>
#include <vector>

namespace Instruments
{
class InplaceEdit : public CEdit
{
public:
    std::function<void(bool commit)> mDone;
    bool mFinished = false;
protected:
    afx_msg void OnKeyDown(UINT nChar, UINT nRepCnt, UINT nFlags);
    afx_msg void OnKillFocus(CWnd* pNewWnd);
    afx_msg UINT OnGetDlgCode();
    DECLARE_MESSAGE_MAP()
};

class PanelWnd : public CWnd, public VariableManager::IVariableChangeListner
{
public:
    PanelWnd();
    virtual ~PanelWnd();

    bool Open(const std::string& file, std::string& error);
    bool CreatePanel(HWND parent, const RECT* placement);
    void Reload();
    void Unregister();

    const std::string& File() const { return mFile; }
    // the place of the window in its parent, for the configuration
    CRect Placement();

    // called by the read thread of BUSMASTER
    void OnVariableChange(VariableManager::VariableData* data) override;

    void RetryRegistration();
    // the databases changed: the signals are looked up again
    void Rebind();

protected:
    void PostNcDestroy() override;          // the window deletes itself when it is destroyed
    afx_msg void OnPaint();
    afx_msg BOOL OnEraseBkgnd(CDC* pDC);
    afx_msg void OnLButtonDown(UINT nFlags, CPoint point);
    afx_msg void OnLButtonUp(UINT nFlags, CPoint point);
    afx_msg void OnLButtonDblClk(UINT nFlags, CPoint point);
    afx_msg void OnMouseMove(UINT nFlags, CPoint point);
    afx_msg LRESULT OnMouseLeave(WPARAM, LPARAM);
    afx_msg BOOL OnMouseWheel(UINT nFlags, short zDelta, CPoint pt);
    afx_msg void OnContextMenu(CWnd* pWnd, CPoint point);
    afx_msg void OnTimer(UINT_PTR nIDEvent);
    afx_msg void OnClose();
    afx_msg int OnMouseActivate(CWnd* pDesktopWnd, UINT nHitTest, UINT message);
    afx_msg void OnCaptureChanged(CWnd* pWnd);
    DECLARE_MESSAGE_MAP()

private:
    struct Incoming
    {
        double mPhysical = 0;
        double mRaw = 0;
    };

    int HitTest(CPoint point) const;        // index of the top control under the point, -1 if none
    void Register();
    void TakeIncoming();
    void SetValue(int index, double value, bool send);
    void BeginEdit(int index);
    void EndEdit(bool commit);
    void FitWindow();

    std::string mFile;
    Instrument mInstrument;
    std::vector<ControlState> mStates;
    std::set<std::string> mRegistered;          // variable paths registered with the variable layer
    std::set<std::string> mUnresolved;          // not found in the database (yet)

    std::mutex mLock;
    std::map<std::string, Incoming> mIncoming;  // values received, not shown yet

    int mPressed = -1;          // control under the mouse button
    int mHover = -1;
    bool mTracking = false;
    int mEditing = -1;
    InplaceEdit mEdit;
    CFont mEditFont;
    int mRetryCount = 0;
};
}
