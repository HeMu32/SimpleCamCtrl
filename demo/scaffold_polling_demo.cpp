/*
 * @file scaffold_polling_demo.cpp
 * @brief Scaffold demo: loop without pauses and continuously call
 * UpdateStatus/GetExposureParams/GetExposureMode/SetExposureParams,
 * while recording response status and call latency.
 */

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <wia.h>

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>
#include <string>

#include <SonyPTP3_Impl.h>
#include <WiaTransport.h>

namespace
{
std::atomic<bool> g_bRunning{true};

/**
 * @brief Safe COM pointer release helper.
 * @param pUnknown COM pointer reference.
 */
void SafeRelease(IUnknown *&pUnknown)
{
    if (pUnknown)
    {
        pUnknown->Release();
        pUnknown = nullptr;
    }
}

/**
 * @brief Convert BSTR to UTF-8 string.
 * @param bstr Source BSTR.
 * @return UTF-8 std::string.
 */
std::string BstrToUtf8(BSTR bstr)
{
    if (!bstr)
    {
        return std::string();
    }

    const int nChars = WideCharToMultiByte(CP_UTF8, 0, bstr, -1, nullptr, 0, nullptr, nullptr);
    if (nChars <= 0)
    {
        return std::string();
    }

    std::string strUtf8(static_cast<std::size_t>(nChars - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, bstr, -1, &strUtf8[0], nChars, nullptr, nullptr);
    return strUtf8;
}

/**
 * @brief Enumerate first WIA device and return IWiaItemExtras.
 * @param outExtras Output extras pointer; caller owns and must Release().
 * @return true on success.
 */
bool GetFirstWiaDeviceId(std::string &outDeviceId)
{
    outDeviceId.clear();

    IWiaDevMgr *pWiaDevMgr = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_WiaDevMgr, nullptr, CLSCTX_LOCAL_SERVER,
                                  IID_IWiaDevMgr, reinterpret_cast<void **>(&pWiaDevMgr));
    if (FAILED(hr) || !pWiaDevMgr)
    {
        std::cerr << "CoCreateInstance(CLSID_WiaDevMgr) failed, hr=0x"
                  << std::hex << hr << "\n";
        SafeRelease(reinterpret_cast<IUnknown *&>(pWiaDevMgr));
        return false;
    }

    IEnumWIA_DEV_INFO *pEnum = nullptr;
    hr = pWiaDevMgr->EnumDeviceInfo(WIA_DEVINFO_ENUM_LOCAL, &pEnum);
    if (FAILED(hr) || !pEnum)
    {
        std::cerr << "EnumDeviceInfo failed, hr=0x" << std::hex << hr << "\n";
        SafeRelease(reinterpret_cast<IUnknown *&>(pEnum));
        SafeRelease(reinterpret_cast<IUnknown *&>(pWiaDevMgr));
        return false;
    }

    IWiaPropertyStorage *pProp = nullptr;
    ULONG nFetched = 0;
    hr = pEnum->Next(1, &pProp, &nFetched);
    if (FAILED(hr) || nFetched == 0 || !pProp)
    {
        std::cerr << "No WIA device found.\n";
        SafeRelease(reinterpret_cast<IUnknown *&>(pProp));
        SafeRelease(reinterpret_cast<IUnknown *&>(pEnum));
        SafeRelease(reinterpret_cast<IUnknown *&>(pWiaDevMgr));
        return false;
    }

    PROPSPEC propSpec[2];
    PROPVARIANT propVal[2];
    for (int i = 0; i < 2; ++i)
    {
        PropVariantInit(&propVal[i]);
    }

    propSpec[0].ulKind = PRSPEC_PROPID;
    propSpec[0].propid = WIA_DIP_DEV_ID;
    propSpec[1].ulKind = PRSPEC_PROPID;
    propSpec[1].propid = WIA_DIP_DEV_NAME;

    hr = pProp->ReadMultiple(2, propSpec, propVal);
    if (FAILED(hr))
    {
        std::cerr << "ReadMultiple failed, hr=0x" << std::hex << hr << "\n";
        for (int i = 0; i < 2; ++i)
        {
            PropVariantClear(&propVal[i]);
        }
        SafeRelease(reinterpret_cast<IUnknown *&>(pProp));
        SafeRelease(reinterpret_cast<IUnknown *&>(pEnum));
        SafeRelease(reinterpret_cast<IUnknown *&>(pWiaDevMgr));
        return false;
    }

    BSTR bstrDevId = nullptr;
    if (propVal[0].vt == VT_BSTR && propVal[0].bstrVal)
    {
        bstrDevId = SysAllocString(propVal[0].bstrVal);
    }

    if (propVal[1].vt == VT_BSTR && propVal[1].bstrVal)
    {
        std::cout << "Using WIA device: " << BstrToUtf8(propVal[1].bstrVal) << "\n";
    }

    for (int i = 0; i < 2; ++i)
    {
        PropVariantClear(&propVal[i]);
    }

    if (!bstrDevId)
    {
        std::cerr << "Cannot read WIA device id.\n";
        SafeRelease(reinterpret_cast<IUnknown *&>(pProp));
        SafeRelease(reinterpret_cast<IUnknown *&>(pEnum));
        SafeRelease(reinterpret_cast<IUnknown *&>(pWiaDevMgr));
        return false;
    }

    outDeviceId = BstrToUtf8(bstrDevId);
    SysFreeString(bstrDevId);

    SafeRelease(reinterpret_cast<IUnknown *&>(pProp));
    SafeRelease(reinterpret_cast<IUnknown *&>(pEnum));
    SafeRelease(reinterpret_cast<IUnknown *&>(pWiaDevMgr));
    return true;
}

/**
 * @brief Ctrl+C handler.
 * @param signum Signal number.
 */
void OnSignal(int signum)
{
    (void)signum;
    g_bRunning.store(false);
}

/**
 * @brief Latency/response statistic for one API call.
 */
struct ApiStat
{
    std::string name;
    std::uint64_t total_calls = 0;
    std::uint64_t success_calls = 0;
    std::uint64_t fail_calls = 0;
    std::uint64_t total_us = 0;
    std::uint64_t min_us = std::numeric_limits<std::uint64_t>::max();
    std::uint64_t max_us = 0;

    explicit ApiStat(std::string apiName)
        : name(std::move(apiName))
    {
    }

    /**
     * @brief Add one call result.
     * @param bOk API returned true if this API has success/fail; or call returned normally.
     * @param durationUs Duration in microseconds.
     */
    void Add(bool bOk, std::uint64_t durationUs)
    {
        ++total_calls;
        if (bOk)
        {
            ++success_calls;
        }
        else
        {
            ++fail_calls;
        }

        total_us += durationUs;
        if (durationUs < min_us)
        {
            min_us = durationUs;
        }
        if (durationUs > max_us)
        {
            max_us = durationUs;
        }
    }

    /**
     * @brief Print one-line stat summary.
     */
    void Print() const
    {
        const double dAvgUs = (total_calls > 0)
                                  ? static_cast<double>(total_us) / static_cast<double>(total_calls)
                                  : 0.0;
        const std::uint64_t shownMin = (min_us == std::numeric_limits<std::uint64_t>::max()) ? 0 : min_us;

        std::cout << std::left << std::setw(20) << name
                  << " total=" << total_calls
                  << " ok=" << success_calls
                  << " fail=" << fail_calls
                  << " avg(us)=" << std::fixed << std::setprecision(2) << dAvgUs
                  << " min(us)=" << shownMin
                  << " max(us)=" << max_us
                  << "\n";
    }
};

/**
 * @brief Measure a callable and return elapsed microseconds.
 * @tparam Fn Callable type.
 * @param fn Function to execute.
 * @return Pair of result and elapsed microseconds.
 */
template <typename Fn>
std::pair<bool, std::uint64_t> MeasureBoolCall(Fn &&fn)
{
    const auto tpBegin = std::chrono::steady_clock::now();
    const bool bResult = fn();
    const auto tpEnd = std::chrono::steady_clock::now();
    const auto us = std::chrono::duration_cast<std::chrono::microseconds>(tpEnd - tpBegin).count();
    return std::make_pair(bResult, static_cast<std::uint64_t>(us));
}

} // namespace

int main(int argc, char *argv[])
{
    std::uint64_t nMaxIterations = 0; // 0 means infinite until Ctrl+C.
    if (argc >= 2)
    {
        try
        {
            nMaxIterations = static_cast<std::uint64_t>(std::stoull(argv[1]));
        }
        catch (...)
        {
            std::cerr << "Invalid max-iterations argument. Use unsigned integer.\n";
            return 1;
        }
    }

    std::signal(SIGINT, OnSignal);

    const HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(hr))
    {
        std::cerr << "CoInitializeEx failed, hr=0x" << std::hex << hr << "\n";
        return 1;
    }

    std::string sWiaDeviceId;
    if (!GetFirstWiaDeviceId(sWiaDeviceId))
    {
        CoUninitialize();
        return 1;
    }

    auto pTransport = std::make_shared<WiaTransport>(sWiaDeviceId);

    SonyPTP3_Impl camCtrl;
    if (!camCtrl.SetPtpTransport(pTransport))
    {
        std::cerr << "SetPtpTransport failed.\n";
        CoUninitialize();
        return 1;
    }

    if (!camCtrl.Connect())
    {
        std::cerr << "Connect failed.\n";
        CoUninitialize();
        return 1;
    }

    ApiStat statUpdateStatus("UpdateStatus");
    ApiStat statGetExposure("GetExposureParams");
    ApiStat statGetMode("GetExposureMode");
    ApiStat statSetExposure("SetExposureParams");

    SonyPTP3_Impl::ExposureParams stLastParams{};
    bool bHaveLastParams = false;

    std::uint64_t nLoopCount = 0;
    constexpr std::uint64_t kPrintEvery = 200;

    std::cout << "Scaffold polling started. Press Ctrl+C to stop.\n";
    if (nMaxIterations > 0)
    {
        std::cout << "Max iterations: " << nMaxIterations << "\n";
    }

    while (g_bRunning.load())
    {
        if (nMaxIterations > 0 && nLoopCount >= nMaxIterations)
        {
            break;
        }

        auto resultUpdate = MeasureBoolCall([&camCtrl]() {
            return camCtrl.UpdateStatus();
        });
        statUpdateStatus.Add(resultUpdate.first, resultUpdate.second);

        SonyPTP3_Impl::ExposureParams stCurrentParams{};
        auto resultGetExposure = MeasureBoolCall([&camCtrl, &stCurrentParams]() {
            return camCtrl.GetExposureParams(stCurrentParams);
        });
        statGetExposure.Add(resultGetExposure.first, resultGetExposure.second);
        if (resultGetExposure.first)
        {
            stLastParams = stCurrentParams;
            bHaveLastParams = true;
        }

        const auto tpModeBegin = std::chrono::steady_clock::now();
        const std::uint32_t nExposureMode = camCtrl.GetExposureMode();
        (void)nExposureMode;
        const auto tpModeEnd = std::chrono::steady_clock::now();
        const auto modeUs = std::chrono::duration_cast<std::chrono::microseconds>(tpModeEnd - tpModeBegin).count();
        statGetMode.Add(true, static_cast<std::uint64_t>(modeUs));

        const SonyPTP3_Impl::ExposureParams &stSetParams = bHaveLastParams ? stLastParams : stCurrentParams;
        auto resultSetExposure = MeasureBoolCall([&camCtrl, &stSetParams]() {
            return camCtrl.SetExposureParams(stSetParams);
        });
        statSetExposure.Add(resultSetExposure.first, resultSetExposure.second);

        ++nLoopCount;

        if ((nLoopCount % kPrintEvery) == 0)
        {
            std::cout << "\n=== Iteration " << nLoopCount << " ===\n";
            statUpdateStatus.Print();
            statGetExposure.Print();
            statGetMode.Print();
            statSetExposure.Print();
            std::cout << std::flush;
        }
    }

    std::cout << "\n=== Final Summary (iterations=" << nLoopCount << ") ===\n";
    statUpdateStatus.Print();
    statGetExposure.Print();
    statGetMode.Print();
    statSetExposure.Print();

    camCtrl.Disconnect();
    CoUninitialize();
    return 0;
}
