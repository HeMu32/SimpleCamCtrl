/*
 * @file demo_driver.cpp
 * @brief Minimal driver example that demonstrates using WiaTransport and
 * SonyPTP3_Impl to exercise the wrapper.  Adapted from the prototype.
 */

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <wia.h>

#include <chrono>
#include <iomanip>
#include <iostream>
#include <thread>

#include <WiaTransport.h>
#include <SonyPTP3_Impl.h>

static void safeRelease(IUnknown *&p)
{
    if (p)
    {
        p->Release();
        p = nullptr;
    }
}

static std::string bstr_to_utf8(BSTR bstr)
{
    if (!bstr)
        return std::string();
    int size = WideCharToMultiByte(CP_UTF8, 0, bstr, -1, NULL, 0, NULL, NULL);
    if (size <= 0)
        return std::string();
    std::string s(size - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, bstr, -1, &s[0], size, NULL, NULL);
    return s;
}

static bool get_first_wia_device_id(std::string &outDeviceId)
{
    outDeviceId.clear();

    IWiaDevMgr *pWiaDevMgr = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_WiaDevMgr, NULL, CLSCTX_LOCAL_SERVER,
                                  IID_IWiaDevMgr, (void **)&pWiaDevMgr);
    if (FAILED(hr) || !pWiaDevMgr)
    {
        std::cerr << "Failed to CoCreateInstance(CLSID_WiaDevMgr): 0x" << std::hex
                  << hr << "\n";
        safeRelease(reinterpret_cast<IUnknown *&>(pWiaDevMgr));
        return false;
    }

    IEnumWIA_DEV_INFO *pEnum = nullptr;
    hr = pWiaDevMgr->EnumDeviceInfo(WIA_DEVINFO_ENUM_LOCAL, &pEnum);
    if (FAILED(hr) || !pEnum)
    {
        std::cerr << "No WIA devices enumerated: 0x" << std::hex << hr << "\n";
        safeRelease(reinterpret_cast<IUnknown *&>(pEnum));
        safeRelease(reinterpret_cast<IUnknown *&>(pWiaDevMgr));
        return false;
    }

    // Print a numbered list of available devices: (DESC) (NAME) (ID)
    {
        std::cout << "Available WIA devices:\n";
        IWiaPropertyStorage *tmpProp = nullptr;
        ULONG tmpFetched = 0;
        int idx = 1;
        // Iterate through the enumerator and print desc/name/id for each device
        while (pEnum->Next(1, &tmpProp, &tmpFetched) == S_OK && tmpFetched == 1)
        {
            PROPSPEC printSpecs[3];
            PROPVARIANT printVals[3];
            for (int i = 0; i < 3; ++i)
                PropVariantInit(&printVals[i]);
            printSpecs[0].ulKind = PRSPEC_PROPID; printSpecs[0].propid = WIA_DIP_DEV_DESC;
            printSpecs[1].ulKind = PRSPEC_PROPID; printSpecs[1].propid = WIA_DIP_DEV_NAME;
            printSpecs[2].ulKind = PRSPEC_PROPID; printSpecs[2].propid = WIA_DIP_DEV_ID;
            HRESULT r = tmpProp->ReadMultiple(3, printSpecs, printVals);
            std::string desc, name, id;
            if (SUCCEEDED(r))
            {
                if (printVals[0].vt == VT_BSTR && printVals[0].bstrVal) desc = bstr_to_utf8(printVals[0].bstrVal);
                if (printVals[1].vt == VT_BSTR && printVals[1].bstrVal) name = bstr_to_utf8(printVals[1].bstrVal);
                if (printVals[2].vt == VT_BSTR && printVals[2].bstrVal) id = bstr_to_utf8(printVals[2].bstrVal);
            }
            std::cout << idx << ". [" << (desc.empty() ? "" : desc) << "] [" << (name.empty() ? "" : name) << "] [" << (id.empty() ? "" : id) << "]\n";
            for (int i = 0; i < 3; ++i) PropVariantClear(&printVals[i]);
            safeRelease(reinterpret_cast<IUnknown *&>(tmpProp));
            ++idx;
        }
        // Reset enumerator back to start to pick first device later
        pEnum->Reset();
    }

    IWiaPropertyStorage *pProp = nullptr;
    ULONG fetched = 0;
    hr = pEnum->Next(1, &pProp, &fetched);
    if (FAILED(hr) || fetched == 0 || !pProp)
    {
        std::cerr 
            << "No WIA device found.\n";
        safeRelease(reinterpret_cast<IUnknown *&>(pProp));
        safeRelease(reinterpret_cast<IUnknown *&>(pEnum));
        safeRelease(reinterpret_cast<IUnknown *&>(pWiaDevMgr));
        return false;
    }

    // Read multiple properties: device id (unique), description and friendly name.
    PROPSPEC ps[3];
    PROPVARIANT pv[3];
    for (int i = 0; i < 3; ++i)
        PropVariantInit(&pv[i]);

    ps[0].ulKind = PRSPEC_PROPID; ps[0].propid = WIA_DIP_DEV_ID;
    ps[1].ulKind = PRSPEC_PROPID; ps[1].propid = WIA_DIP_DEV_DESC;
    ps[2].ulKind = PRSPEC_PROPID; ps[2].propid = WIA_DIP_DEV_NAME;

    hr = pProp->ReadMultiple(3, ps, pv);
    if (FAILED(hr))
    {
        std::cerr << "ReadMultiple failed: 0x" << std::hex << hr << "\n";
        for (int i = 0; i < 3; ++i)
            PropVariantClear(&pv[i]);
        safeRelease(reinterpret_cast<IUnknown *&>(pProp));
        safeRelease(reinterpret_cast<IUnknown *&>(pEnum));
        safeRelease(reinterpret_cast<IUnknown *&>(pWiaDevMgr));
        return false;
    }

    std::string devIdStr;
    BSTR devId = nullptr;
    if (pv[0].vt == VT_BSTR && pv[0].bstrVal)
    {
        devId = SysAllocString(pv[0].bstrVal);
        devIdStr = bstr_to_utf8(pv[0].bstrVal);
    }
    if (!devIdStr.empty())
        std::cout << "Using " << devIdStr << "\n";

    for (int i = 0; i < 3; ++i)
        PropVariantClear(&pv[i]);

    if (!devId)
    {
        std::cerr << "Failed to copy device id string\n";
        safeRelease(reinterpret_cast<IUnknown *&>(pProp));
        safeRelease(reinterpret_cast<IUnknown *&>(pEnum));
        safeRelease(reinterpret_cast<IUnknown *&>(pWiaDevMgr));
        return false;
    }

    IWiaItem *pWiaItemRoot = nullptr;
    hr = pWiaDevMgr->CreateDevice(devId, &pWiaItemRoot);
    SysFreeString(devId);
    if (FAILED(hr) || !pWiaItemRoot)
    {
        std::cerr << "CreateDevice failed: 0x" << std::hex << hr << "\n";
        safeRelease(reinterpret_cast<IUnknown *&>(pWiaItemRoot));
        safeRelease(reinterpret_cast<IUnknown *&>(pProp));
        safeRelease(reinterpret_cast<IUnknown *&>(pEnum));
        safeRelease(reinterpret_cast<IUnknown *&>(pWiaDevMgr));
        return false;
    }

    // We only need device id for WiaTransport; avoid handing raw IWiaItemExtras across apartments.
    outDeviceId = devIdStr;

    safeRelease(reinterpret_cast<IUnknown *&>(pWiaItemRoot));
    safeRelease(reinterpret_cast<IUnknown *&>(pProp));
    safeRelease(reinterpret_cast<IUnknown *&>(pEnum));
    safeRelease(reinterpret_cast<IUnknown *&>(pWiaDevMgr));
    return true;
}

int main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    HRESULT hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    if (FAILED(hr))
    {
        std::cerr << "CoInitializeEx failed: 0x" << std::hex << hr << "\n";
        return 1;
    }

    std::string wiaDeviceId;
    if (!get_first_wia_device_id(wiaDeviceId))
    {
        CoUninitialize();
        return 1;
    }

    // Wrap the device ID in our transport and use the Sony wrapper
    auto transport = std::make_shared<WiaTransport>(wiaDeviceId);

    SonyPTP3_Impl cam;
    cam.SetPtpTransport(transport);
    if (!cam.Connect())
    {
        std::cerr << "Failed to connect to camera\n";
        CoUninitialize();
        return 1;
    }

    if (!cam.UpdateStatus())
    {
        std::cerr << "Failed to update status\n";
    }

    SonyPTP3_Impl::ExposureParams params;
    if (cam.GetExposureParams(params))
    {
        std::cout << std::fixed << std::setprecision(4)
                  << "Shutter(1/sec): " << params.shutter_speed
                  << " FNo: " << params.f_number
                  << " ISO: " << params.iso << "\n";
    }

    cam.Disconnect();
    CoUninitialize();
    return 0;
}
