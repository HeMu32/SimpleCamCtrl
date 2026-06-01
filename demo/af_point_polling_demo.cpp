/*
 * @file af_point_polling_demo.cpp
 * @brief Poll AF area point position at 5 Hz and print normalized percentages.
 */

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <iostream>
#include <memory>
#include <thread>
#include <iomanip>

#include <CamCtrl/ISimpleCamCtrl.h>
#include <SonyPTP3_aux.h>
#include <SonyPTP3DevEnum.h>
#include <SonyPTP3_Impl.h>

namespace
{
std::atomic<bool> g_running{true};

void OnSignal(int signum)
{
    (void)signum;
    g_running.store(false);
}

std::shared_ptr<SonyPTP3_Impl> OpenFirstSonyDevice()
{
    SonyPTP3DevEnum stEnum;
    if (!stEnum.Refresh())
    {
        std::cerr << "Device refresh failed.\n";
        return nullptr;
    }

    const auto vDevices = stEnum.ListDevices();
    if (vDevices.empty())
    {
        std::cerr << "No matching Sony PTP3 device found.\n";
        return nullptr;
    }

    std::cout << "Using device[0]: " << vDevices[0].sDeviceName << "\n";

    TSonyPTP3OpenParams stOpenParams;
    stOpenParams.bAutoUpdateStatus = true;
    TOpaqueDeviceHandle hDev = stEnum.OpenByIndex(vDevices[0].nDeviceIndex, stOpenParams);
    if (!hDev)
    {
        std::cerr << "OpenByIndex failed for first matching device.\n";
        return nullptr;
    }

    auto spCam = std::static_pointer_cast<SonyPTP3_Impl>(hDev);
    if (!spCam || !spCam->IsConnected())
    {
        std::cerr << "Opened device is not connected.\n";
        return nullptr;
    }
    return spCam;
}

} // namespace

int main(int argc, char *argv[])
{
    std::uint32_t nMaxSamples = 0;
    if (argc >= 2)
    {
        try { nMaxSamples = static_cast<std::uint32_t>(std::stoul(argv[1])); }
        catch (...) { std::cerr << "Invalid max-samples.\n"; return 1; }
    }

    std::signal(SIGINT, OnSignal);

    auto spCam = OpenFirstSonyDevice();
    if (!spCam)
    {
        return 1;
    }

    const bool bPositionKeyOk = spCam->SetPositionKeyBestEffort(0x01);
    const bool bFocusModeOk = spCam->SetFocusModeBestEffort(sonyptp3::DPC_SONY_FOCUS_MODE_AF_C);
    const bool bAreaModeOk = spCam->SetAfAreaModeBestEffort(sonyptp3::DPC_SONY_FOCUS_AREA_FLEXIBLE_SPOT_FREE_SIZE_1);
    const bool bAfFreeSizeOk = spCam->SetAfFreeSizeAndPositionBestEffort(7.9, 6.1, 50.0, 50.0);
    // Values are normalized percentages of frame size/position; the
    // implementation maps them to the camera's supported range.
    // Box size (7.9, 6.1): in raw value (39, 38): likely the min. size for ILCE-7RM5; 
    // (39, 1): min. accpected raw value for ILCE-7RM5 
    // (Requires FW ver 4.00+ on ILCE-7RM5; Older FW dosen't support Free Size)
    std::cout << "Set position-key=" << (bPositionKeyOk ? "ok" : "fail")
              << " AF-C=" << (bFocusModeOk ? "ok" : "fail")
              << " AF area mode=" << (bAreaModeOk ? "ok" : "fail")
              << " AF free size/pos=" << (bAfFreeSizeOk ? "ok" : "fail") << "\n";

    (void)spCam->UpdateStatus();

    std::cout << "Polling AF area point at 5 Hz (200 ms). This is AF point position, not focus distance.\n";

    std::uint32_t nSample = 0;
    while (g_running.load())
    {
        if (nMaxSamples > 0 && nSample >= nMaxSamples)
        {
            break;
        }

        const bool bUpdateOk = spCam->UpdateStatus();
        ISimpleCamCtrl::FocusPositionInfo stInfo;
        const bool bInfoOk = spCam->GetFocusPositionInfo(stInfo);

        std::cout << "sample=" << nSample;
        if (!bUpdateOk)
        {
            std::cout << " update=fail";
        }
        if (bInfoOk && stInfo.has_af_area_position)
        {
            std::cout << std::fixed << std::setprecision(1) << " AF(x%,y%)=(" << stInfo.af_area_x << "," << stInfo.af_area_y << ")";
        }
        else
        {
            std::cout << " AF(x%,y%)=unavailable";
        }
        std::cout << "\n";

        ++nSample;
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    spCam->Disconnect();
    return 0;
}
