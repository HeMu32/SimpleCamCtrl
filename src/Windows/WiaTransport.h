#pragma once

#include <CamCtrl/PTPTransport.h>
#include <wia.h>

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
 * @brief Simple transport that wraps an @c IWiaItemExtras* and delegates
 * vendor escape operations to the demo helper functions.
 *
 * The transport holds a COM reference to the provided @c IWiaItemExtras (calls
 * AddRef in the constructor and Release in the destructor).
 */
class WiaTransport : public IPTPTransport
{
public:
    // Takes ownership of a reference to pItemExtra (calls AddRef internally).
    /**
     * @brief Construct a WiaTransport from an existing IWiaItemExtras pointer.
     * @param pItemExtra Pointer to an IWiaItemExtras. The constructor will call
     * AddRef on the pointer.
     */
    explicit WiaTransport(IWiaItemExtras *pItemExtra);
    ~WiaTransport() override;

    /**
     * @brief Perform a vendor escape via the underlying IWiaItemExtras.
     */
    PTP_EscapeResult Escape(std::uint16_t opcode, const std::vector<std::uint32_t> &params,
                            const std::uint8_t *writeData, size_t writeSize) override;

private:
    IWiaItemExtras *pItemExtra_ = nullptr;
};
