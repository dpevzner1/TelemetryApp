#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <wbemidl.h>
#include <comdef.h>
#include <vector>
#include <string>
#include "temperatures.h"

#pragma comment(lib, "wbemuuid.lib")

// Queries ACPI thermal zones via WMI root\wmi MSAcpi_ThermalZoneTemperature.
// These don't require LibreHardwareMonitor or admin rights on most systems.
// LHM/OHM integration is deferred to a future iteration requiring an elevated service.

namespace Sensors {

static bool s_com_initialized = false;

bool TempInit() {
    if (s_com_initialized) return true;
    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    s_com_initialized = (SUCCEEDED(hr) || hr == RPC_E_CHANGED_MODE);
    return s_com_initialized;
}

bool TempPoll(std::vector<TempReading>& readings) {
    readings.clear();

    IWbemLocator*  pLoc  = nullptr;
    IWbemServices* pSvc  = nullptr;

    HRESULT hr = CoCreateInstance(CLSID_WbemLocator, nullptr, CLSCTX_INPROC_SERVER,
                                  IID_IWbemLocator, (void**)&pLoc);
    if (FAILED(hr)) return false;

    hr = pLoc->ConnectServer(_bstr_t(L"ROOT\\WMI"), nullptr, nullptr, nullptr,
                              0, nullptr, nullptr, &pSvc);
    if (FAILED(hr)) { pLoc->Release(); return false; }

    CoSetProxyBlanket(pSvc, RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE,
                      nullptr, RPC_C_AUTHN_LEVEL_CALL, RPC_C_IMP_LEVEL_IMPERSONATE,
                      nullptr, EOAC_NONE);

    IEnumWbemClassObject* pEnum = nullptr;
    hr = pSvc->ExecQuery(_bstr_t(L"WQL"),
        _bstr_t(L"SELECT * FROM MSAcpi_ThermalZoneTemperature"),
        WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY,
        nullptr, &pEnum);

    if (SUCCEEDED(hr) && pEnum) {
        IWbemClassObject* pObj = nullptr;
        ULONG returned = 0;
        while (pEnum->Next(WBEM_INFINITE, 1, &pObj, &returned) == S_OK) {
            VARIANT vTemp{}, vName{};
            if (SUCCEEDED(pObj->Get(L"CurrentTemperature", 0, &vTemp, nullptr, nullptr)) &&
                vTemp.vt == VT_I4) {
                double celsius = (vTemp.lVal - 2732) / 10.0;
                pObj->Get(L"InstanceName", 0, &vName, nullptr, nullptr);
                std::string name = "ThermalZone";
                if (vName.vt == VT_BSTR) {
                    char buf[128]{};
                    WideCharToMultiByte(CP_UTF8, 0, vName.bstrVal, -1, buf, 128, nullptr, nullptr);
                    name = buf;
                }
                readings.push_back({"acpi/" + name, "acpi", celsius});
                VariantClear(&vName);
            }
            VariantClear(&vTemp);
            pObj->Release();
        }
        pEnum->Release();
    }

    pSvc->Release();
    pLoc->Release();
    return true;
}

void TempShutdown() {
    if (s_com_initialized) CoUninitialize();
    s_com_initialized = false;
}

} // namespace Sensors
