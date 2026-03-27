#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>

/**
 * @file SonyPTP3_aux.h
 * @brief Helper utilities for the Sony PTP3 wrapper (constants, string conversions, parsing).
 */

namespace sonyptp3
{
    // =========================================================================
    // PTP Constants (Shared Ops & Props)
    // =========================================================================

    // Operation Codes
    constexpr std::uint16_t PTP_OC_CloseSession = 0x1003;
    constexpr std::uint16_t PTP_OC_SDIOConnect = 0x9201;
    constexpr std::uint16_t PTP_OC_SDIOGetExtDeviceInfo = 0x9202;
    constexpr std::uint16_t PTP_OC_SDIOSetExtDevicePropValue = 0x9205;
    constexpr std::uint16_t PTP_OC_SDIOControlDevice = 0x9207;
    constexpr std::uint16_t PTP_OC_SDIOGetAllExtDeviceInfo = 0x9209;
    constexpr std::uint16_t PTP_OC_SDIOSetContentsTransferMode = 0x9212;
    constexpr std::uint16_t PTP_OC_SDIOOpenSession = 0x9203;

    // SDI extension version used by SDIOGetExtDeviceInfo
    constexpr std::uint32_t SDI_Extension_Version = 0x12C;

    // Response Codes
    constexpr std::uint16_t PTP_RC_OK = 0x2001;
    constexpr std::uint16_t PTP_RC_UNDEFINED = 0x2000;

    // The following response codes are treated as *immediate rejects* during
    // the SDIO connection sequence (e.g. camera says "no" before the session
    // is established). When these occur, we send a CloseSession to ensure the
    // camera is returned to a clean state and then retry once.
    constexpr std::uint16_t PTP_RC_SESSION_NOT_OPEN = 0x2003;
    constexpr std::uint16_t PTP_RC_DEVICE_BUSY = 0x2019;
    constexpr std::uint16_t PTP_RC_SESSION_ALREADY_OPEN = 0x201E;
    constexpr std::uint16_t PTP_RC_AUTHENTICATION_FAILED = 0xA101;

    // Device Property Codes (Sony Vendor Extension)
    constexpr std::uint32_t DPC_MOVIE_REC = 0xD2C8;
    constexpr std::uint32_t DPC_S1_BUTTON = 0xD2C1;
    constexpr std::uint32_t DPC_S2_BUTTON = 0xD2C2;
    constexpr std::uint32_t DPC_SHUTTER_SPEED = 0xD20D;
    constexpr std::uint32_t DPC_FNUMBER = 0x5007; // Standard PTP
    constexpr std::uint32_t DPC_ISO = 0xD21E;
    constexpr std::uint32_t DPC_EXPOSURE_COMPENSATION = 0x5010;
    constexpr std::uint32_t DPC_EXPOSURE_MODE = 0x500E;
    constexpr std::uint32_t DPC_FOCAL_LENGTH = 0x5008; // Standard PTP (if available)
    constexpr std::uint32_t DPC_FOCAL_LENGTH_STEADY_SHOT = 0xD193; // Image stabilization steady shot focal length
    constexpr std::uint32_t DPC_FOCAL_LENGTH_VENDOR = 0xD2D6; // Sony vendor extension fallback
    constexpr std::uint32_t DPC_ZOOM_DISTANCE = 0xD00B; // Zoom distance (0.001 mm unit)

    // =========================================================================
    // Utilities
    // =========================================================================

    /**
     * @brief Parses the payload from PTP_OC_SDIOGetAllExtDeviceInfo.
     * @param payload Raw byte data.
     * @return Map of PropertyCode -> Value (as u64).
     */
    std::unordered_map<uint16_t, uint64_t> ParseDeviceProperties(const std::vector<uint8_t> &payload);

    /**
     * @brief Convert a UTF-16 wide string to UTF-8.
     * @param input Wide UTF-16 string.
     * @return UTF-8 encoded std::string.
     */
    std::string WideToUtf8(const std::wstring &input);

    /**
     * @brief Convert a UTF-8 string to UTF-16.
     * @param input UTF-8 encoded std::string.
     * @return UTF-16 encoded std::wstring.
     */
    std::wstring Utf8ToWide(const std::string &input);

    /**
     * @brief Clamp a value between min and max.
     * @tparam T Numeric type.
     * @param value Value to clamp.
     * @param min_value Minimum allowed value.
     * @param max_value Maximum allowed value.
     * @return Clamped value.
     */
    template <typename T>
    T Clamp(T value, T min_value, T max_value)
    {
        return (value < min_value) ? min_value : ((value > max_value) ? max_value : value);
    }

} // namespace sonyptp3
