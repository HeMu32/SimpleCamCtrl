#pragma once

#include <CamCtrl/PTPTransport.h>
#include <wia.h>

#include <condition_variable>
#include <deque>
#include <future>
#include <mutex>
#include <thread>

#include <windows.h>
// Missing standard WIA interface definition for IWiaItemExtras
// Sony cameras expose this interface for vendor passthrough, but it is not 
// in the standard MinGW/MSVC SDK <wia.h> headers by default.
// We define the IID and interface manually if not present.

#ifndef __IWiaItemExtras_INTERFACE_DEFINED__
#define __IWiaItemExtras_INTERFACE_DEFINED__

// Interface IID: {6291EF2C-36EF-4532-876A-8E132593778D}
static const IID IID_IWiaItemExtras = 
{ 0x6291ef2c, 0x36ef, 0x4532, { 0x87, 0x6a, 0x8e, 0x13, 0x25, 0x93, 0x77, 0x8d } };

MIDL_INTERFACE("6291EF2C-36EF-4532-876A-8E132593778D")
IWiaItemExtras : public IUnknown
{
public:
    virtual HRESULT STDMETHODCALLTYPE GetExtendedUserInfo( 
        /* [in] */ BSTR bstrUserInfo,
        /* [out] */ BSTR *pbstrUserInfo,
        /* [out] */ LONG *plRet) = 0;
    
    virtual HRESULT STDMETHODCALLTYPE Escape( 
        /* [in] */ DWORD dwEscapeCode,
        /* [in] */ BYTE *lpInData,
        /* [in] */ DWORD cbInDataSize,
        /* [out] */ BYTE *pOutData,
        /* [in] */ DWORD dwOutDataSize,
        /* [out] */ DWORD *pdwActualDataSize) = 0;
    
    virtual HRESULT STDMETHODCALLTYPE Cancel( void) = 0;
    
};
#endif // __IWiaItemExtras_INTERFACE_DEFINED__

/**
 * @file WiaTransport.h
 * @brief WIA-based transport implementation that wraps an IWiaItemExtras.
 */

/**
 * @brief WIA transport backed by a dedicated owner STA thread.
 *
 * All COM object acquisition, Escape() execution, and COM object teardown happen
 * on the same owner thread to avoid cross-apartment misuse of IWiaItemExtras.
 */
class WiaTransport : public IPTPTransport
{
public:
    /**
     * @brief Construct a transport that owns a dedicated WIA/COM STA thread.
     * @param sWiaDeviceId WIA device id used to reopen the device on the owner thread.
     */
    explicit WiaTransport(std::string sWiaDeviceId);
    ~WiaTransport() override;

    /**
     * @brief Perform a vendor escape via the underlying IWiaItemExtras.
     */
    PTP_EscapeResult Escape(std::uint16_t opcode, const std::vector<std::uint32_t> &params,
                            const std::uint8_t *writeData, size_t writeSize) override;

private:
    struct TEscapeTask
    {
        std::uint16_t opcode = 0;
        std::vector<std::uint32_t> params;
        std::vector<std::uint8_t> writeData;
        std::promise<PTP_EscapeResult> promise;
    };

    void EnsureWorkerStarted();
    void WorkerMain();
    PTP_EscapeResult ExecuteEscapeOnOwnerThread(
        std::uint16_t opcode,
        const std::vector<std::uint32_t>& params,
        const std::uint8_t* writeData,
        size_t writeSize);

    static PTP_EscapeResult BuildTooManyParamsError();

private:
    std::string m_sWiaDeviceId;
    std::thread m_thOwner;
    std::mutex m_mtx;
    std::condition_variable m_cv;
    std::deque<std::shared_ptr<TEscapeTask>> m_qTasks;
    bool m_bStop = false;
    bool m_bStarted = false;
};
