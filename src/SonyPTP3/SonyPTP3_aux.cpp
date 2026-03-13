#include "SonyPTP3_aux.h"
#include <cstring>
#include <algorithm>
#include <limits>

namespace sonyptp3
{
    namespace
    {
        constexpr uint16_t PTP_DT_INT8 = 0x0001;
        constexpr uint16_t PTP_DT_UINT8 = 0x0002;
        constexpr uint16_t PTP_DT_INT16 = 0x0003;
        constexpr uint16_t PTP_DT_UINT16 = 0x0004;
        constexpr uint16_t PTP_DT_INT32 = 0x0005;
        constexpr uint16_t PTP_DT_UINT32 = 0x0006;
        constexpr uint16_t PTP_DT_INT64 = 0x0007;
        constexpr uint16_t PTP_DT_UINT64 = 0x0008;
        constexpr uint16_t PTP_DT_STR = 0xFFFF;

        constexpr uint16_t PTP_DT_AINT8 = 0x4001;
        constexpr uint16_t PTP_DT_AUINT8 = 0x4002;
        constexpr uint16_t PTP_DT_AINT16 = 0x4003;
        constexpr uint16_t PTP_DT_AUINT16 = 0x4004;
        constexpr uint16_t PTP_DT_AINT32 = 0x4005;
        constexpr uint16_t PTP_DT_AUINT32 = 0x4006;
        constexpr uint16_t PTP_DT_AINT64 = 0x4007;
        constexpr uint16_t PTP_DT_AUINT64 = 0x4008;

        static uint64_t read_u64_buf(const std::vector<std::uint8_t> &b, size_t off)
        {
            uint64_t v = 0;
            if (off <= b.size() && (b.size() - off) >= 8)
                std::memcpy(&v, &b[off], 8);
            return v;
        }
        static uint32_t read_u32_buf(const std::vector<std::uint8_t> &b, size_t off)
        {
            uint32_t v = 0;
            if (off <= b.size() && (b.size() - off) >= 4)
                std::memcpy(&v, &b[off], 4);
            return v;
        }
        static uint16_t read_u16_buf(const std::vector<std::uint8_t> &b, size_t off)
        {
            uint16_t v = 0;
            if (off <= b.size() && (b.size() - off) >= 2)
                std::memcpy(&v, &b[off], 2);
            return v;
        }
        static uint8_t read_u8_buf(const std::vector<std::uint8_t> &b, size_t off)
        {
            uint8_t v = 0;
            if (off <= b.size() && (b.size() - off) >= 1)
                std::memcpy(&v, &b[off], 1);
            return v;
        }
    }

    std::unordered_map<uint16_t, uint64_t> ParseDeviceProperties(const std::vector<uint8_t> &payload)
    {
        std::unordered_map<uint16_t, uint64_t> out;
        if (payload.size() < 8)
            return out;

        auto can_read = [&](size_t off, size_t nBytes) -> bool
        {
            return (off <= payload.size()) && (nBytes <= (payload.size() - off));
        };

        auto advance = [&](size_t& off, size_t nBytes) -> bool
        {
            if (!can_read(off, nBytes))
            {
                return false;
            }
            off += nBytes;
            return true;
        };

        auto advance_mul = [&](size_t& off, size_t nUnits, size_t nUnitBytes) -> bool
        {
            if (nUnitBytes != 0 && nUnits > (std::numeric_limits<size_t>::max() / nUnitBytes))
            {
                return false;
            }
            const size_t nBytes = nUnits * nUnitBytes;
            return advance(off, nBytes);
        };

        unsigned long long length = read_u64_buf(payload, 0);
        size_t offset = 8;

        for (unsigned long long i = 0; i < length && offset < payload.size(); ++i)
        {
            if (!can_read(offset, sizeof(uint16_t) * 2 + 2))
                break;
            uint16_t propertyCode = read_u16_buf(payload, offset);
            if (!advance(offset, sizeof(uint16_t))) break;
            uint16_t dataType = read_u16_buf(payload, offset);
            if (!advance(offset, sizeof(uint16_t))) break;
            if (!advance(offset, sizeof(uint8_t))) break;
            if (!advance(offset, sizeof(uint8_t))) break;

            unsigned long sizeofType = 0;
            unsigned long long defaultValue = 0;
            unsigned long long currentValue = 0;
            unsigned char numchar_defaultValue = 0;
            unsigned char numchar_currentValue = 0;

            switch (dataType)
            {
            case PTP_DT_INT8:
            case PTP_DT_UINT8:
                sizeofType = 1;
                if (can_read(offset, 1))
                    defaultValue = read_u8_buf(payload, offset);
                if (!advance(offset, 1)) break;
                if (can_read(offset, 1))
                    currentValue = read_u8_buf(payload, offset);
                if (!advance(offset, 1)) break;
                break;
            case PTP_DT_INT16:
            case PTP_DT_UINT16:
                sizeofType = 2;
                if (can_read(offset, 2))
                    defaultValue = read_u16_buf(payload, offset);
                if (!advance(offset, 2)) break;
                if (can_read(offset, 2))
                    currentValue = read_u16_buf(payload, offset);
                if (!advance(offset, 2)) break;
                break;
            case PTP_DT_INT32:
            case PTP_DT_UINT32:
                sizeofType = 4;
                if (can_read(offset, 4))
                    defaultValue = read_u32_buf(payload, offset);
                if (!advance(offset, 4)) break;
                if (can_read(offset, 4))
                    currentValue = read_u32_buf(payload, offset);
                if (!advance(offset, 4)) break;
                break;
            case PTP_DT_INT64:
            case PTP_DT_UINT64:
                sizeofType = 8;
                if (can_read(offset, 8))
                    defaultValue = read_u64_buf(payload, offset);
                if (!advance(offset, 8)) break;
                if (can_read(offset, 8))
                    currentValue = read_u64_buf(payload, offset);
                if (!advance(offset, 8)) break;
                break;
            case PTP_DT_STR:
                if (can_read(offset, 1))
                    numchar_defaultValue = read_u8_buf(payload, offset);
                if (!advance(offset, 1)) break;
                if (!advance_mul(offset, static_cast<size_t>(numchar_defaultValue), 2)) break;
                if (can_read(offset, 1))
                    numchar_currentValue = read_u8_buf(payload, offset);
                if (!advance(offset, 1)) break;
                if (!advance_mul(offset, static_cast<size_t>(numchar_currentValue), 2)) break;
                break;
            case PTP_DT_AINT8:
            case PTP_DT_AUINT8:
                sizeofType = 0;
                if (can_read(offset, 4))
                    defaultValue = read_u32_buf(payload, offset);
                if (!advance(offset, 4)) break;
                if (!advance_mul(offset, static_cast<size_t>(defaultValue), 1)) break;
                if (can_read(offset, 4))
                    currentValue = read_u32_buf(payload, offset);
                if (!advance(offset, 4)) break;
                if (!advance_mul(offset, static_cast<size_t>(currentValue), 1)) break;
                break;
            case PTP_DT_AINT16:
            case PTP_DT_AUINT16:
                sizeofType = 0;
                if (can_read(offset, 4))
                    defaultValue = read_u32_buf(payload, offset);
                if (!advance(offset, 4)) break;
                if (!advance_mul(offset, static_cast<size_t>(defaultValue), 2)) break;
                if (can_read(offset, 4))
                    currentValue = read_u32_buf(payload, offset);
                if (!advance(offset, 4)) break;
                if (!advance_mul(offset, static_cast<size_t>(currentValue), 2)) break;
                break;
            case PTP_DT_AINT32:
            case PTP_DT_AUINT32:
                sizeofType = 0;
                if (can_read(offset, 4))
                    defaultValue = read_u32_buf(payload, offset);
                if (!advance(offset, 4)) break;
                if (!advance_mul(offset, static_cast<size_t>(defaultValue), 4)) break;
                if (can_read(offset, 4))
                    currentValue = read_u32_buf(payload, offset);
                if (!advance(offset, 4)) break;
                if (!advance_mul(offset, static_cast<size_t>(currentValue), 4)) break;
                break;
            case PTP_DT_AINT64:
            case PTP_DT_AUINT64:
                sizeofType = 0;
                if (can_read(offset, 4))
                    defaultValue = read_u32_buf(payload, offset);
                if (!advance(offset, 4)) break;
                if (!advance_mul(offset, static_cast<size_t>(defaultValue), 8)) break;
                if (can_read(offset, 4))
                    currentValue = read_u32_buf(payload, offset);
                if (!advance(offset, 4)) break;
                if (!advance_mul(offset, static_cast<size_t>(currentValue), 8)) break;
                break;
            default:
                return out;
            }

            unsigned char formFlag = 0;
            if (can_read(offset, 1))
                formFlag = read_u8_buf(payload, offset);
            if (!advance(offset, 1)) break;

            if (formFlag == 0x01)
            {
                if (!advance_mul(offset, 3, static_cast<size_t>(sizeofType))) break;
            }
            else if (formFlag == 0)
            {
            }
            else
            {
                if (!can_read(offset, 2))
                    break;
                unsigned short num = read_u16_buf(payload, offset);
                if (!advance(offset, 2)) break;
                if (!advance_mul(offset, static_cast<size_t>(num), static_cast<size_t>(sizeofType))) break;

                if (!can_read(offset, 2))
                    break;
                num = read_u16_buf(payload, offset);
                if (!advance(offset, 2)) break;
                if (!advance_mul(offset, static_cast<size_t>(num), static_cast<size_t>(sizeofType))) break;
            }

            out[propertyCode] = currentValue;
        }

        return out;
    }

    std::string WideToUtf8(const std::wstring &input) { return ""; }
    std::wstring Utf8ToWide(const std::string &input) { return L""; }

} // namespace sonyptp3
