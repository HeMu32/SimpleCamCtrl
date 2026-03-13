// Heavy header (Windows-only; pulls in WiaTransport and SonyPTP3_Impl via .cpp).
// Sony PTP3 camera device enumerator implementing IDevEnum.
// Requires Windows WIA. All WIA/COM headers are kept in the .cpp to avoid
// polluting callers.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <DevEnum/IDevEnum.h>

// Forward declarations – full types resolved in SonyPTP3DevEnum.cpp.
class SonyPTP3_Impl;

/**
 * @brief Open parameters for SonyPTP3DevEnum::OpenByIndex().
 *
 * Currently only controls whether the connection sequence ends with an
 * automatic SonyPTP3_Impl::UpdateStatus() call.  Extend as needed.
 */
struct TSonyPTP3OpenParams
{
    /// @brief Call UpdateStatus() immediately after a successful Connect().
    bool bAutoUpdateStatus = true;
};

/**
 * @brief Windows WIA-based device enumerator for Sony PTP3 cameras.
 *
 * ### Usage
 * ```cpp
 * SonyPTP3DevEnum stEnum;
 * stEnum.Refresh();
 * for (auto& stInfo : stEnum.ListDevices()) { ... }
 * auto hDev = stEnum.OpenByIndex(0, {});
 * auto spSony = std::static_pointer_cast<SonyPTP3_Impl>(hDev);
 * ```
 *
 * ### Refresh() – two-pass filtering
 * 1. First pass  : enumerate all local WIA devices.
 * 2. Second pass : retain only devices whose @c WIA_DIP_DEV_DESC or
 *    @c WIA_DIP_DEV_NAME contains a known Sony model token (case-insensitive):
 *    ILCE, ILCA, ILME, DSC, ZV, PXW, PMW, HXR, BRC, ILX, MPC.
 *    Note: @c WIA_DIP_DEV_ID is a Windows GUID and is intentionally
 *    excluded from token matching to avoid false positives.
 *
 * ### OpenByIndex()
 * Opens the WIA device via @c IWiaDevMgr::CreateDevice(), wraps the
 * resulting @c IWiaItemExtras in a @c WiaTransport, constructs and connects
 * a @c SonyPTP3_Impl.  The returned opaque handle wraps a
 * @c std::shared_ptr<SonyPTP3_Impl>; cast with @c std::static_pointer_cast.
 *
 * @note Windows-only.  All WIA/COM headers are internal to the .cpp.
 */
class SonyPTP3DevEnum final
    : public IDevEnum<TSonyPTP3OpenParams>
{
public:
    SonyPTP3DevEnum()  = default;
    ~SonyPTP3DevEnum() override = default;

    /**
     * @brief Enumerate local WIA devices and apply Sony model-prefix filter.
     *
     * @return @c true  when the WIA backend is reachable (even if no Sony
     *                  cameras are found; test @c ListDevices().empty()).
     * @return @c false only when COM or WIA manager initialisation fails.
     */
    bool Refresh() override;

    /**
     * @brief Return the current Sony camera snapshot.
     *
     * @c sDeviceType is always @c "sony_ptp3".
     *
     * @c vExtraFields members:
     *   - @c "wiaId"            : @c WIA_DIP_DEV_ID (used by OpenByIndex())
     *   - @c "wiaDesc"          : @c WIA_DIP_DEV_DESC value
     *   - @c "wiaName"          : @c WIA_DIP_DEV_NAME value
     *   - @c "transport"        : always @c "wia"
     *   - @c "sonyMatchedToken" : the token substring that caused inclusion
     */
    std::vector<TDevEnumDeviceInfo> ListDevices() const override;

    /**
     * @brief Open a Sony camera by snapshot index.
     *
     * @param nDeviceIndex  Snapshot index (0-based, from ListDevices()).
     * @param stOpenParams  Open options (e.g. @c bAutoUpdateStatus).
     * @return Non-null @c TOpaqueDeviceHandle wrapping a connected
     *         @c std::shared_ptr<SonyPTP3_Impl>, or @c nullptr on failure.
     */
    TOpaqueDeviceHandle OpenByIndex(
        std::int32_t              nDeviceIndex,
        const TSonyPTP3OpenParams& stOpenParams) override;

private:
    std::vector<TDevEnumDeviceInfo> m_vecDeviceInfo; ///< Current device snapshot.
};
