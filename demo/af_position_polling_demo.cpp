// Focus distance (meter) polling scaffold

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <iostream>
#include <memory>
#include <thread>
#include <iomanip>
#include <cmath>

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

    std::cout << "Polling AF focus distance at 4 Hz (250 ms). Unit: meters.\n";

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
        if (bInfoOk && stInfo.has_focal_distance_meter)
        {
            if (std::isinf(stInfo.focal_distance_meters))
            {
                std::cout << " distance=infinity";
            }
            else
            {
                std::cout << std::fixed << std::setprecision(3) << " distance=" << stInfo.focal_distance_meters << " m";
            }
        }
        else
        {
            std::cout << " distance=unavailable";
        }
        std::cout << "\n";

        ++nSample;
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
    }

    spCam->Disconnect();
    return 0;
}
