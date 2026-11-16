/*
 * BUSMASTER-NG Instruments: choice of the signal of a control, among the databases associated in BUSMASTER.
 */
#pragma once

#include "resource.h"

#include <string>
#include <vector>

namespace Instruments
{
class VariableDlg : public CDialog
{
public:
    VariableDlg(const std::string& current, CWnd* parent);

    std::string mPath;      // the chosen variable path; "" to clear the association

protected:
    BOOL OnInitDialog() override;
    void OnOK() override;
    afx_msg void OnFilterChange();
    afx_msg void OnSelChanged(NMHDR* pNMHDR, LRESULT* pResult);
    afx_msg void OnDblClk(NMHDR* pNMHDR, LRESULT* pResult);
    afx_msg void OnClear();
    DECLARE_MESSAGE_MAP()

private:
    struct SignalEntry
    {
        std::string mBus;           // "CAN", "LIN"
        int mChannel = 0;           // 0 based
        std::string mDatabase;
        std::string mMessage;
        unsigned int mId = 0;
        std::string mSignal;
        std::string mUnit;
        unsigned int mLength = 0;
        std::string Path() const;
    };

    void Collect();
    void Fill(const std::string& filter);
    void ShowInfo();

    CTreeCtrl mTree;
    std::vector<SignalEntry> mSignals;
};
}
