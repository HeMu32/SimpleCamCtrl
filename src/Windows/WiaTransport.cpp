#include "WiaTransport.h"

// Helper to release COM objects
template <class T>
void SafeRelease(T **ppT)
{
    if (*ppT)
    {
        (*ppT)->Release();
        *ppT = NULL;
    }
}

WiaTransport::WiaTransport(IWiaItemExtras *pItemExtra) : pItemExtra_(pItemExtra)
{
    if (pItemExtra_)
        pItemExtra_->AddRef();
}

WiaTransport::~WiaTransport()
{
    if (pItemExtra_)
        pItemExtra_->Release();
    pItemExtra_ = nullptr;
}

PTP_EscapeResult WiaTransport::Escape(std::uint16_t opcode, const std::vector<std::uint32_t> &params,
                                      const std::uint8_t *writeData, size_t writeSize)
{
    PTP_EscapeResult out;
    out.hr = E_FAIL;
    out.responseCode = 0;

    if (!pItemExtra_)
    {
        out.hr = E_POINTER;
        return out;
    }

    bool bCOMInitHere = false;
    {
        const HRESULT hrCom = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        if (hrCom == S_OK)
        {
            bCOMInitHere = true;
        }
        else if (hrCom == S_FALSE || hrCom == RPC_E_CHANGED_MODE)
        {
        }
        else
        {
            out.hr = static_cast<std::int32_t>(hrCom);
            return out;
        }
    }

#pragma pack(push, 1)
    struct Local_PTP_VENDOR_DATA_IN
    {
        WORD OpCode;
        DWORD SessionId;
        DWORD TransactionId;
        DWORD Params[PTP_MAX_PARAMS];
        DWORD NumParams;
        DWORD NextPhase;
        BYTE VendorWriteData[1];
    };
    struct Local_PTP_VENDOR_DATA_OUT
    {
        WORD ResponseCode;
        DWORD SessionId;
        DWORD TransactionId;
        DWORD Params[PTP_MAX_PARAMS];
        BYTE VendorReadData[1];
    };
#pragma pack(pop)

    const DWORD SIZEOF_REQUIRED_VENDOR_DATA_IN = sizeof(Local_PTP_VENDOR_DATA_IN) - 1;
    const DWORD SIZEOF_REQUIRED_VENDOR_DATA_OUT = sizeof(Local_PTP_VENDOR_DATA_OUT) - 1;

    DWORD dwInSize = SIZEOF_REQUIRED_VENDOR_DATA_IN + writeSize;
    Local_PTP_VENDOR_DATA_IN *pIn = (Local_PTP_VENDOR_DATA_IN *)CoTaskMemAlloc(dwInSize);
    if (!pIn)
    {
        out.hr = E_OUTOFMEMORY;
        if (bCOMInitHere)
            CoUninitialize();
        return out;
    }
    ZeroMemory(pIn, dwInSize);

    pIn->OpCode = opcode;
    pIn->NextPhase = (writeSize > 0) ? PTP_NEXTPHASE_WRITE_DATA : PTP_NEXTPHASE_READ_DATA;
    for (size_t i = 0; i < params.size() && i < PTP_MAX_PARAMS; ++i)
    {
        pIn->Params[i] = params[i];
    }
    pIn->NumParams = static_cast<DWORD>(params.size());
    if (writeSize && writeData)
    {
        memcpy(pIn->VendorWriteData, writeData, writeSize);
    }

    DWORD dwOutSize = SIZEOF_REQUIRED_VENDOR_DATA_OUT + 0x8000;
    Local_PTP_VENDOR_DATA_OUT *pOut = (Local_PTP_VENDOR_DATA_OUT *)CoTaskMemAlloc(dwOutSize);
    if (!pOut)
    {
        CoTaskMemFree(pIn);
        out.hr = E_OUTOFMEMORY;
        if (bCOMInitHere)
            CoUninitialize();
        return out;
    }
    ZeroMemory(pOut, dwOutSize);

    DWORD dwActualLocal = 0;

    HRESULT hrLocal = pItemExtra_->Escape(ESCAPE_PTP_VENDOR_COMMAND, (BYTE *)pIn, dwInSize, (BYTE *)pOut, dwOutSize, &dwActualLocal);
    out.hr = static_cast<std::int32_t>(hrLocal);

    if (PTP_HR_SUCCEEDED(out.hr))
    {
        out.responseCode = pOut->ResponseCode;

        DWORD dataOffset = SIZEOF_REQUIRED_VENDOR_DATA_OUT;
        if (dwActualLocal > dataOffset)
        {
            DWORD payloadSize = dwActualLocal - dataOffset;
            out.payload.resize(payloadSize);
            memcpy(out.payload.data(), pOut->VendorReadData, payloadSize);
        }
    }

    CoTaskMemFree(pIn);
    CoTaskMemFree(pOut);

    if (bCOMInitHere)
        CoUninitialize();

    return out;
}