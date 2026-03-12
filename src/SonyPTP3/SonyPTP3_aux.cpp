#include "SonyPTP3_aux.h"
#include <cstring>
#include <algorithm>

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
            if (off + 8 <= b.size())
                std::memcpy(&v, &b[off], 8);
            return v;
        }
        static uint32_t read_u32_buf(const std::vector<std::uint8_t> &b, size_t off)
        {
            uint32_t v = 0;
            if (off + 4 <= b.size())
                std::memcpy(&v, &b[off], 4);
            return v;
        }
        static uint16_t read_u16_buf(const std::vector<std::uint8_t> &b, size_t off)
        {
            uint16_t v = 0;
            if (off + 2 <= b.size())
                std::memcpy(&v, &b[off], 2);
            return v;
        }
        static uint8_t read_u8_buf(const std::vector<std::uint8_t> &b, size_t off)
        {
            uint8_t v = 0;
            if (off + 1 <= b.size())
                std::memcpy(&v, &b[off], 1);
            return v;
        }
    }

    std::unordered_map<uint16_t, uint64_t> ParseDeviceProperties(const std::vector<uint8_t> &payload)
    {
        std::unordered_map<uint16_t, uint64_t> out;
        if (payload.size() < 8)
            return out;

        unsigned long long length = read_u64_buf(payload, 0);
        size_t offset = 8;

        for (unsigned long long i = 0; i < length && offset < payload.size(); ++i)
        {
            if (offset + sizeof(uint16_t) * 2 + 2 > payload.size())
                break;
            uint16_t propertyCode = read_u16_buf(payload, offset);
            offset += sizeof(uint16_t);
            uint16_t dataType = read_u16_buf(payload, offset);
            offset += sizeof(uint16_t);
            offset += sizeof(uint8_t);
            offset += sizeof(uint8_t);

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
                if (offset + 1 <= payload.size())
                    defaultValue = read_u8_buf(payload, offset);
                offset += 1;
                if (offset + 1 <= payload.size())
                    currentValue = read_u8_buf(payload, offset);
                offset += 1;
                break;
            case PTP_DT_INT16:
            case PTP_DT_UINT16:
                sizeofType = 2;
                if (offset + 2 <= payload.size())
                    defaultValue = read_u16_buf(payload, offset);
                offset += 2;
                if (offset + 2 <= payload.size())
                    currentValue = read_u16_buf(payload, offset);
                offset += 2;
                break;
            case PTP_DT_INT32:
            case PTP_DT_UINT32:
                sizeofType = 4;
                if (offset + 4 <= payload.size())
                    defaultValue = read_u32_buf(payload, offset);
                offset += 4;
                if (offset + 4 <= payload.size())
                    currentValue = read_u32_buf(payload, offset);
                offset += 4;
                break;
            case PTP_DT_INT64:
            case PTP_DT_UINT64:
                sizeofType = 8;
                if (offset + 8 <= payload.size())
                    defaultValue = read_u64_buf(payload, offset);
                offset += 8;
                if (offset + 8 <= payload.size())
                    currentValue = read_u64_buf(payload, offset);
                offset += 8;
                break;
            case PTP_DT_STR:
                if (offset + 1 <= payload.size())
                    numchar_defaultValue = read_u8_buf(payload, offset);
                offset += 1;
                offset += static_cast<size_t>(numchar_defaultValue) * 2;
                if (offset + 1 <= payload.size())
                    numchar_currentValue = read_u8_buf(payload, offset);
                offset += 1;
                offset += static_cast<size_t>(numchar_currentValue) * 2;
                break;
            case PTP_DT_AINT8:
            case PTP_DT_AUINT8:
                sizeofType = 0;
                if (offset + 4 <= payload.size())
                    defaultValue = read_u32_buf(payload, offset);
                offset += 4;
                offset += static_cast<unsigned int>(1 * defaultValue);
                if (offset + 4 <= payload.size())
                    currentValue = read_u32_buf(payload, offset);
                offset += 4;
                offset += static_cast<unsigned int>(1 * currentValue);
                break;
            case PTP_DT_AINT16:
            case PTP_DT_AUINT16:
                sizeofType = 0;
                if (offset + 4 <= payload.size())
                    defaultValue = read_u32_buf(payload, offset);
                offset += 4;
                offset += static_cast<unsigned int>(2 * defaultValue);
                if (offset + 4 <= payload.size())
                    currentValue = read_u32_buf(payload, offset);
                offset += 4;
                offset += static_cast<unsigned int>(2 * currentValue);
                break;
            case PTP_DT_AINT32:
            case PTP_DT_AUINT32:
                sizeofType = 0;
                if (offset + 4 <= payload.size())
                    defaultValue = read_u32_buf(payload, offset);
                offset += 4;
                offset += static_cast<unsigned int>(4 * defaultValue);
                if (offset + 4 <= payload.size())
                    currentValue = read_u32_buf(payload, offset);
                offset += 4;
                offset += static_cast<unsigned int>(4 * currentValue);
                break;
            case PTP_DT_AINT64:
            case PTP_DT_AUINT64:
                sizeofType = 0;
                if (offset + 4 <= payload.size())
                    defaultValue = read_u32_buf(payload, offset);
                offset += 4;
                offset += static_cast<unsigned int>(8 * defaultValue);
                if (offset + 4 <= payload.size())
                    currentValue = read_u32_buf(payload, offset);
                offset += 4;
                offset += static_cast<unsigned int>(8 * currentValue);
                break;
            default:
                return out;
            }

            unsigned char formFlag = 0;
            if (offset + 1 <= payload.size())
                formFlag = read_u8_buf(payload, offset);
            offset += 1;

            if (formFlag == 0x01)
            {
                offset += (sizeofType * 3);
            }
            else if (formFlag == 0)
            {
            }
            else
            {
                if (offset + 2 > payload.size())
                    break;
                unsigned short num = read_u16_buf(payload, offset);
                offset += 2;
                offset += (num * sizeofType);

                if (offset + 2 > payload.size())
                    break;
                num = read_u16_buf(payload, offset);
                offset += 2;
                offset += (num * sizeofType);
            }

            out[propertyCode] = currentValue;
        }

        return out;
    }

    std::string WideToUtf8(const std::wstring &input) { return ""; }
    std::wstring Utf8ToWide(const std::string &input) { return L""; }

} // namespace sonyptp3
