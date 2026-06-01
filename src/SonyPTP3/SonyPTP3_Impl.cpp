#include "SonyPTP3_Impl.h"
#include "SonyPTP3_aux.h"

#include <cmath>
#include <limits>
#include <cstring> // for std::memcpy
#include <iostream>
#include <thread>

namespace
{
    void LogSonyPTP3ImplLifecycle(const char* pszStage, const SonyPTP3_Impl* pSelf)
    {
#if defined(_DEBUG)
        std::cerr << "[Lifecycle][SonyPTP3_Impl] " << pszStage
                  << " this=" << pSelf
                  << " thread=" << std::this_thread::get_id()
                  << std::endl;
#else
        (void)pszStage;
        (void)pSelf;
#endif
    }

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

    double RawCoordinateToPercent(std::uint16_t raw_value, std::uint16_t raw_max)
    {
        if (raw_max == 0U)
        {
            return 0.0;
        }

        return (static_cast<double>(raw_value) * 100.0) / static_cast<double>(raw_max);
    }

    std::uint16_t PercentToRawCoordinate(double percent_value, std::uint16_t raw_max)
    {
        if (raw_max == 0U)
        {
            return 0U;
        }

        const double clamped_percent = sonyptp3::Clamp(percent_value, 0.0, 100.0);
        const double raw_value = (clamped_percent / 100.0) * static_cast<double>(raw_max);
        const long long raw_rounded = std::llround(raw_value);
        if (raw_rounded < 0LL)
        {
            return 0U;
        }
        if (raw_rounded > static_cast<long long>(raw_max))
        {
            return raw_max;
        }
        return static_cast<std::uint16_t>(raw_rounded);
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

SonyPTP3_Impl::SonyPTP3_Impl()
{
    SetFriendlyName("SonyPTP3");
    StartPollingWorker();
}

SonyPTP3_Impl::~SonyPTP3_Impl()
{
    StopPollingWorker();
    Disconnect();
}

bool SonyPTP3_Impl::Connect()
{
    LogSonyPTP3ImplLifecycle("Connect begin", this);
    std::unique_lock<std::timed_mutex> lock(api_mutex_, std::chrono::milliseconds(_CONN_TIMEOUT_MS));
    if (!lock)
    {
        LogSonyPTP3ImplLifecycle("Connect end: api lock timeout", this);
        return false;
    }
    
    if (!transport_)
    {
        connection_state_ = ConnectionState::Disconnected;
        LogSonyPTP3ImplLifecycle("Connect end: no transport", this);
        return false;
    }

    if (connection_state_ == ConnectionState::SessionOpen)
    {
        LogSonyPTP3ImplLifecycle("Connect end: already open", this);
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
                LogSonyPTP3ImplLifecycle("Connect end: retry failed", this);
                return false;
            }
        }
        else
        {
            connection_state_ = transport_ ? ConnectionState::TransportReady
                                           : ConnectionState::Disconnected;
            LogSonyPTP3ImplLifecycle("Connect end: initial connect failed", this);
            return false;
        }
    }

    connection_state_ = ConnectionState::SessionOpen;
    has_status_ = false;
    LogSonyPTP3ImplLifecycle("Connect end: success", this);
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
    has_cached_status_params_ = false;
    cached_status_params_.clear();
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
    LogSonyPTP3ImplLifecycle("Disconnect begin", this);
    std::unique_lock<std::timed_mutex> lock(api_mutex_, std::chrono::milliseconds(5000));
    if (!lock)
    {
        LogSonyPTP3ImplLifecycle("Disconnect end: api lock timeout", this);
        return;
    }

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
    has_cached_status_params_ = false;
    cached_status_params_.clear();
    LogSonyPTP3ImplLifecycle("Disconnect end", this);
}

bool SonyPTP3_Impl::IsConnected() const
{
    std::unique_lock<std::timed_mutex> lock(api_mutex_, std::chrono::milliseconds(5000));
    if (!lock) return false;

    return (connection_state_ == ConnectionState::SessionOpen) && transport_;
}

std::string SonyPTP3_Impl::GetFriendlyName() const
{
    std::lock_guard<std::mutex> lock(cache_mutex_);
    return friendly_name_;
}

void SonyPTP3_Impl::SetFriendlyName(const std::string& sName)
{
    std::lock_guard<std::mutex> lock(cache_mutex_);
    friendly_name_ = sName.empty() ? std::string("SonyPTP3") : sName;
}

bool SonyPTP3_Impl::UpdateStatus()
{
    std::unique_lock<std::timed_mutex> lock(api_mutex_, std::chrono::milliseconds(5000));
    if (!lock) return false;

    if (!EnsureConnected())
    {
        return false;
    }

    // Prefer requesting full property snapshot with extended-device-property
    // option enabled (per PTP3 spec: param1=0, param2=1). Fall back to older
    // calling conventions for compatibility with legacy bodies/firmware.
    static const std::vector<std::uint32_t> kFallbackParams[] = {
        {0U, 1U},
        {0U},
        {},
    };

    PTP_EscapeResult res;
    int nSuccessIdx = -1;

    if (has_cached_status_params_)
    {
        res = transport_->Escape(sonyptp3::PTP_OC_SDIOGetAllExtDeviceInfo,
                                 cached_status_params_, nullptr, 0);
        if (PTP_HR_SUCCEEDED(res.hr) && res.responseCode == sonyptp3::PTP_RC_OK)
        {
            nSuccessIdx = -2;
        }
    }

    if (nSuccessIdx == -1)
    {
        for (int i = 0; i < static_cast<int>(sizeof(kFallbackParams) / sizeof(kFallbackParams[0])); ++i)
        {
            res = transport_->Escape(sonyptp3::PTP_OC_SDIOGetAllExtDeviceInfo,
                                     kFallbackParams[i], nullptr, 0);
            if (PTP_HR_SUCCEEDED(res.hr) && res.responseCode == sonyptp3::PTP_RC_OK)
            {
                nSuccessIdx = i;
                break;
            }
        }
    }

    if (nSuccessIdx == -1)
    {
        return false;
    }

    if (nSuccessIdx >= 0 && !has_cached_status_params_)
    {
        cached_status_params_ = kFallbackParams[nSuccessIdx];
        has_cached_status_params_ = true;
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
    const auto itFocalLength = props.find(static_cast<std::uint16_t>(sonyptp3::DPC_FOCAL_LENGTH));
    const auto itFocalLengthSteadyShot = props.find(static_cast<std::uint16_t>(sonyptp3::DPC_FOCAL_LENGTH_STEADY_SHOT));
    const auto itFocalLengthVendor = props.find(static_cast<std::uint16_t>(sonyptp3::DPC_FOCAL_LENGTH_VENDOR));
    const auto itZoomDistance = props.find(static_cast<std::uint16_t>(sonyptp3::DPC_ZOOM_DISTANCE));
    const auto itFocusPositionTarget = props.find(static_cast<std::uint16_t>(sonyptp3::DPC_FOCUS_POSITION_SETTING));
    const auto itFocusPositionCurrent = props.find(static_cast<std::uint16_t>(sonyptp3::DPC_FOCUS_POSITION_CURRENT_VALUE));
    const auto itFocalDistanceMeter = props.find(static_cast<std::uint16_t>(sonyptp3::DPC_FOCAL_DISTANCE_IN_METER));
    const auto itFollowFocusPosition = props.find(static_cast<std::uint16_t>(sonyptp3::DPC_FOLLOW_FOCUS_POSITION_CURRENT_VALUE));
    const auto itAfAreaPosition = props.find(static_cast<std::uint16_t>(sonyptp3::DPC_FOCUS_AREA_X_Y_AF_C));
    const auto itAfFreeSizePosition = props.find(static_cast<std::uint16_t>(sonyptp3::DPC_AF_FREE_SIZE_AND_POSITION_SETTING));
    const auto itFocusModeStatus = props.find(static_cast<std::uint16_t>(sonyptp3::DPC_FOCUS_MODE_STATUS));
    const auto itFocusTrackingStatus = props.find(static_cast<std::uint16_t>(sonyptp3::DPC_FOCUS_TRACKING_STATUS));
    const auto itMovie = props.find(static_cast<std::uint16_t>(sonyptp3::DPC_MOVIE_REC));

    const bool bHasAnyStatusField =
        (itShutter != props.end()) ||
        (itFNo != props.end()) ||
        (itIso != props.end()) ||
        (itExpComp != props.end()) ||
        (itExpMode != props.end()) ||
        (itFocalLength != props.end()) ||
        (itFocalLengthSteadyShot != props.end()) ||
        (itFocalLengthVendor != props.end()) ||
        (itZoomDistance != props.end()) ||
        (itFocusPositionTarget != props.end()) ||
        (itFocusPositionCurrent != props.end()) ||
        (itFocalDistanceMeter != props.end()) ||
        (itFollowFocusPosition != props.end()) ||
        (itAfAreaPosition != props.end()) ||
        (itAfFreeSizePosition != props.end()) ||
        (itFocusModeStatus != props.end()) ||
        (itFocusTrackingStatus != props.end());

    if (!bHasAnyStatusField && !has_status_)
    {
        return false;
    }

    // Use cache_mutex_ for short-duration updates to avoid blocking simple getters.
    {
        std::lock_guard<std::mutex> cache_lock(cache_mutex_);

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
        if (itFocalLength != props.end() || itFocalLengthSteadyShot != props.end() ||
            itFocalLengthVendor != props.end() || itZoomDistance != props.end())
        {
            double focal_mm = cache_.exposure_params.focal_length;
            if (itFocalLength != props.end())
            {
                // Standard PTP focal length (0x5008) is represented in 0.01mm units.
                focal_mm = static_cast<double>(itFocalLength->second) / 100.0;
            }
            else if (itZoomDistance != props.end() && itZoomDistance->second > 0ULL)
            {
                // 0xD00B is Zoom Distance with 0.001 mm unit.
                focal_mm = static_cast<double>(itZoomDistance->second) / 1000.0;
            }
            else if (itFocalLengthSteadyShot != props.end() && itFocalLengthSteadyShot->second > 0ULL)
            {
                // Sony 0xD193 list values are in millimeters.
                focal_mm = static_cast<double>(itFocalLengthSteadyShot->second);
            }
            else if (itFocalLengthVendor != props.end() && itFocalLengthVendor->second > 0ULL)
            {
                // Historical vendor fallback kept at 0.01mm scaling.
                focal_mm = static_cast<double>(itFocalLengthVendor->second) / 100.0;
            }
            cache_.exposure_params.focal_length = focal_mm;
        }
        if (itMovie != props.end())
        {
            cache_.movie_recording = (itMovie->second != 0);
        }

        cache_.focus_position = ISimpleCamCtrl::FocusPositionInfo{};
        if (itFocusPositionTarget != props.end())
        {
            cache_.focus_position.sony_type_mask |= SONY_FOCUS_POSITION_TYPE_ABSOLUTE;
            cache_.focus_position.absolute_target = static_cast<std::uint16_t>(itFocusPositionTarget->second);
            cache_.focus_position.has_absolute_target = true;
        }
        if (itFocusPositionCurrent != props.end())
        {
            cache_.focus_position.sony_type_mask |= SONY_FOCUS_POSITION_TYPE_ABSOLUTE;
            cache_.focus_position.absolute_current = static_cast<std::uint16_t>(itFocusPositionCurrent->second);
            cache_.focus_position.has_absolute_current = true;
        }
        if (itFocalDistanceMeter != props.end())
        {
            cache_.focus_position.sony_type_mask |= SONY_FOCUS_POSITION_TYPE_FOCAL_DISTANCE_METER;
            const std::uint32_t raw = static_cast<std::uint32_t>(itFocalDistanceMeter->second);
            if (raw == 0xFFFFFFFFU)
            {
                cache_.focus_position.focal_distance_meters = std::numeric_limits<double>::infinity();
            }
            else
            {
                // Device reports focal distance in units of 0.001 m per spec.
                cache_.focus_position.focal_distance_meters = static_cast<double>(raw) / 1000.0;
            }
            cache_.focus_position.has_focal_distance_meter = true;
        }
        if (itFollowFocusPosition != props.end())
        {
            cache_.focus_position.sony_type_mask |= SONY_FOCUS_POSITION_TYPE_FOLLOW_FOCUS;
            cache_.focus_position.follow_focus_current = static_cast<std::uint32_t>(itFollowFocusPosition->second);
            cache_.focus_position.has_follow_focus_current = true;
        }
        if (itAfAreaPosition != props.end())
        {
            cache_.focus_position.sony_type_mask |= SONY_FOCUS_POSITION_TYPE_AF_AREA_POINT;
            const std::uint32_t raw_xy = static_cast<std::uint32_t>(itAfAreaPosition->second);
            cache_.focus_position.af_area_x = RawCoordinateToPercent(
                static_cast<std::uint16_t>((raw_xy >> 16) & 0xFFFFU),
                sonyptp3::DPC_SONY_AF_AREA_X_MAX);
            cache_.focus_position.af_area_y = RawCoordinateToPercent(
                static_cast<std::uint16_t>(raw_xy & 0xFFFFU),
                sonyptp3::DPC_SONY_AF_AREA_Y_MAX);
            cache_.focus_position.has_af_area_position = true;
        }
        else if (itAfFreeSizePosition != props.end())
        {
            cache_.focus_position.sony_type_mask |= SONY_FOCUS_POSITION_TYPE_AF_AREA_POINT;
            const std::uint32_t raw_xy = static_cast<std::uint32_t>(itAfFreeSizePosition->second & 0xFFFFFFFFULL);
            cache_.focus_position.af_area_x = RawCoordinateToPercent(
                static_cast<std::uint16_t>((raw_xy >> 16) & 0xFFFFU),
                sonyptp3::DPC_SONY_AF_AREA_X_MAX);
            cache_.focus_position.af_area_y = RawCoordinateToPercent(
                static_cast<std::uint16_t>(raw_xy & 0xFFFFU),
                sonyptp3::DPC_SONY_AF_AREA_Y_MAX);
            cache_.focus_position.has_af_area_position = true;
        }
        if (itFocusModeStatus != props.end())
        {
            cache_.focus_position.focus_mode_status = static_cast<std::uint8_t>(itFocusModeStatus->second);
            cache_.focus_position.has_focus_mode_status = true;
        }
        if (itFocusTrackingStatus != props.end())
        {
            cache_.focus_position.focus_tracking_status = static_cast<std::uint8_t>(itFocusTrackingStatus->second);
            cache_.focus_position.has_focus_tracking_status = true;
        }
    }

    if (!bHasAnyStatusField)
    {
        // If we cannot find any cached status field in the returned properties,
        // log raw properties for debug analysis.
        std::cerr << "[SonyPTP3_Impl] UpdateStatus: no cached status field found. Available props: ";
        for (const auto &p : props)
        {
            std::cerr << std::hex << "0x" << p.first << "=0x" << p.second << " ";
        }
        std::cerr << std::dec << "\n";
    }

    if (bHasAnyStatusField)
    {
        has_status_ = true;
    }
    return true;
}

bool SonyPTP3_Impl::GetExposureParams(ExposureParams &out_params) const
{
    std::lock_guard<std::mutex> lock(cache_mutex_);
    if (!has_status_)
    {
        return false;
    }
    out_params = cache_.exposure_params;
    return true;
}

bool SonyPTP3_Impl::GetFocusPositionInfo(ISimpleCamCtrl::FocusPositionInfo &out_info) const
{
    std::lock_guard<std::mutex> lock(cache_mutex_);
    if (!has_status_)
    {
        return false;
    }
    out_info = cache_.focus_position;
        return (out_info.has_absolute_target || out_info.has_absolute_current ||
            out_info.has_focal_distance_meter || out_info.has_follow_focus_current ||
            out_info.has_af_area_position ||
            out_info.has_focus_mode_status || out_info.has_focus_tracking_status);
}

bool SonyPTP3_Impl::SetFocusPositionBestEffort(std::uint16_t raw_position)
{
    std::unique_lock<std::timed_mutex> lock(api_mutex_, std::chrono::milliseconds(50));
    if (!lock)
    {
        return false;
    }

    if (!EnsureConnected())
    {
        return false;
    }

    const bool ok = SetDevicePropValue(sonyptp3::DPC_FOCUS_POSITION_SETTING,
                                       raw_position, sizeof(raw_position));
    if (ok)
    {
        std::lock_guard<std::mutex> cache_lock(cache_mutex_);
        cache_.focus_position.sony_type_mask |= SONY_FOCUS_POSITION_TYPE_ABSOLUTE;
        cache_.focus_position.absolute_target = raw_position;
        cache_.focus_position.has_absolute_target = true;
        has_status_ = true;
    }
    return ok;
}

bool SonyPTP3_Impl::SetAfAreaPositionBestEffort(double x_percent, double y_percent)
{
    std::unique_lock<std::timed_mutex> lock(api_mutex_, std::chrono::milliseconds(50));
    if (!lock)
    {
        return false;
    }

    if (!std::isfinite(x_percent) || !std::isfinite(y_percent))
    {
        return false;
    }

    if (!EnsureConnected())
    {
        return false;
    }

    const std::uint16_t x = PercentToRawCoordinate(x_percent, sonyptp3::DPC_SONY_AF_AREA_X_MAX);
    const std::uint16_t y = PercentToRawCoordinate(y_percent, sonyptp3::DPC_SONY_AF_AREA_Y_MAX);
    const double applied_x_percent = RawCoordinateToPercent(x, sonyptp3::DPC_SONY_AF_AREA_X_MAX);
    const double applied_y_percent = RawCoordinateToPercent(y, sonyptp3::DPC_SONY_AF_AREA_Y_MAX);

    // Sony AF Area Position (0xD2DC): the implementation maps normalized
    // [0, 100] percentages to the vendor coordinate range, then packs x into
    // the upper 16 bits and y into the lower 16 bits.
    const std::uint32_t packed_xy = (static_cast<std::uint32_t>(x) << 16) |
                                    static_cast<std::uint32_t>(y);
    const bool ok = ControlDevice(sonyptp3::DPC_FOCUS_AREA_X_Y, packed_xy);
    if (ok)
    {
        std::lock_guard<std::mutex> cache_lock(cache_mutex_);
        cache_.focus_position.sony_type_mask |= SONY_FOCUS_POSITION_TYPE_AF_AREA_POINT;
        cache_.focus_position.af_area_x = applied_x_percent;
        cache_.focus_position.af_area_y = applied_y_percent;
        cache_.focus_position.has_af_area_position = true;
        has_status_ = true;
    }
    return ok;
}

bool SonyPTP3_Impl::SetAfAreaModeBestEffort(std::uint16_t raw_area_mode)
{
    std::unique_lock<std::timed_mutex> lock(api_mutex_, std::chrono::milliseconds(50));
    if (!lock)
    {
        return false;
    }

    if (!EnsureConnected())
    {
        return false;
    }

    return SetDevicePropValue(sonyptp3::DPC_FOCUS_AREA,
                              raw_area_mode,
                              sizeof(raw_area_mode));
}

bool SonyPTP3_Impl::SetAfFreeSizeAndPositionBestEffort(double height_percent,
                                                       double width_percent,
                                                       double x_percent,
                                                       double y_percent)
{
    std::unique_lock<std::timed_mutex> lock(api_mutex_, std::chrono::milliseconds(50));
    if (!lock)
    {
        return false;
    }

    if (!std::isfinite(height_percent) || !std::isfinite(width_percent) ||
        !std::isfinite(x_percent) || !std::isfinite(y_percent))
    {
        return false;
    }

    if (!EnsureConnected())
    {
        return false;
    }

    const std::uint16_t height = PercentToRawCoordinate(height_percent, sonyptp3::DPC_SONY_AF_AREA_Y_MAX);
    const std::uint16_t width  = PercentToRawCoordinate(width_percent, sonyptp3::DPC_SONY_AF_AREA_X_MAX);
    
    // raw value (39, 38): likely the min. AF area size for ILCE-7RM5; 
    // (39, 1): min. accpected size for ILCE-7RM5; on smaller value it discard size setting and set only position.
    // (Requires FW ver 4.00+ on ILCE-7RM5; Older FW dosen't support Free Size)
     
    const std::uint16_t x = PercentToRawCoordinate(x_percent, sonyptp3::DPC_SONY_AF_AREA_X_MAX);
    const std::uint16_t y = PercentToRawCoordinate(y_percent, sonyptp3::DPC_SONY_AF_AREA_Y_MAX);

    const std::uint64_t packed =
        (static_cast<std::uint64_t>(height) << 48) |
        (static_cast<std::uint64_t>(width) << 32) |
        (static_cast<std::uint64_t>(x) << 16) |
        static_cast<std::uint64_t>(y);
    const bool ok = SetDevicePropValue(sonyptp3::DPC_AF_FREE_SIZE_AND_POSITION_SETTING,
                                        packed,
                                        sizeof(packed));
    if (ok)
    {
        std::lock_guard<std::mutex> cache_lock(cache_mutex_);
        cache_.focus_position.sony_type_mask |= SONY_FOCUS_POSITION_TYPE_AF_AREA_POINT;
        cache_.focus_position.af_area_x = RawCoordinateToPercent(x, sonyptp3::DPC_SONY_AF_AREA_X_MAX);
        cache_.focus_position.af_area_y = RawCoordinateToPercent(y, sonyptp3::DPC_SONY_AF_AREA_Y_MAX);
        cache_.focus_position.has_af_area_position = true;
        has_status_ = true;
    }
    return ok;
}

bool SonyPTP3_Impl::SetPositionKeyBestEffort(std::uint8_t raw_key)
{
    std::unique_lock<std::timed_mutex> lock(api_mutex_, std::chrono::milliseconds(50));
    if (!lock)
    {
        return false;
    }

    if (!EnsureConnected())
    {
        return false;
    }

    return SetDevicePropValue(sonyptp3::DPC_POSITION_KEY,
                              raw_key,
                              sizeof(raw_key));
}

bool SonyPTP3_Impl::SetFocusModeBestEffort(std::uint32_t raw_mode)
{
    std::unique_lock<std::timed_mutex> lock(api_mutex_, std::chrono::milliseconds(50));
    if (!lock)
    {
        return false;
    }

    if (!EnsureConnected())
    {
        return false;
    }

    return SetDevicePropValue(sonyptp3::DPC_FOCUS_MODE, raw_mode, sizeof(raw_mode));
}

std::uint32_t SonyPTP3_Impl::GetExposureMode() const
{
    std::lock_guard<std::mutex> lock(cache_mutex_);
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

bool SonyPTP3_Impl::UpdateStatusInternal()
{
    // This internal method is expected to not be called directly from clients;
    // it is used by the polling worker, and is allowed to run concurrently with
    // the externals via the same locking policy as UpdateStatus().
    return UpdateStatus();
}

bool SonyPTP3_Impl::StartPollingWorker()
{
    bool expected = false;
    if (!polling_worker_running_.compare_exchange_strong(expected, true))
    {
        return false; // already running
    }

    LogSonyPTP3ImplLifecycle("PollingWorker start", this);
    polling_worker_thread_ = std::thread(&SonyPTP3_Impl::PollingWorkerLoop, this);
    return true;
}

void SonyPTP3_Impl::StopPollingWorker()
{
    bool expected = true;
    if (!polling_worker_running_.compare_exchange_strong(expected, false))
    {
        return; // already stopped
    }

    LogSonyPTP3ImplLifecycle("PollingWorker stop", this);
    polling_worker_cv_.notify_all();
    if (polling_worker_thread_.joinable())
    {
        polling_worker_thread_.join();
    }

    // Reset any pending future so destructor/stop doesn't block on stale state.
    if (polling_worker_future_.valid())
    {
        // if still running, we cannot cancel, but future object can be reset.
        polling_worker_future_ = std::future<bool>();
    }
}

void SonyPTP3_Impl::PollingWorkerLoop()
{
    using namespace std::chrono;

    LogSonyPTP3ImplLifecycle("PollingWorker loop begin", this);
    while (polling_worker_running_)
    {
        const auto cycle_start = steady_clock::now();

        // Check previous update completion
        if (polling_worker_future_.valid())
        {
            if (polling_worker_future_.wait_for(milliseconds(0)) == std::future_status::ready)
            {
                try
                {
                    (void)polling_worker_future_.get();
                }
                catch (...) { }
                polling_worker_future_ = std::future<bool>();
            }
        }

        if (!polling_worker_future_.valid())
        {
            polling_worker_future_ = std::async(std::launch::async, [this]() -> bool {
                return UpdateStatusInternal();
            });
        }

        if (polling_worker_future_.valid())
        {
            if (polling_worker_future_.wait_for(milliseconds(_POLLING_WORKER_TIMEOUT_MS)) == std::future_status::ready)
            {
                try
                {
                    (void)polling_worker_future_.get();
                }
                catch (...) { }
                polling_worker_future_ = std::future<bool>();
            }
            else
            {
                LogSonyPTP3ImplLifecycle("PollingWorker UpdateStatus timeout", this);
            }
            // if timeout, do not block on result; continue next loop
        }

        const auto cycle_end = steady_clock::now();
        const auto elapsed = duration_cast<milliseconds>(cycle_end - cycle_start);
        const auto sleep_time = milliseconds(_POLLING_WORKER_INTER_MS) - elapsed;

        std::unique_lock<std::mutex> lock(polling_worker_mutex_);
        if (sleep_time.count() > 0)
        {
            polling_worker_cv_.wait_for(lock, sleep_time, [this]() { return !polling_worker_running_; });
        }
    }
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
