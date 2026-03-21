#pragma once

/// @brief Total timeout for connection attempt
#define _CONN_TIMEOUT_MS 500
/// @brief Timeout for retry (disconnect-connect process)
#define _CONN_RETRY_TIMEOUT_MS 120

#include <cstdint>
#include <mutex>
#include <string>

#include <CamCtrl/ISimpleCamCtrl.h>
#include <CamCtrl/PTPTransport.h>

// Forward declarations to avoid pulling GUI/MFC headers into MinGW builds.
class PTPControl;
class DataManager;

/**
 * @brief Implementation of ISimpleCamCtrl that uses a PTP transport to send
 * vendor escape commands and maintain a local status cache.
 *
 * This implementation is transport-agnostic and accepts an @c IPTPTransport
 * instance (for example, a @c WiaTransport) to perform low-level PTP
 * operations.
 */
class SonyPTP3_Impl : public ISimpleCamCtrl
{
public:
    SonyPTP3_Impl();
    ~SonyPTP3_Impl() override;

    /** @name Lifecycle (blocking) */
    /** @{ */
    /**
     * @brief Establish connection / prepare transport and session.
     * @return true on success, false on failure.
     */
    bool Connect();

    /**
     * @brief Connect using an externally-provided transport implementation.
     *
     * Ownership of the @c IPTPTransportPtr is shared; the transport instance
	 * may outlive an active session and be reused across Disconnect()/Connect()
	 * cycles. Passing @c nullptr explicitly removes the current transport.
     * @param transport Shared pointer to an @c IPTPTransport implementation.
     * @return true on success.
     */
    bool SetPtpTransport(IPTPTransportPtr transport) override;
    void Disconnect();
    bool IsConnected() const;

    // ISimpleCamCtrl
    bool UpdateStatus() override;
    bool GetExposureParams(ExposureParams &out_params) const override;
    std::uint32_t GetExposureMode() const override;
    bool FocusStart() override;
    bool FocusEnd() override;
    bool ShutterStart() override;
    bool ShutterEnd() override;
    bool MovieRecStart() override;
    bool MovieRecEnd() override;
    // Movie recording state accessor
    bool IsMovieRecording() const override;
    /**
     * @brief Change exposure parameters using physical values.
     * @param params Desired exposure parameters; implementations may apply a subset
        * of fields depending on device support. Shutter is represented in reciprocal
        * form (1/sec). Any non-exposure fields are ignored.
     * @return true on success.
     */
    bool SetExposureParams(const ExposureParams &params) override;

private:
    struct StateCache
    {
        ExposureParams exposure_params;
        std::uint32_t exposure_mode = 0;
        bool liveview_valid = false;
        bool movie_recording = false;
    };

    /**
     * @brief Ensure transport/session is established. Blocking.
     * @return true if connected and ready.
     */
    bool EnsureConnected();

    /**
     * @brief Fetch parsed values from a DataManager-like source into cache.
     * @return true on success.
     */
    bool UpdateCacheFromDataManager();

    // Helper wrappers for underlying PTP operations.
    /**
     * @brief Send a control/command to a device property (SDIOControlDevice).
     * @param property_code Device property code (DPC_* constant).
     * @param value Numeric value to pass as control parameter.
     * @return true on success.
     */
    bool ControlDevice(std::uint32_t property_code, std::uint32_t value);

    /**
     * @brief Set an extended device property value (SDIOSetExtDevicePropValue).
     * @param property_code Property code (DPC_*).
     * @param value Raw numeric value to set.
     * @param size_bytes Number of bytes of the value (1/2/4/8).
     * @return true on success.
     */
    bool SetDevicePropValue(std::uint32_t property_code, std::uint64_t value,
                            std::uint32_t size_bytes);

    /**
     * @brief Connection state of the PTP session.
     * 
     * State transitions:
     * - Disconnected -> TransportReady: SetPtpTransport() is called with a valid transport.
     * - TransportReady -> SessionOpen: Connect() successfully completes the PTP handshake.
	 * - SessionOpen -> TransportReady: Disconnect() closes the active session but retains transport.
	 * - TransportReady -> Disconnected: SetPtpTransport(nullptr) is called.
	 * - SessionOpen -> Disconnected: active session is closed and transport is explicitly removed.
	 * - Any state -> Disconnected: Connect() fails and no transport remains available.
     */
    enum class ConnectionState {
        Disconnected,   ///< No transport, or session closed/failed.
        TransportReady, ///< Transport injected, but PTP session not yet established.
        SessionOpen     ///< PTP session active and ready.
    };

    mutable std::timed_mutex api_mutex_; ///< Main lock ensuring thread safety across all public APIs and underlying transport calls.
    std::mutex cache_mutex_;       ///< (Optional) legacy cache lock, now superseded by api_mutex_ but kept for structure.
    StateCache cache_;

    ConnectionState connection_state_ = ConnectionState::Disconnected;

    // Indicates whether the local `cache_` has been successfully populated with data
    // from the device at least once (e.g., via UpdateStatus).
    // Note: A session can be open (ConnectionState::SessionOpen) while the cache
    // is still empty. This flag prevents returning uninitialized default values
    // if getters are called immediately after Connect() but before UpdateStatus().
    bool has_status_ = false;

    PTPControl *ptp_ = nullptr;
    DataManager *data_mgr_ = nullptr;
    IPTPTransportPtr transport_;
};
