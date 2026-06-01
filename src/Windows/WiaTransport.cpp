#include "WiaTransport.h"

#include <algorithm>
#include <chrono>
#include <iostream>
#include <string>
#include <thread>

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

void LogWiaTransportLifecycle(const char* pszStage, const WiaTransport* pSelf)
{
#if defined(_DEBUG)
    std::cerr << "[Lifecycle][WiaTransport] " << pszStage
              << " this=" << pSelf
              << " thread=" << std::this_thread::get_id()
              << std::endl;
#else
    (void)pszStage;
    (void)pSelf;
#endif
}

WiaTransport::WiaTransport(std::string sWiaDeviceId)
    : m_sWiaDeviceId(std::move(sWiaDeviceId))
{
    LogWiaTransportLifecycle("ctor", this);
}

WiaTransport::~WiaTransport()
{
    LogWiaTransportLifecycle("dtor begin", this);
    {
        std::lock_guard<std::mutex> lk(m_mtx);
        m_bStop = true;
    }
    m_cv.notify_all();
    if (m_thOwner.joinable())
    {
        LogWiaTransportLifecycle("dtor waiting worker join", this);
        m_thOwner.join();
    }
    LogWiaTransportLifecycle("dtor end", this);
}

PTP_EscapeResult WiaTransport::Escape(std::uint16_t opcode, const std::vector<std::uint32_t> &params,
                                      const std::uint8_t *writeData, size_t writeSize)
{
    if (params.size() > PTP_MAX_PARAMS)
    {
        return BuildTooManyParamsError();
    }

    EnsureWorkerStarted();
    return ExecuteEscapeOnOwnerThread(opcode, params, writeData, writeSize);
}

void WiaTransport::EnsureWorkerStarted()
{
    std::lock_guard<std::mutex> lk(m_mtx);
    if (m_bStarted)
    {
        return;
    }

    m_bStarted = true;
    LogWiaTransportLifecycle("EnsureWorkerStarted spawn", this);
    m_thOwner = std::thread([this]()
    {
        WorkerMain();
    });
}

PTP_EscapeResult WiaTransport::ExecuteEscapeOnOwnerThread(
    std::uint16_t opcode,
    const std::vector<std::uint32_t>& params,
    const std::uint8_t* writeData,
    size_t writeSize)
{
    auto spTask = std::make_shared<TEscapeTask>();
    spTask->opcode = opcode;
    spTask->params = params;
    if (writeData != nullptr && writeSize > 0)
    {
        spTask->writeData.assign(writeData, writeData + writeSize);
    }

    auto future = spTask->promise.get_future();
    {
        std::lock_guard<std::mutex> lk(m_mtx);
        m_qTasks.push_back(spTask);
    }
    LogWiaTransportLifecycle("ExecuteEscape enqueue", this);
    m_cv.notify_all();

    if (future.wait_for(kEscapeWaitTimeout) == std::future_status::ready)
    {
        LogWiaTransportLifecycle("ExecuteEscape ready", this);
        return future.get();
    }

    LogWiaTransportLifecycle("ExecuteEscape timeout", this);
    PTP_EscapeResult stTimeout;
    stTimeout.hr = HRESULT_FROM_WIN32(WAIT_TIMEOUT);
    stTimeout.responseCode = 0;
    return stTimeout;
}

PTP_EscapeResult WiaTransport::BuildTooManyParamsError()
{
    PTP_EscapeResult out;
    out.hr = E_INVALIDARG;
    out.responseCode = 0;
    return out;
}

void WiaTransport::WorkerMain()
{
    LogWiaTransportLifecycle("WorkerMain begin", this);
    bool bCOMInitHere = false;
    const HRESULT hrCom = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (hrCom == S_OK)
    {
        bCOMInitHere = true;
    }
    else if (hrCom != S_FALSE)
    {
        // keep running, but all requests will fail below
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

    auto fnAcquireCachedExtras = [this]() -> IWiaItemExtras* {
        if (m_pCachedItemExtra)
        {
            return m_pCachedItemExtra;
        }

        IWiaDevMgr* pWiaMgr = nullptr;
        IWiaItem* pWiaItem = nullptr;
        IWiaItemExtras* pExtra = nullptr;

        BSTR bstrId = SysAllocStringLen(nullptr, static_cast<UINT>(m_sWiaDeviceId.size()));
        if (!bstrId)
        {
            return nullptr;
        }
        MultiByteToWideChar(CP_UTF8, 0, m_sWiaDeviceId.c_str(), -1, bstrId,
                            static_cast<int>(m_sWiaDeviceId.size() + 1));

        if (SUCCEEDED(CoCreateInstance(CLSID_WiaDevMgr, nullptr, CLSCTX_LOCAL_SERVER,
                                       IID_IWiaDevMgr, reinterpret_cast<void**>(&pWiaMgr))) && pWiaMgr)
        {
            if (SUCCEEDED(pWiaMgr->CreateDevice(bstrId, &pWiaItem)) && pWiaItem)
            {
                pWiaItem->QueryInterface(IID_IWiaItemExtras, reinterpret_cast<void**>(&pExtra));
            }
        }

        SafeRelease(&pWiaItem);
        SafeRelease(&pWiaMgr);
        SysFreeString(bstrId);

        if (pExtra)
        {
            m_pCachedItemExtra = pExtra;
            LogWiaTransportLifecycle("WorkerMain cached IWiaItemExtras acquired", this);
        }
        return pExtra;
    };

    auto fnInvalidateCache = [this]() {
        if (m_pCachedItemExtra)
        {
            m_pCachedItemExtra->Release();
            m_pCachedItemExtra = nullptr;
            LogWiaTransportLifecycle("WorkerMain invalidated cached IWiaItemExtras", this);
        }
    };

    for (;;)
    {
        std::shared_ptr<TEscapeTask> spTask;
        {
            std::unique_lock<std::mutex> lk(m_mtx);
            m_cv.wait(lk, [this]()
            {
                return m_bStop || !m_qTasks.empty();
            });
            if (m_bStop && m_qTasks.empty())
            {
                break;
            }
            spTask = m_qTasks.front();
            m_qTasks.pop_front();
        }

        PTP_EscapeResult out;
        out.hr = E_FAIL;
        out.responseCode = 0;

        LogWiaTransportLifecycle("WorkerMain task begin", this);

        bool bCOMInitReq = false;
        if (hrCom == S_OK || hrCom == S_FALSE)
        {
            bCOMInitReq = true;
        }
        if (!bCOMInitReq)
        {
            out.hr = static_cast<std::int32_t>(hrCom);
            spTask->promise.set_value(out);
            continue;
        }

        const DWORD dwInSize = SIZEOF_REQUIRED_VENDOR_DATA_IN + static_cast<DWORD>(spTask->writeData.size());
        Local_PTP_VENDOR_DATA_IN* pIn = (Local_PTP_VENDOR_DATA_IN*)CoTaskMemAlloc(dwInSize);
        if (!pIn)
        {
            out.hr = E_OUTOFMEMORY;
            spTask->promise.set_value(out);
            continue;
        }
        ZeroMemory(pIn, dwInSize);

        pIn->OpCode = spTask->opcode;
        pIn->NextPhase = spTask->writeData.empty() ? PTP_NEXTPHASE_READ_DATA : PTP_NEXTPHASE_WRITE_DATA;
        const DWORD dwParamCount = static_cast<DWORD>(std::min<std::size_t>(spTask->params.size(), PTP_MAX_PARAMS));
        for (DWORD i = 0; i < dwParamCount; ++i)
        {
            pIn->Params[i] = spTask->params[i];
        }
        pIn->NumParams = dwParamCount;
        if (!spTask->writeData.empty())
        {
            memcpy(pIn->VendorWriteData, spTask->writeData.data(), spTask->writeData.size());
        }

        const DWORD dwOutSize = SIZEOF_REQUIRED_VENDOR_DATA_OUT + 0x8000;
        Local_PTP_VENDOR_DATA_OUT* pOut = (Local_PTP_VENDOR_DATA_OUT*)CoTaskMemAlloc(dwOutSize);
        if (!pOut)
        {
            CoTaskMemFree(pIn);
            out.hr = E_OUTOFMEMORY;
            spTask->promise.set_value(out);
            continue;
        }
        ZeroMemory(pOut, dwOutSize);

        DWORD dwActualLocal = 0;
        HRESULT hrLocal = E_FAIL;

        IWiaItemExtras* pItemExtra = fnAcquireCachedExtras();
        if (pItemExtra)
        {
            LogWiaTransportLifecycle("WorkerMain Escape call begin", this);
            hrLocal = pItemExtra->Escape(
                ESCAPE_PTP_VENDOR_COMMAND,
                reinterpret_cast<BYTE*>(pIn),
                dwInSize,
                reinterpret_cast<BYTE*>(pOut),
                dwOutSize,
                &dwActualLocal);
            LogWiaTransportLifecycle("WorkerMain Escape call end", this);

            if (FAILED(hrLocal))
            {
                fnInvalidateCache();
            }
        }
        out.hr = static_cast<std::int32_t>(hrLocal);

        if (PTP_HR_SUCCEEDED(out.hr))
        {
            out.responseCode = pOut->ResponseCode;
            const DWORD dataOffset = SIZEOF_REQUIRED_VENDOR_DATA_OUT;
            if (dwActualLocal > dataOffset)
            {
                const DWORD payloadSize = dwActualLocal - dataOffset;
                out.payload.resize(payloadSize);
                memcpy(out.payload.data(), pOut->VendorReadData, payloadSize);
            }
        }

        CoTaskMemFree(pIn);
        CoTaskMemFree(pOut);
        spTask->promise.set_value(out);
        LogWiaTransportLifecycle("WorkerMain task end", this);
    }

    if (m_pCachedItemExtra)
    {
        m_pCachedItemExtra->Release();
        m_pCachedItemExtra = nullptr;
    }

    if (bCOMInitHere)
    {
        CoUninitialize();
    }
    LogWiaTransportLifecycle("WorkerMain end", this);
}
