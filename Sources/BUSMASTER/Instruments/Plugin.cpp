#include "stdafx.h"
#include "Plugin.h"
#include "Editor.h"
#include "PanelWnd.h"

#include "IBMNetWorkGetService.h"
#include "ICluster.h"

#include <algorithm>
#include <cmath>
#include <sstream>

using namespace VariableManager;

namespace Instruments
{
namespace
{
const char* const kConfigNode = "BUSMASTER_INSTRUMENTS";
const char* const kMenuNew = "instruments.new";
const char* const kMenuImport = "instruments.import";
const char* const kMenuCloseAll = "instruments.closeall";

InstrumentsPlugin* gPlugin = nullptr;

std::vector<std::string> Split(const std::string& text, char separator)
{
    std::vector<std::string> parts;
    std::stringstream stream(text);
    std::string part;
    while (std::getline(stream, part, separator))
    {
        parts.push_back(part);
    }
    return parts;
}

xmlNodePtr FindElement(xmlNodePtr node, const char* name)
{
    for (; node != nullptr; node = node->next)
    {
        if (node->type == XML_ELEMENT_NODE)
        {
            if (!xmlStrcmp(node->name, BAD_CAST name))
            {
                return node;
            }
            xmlNodePtr found = FindElement(node->children, name);
            if (found != nullptr)
            {
                return found;
            }
        }
    }
    return nullptr;
}

std::string Text(xmlNodePtr node)
{
    xmlChar* value = xmlNodeGetContent(node);
    std::string result;
    if (value != nullptr)
    {
        result = (const char*)value;
        xmlFree(value);
    }
    return result;
}
}

Host* GetHost()
{
    return gPlugin;
}

InstrumentsPlugin::InstrumentsPlugin()
{
    gPlugin = this;
}

InstrumentsPlugin::~InstrumentsPlugin()
{
    Shutdown();
    if (gPlugin == this)
    {
        gPlugin = nullptr;
    }
}

void InstrumentsPlugin::setBusmasterInterface(IBusmasterPluginInterface* bm)
{
    AFX_MANAGE_STATE(AfxGetStaticModuleState());
    mBusmaster = bm;
    if (bm == nullptr)
    {
        return;
    }
    bm->getVariableCommunicationLayer(&mVariables);
    bm->getDbService(&mDatabase);
    HWND main = nullptr;
    bm->getInfo(busmaster_window, 0, (OUT_INFO_PARAM)&main);
    mMain = main;
    InstallMessageHook();
}

void InstrumentsPlugin::getMenuInterface(IMenuInterface** menu)
{
    *menu = this;
}

void InstrumentsPlugin::getNotifySink(IEvent** sink)
{
    *sink = this;
}

void InstrumentsPlugin::getConfiguration(xmlNodePtr& node)
{
    AFX_MANAGE_STATE(AfxGetStaticModuleState());
    node = xmlNewNode(nullptr, BAD_CAST kConfigNode);
    for (PanelWnd* panel : mPanels)
    {
        if (panel->GetSafeHwnd() == nullptr)
        {
            continue;
        }
        xmlNodePtr item = xmlNewChild(node, nullptr, BAD_CAST "Instrument", nullptr);
        xmlNewChild(item, nullptr, BAD_CAST "File", BAD_CAST panel->File().c_str());
        CRect place = panel->Placement();
        xmlNewChild(item, nullptr, BAD_CAST "Left", BAD_CAST std::to_string(place.left).c_str());
        xmlNewChild(item, nullptr, BAD_CAST "Top", BAD_CAST std::to_string(place.top).c_str());
    }
}

void InstrumentsPlugin::setConfiguration(const xmlDocPtr doc)
{
    AFX_MANAGE_STATE(AfxGetStaticModuleState());
    CloseAllPanels();
    if (doc == nullptr)
    {
        return;
    }
    xmlNodePtr config = FindElement(xmlDocGetRootElement(doc), kConfigNode);
    if (config == nullptr)
    {
        return;
    }
    std::vector<std::string> missing;
    for (xmlNodePtr item = config->children; item != nullptr; item = item->next)
    {
        if (item->type != XML_ELEMENT_NODE || xmlStrcmp(item->name, BAD_CAST "Instrument"))
        {
            continue;
        }
        std::string file;
        CRect place(20, 20, 20, 20);
        bool hasPlace = false;
        for (xmlNodePtr child = item->children; child != nullptr; child = child->next)
        {
            if (child->type != XML_ELEMENT_NODE)
            {
                continue;
            }
            if (!xmlStrcmp(child->name, BAD_CAST "File"))
            {
                file = Text(child);
            }
            else if (!xmlStrcmp(child->name, BAD_CAST "Left"))
            {
                place.left = place.right = atoi(Text(child).c_str());
                hasPlace = true;
            }
            else if (!xmlStrcmp(child->name, BAD_CAST "Top"))
            {
                place.top = place.bottom = atoi(Text(child).c_str());
                hasPlace = true;
            }
        }
        if (!file.empty() && Import(file, hasPlace ? &place : nullptr, false) == nullptr)
        {
            missing.push_back(file);
        }
    }
    if (!missing.empty())
    {
        std::string message = "These instruments of the configuration could not be opened:\n";
        for (auto& file : missing)
        {
            message += "\n" + file;
        }
        AfxMessageBox(message.c_str(), MB_ICONWARNING);
    }
}

void InstrumentsPlugin::getConnectPoint(IBusmasterPluginConnection** connection)
{
    *connection = nullptr;
}

ILicenseProvider* InstrumentsPlugin::getLicenseProvider()
{
    return nullptr;
}

void InstrumentsPlugin::OnMenuItemClicked(const char* menuId)
{
    AFX_MANAGE_STATE(AfxGetStaticModuleState());
    std::string id = menuId != nullptr ? menuId : "";
    if (id == kMenuNew)
    {
        bool existing = EditorFrame::Current() != nullptr;
        EditorFrame* editor = EditorFrame::Open(mMain);
        if (editor != nullptr && existing)
        {
            editor->NewDocument();
        }
    }
    else if (id == kMenuImport)
    {
        CFileDialog dialog(TRUE, "bminst", nullptr, OFN_FILEMUSTEXIST | OFN_HIDEREADONLY | OFN_ALLOWMULTISELECT,
                           "BUSMASTER instruments (*.bminst)|*.bminst|All files (*.*)|*.*||", CWnd::FromHandle(mMain));
        if (dialog.DoModal() == IDOK)
        {
            POSITION pos = dialog.GetStartPosition();
            while (pos != nullptr)
            {
                Import((const char*)dialog.GetNextPathName(pos), nullptr, true);
            }
        }
    }
    else if (id == kMenuCloseAll)
    {
        CloseAllPanels();
    }
}

void InstrumentsPlugin::updateMenuItem(const char* menuId, IMenuItem* item)
{
    AFX_MANAGE_STATE(AfxGetStaticModuleState());
    if (item == nullptr || menuId == nullptr)
    {
        return;
    }
    if (std::string(menuId) == kMenuCloseAll)
    {
        item->enableItem(!mPanels.empty());
    }
    else
    {
        item->enableItem(true);
    }
}

int InstrumentsPlugin::onBusmasterEvent(eBusmaster_Event event, void*)
{
    AFX_MANAGE_STATE(AfxGetStaticModuleState());
    switch (event)
    {
        case network_config_changed:
        case database_dissociated:
        case new_configuration_loaded:
        case plugin_load_completed:
            // the databases changed: the signals are looked up again
            for (PanelWnd* panel : mPanels)
            {
                panel->Rebind();
            }
            break;
        case unloading_plugins:
        case busmaster_exit:
            Shutdown();
            break;
        default:
            break;
    }
    return 0;
}

VariableManager::IVariableLayer* InstrumentsPlugin::Variables()
{
    return mShutDown ? nullptr : mVariables;
}

IBMNetWorkGetService* InstrumentsPlugin::Database()
{
    return mDatabase;
}

HWND InstrumentsPlugin::MainWindow()
{
    return mMain;
}

HWND InstrumentsPlugin::PanelParent()
{
    HWND client = mMain != nullptr ? FindWindowEx(mMain, nullptr, "MDIClient", nullptr) : nullptr;
    return client != nullptr ? client : mMain;
}

std::string InstrumentsPlugin::UnitOf(const std::string& variablePath)
{
    // SIGNAL:<bus>:CHANNEL<n>:<database>:<message>:<signal>
    std::vector<std::string> parts = Split(variablePath, ':');
    if (mDatabase == nullptr || parts.size() < 6 || parts[2].compare(0, 7, "CHANNEL") != 0)
    {
        return "";
    }
    ETYPE_BUS bus = parts[1] == "LIN" ? LIN : CAN;
    int channel = atoi(parts[2].c_str() + 7);
    std::list<ICluster*> clusters;
    mDatabase->GetDBServiceList(bus, channel, clusters);
    for (ICluster* cluster : clusters)
    {
        std::string name;
        if (cluster == nullptr || (cluster->GetName(name), name != parts[3]))
        {
            continue;
        }
        IElement* element = nullptr;
        cluster->GetElementByName(eFrameElement, parts[4], &element);
        IFrame* frame = (IFrame*)element;
        if (frame == nullptr)
        {
            continue;
        }
        std::map<ISignal*, SignalInstanse> signals;
        frame->GetSignalList(signals);
        for (auto& signal : signals)
        {
            std::string signalName;
            signal.first->GetName(signalName);
            if (signalName == parts[5])
            {
                std::string unit;
                signal.first->GetUnit(unit);
                return unit;
            }
        }
    }
    return "";
}

bool InstrumentsPlugin::Send(const std::string& variablePath, double value, bool physical)
{
    IVariableLayer* variables = Variables();
    if (variables == nullptr)
    {
        return false;
    }
    VariableData data;
    data.mVariablePath = variablePath;
    data.mVariableType = Signal;
    data.mPhysicalValue = value;
    double rounded = floor(value + 0.5);
    if (rounded < 0)
    {
        data.mValue.mRawValueType = Long;
        data.mValue.LongValue = (__int64)rounded;
        data.mValue.ULongValue = (unsigned __int64)data.mValue.LongValue;
    }
    else
    {
        data.mValue.mRawValueType = Ulong;
        data.mValue.ULongValue = (unsigned __int64)rounded;
        data.mValue.LongValue = (__int64)data.mValue.ULongValue;
    }
    return variables->setVariableValue(&data, physical) == S_OK;
}

void InstrumentsPlugin::OpenEditor(const std::string& file)
{
    EditorFrame* editor = EditorFrame::Open(mMain);
    if (editor != nullptr && !file.empty())
    {
        editor->OpenFile(file);
    }
}

PanelWnd* InstrumentsPlugin::Import(const std::string& file, const RECT* placement, bool showErrors)
{
    for (PanelWnd* panel : mPanels)
    {
        if (_stricmp(panel->File().c_str(), file.c_str()) == 0)
        {
            panel->SetWindowPos(&CWnd::wndTop, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
            return panel;
        }
    }
    PanelWnd* panel = new PanelWnd();
    std::string error;
    if (!panel->Open(file, error))
    {
        delete panel;
        if (showErrors)
        {
            AfxMessageBox(error.c_str(), MB_ICONWARNING);
        }
        return nullptr;
    }
    RECT cascade;
    if (placement == nullptr)
    {
        int offset = 20 + 24 * (int)(mPanels.size() % 10);
        cascade = { offset, offset, offset, offset };
        placement = &cascade;
    }
    if (!panel->CreatePanel(PanelParent(), placement))
    {
        delete panel;
        return nullptr;
    }
    mPanels.push_back(panel);
    return panel;
}

void InstrumentsPlugin::ClosePanel(PanelWnd* panel)
{
    auto it = std::find(mPanels.begin(), mPanels.end(), panel);
    if (it == mPanels.end())
    {
        return;
    }
    mPanels.erase(it);
    panel->Unregister();
    panel->DestroyWindow();         // deletes the panel
}

void InstrumentsPlugin::CloseAllPanels()
{
    while (!mPanels.empty())
    {
        ClosePanel(mPanels.back());
    }
}

void InstrumentsPlugin::ReloadPanel(PanelWnd* panel)
{
    panel->Reload();
}

void InstrumentsPlugin::FileSaved(const std::string& file)
{
    for (PanelWnd* panel : mPanels)
    {
        if (_stricmp(panel->File().c_str(), file.c_str()) == 0)
        {
            panel->Reload();
        }
    }
}

void InstrumentsPlugin::Shutdown()
{
    if (mShutDown)
    {
        return;
    }
    AFX_MANAGE_STATE(AfxGetStaticModuleState());
    CloseAllPanels();
    mShutDown = true;
    EditorFrame* editor = EditorFrame::Current();
    if (editor != nullptr && editor->GetSafeHwnd() != nullptr)
    {
        editor->CloseForExit();
    }
    RemoveMessageHook();
}
}
