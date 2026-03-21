#include "SonyPTP3_Impl.h"
#include "SonyPTP3_aux.h"

#include <cmath>
#include <cstring> // for std::memcpy
#include <thread>

namespace
{
    constexpr std::uint32_t kButtonDown = 0x0002;
    constexpr std::uint32_t kButtonUp = 0x0001;

    double DecodeShutterSpeedSeconds(std::uint32_t raw_shutter)
    {
        if (raw_shutter == 0U || raw_shutter == 0xFFFFFFFFU)
        {
            return 0.0;
        }

        const std::uint32_t numerator = (raw_shutter >> 16) & 0xFFFFU;
        const std::uint32_t denominator = raw_shutter & 0xFFFFU;
        if (denominator == 0U)
        {
            return 0.0;
        }

        return static_cast<double>(numerator) / static_cast<double>(denominator);
    }

    double DecodeShutterSpeedReciprocal(std::uint32_t raw_shutter)
    {
        const double shutter_seconds = DecodeShutterSpeedSeconds(raw_shutter);
        if (!(shutter_seconds > 0.0))
        {
            return 0.0;
        }
        return 1.0 / shutter_seconds;
    }

    std::uint32_t EncodeShutterSpeedRawFromSeconds(double shutter_seconds)
    {
        if (!(shutter_seconds > 0.0))
        {
            return 0U;
        }

        std::uint32_t best_numerator = 1U;
        std::uint32_t best_denominator = 1U;
        double best_error = std::abs(shutter_seconds - 1.0);

        for (std::uint32_t denominator = 1U; denominator <= 10000U; ++denominator)
        {
            const double numerator_real = shutter_seconds * static_cast<double>(denominator);
            const long long numerator_rounded = std::llround(numerator_real);
            if (numerator_rounded < 1LL || numerator_rounded > 65535LL)
            {
                continue;
            }

            const double candidate = static_cast<double>(numerator_rounded) /
                                     static_cast<double>(denominator);
            const double error = std::abs(shutter_seconds - candidate);
            if (error < best_error)
            {
                best_error = error;
                best_numerator = static_cast<std::uint32_t>(numerator_rounded);
                best_denominator = denominator;
                if (best_error == 0.0)
                {
                    break;
                }
            }
        }

        return (best_numerator << 16) | best_denominator;
    }

    std::uint32_t EncodeShutterSpeedRaw(double shutter_speed_reciprocal)
    {
        if (!(shutter_speed_reciprocal > 0.0))
        {
            return 0U;
        }

        const double shutter_seconds = 1.0 / shutter_speed_reciprocal;
        return EncodeShutterSpeedRawFromSeconds(shutter_seconds);
    }

    double DecodeFNumber(std::uint16_t raw_f_number)
    {
        return static_cast<double>(raw_f_number) / 100.0;
    }

    std::uint16_t EncodeFNumberRaw(double f_number)
    {
        if (!(f_number > 0.0))
        {
            return 0U;
        }

        const long long raw = std::llround(f_number * 100.0);
        if (raw < 0LL)
        {
            return 0U;
        }
        if (raw > 65535LL)
        {
            return 65535U;
        }
        return static_cast<std::uint16_t>(raw);
    }
}

SonyPTP3_Impl::SonyPTP3_Impl() = default;

SonyPTP3_Impl::~SonyPTP3_Impl() { Disconnect(); }

bool SonyPTP3_Impl::Connect()
{
    std::unique_lock<std::timed_mutex> lock(api_mutex_, std::chrono::milliseconds(_CONN_TIMEOUT_MS));
    if (!lock) return false;
    
    if (!transport_)
    {
        connection_state_ = ConnectionState::Disconnected;
        return false;
    }

    if (connection_state_ == ConnectionState::SessionOpen)
    {
        return true;
    }

    connection_state_ = ConnectionState::TransportReady;

    auto call = [&](std::uint16_t opcode, const std::vector<std::uint32_t> &params,
                    PTP_EscapeResult &out_res) -> bool
    {
        out_res = transport_->Escape(opcode, params, nullptr, 0);
        return !PTP_HR_FAILED(out_res.hr) && out_res.responseCode == sonyptp3::PTP_RC_OK;
    };

    // Helper method 
    // When the camera immediately rejects connection attempts (e.g. Device Busy,
    // Session Not Open, Authentication Failed), we send a CloseSession before
    // retrying to ensure the camera is left in a clean, disconnected state.
    auto send_disconnect_message = [&]()
    {
        PTP_EscapeResult close_res = transport_->Escape(sonyptp3::PTP_OC_CloseSession, {}, nullptr, 0);
        (void)close_res;
    };

    // Hepler method
    // Some response codes mean the camera actively refused the connection
    // attempt (e.g. it is busy, already has a session, or rejected auth). In
    // these cases we want to also emit a CloseSession before retrying to
    // recover the camera into a known clean state.
    auto is_immediate_reject = [&](const PTP_EscapeResult &res) -> bool
    {
        if (PTP_HR_FAILED(res.hr))
        {
            // Transport-level failure; retry logic belongs at call site.
            return false;
        }
        if (res.responseCode == sonyptp3::PTP_RC_OK)
        {
            return false;
        }
        switch (res.responseCode)
        {
        case sonyptp3::PTP_RC_DEVICE_BUSY:
        case sonyptp3::PTP_RC_SESSION_ALREADY_OPEN:
        case sonyptp3::PTP_RC_SESSION_NOT_OPEN:
        case sonyptp3::PTP_RC_AUTHENTICATION_FAILED:
            return true;
        default:
            return false;
        }
    };

    PTP_EscapeResult last_res{};

    // Helper method
    auto connect_once_with_detail = [&](PTP_EscapeResult &out_last_res) -> bool
    {
        PTP_EscapeResult res{};
        if (!call(sonyptp3::PTP_OC_SDIOConnect, {1, 0, 0}, res))
        {
            out_last_res = res;
            return false;
        }
        if (!call(sonyptp3::PTP_OC_SDIOConnect, {2, 0, 0}, res))
        {
            out_last_res = res;
            return false;
        }

        bool ext_info_ok = false;
        PTP_EscapeResult last_ext_res = res;
        for (int iTry = 0; iTry < 3; ++iTry)
        {
            if (!call(sonyptp3::PTP_OC_SDIOGetExtDeviceInfo, {sonyptp3::SDI_Extension_Version}, res))
            {
                last_ext_res = res;
                continue;
            }
            last_ext_res = res;
            if (!res.payload.empty())
            {
                ext_info_ok = true;
                break;
            }
        }
        if (!ext_info_ok)
        {
            out_last_res = last_ext_res;
            return false;
        }

        if (!call(sonyptp3::PTP_OC_SDIOConnect, {3, 0, 0}, res))
        {
            out_last_res = res;
            return false;
        }

        PTP_EscapeResult open_res = transport_->Escape(sonyptp3::PTP_OC_SDIOOpenSession, {1}, nullptr, 0);
        if (PTP_HR_FAILED(open_res.hr) || open_res.responseCode != sonyptp3::PTP_RC_OK)
        {
            out_last_res = open_res;
            return false;
        }
        out_last_res = open_res;
        return true;
    };

    if (!connect_once_with_detail(last_res))
    {
        if (is_immediate_reject(last_res))
        {
            send_disconnect_message();
            std::this_thread::sleep_for(std::chrono::milliseconds(_CONN_RETRY_TIMEOUT_MS));
            if (!connect_once_with_detail(last_res))
            {
                connection_state_ = transport_ ? ConnectionState::TransportReady
                                               : ConnectionState::Disconnected;
                return false;
            }
        }
        else
        {
            connection_state_ = transport_ ? ConnectionState::TransportReady
                                           : ConnectionState::Disconnected;
            return false;
        }
    }

    connection_state_ = ConnectionState::SessionOpen;
    has_status_ = false;
    return true;
}

bool SonyPTP3_Impl::SetPtpTransport(IPTPTransportPtr transport)
{
    std::unique_lock<std::timed_mutex> lock(api_mutex_, std::chrono::milliseconds(5000));
    if (!lock) return false;

    if (transport_.get() == transport.get())
    {
        if (!transport_)
        {
            connection_state_ = ConnectionState::Disconnected;
            has_status_ = false;
            return false;
        }

        if (connection_state_ != ConnectionState::SessionOpen)
        {
            connection_state_ = ConnectionState::TransportReady;
        }
        return true;
    }

    if (transport_ && connection_state_ == ConnectionState::SessionOpen)
    {
        ControlDevice(sonyptp3::DPC_MOVIE_REC, kButtonUp);
        ControlDevice(sonyptp3::DPC_S1_BUTTON, kButtonUp);
        PTP_EscapeResult close_res = transport_->Escape(sonyptp3::PTP_OC_CloseSession, {}, nullptr, 0);
        (void)close_res;
    }

    transport_ = std::move(transport);
    has_status_ = false;
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

    connection_state_ = transport_ ? ConnectionState::TransportReady
                                   : ConnectionState::Disconnected;
    has_status_ = false;
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
        cache_.exposure_params.shutter_speed =
            DecodeShutterSpeedReciprocal(static_cast<std::uint32_t>(itShutter->second));
    }
    if (itFNo != props.end())
    {
        cache_.exposure_params.f_number =
            DecodeFNumber(static_cast<std::uint16_t>(itFNo->second));
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
    ok &= SetDevicePropValue(sonyptp3::DPC_SHUTTER_SPEED,
                             EncodeShutterSpeedRaw(params.shutter_speed),
                             sizeof(std::uint32_t));
    ok &= SetDevicePropValue(sonyptp3::DPC_FNUMBER,
                             EncodeFNumberRaw(params.f_number),
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
