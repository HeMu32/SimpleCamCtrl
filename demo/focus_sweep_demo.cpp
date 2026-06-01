/*
 * @file focus_sweep_demo.cpp
 * @brief Demo that moves the Sony AF area point diagonally across the frame.
 */

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <iostream>
#include <memory>
#include <thread>
#include <iomanip>

#include <SonyPTP3DevEnum.h>
#include <SonyPTP3_Impl.h>
#include <SonyPTP3_aux.h>

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
    std::uint32_t steps = 120;
    std::uint32_t delay_ms = 120;
    if (argc >= 2)
    {
        try { steps = static_cast<std::uint32_t>(std::stoul(argv[1])); }
        catch (...) { std::cerr << "Invalid steps.\n"; return 1; }
    }
    if (argc >= 3)
    {
        try { delay_ms = static_cast<std::uint32_t>(std::stoul(argv[2])); }
        catch (...) { std::cerr << "Invalid delay-ms.\n"; return 1; }
    }
    if (steps == 0)
    {
        steps = 1;
    }

    std::signal(SIGINT, OnSignal);

    auto spCam = OpenFirstSonyDevice();
    if (!spCam)
    {
        return 1;
    }

    (void)spCam->UpdateStatus();

    const bool modeSetOk = spCam->SetFocusModeBestEffort(sonyptp3::DPC_SONY_FOCUS_MODE_AF_C);
    std::cout << "Set to AF-C mode best-effort: " << (modeSetOk ? "ok" : "failed") << "\n";

    const bool areaModeOk = spCam->SetAfAreaModeBestEffort(sonyptp3::DPC_SONY_FOCUS_AREA_FLEXIBLE_SPOT_FREE_SIZE_1);
    std::cout << "Set AF area mode to Flexible Spot Free Size 1: " << (areaModeOk ? "ok" : "failed") << "\n";

    std::cout << "Moving AF area point from (0%,0%) to (100%,100%), steps=" << steps
              << " delay_ms=" << delay_ms << "\n";

    for (std::uint32_t i = 0; g_running.load() && i <= steps; ++i)
    {
        const double x_percent = (static_cast<double>(i) * 100.0) / static_cast<double>(steps);
        const double y_percent = (static_cast<double>(i) * 100.0) / static_cast<double>(steps);

        const bool ok = spCam->SetAfAreaPositionBestEffort(x_percent, y_percent);
        std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));
        std::cout << std::fixed << std::setprecision(1) << "AF(x%,y%)=(" << x_percent << "," << y_percent << ")"
                  << " set=" << (ok ? "ok" : "fail") << "\n";
    }

    spCam->Disconnect();
    return 0;
}