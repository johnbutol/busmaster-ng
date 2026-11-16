/*
 * BUSMASTER-NG Instruments: the plugin, loaded by BUSMASTER from BusmasterPlugins\Instruments.xml.
 *
 * Adds the ribbon tab Instruments (New: the Instrument Editor, Import: shows instruments in BUSMASTER, Close All),
 * keeps the instruments shown in the configuration of BUSMASTER, and gives the windows what they need (Host).
 */
#pragma once

#include "Host.h"
#include "IBusmasterPlugin.h"

#include <string>
#include <vector>

namespace Instruments
{
class PanelWnd;

class InstrumentsPlugin : public IBusmasterPlugin, public IMenuInterface, public IEvent, public Host
{
public:
    InstrumentsPlugin();
    ~InstrumentsPlugin() override;

    // IBusmasterPlugin
    void setBusmasterInterface(IBusmasterPluginInterface* bm) override;
    void getMenuInterface(IMenuInterface** menu) override;
    void getNotifySink(IEvent** sink) override;
    void getConfiguration(xmlNodePtr& node) override;
    void setConfiguration(const xmlDocPtr doc) override;
    void getConnectPoint(IBusmasterPluginConnection** connection) override;
    ILicenseProvider* getLicenseProvider() override;

    // IMenuInterface
    void OnMenuItemClicked(const char* menuId) override;
    void updateMenuItem(const char* menuId, IMenuItem* item) override;

    // IEvent
    int onBusmasterEvent(eBusmaster_Event event, void* data) override;

    // Host
    VariableManager::IVariableLayer* Variables() override;
    IBMNetWorkGetService* Database() override;
    HWND MainWindow() override;
    HWND PanelParent() override;
    std::string UnitOf(const std::string& variablePath) override;
    bool Send(const std::string& variablePath, double value, bool physical) override;
    void OpenEditor(const std::string& file) override;
    void ClosePanel(PanelWnd* panel) override;
    void ReloadPanel(PanelWnd* panel) override;
    void FileSaved(const std::string& file) override;

    PanelWnd* Import(const std::string& file, const RECT* placement, bool showErrors);
    void CloseAllPanels();
    void Shutdown();

private:
    IBusmasterPluginInterface* mBusmaster = nullptr;
    VariableManager::IVariableLayer* mVariables = nullptr;
    IBMNetWorkGetService* mDatabase = nullptr;
    HWND mMain = nullptr;
    std::vector<PanelWnd*> mPanels;
    bool mShutDown = false;
};
}
