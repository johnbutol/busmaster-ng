/*
 * BUSMASTER-NG Instruments: what the windows of the plugin need from BUSMASTER, given by the plugin.
 */
#pragma once

#include <string>

class IBMNetWorkGetService;
namespace VariableManager
{
class IVariableLayer;
}

namespace Instruments
{
class PanelWnd;

class Host
{
public:
    virtual ~Host() {}
    virtual VariableManager::IVariableLayer* Variables() = 0;
    virtual IBMNetWorkGetService* Database() = 0;
    virtual HWND MainWindow() = 0;
    // the window that holds the instrument panels (the client area of BUSMASTER)
    virtual HWND PanelParent() = 0;

    // unit of the signal of a variable path in the database, "" if unknown
    virtual std::string UnitOf(const std::string& variablePath) = 0;
    // sends a value of a signal; physical: the scaled value, otherwise the raw value
    virtual bool Send(const std::string& variablePath, double value, bool physical) = 0;

    virtual void OpenEditor(const std::string& file) = 0;
    virtual void ClosePanel(PanelWnd* panel) = 0;
    virtual void ReloadPanel(PanelWnd* panel) = 0;
    // the editor saved a file: the panels showing it are loaded again
    virtual void FileSaved(const std::string& file) = 0;
};

Host* GetHost();
}
