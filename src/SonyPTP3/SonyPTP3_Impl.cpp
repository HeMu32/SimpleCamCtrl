#include "SonyPTP3_Impl.h"
#include "SonyPTP3_aux.h"

#include <cstring> // for std::memcpy

namespace
{
    constexpr std::uint32_t kButtonDown = 0x0002;
    constexpr std::uint32_t kButtonUp = 0x0001;
}

SonyPTP3_Impl::SonyPTP3_Impl() = default;

SonyPTP3_Impl::~SonyPTP3_Impl() { Disconnect(); }

bool SonyPTP3_Impl::Connect()
{
    std::unique_lock<std::timed_mutex> lock(api_mutex_, std::chrono::milliseconds(5000));
    if (!lock) return false;
    
    if (!transport_)
    {
        connection_state_ = ConnectionState::Disconnected;
        return false;
    }

    auto call = [&](std::uint16_t opcode, const std::vector<std::uint32_t> &params) -> bool
    {
        PTP_EscapeResult res = transport_->Escape(opcode, params, nullptr, 0);
        return !PTP_HR_FAILED(res.hr);
    };

    if (!call(sonyptp3::PTP_OC_SDIOConnect, {1, 0, 0}))
    {
        connection_state_ = ConnectionState::Disconnected;
        return false;
    }
    if (!call(sonyptp3::PTP_OC_SDIOConnect, {2, 0, 0}))
    {
        connection_state_ = ConnectionState::Disconnected;
        return false;
    }

    if (!call(sonyptp3::PTP_OC_SDIOGetExtDeviceInfo, {sonyptp3::SDI_Extension_Version}))
    {
        connection_state_ = ConnectionState::Disconnected;
        return false;
    }

    if (!call(sonyptp3::PTP_OC_SDIOConnect, {3, 0, 0}))
    {
        connection_state_ = ConnectionState::Disconnected;
        return false;
    }

    PTP_EscapeResult openRes = transport_->Escape(sonyptp3::PTP_OC_SDIOOpenSession, {1}, nullptr, 0);
    if (PTP_HR_FAILED(openRes.hr) || openRes.responseCode != sonyptp3::PTP_RC_OK)
    {
        connection_state_ = ConnectionState::Disconnected;
        return false;
    }

    connection_state_ = ConnectionState::SessionOpen;
    has_status_ = false;
    return true;
}

bool SonyPTP3_Impl::SetPtpTransport(IPTPTransportPtr transport)
{
    std::unique_lock<std::timed_mutex> lock(api_mutex_, std::chrono::milliseconds(5000));
    if (!lock) return false;

    transport_ = std::move(transport);
    if (transport_)
    {
        connection_state_ = ConnectionState::TransportReady;
        return true;
    }
    else
    {
        connection_state_ = ConnectionState::Disconnected;
        return false;
    }
}

void SonyPTP3_Impl::Disconnect()
{
    std::unique_lock<std::timed_mutex> lock(api_mutex_, std::chrono::milliseconds(5000));
    if (!lock) return;

    if (transport_ && connection_state_ == ConnectionState::SessionOpen)
    {
        ControlDevice(sonyptp3::DPC_MOVIE_REC, kButtonUp);
        ControlDevice(sonyptp3::DPC_S1_BUTTON, kButtonUp);

        PTP_EscapeResult r = transport_->Escape(sonyptp3::PTP_OC_CloseSession, {}, nullptr, 0);
        (void)r;
    }

    connection_state_ = ConnectionState::Disconnected;
    has_status_ = false;
    transport_.reset();
}

bool SonyPTP3_Impl::IsConnected() const
{
    std::unique_lock<std::timed_mutex> lock(api_mutex_, std::chrono::milliseconds(5000));
    if (!lock) return false;

    return (connection_state_ == ConnectionState::SessionOpen) && transport_;
}

bool SonyPTP3_Impl::UpdateStatus()
{
    std::unique_lock<std::timed_mutex> lock(api_mutex_, std::chrono::milliseconds(5000));
    if (!lock) return false;

    if (!EnsureConnected())
    {
        return false;
    }

    PTP_EscapeResult res = transport_->Escape(sonyptp3::PTP_OC_SDIOGetAllExtDeviceInfo, {},
                                              nullptr, 0);
    if (PTP_HR_FAILED(res.hr) || res.responseCode != sonyptp3::PTP_RC_OK)
    {
        return false;
    }

    std::unordered_map<std::uint16_t, std::uint64_t> props;
    try
    {
        props = sonyptp3::ParseDeviceProperties(res.payload);
    }
    catch (...)
    {
        return false;
    }
    const auto itShutter = props.find(static_cast<std::uint16_t>(sonyptp3::DPC_SHUTTER_SPEED));
    const auto itFNo = props.find(static_cast<std::uint16_t>(sonyptp3::DPC_FNUMBER));
    const auto itIso = props.find(static_cast<std::uint16_t>(sonyptp3::DPC_ISO));
    const auto itExpComp = props.find(static_cast<std::uint16_t>(sonyptp3::DPC_EXPOSURE_COMPENSATION));
    const auto itExpMode = props.find(static_cast<std::uint16_t>(sonyptp3::DPC_EXPOSURE_MODE));
    const auto itMovie = props.find(static_cast<std::uint16_t>(sonyptp3::DPC_MOVIE_REC));

    const bool bHasAnyExposureField =
        (itShutter != props.end()) ||
        (itFNo != props.end()) ||
        (itIso != props.end()) ||
        (itExpComp != props.end()) ||
        (itExpMode != props.end());

    if (!bHasAnyExposureField && !has_status_)
    {
        return false;
    }

    if (itShutter != props.end())
    {
        cache_.exposure_params.shutter_speed = static_cast<std::uint32_t>(itShutter->second);
    }
    if (itFNo != props.end())
    {
        cache_.exposure_params.f_number = static_cast<std::uint16_t>(itFNo->second);
    }
    if (itIso != props.end())
    {
        cache_.exposure_params.iso = static_cast<std::uint32_t>(itIso->second);
    }
    if (itExpComp != props.end())
    {
        cache_.exposure_params.exposure_comp = static_cast<std::int32_t>(itExpComp->second);
    }
    if (itExpMode != props.end())
    {
        cache_.exposure_mode = static_cast<std::uint32_t>(itExpMode->second);
    }
    if (itMovie != props.end())
    {
        cache_.movie_recording = (itMovie->second != 0);
    }

    if (bHasAnyExposureField)
    {
        has_status_ = true;
    }
    return true;
}

bool SonyPTP3_Impl::GetExposureParams(ExposureParams &out_params) const
{
    std::unique_lock<std::timed_mutex> lock(api_mutex_, std::chrono::milliseconds(5000));
    if (!lock) return false;

    if (!has_status_)
    {
        return false;
    }
    out_params = cache_.exposure_params;
    return true;
}

std::uint32_t SonyPTP3_Impl::GetExposureMode() const
{
    std::unique_lock<std::timed_mutex> lock(api_mutex_, std::chrono::milliseconds(5000));
    if (!lock) return 0;

    return cache_.exposure_mode;
}

bool SonyPTP3_Impl::FocusStart()
{
    std::unique_lock<std::timed_mutex> lock(api_mutex_, std::chrono::milliseconds(5000));
    if (!lock) return false;

    return ControlDevice(sonyptp3::DPC_S1_BUTTON, kButtonDown);
}

bool SonyPTP3_Impl::FocusEnd()
{
    std::unique_lock<std::timed_mutex> lock(api_mutex_, std::chrono::milliseconds(5000));
    if (!lock) return false;

    return ControlDevice(sonyptp3::DPC_S1_BUTTON, kButtonUp);
}

bool SonyPTP3_Impl::ShutterStart()
{
    std::unique_lock<std::timed_mutex> lock(api_mutex_, std::chrono::milliseconds(5000));
    if (!lock) return false;

    return ControlDevice(sonyptp3::DPC_S2_BUTTON, kButtonDown);
}

bool SonyPTP3_Impl::ShutterEnd()
{
    std::unique_lock<std::timed_mutex> lock(api_mutex_, std::chrono::milliseconds(5000));
    if (!lock) return false;

    return ControlDevice(sonyptp3::DPC_S2_BUTTON, kButtonUp);
}

bool SonyPTP3_Impl::MovieRecStart()
{
    std::unique_lock<std::timed_mutex> lock(api_mutex_, std::chrono::milliseconds(5000));
    if (!lock) return false;

    bool ok = ControlDevice(sonyptp3::DPC_MOVIE_REC, kButtonDown);
    if (ok)
    {
        cache_.movie_recording = true;
    }
    return ok;
}

bool SonyPTP3_Impl::MovieRecEnd()
{
    std::unique_lock<std::timed_mutex> lock(api_mutex_, std::chrono::milliseconds(5000));
    if (!lock) return false;

    bool ok = ControlDevice(sonyptp3::DPC_MOVIE_REC, kButtonUp);
    if (ok)
    {
        cache_.movie_recording = false;
    }
    return ok;
}

bool SonyPTP3_Impl::IsMovieRecording() const
{
    std::unique_lock<std::timed_mutex> lock(api_mutex_, std::chrono::milliseconds(5000));
    if (!lock) return false;

    return cache_.movie_recording;
}

bool SonyPTP3_Impl::SetExposureParams(const ExposureParams &params)
{
    std::unique_lock<std::timed_mutex> lock(api_mutex_, std::chrono::milliseconds(5000));
    if (!lock) return false;

    if (!EnsureConnected())
    {
        return false;
    }

    bool ok = true;
    ok &= SetDevicePropValue(sonyptp3::DPC_SHUTTER_SPEED, params.shutter_speed,
                             sizeof(std::uint32_t));
    ok &= SetDevicePropValue(sonyptp3::DPC_FNUMBER, params.f_number,
                             sizeof(std::uint16_t));
    ok &= SetDevicePropValue(sonyptp3::DPC_ISO, params.iso, sizeof(std::uint32_t));
    ok &= SetDevicePropValue(sonyptp3::DPC_EXPOSURE_COMPENSATION, params.exposure_comp,
                             sizeof(std::uint32_t));
    return ok;
}

bool SonyPTP3_Impl::EnsureConnected()
{
    return (connection_state_ == ConnectionState::SessionOpen) && transport_;
}

bool SonyPTP3_Impl::UpdateCacheFromDataManager()
{
    return false;
}

bool SonyPTP3_Impl::ControlDevice(std::uint32_t property_code,
                                  std::uint32_t value)
{
    if (!EnsureConnected())
    {
        return false;
    }

    PTP_EscapeResult res =
        transport_->Escape(sonyptp3::PTP_OC_SDIOControlDevice, {property_code},
                           reinterpret_cast<const std::uint8_t *>(&value),
                           sizeof(value));
    return PTP_HR_SUCCEEDED(res.hr) && res.responseCode == sonyptp3::PTP_RC_OK;
}

bool SonyPTP3_Impl::SetDevicePropValue(std::uint32_t property_code,
                                       std::uint64_t value,
                                       std::uint32_t size_bytes)
{
    if (!EnsureConnected())
    {
        return false;
    }
    if (size_bytes == 0 || size_bytes > sizeof(std::uint64_t))
    {
        return false;
    }

    std::uint8_t buffer[sizeof(std::uint64_t)] = {};
    std::memcpy(buffer, &value, size_bytes);
    PTP_EscapeResult res =
        transport_->Escape(sonyptp3::PTP_OC_SDIOSetExtDevicePropValue, {property_code},
                           reinterpret_cast<const std::uint8_t *>(buffer), size_bytes);
    return PTP_HR_SUCCEEDED(res.hr) && res.responseCode == sonyptp3::PTP_RC_OK;
}