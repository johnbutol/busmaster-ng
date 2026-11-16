#include "stdafx.h"
#include "VariableDlg.h"
#include "Host.h"
#include "Model.h"

#include "IBMNetWorkGetService.h"
#include "ICluster.h"

#include <algorithm>
#include <map>

namespace Instruments
{
BEGIN_MESSAGE_MAP(VariableDlg, CDialog)
    ON_EN_CHANGE(IDC_FILTER, OnFilterChange)
    ON_NOTIFY(TVN_SELCHANGED, IDC_TREE, OnSelChanged)
    ON_NOTIFY(NM_DBLCLK, IDC_TREE, OnDblClk)
    ON_BN_CLICKED(IDC_CLEAR, OnClear)
END_MESSAGE_MAP()

std::string VariableDlg::SignalEntry::Path() const
{
    return "SIGNAL:" + mBus + ":CHANNEL" + std::to_string(mChannel) + ":" + mDatabase + ":" + mMessage + ":" + mSignal;
}

VariableDlg::VariableDlg(const std::string& current, CWnd* parent)
    : CDialog(IDD_SELECT_VARIABLE, parent), mPath(current)
{
}

BOOL VariableDlg::OnInitDialog()
{
    CDialog::OnInitDialog();
    mTree.SubclassDlgItem(IDC_TREE, this);
    Collect();
    Fill("");
    if (mSignals.empty())
    {
        SetDlgItemText(IDC_INFO, "No database is associated in BUSMASTER: associate one (CAN or LIN > Database) and "
                       "open this window again.");
    }
    GetDlgItem(IDC_FILTER)->SetFocus();
    return FALSE;
}

void VariableDlg::Collect()
{
    mSignals.clear();
    IBMNetWorkGetService* db = GetHost()->Database();
    if (db == nullptr)
    {
        return;
    }
    const std::pair<ETYPE_BUS, const char*> buses[] = { { CAN, "CAN" }, { LIN, "LIN" } };
    for (auto& bus : buses)
    {
        int channels = 0;
        db->GetChannelCount(bus.first, channels);
        for (int channel = 0; channel < channels; channel++)
        {
            std::list<ICluster*> clusters;
            db->GetDBServiceList(bus.first, channel, clusters);
            for (ICluster* cluster : clusters)
            {
                if (cluster == nullptr)
                {
                    continue;
                }
                std::string database;
                cluster->GetName(database);
                std::list<IFrame*> frames;
                cluster->GetFrameList(frames);
                for (IFrame* frame : frames)
                {
                    if (frame == nullptr)
                    {
                        continue;
                    }
                    SignalEntry entry;
                    entry.mBus = bus.second;
                    entry.mChannel = channel;
                    entry.mDatabase = database;
                    frame->GetName(entry.mMessage);
                    frame->GetFrameId(entry.mId);
                    std::map<ISignal*, SignalInstanse> signals;
                    frame->GetSignalList(signals);
                    for (auto& signal : signals)
                    {
                        signal.first->GetName(entry.mSignal);
                        entry.mUnit.clear();
                        signal.first->GetUnit(entry.mUnit);
                        signal.first->GetLength(entry.mLength);
                        mSignals.push_back(entry);
                    }
                }
            }
        }
    }
    std::sort(mSignals.begin(), mSignals.end(), [](const SignalEntry& a, const SignalEntry& b)
    {
        if (a.mBus != b.mBus)
        {
            return a.mBus < b.mBus;
        }
        if (a.mChannel != b.mChannel)
        {
            return a.mChannel < b.mChannel;
        }
        if (a.mDatabase != b.mDatabase)
        {
            return a.mDatabase < b.mDatabase;
        }
        if (a.mMessage != b.mMessage)
        {
            return a.mMessage < b.mMessage;
        }
        return a.mSignal < b.mSignal;
    });
}

void VariableDlg::Fill(const std::string& filter)
{
    std::string lowerFilter = filter;
    std::transform(lowerFilter.begin(), lowerFilter.end(), lowerFilter.begin(), ::tolower);
    mTree.SetRedraw(FALSE);
    mTree.DeleteAllItems();
    std::map<std::string, HTREEITEM> parents;
    HTREEITEM selected = nullptr;
    auto parentOf = [&](const std::string& key, const std::string& text, HTREEITEM parent)
    {
        auto it = parents.find(key);
        if (it != parents.end())
        {
            return it->second;
        }
        HTREEITEM item = mTree.InsertItem(text.c_str(), parent != nullptr ? parent : TVI_ROOT);
        mTree.SetItemData(item, (DWORD_PTR) - 1);
        parents[key] = item;
        return item;
    };
    for (size_t i = 0; i < mSignals.size(); i++)
    {
        const SignalEntry& s = mSignals[i];
        if (!lowerFilter.empty())
        {
            std::string name = s.mMessage + "." + s.mSignal;
            std::transform(name.begin(), name.end(), name.begin(), ::tolower);
            if (name.find(lowerFilter) == std::string::npos)
            {
                continue;
            }
        }
        std::string busKey = s.mBus;
        std::string channelKey = busKey + "/" + std::to_string(s.mChannel);
        std::string dbKey = channelKey + "/" + s.mDatabase;
        std::string msgKey = dbKey + "/" + s.mMessage;
        HTREEITEM bus = parentOf(busKey, s.mBus, nullptr);
        HTREEITEM channel = parentOf(channelKey, "Channel " + std::to_string(s.mChannel + 1), bus);
        HTREEITEM db = parentOf(dbKey, s.mDatabase, channel);
        char id[32];
        sprintf_s(id, " (0x%X)", s.mId);
        HTREEITEM msg = parentOf(msgKey, s.mMessage + id, db);
        std::string text = s.mSignal;
        if (!s.mUnit.empty())
        {
            text += " [" + s.mUnit + "]";
        }
        HTREEITEM item = mTree.InsertItem(text.c_str(), msg);
        mTree.SetItemData(item, (DWORD_PTR)i);
        if (s.Path() == mPath)
        {
            selected = item;
        }
    }
    // with a filter, everything is open; otherwise down to the databases
    for (auto& parent : parents)
    {
        bool isMessage = std::count(parent.first.begin(), parent.first.end(), '/') == 3;
        if (!lowerFilter.empty() || !isMessage)
        {
            mTree.Expand(parent.second, TVE_EXPAND);
        }
    }
    if (selected != nullptr)
    {
        mTree.SelectItem(selected);
        mTree.EnsureVisible(selected);
    }
    mTree.SetRedraw(TRUE);
    ShowInfo();
}

void VariableDlg::ShowInfo()
{
    HTREEITEM item = mTree.GetSelectedItem();
    DWORD_PTR index = item != nullptr ? mTree.GetItemData(item) : (DWORD_PTR) - 1;
    if (index < mSignals.size())
    {
        const SignalEntry& s = mSignals[index];
        char text[512];
        sprintf_s(text, "%s.%s, %s channel %d, %u bits%s%s", s.mMessage.c_str(), s.mSignal.c_str(), s.mBus.c_str(),
                  s.mChannel + 1, s.mLength, s.mUnit.empty() ? "" : ", unit ", s.mUnit.c_str());
        SetDlgItemText(IDC_INFO, text);
        GetDlgItem(IDOK)->EnableWindow(TRUE);
    }
    else
    {
        if (!mSignals.empty())
        {
            SetDlgItemText(IDC_INFO, mPath.empty() ? "Choose a signal." : ("Now: " + VariableLabel(mPath)).c_str());
        }
        GetDlgItem(IDOK)->EnableWindow(FALSE);
    }
}

void VariableDlg::OnFilterChange()
{
    CString filter;
    GetDlgItemText(IDC_FILTER, filter);
    Fill((const char*)filter);
}

void VariableDlg::OnSelChanged(NMHDR*, LRESULT* pResult)
{
    ShowInfo();
    *pResult = 0;
}

void VariableDlg::OnDblClk(NMHDR*, LRESULT* pResult)
{
    HTREEITEM item = mTree.GetSelectedItem();
    if (item != nullptr && mTree.GetItemData(item) < mSignals.size())
    {
        OnOK();
        *pResult = 1;
        return;
    }
    *pResult = 0;
}

void VariableDlg::OnClear()
{
    mPath.clear();
    EndDialog(IDOK);
}

void VariableDlg::OnOK()
{
    HTREEITEM item = mTree.GetSelectedItem();
    DWORD_PTR index = item != nullptr ? mTree.GetItemData(item) : (DWORD_PTR) - 1;
    if (index >= mSignals.size())
    {
        return;
    }
    mPath = mSignals[index].Path();
    CDialog::OnOK();
}
}
