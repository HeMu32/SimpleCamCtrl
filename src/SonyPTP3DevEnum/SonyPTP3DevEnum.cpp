/**
 * @file  SonyPTP3DevEnum.cpp
 * @brief Windows WIA-based device enumerator for Sony PTP3 cameras.
 */

#ifndef NOMINMAX
#   define NOMINMAX
#endif

#include <windows.h>
#include <wia.h>

#include <algorithm>
#include <cctype>
#include <memory>
#include <string>
#include <vector>

#include "SonyPTP3DevEnum.h"
#include "SonyPTP3_Impl.h"
#include "WiaTransport.h"

// ─────────────────────────────────────────────────────────────────────────────
// Helpers (translation-unit internal)
// ─────────────────────────────────────────────────────────────────────────────

namespace
{

/**
 * @brief Release a COM pointer and null the original variable.
 * @tparam T COM interface type.
 * @param pObj Reference to the COM pointer.
 */
template <typename T>
void SafeRelease(T*& pObj)
{
    if (pObj)
    {
        pObj->Release();
        pObj = nullptr;
    }
}

/**
 * @brief Convert a BSTR to a UTF-8 std::string.
 * @param bstr Source BSTR (may be null).
 * @return Converted UTF-8 string, or empty string on failure/null input.
 */
std::string BstrToUtf8(BSTR bstr)
{
    if (!bstr)
    {
        return std::string();
    }

    const int nChars = WideCharToMultiByte(
        CP_UTF8, 0, bstr, -1, nullptr, 0, nullptr, nullptr);
    if (nChars <= 0)
    {
        return std::string();
    }

    std::string sResult(static_cast<std::size_t>(nChars - 1), '\0');
    WideCharToMultiByte(
        CP_UTF8, 0, bstr, -1, &sResult[0], nChars, nullptr, nullptr);
    return sResult;
}

/**
 * @brief Convert a UTF-8 std::string to a new BSTR (caller must SysFreeString).
 * @param sUtf8 Source UTF-8 string.
 * @return Newly allocated BSTR, or nullptr on failure/empty input.
 */
BSTR Utf8ToBstr(const std::string& sUtf8)
{
    if (sUtf8.empty())
    {
        return nullptr;
    }

    const int nWideChars = MultiByteToWideChar(
        CP_UTF8, 0, sUtf8.c_str(), -1, nullptr, 0);
    if (nWideChars <= 0)
    {
        return nullptr;
    }

    std::vector<wchar_t> vecWide(static_cast<std::size_t>(nWideChars), L'\0');
    MultiByteToWideChar(
        CP_UTF8, 0, sUtf8.c_str(), -1, vecWide.data(), nWideChars);

    return SysAllocString(vecWide.data());
}

/**
 * @brief Known Sony camera model name prefixes used for WIA device filtering.
 *
 * Matching is performed against the upper-cased WIA_DIP_DEV_DESC and
 * WIA_DIP_DEV_NAME fields only.  WIA_DIP_DEV_ID is a Windows GUID and is
 * intentionally excluded to avoid false positives.
 */
static const char* const k_SonyTokens[] = {
    "ILCE", "ILCA", "ILME",
    "DSC",  "ZV",
    "PXW",  "PMW",  "HXR",
    "BRC",  "ILX",  "MPC"
};

/**
 * @brief Test whether a string contains any Sony model token.
 *
 * The input string is converted to upper-case before comparison.
 * Matching is substring-based (the token may appear anywhere in the string).
 *
 * @param sField UTF-8 field value to test (@c sDesc or @c sName; never @c sId).
 * @param outToken If non-null and a match is found, receives the matched token.
 * @return @c true if at least one Sony token was found.
 */
bool ContainsSonyToken(const std::string& sField, std::string* outToken = nullptr)
{
    std::string sUpper = sField;
    std::transform(sUpper.begin(), sUpper.end(), sUpper.begin(),
                   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });

    for (const char* pToken : k_SonyTokens)
    {
        if (sUpper.find(pToken) != std::string::npos)
        {
            if (outToken)
            {
                *outToken = pToken;
            }
            return true;
        }
    }
    return false;
}

/**
 * @brief Read WIA_DIP_DEV_ID, WIA_DIP_DEV_DESC and WIA_DIP_DEV_NAME from
 *        an @c IWiaPropertyStorage instance.
 *
 * @param pProp  Source property storage (non-null).
 * @param outId   Receives DIP_DEV_ID value.
 * @param outDesc Receives DIP_DEV_DESC value.
 * @param outName Receives DIP_DEV_NAME value.
 * @return @c true if ReadMultiple() succeeded.
 */
bool ReadWiaDeviceStrings(IWiaPropertyStorage* pProp,
                          std::string& outId,
                          std::string& outDesc,
                          std::string& outName)
{
    outId.clear();
    outDesc.clear();
    outName.clear();

    PROPSPEC  arrSpec[3];
    PROPVARIANT arrVal[3];
    for (int i = 0; i < 3; ++i)
    {
        PropVariantInit(&arrVal[i]);
    }

    arrSpec[0].ulKind = PRSPEC_PROPID; arrSpec[0].propid = WIA_DIP_DEV_ID;
    arrSpec[1].ulKind = PRSPEC_PROPID; arrSpec[1].propid = WIA_DIP_DEV_DESC;
    arrSpec[2].ulKind = PRSPEC_PROPID; arrSpec[2].propid = WIA_DIP_DEV_NAME;

    const HRESULT hr = pProp->ReadMultiple(3, arrSpec, arrVal);

    if (SUCCEEDED(hr))
    {
        if (arrVal[0].vt == VT_BSTR && arrVal[0].bstrVal)
        {
            outId   = BstrToUtf8(arrVal[0].bstrVal);
        }
        if (arrVal[1].vt == VT_BSTR && arrVal[1].bstrVal)
        {
            outDesc = BstrToUtf8(arrVal[1].bstrVal);
        }
        if (arrVal[2].vt == VT_BSTR && arrVal[2].bstrVal)
        {
            outName = BstrToUtf8(arrVal[2].bstrVal);
        }
    }

    for (int i = 0; i < 3; ++i)
    {
        PropVariantClear(&arrVal[i]);
    }

    return SUCCEEDED(hr);
}

} // namespace

// ─────────────────────────────────────────────────────────────────────────────
// Refresh
// ─────────────────────────────────────────────────────────────────────────────

bool SonyPTP3DevEnum::Refresh()
{
    m_vecDeviceInfo.clear();

    // ── COM initialisation ─────────────────────────────────────────────────
    // Prefer STA to match Qt's OleInitialize apartment and avoid
    // RPC_E_CHANGED_MODE.  S_FALSE means another call in this thread already
    // initialised COM; we still proceed.
    bool bCOMInitHere = false;
    {
        const HRESULT hrCom =
            CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        if (hrCom == S_OK)
        {
            bCOMInitHere = true;
        }
        else if (hrCom == S_FALSE)
        {
            // Already initialised by caller – do not uninitialise on exit.
        }
        else if (hrCom == RPC_E_CHANGED_MODE)
        {
            // Mismatch (e.g. MTA thread); log but continue – WIA may still work.
        }
        else
        {
            // Genuine failure.
            return false;
        }
    }

    // ── Acquire WIA device manager ─────────────────────────────────────────
    IWiaDevMgr* pWiaMgr = nullptr;
    {
        const HRESULT hr = CoCreateInstance(
            CLSID_WiaDevMgr, nullptr, CLSCTX_LOCAL_SERVER,
            IID_IWiaDevMgr, reinterpret_cast<void**>(&pWiaMgr));
        if (FAILED(hr) || !pWiaMgr)
        {
            SafeRelease(pWiaMgr);
            if (bCOMInitHere) { CoUninitialize(); }
            return false;
        }
    }

    // ── Enumerate all local WIA devices ───────────────────────────────────
    IEnumWIA_DEV_INFO* pEnum = nullptr;
    {
        const HRESULT hr =
            pWiaMgr->EnumDeviceInfo(WIA_DEVINFO_ENUM_LOCAL, &pEnum);
        if (FAILED(hr) || !pEnum)
        {
            SafeRelease(pEnum);
            SafeRelease(pWiaMgr);
            if (bCOMInitHere) { CoUninitialize(); }
            // Return true: backend accessible, just no devices.
            return true;
        }
    }

    // ── First pass: collect all WIA devices (no filter yet) ────────────────
    struct WiaRawEntry
    {
        std::string sId;
        std::string sDesc;
        std::string sName;
    };
    std::vector<WiaRawEntry> vecRaw;

    while (true)
    {
        IWiaPropertyStorage* pProp = nullptr;
        ULONG nFetched = 0;
        const HRESULT hrNext = pEnum->Next(1, &pProp, &nFetched);
        if (hrNext != S_OK || nFetched == 0 || !pProp)
        {
            SafeRelease(pProp);
            break;
        }

        WiaRawEntry stRaw;
        ReadWiaDeviceStrings(pProp, stRaw.sId, stRaw.sDesc, stRaw.sName);
        SafeRelease(pProp);

        if (!stRaw.sId.empty())
        {
            vecRaw.push_back(std::move(stRaw));
        }
    }

    SafeRelease(pEnum);
    SafeRelease(pWiaMgr);
    if (bCOMInitHere) { CoUninitialize(); }

    // ── Second pass: retain only Sony devices ─────────────────────────────
    // Token matching is applied to sDesc and sName only.
    // sId is a Windows GUID and is excluded to avoid false positives.
    std::int32_t nIdx = 0;
    for (const WiaRawEntry& stRaw : vecRaw)
    {
        std::string sMatchedToken;
        if (!ContainsSonyToken(stRaw.sDesc, &sMatchedToken) &&
            !ContainsSonyToken(stRaw.sName, &sMatchedToken))
        {
            continue; // Not a recognised Sony device.
        }

        TDevEnumDeviceInfo stInfo;
        stInfo.nDeviceIndex    = nIdx++;
        // Prefer the human-readable description; fall back to name.
        stInfo.sDeviceName     = stRaw.sDesc.empty() ? stRaw.sName : stRaw.sDesc;
        stInfo.sDeviceType     = "sony_ptp3";
        stInfo.sDeviceLocation = stRaw.sId; // WIA GUID serves as a unique location key.

        stInfo.vExtraFields.push_back({"wiaId",            stRaw.sId});
        stInfo.vExtraFields.push_back({"wiaDesc",          stRaw.sDesc});
        stInfo.vExtraFields.push_back({"wiaName",          stRaw.sName});
        stInfo.vExtraFields.push_back({"transport",        "wia"});
        stInfo.vExtraFields.push_back({"sonyMatchedToken", sMatchedToken});

        m_vecDeviceInfo.push_back(std::move(stInfo));
    }

    return true; // Backend reachable; callers test ListDevices().empty() for presence.
}

// ─────────────────────────────────────────────────────────────────────────────
// ListDevices
// ─────────────────────────────────────────────────────────────────────────────

std::vector<TDevEnumDeviceInfo> SonyPTP3DevEnum::ListDevices() const
{
    return m_vecDeviceInfo;
}

// ─────────────────────────────────────────────────────────────────────────────
// OpenByIndex
// ─────────────────────────────────────────────────────────────────────────────

TOpaqueDeviceHandle SonyPTP3DevEnum::OpenByIndex(
    std::int32_t               nDeviceIndex,
    const TSonyPTP3OpenParams& stOpenParams)
{
    if (nDeviceIndex < 0 ||
        nDeviceIndex >= static_cast<std::int32_t>(m_vecDeviceInfo.size()))
    {
        return nullptr;
    }

    // Retrieve WIA device ID from extra fields.
    const TDevEnumDeviceInfo& stEntry = m_vecDeviceInfo[static_cast<std::size_t>(nDeviceIndex)];
    std::string sWiaId;
    for (const auto& stPair : stEntry.vExtraFields)
    {
        if (stPair.first == "wiaId")
        {
            sWiaId = stPair.second;
            break;
        }
    }

    if (sWiaId.empty())
    {
        return nullptr;
    }

    // ── COM initialisation ─────────────────────────────────────────────────
    bool bCOMInitHere = false;
    {
        const HRESULT hrCom =
            CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        if (hrCom == S_OK)
        {
            bCOMInitHere = true;
        }
        else if (hrCom == S_FALSE || hrCom == RPC_E_CHANGED_MODE)
        {
            // Already / differently initialised by caller; continue.
        }
        else
        {
            return nullptr;
        }
    }

    // ── Open device via WIA ────────────────────────────────────────────────
    IWiaDevMgr* pWiaMgr = nullptr;
    {
        const HRESULT hr = CoCreateInstance(
            CLSID_WiaDevMgr, nullptr, CLSCTX_LOCAL_SERVER,
            IID_IWiaDevMgr, reinterpret_cast<void**>(&pWiaMgr));
        if (FAILED(hr) || !pWiaMgr)
        {
            SafeRelease(pWiaMgr);
            if (bCOMInitHere) { CoUninitialize(); }
            return nullptr;
        }
    }

    BSTR bstrId = Utf8ToBstr(sWiaId);
    if (!bstrId)
    {
        SafeRelease(pWiaMgr);
        if (bCOMInitHere) { CoUninitialize(); }
        return nullptr;
    }

    IWiaItem* pWiaItem = nullptr;
    {
        const HRESULT hr = pWiaMgr->CreateDevice(bstrId, &pWiaItem);
        SysFreeString(bstrId);
        if (FAILED(hr) || !pWiaItem)
        {
            SafeRelease(pWiaItem);
            SafeRelease(pWiaMgr);
            if (bCOMInitHere) { CoUninitialize(); }
            return nullptr;
        }
    }

    IWiaItemExtras* pExtras = nullptr;
    {
        const HRESULT hr = pWiaItem->QueryInterface(
            IID_IWiaItemExtras, reinterpret_cast<void**>(&pExtras));
        SafeRelease(pWiaItem);
        if (FAILED(hr) || !pExtras)
        {
            SafeRelease(pExtras);
            SafeRelease(pWiaMgr);
            if (bCOMInitHere) { CoUninitialize(); }
            return nullptr;
        }
    }

    // ── Wrap transport and create SonyPTP3_Impl ────────────────────────────
    auto spTransport = std::make_shared<WiaTransport>(pExtras);
    SafeRelease(pExtras);
    SafeRelease(pWiaMgr);
    if (bCOMInitHere) { CoUninitialize(); }

    auto spSony = std::make_shared<SonyPTP3_Impl>();
    if (!spSony->SetPtpTransport(spTransport))
    {
        return nullptr;
    }

    if (!spSony->Connect())
    {
        return nullptr;
    }

    if (stOpenParams.bAutoUpdateStatus)
    {
        spSony->UpdateStatus();
    }

    return std::static_pointer_cast<void>(spSony);
}
