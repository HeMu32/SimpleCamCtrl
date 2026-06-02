#pragma once

/// @brief Total timeout for connection attempt
#define _CONN_TIMEOUT_MS 500
/// @brief Timeout for retry (disconnect-connect process)
#define _CONN_RETRY_TIMEOUT_MS 120

/// @brief Polling worker interval and timeout (ms)
#define _POLLING_WORKER_INTER_MS 50
#define _POLLING_WORKER_TIMEOUT_MS 500

/// @brief Best-effort API lock timeout (ms).
/// Short enough to avoid blocking the caller behind heavy operations
/// (UpdateStatus ~17ms, SetExposureParams ~29ms), but long enough to
/// slip in between consecutive I/O bursts in the polling loop.
/// At 20ms this covers the tail end of an UpdateStatus hold, keeping
/// the failure rate well below 1% while returning ~5x faster than the
/// previous 150ms timeout.
#define _BEST_EFFORT_LOCK_TIMEOUT_MS 20

/// @brief Interactive single-I/O (Focus/Shutter/MovieRec): reasonable wait for user-triggered actions
#define _INTERACTIVE_LOCK_TIMEOUT_MS 500

#include <cstdint>
#include <atomic>
#include <condition_variable>
#include <future>
#include <mutex>
#include <string>
#include <thread>

#include <CamCtrl/ISimpleCamCtrl.h>
#include <CamCtrl/PTPTransport.h>

#ifndef SONY_FOCUS_POSITION_TYPE_NONE
#define SONY_FOCUS_POSITION_TYPE_NONE 0x00000000U
#define SONY_FOCUS_POSITION_TYPE_ABSOLUTE 0x00000001U
#define SONY_FOCUS_POSITION_TYPE_FOCAL_DISTANCE_METER 0x00000002U
#define SONY_FOCUS_POSITION_TYPE_FOLLOW_FOCUS 0x00000004U
#define SONY_FOCUS_POSITION_TYPE_AF_AREA_POINT 0x00000008U
#define SONY_FOCUS_POSITION_TYPE_SONY_MASK 0x0000000FU
#endif

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
    std::string GetFriendlyName() const;

    // Device-enumerator side metadata injection.
    // This does not affect transport/session behavior.
    void SetFriendlyName(const std::string& sName);

    // ISimpleCamCtrl
    /**
     * @brief Refresh the cached camera status directly from device.
     *
     * SonyPTP3_Impl internally runs a periodic polling worker (with
     * _POLLING_WORKER_INTER_MS interval and _POLLING_WORKER_TIMEOUT_MS timeout)
     * that keeps cache_ updated automatically. In normal operation (worker
     * mode), callers may not need to call UpdateStatus() explicitly. This
     * method is still provided for manual on-demand refresh.
     *
     * @return true on success.
     */
    bool UpdateStatus() override;
    bool GetExposureParams(ExposureParams &out_params) const override;
    std::uint32_t GetExposureMode() const override;
    
    /**
     * @brief Get cached focus-related values (generic snapshot defined in the
     *        public interface).
     * @param out_info Output focus info snapshot.
     * @return true if at least one focus-related field is available.
     */
    bool GetFocusPositionInfo(ISimpleCamCtrl::FocusPositionInfo &out_info) const override;

    /**
     * @brief Best-effort absolute focus position request.
     *
     * This uses Sony's absolute focus position property (0xE042) and keeps the
     * lock window intentionally short so callers can continue even when the
     * camera is busy or the transport is slow.
     * @param raw_position Sony absolute focus position raw value.
     * @return true if the command was accepted by the transport/device.
     */
    bool SetFocusPositionBestEffort(std::uint16_t raw_position) override;

     /**
      * @brief Best-effort AF area point move using normalized percentages.
      *
      * Callers provide x as a width percentage and y as a height percentage,
      * both in [0, 100]. Sony-specific raw coordinates are derived internally
      * from the device's supported range before sending the command.
      * @param x_percent AF point x position as a percentage of frame width.
      * @param y_percent AF point y position as a percentage of frame height.
      * @return true if the command was accepted by the transport/device.
      */
     bool SetAfAreaPositionBestEffort(double x_percent, double y_percent) override;

    /**
     * @brief Best-effort AF area mode request (e.g. Flexible Spot M).
     * @param raw_area_mode Sony AF area mode raw value.
     * @return true if the command was accepted by the transport/device.
     */
    bool SetAfAreaModeBestEffort(std::uint16_t raw_area_mode) override;

    /**
     * @brief Best-effort AF free size and position setting.
     *
     * Callers specify the AF box size and position as percentages of the
     * frame, all in [0, 100]. Implementations are responsible for converting
     * those normalized values to the device/vendor coordinate range before
     * sending the command.
     * 
     * @param height_percent AF box height as a percentage of frame height.
     * @param width_percent AF box width as a percentage of frame width.
     * @param x_percent AF box x position as a percentage of frame width.
     * @param y_percent AF box y position as a percentage of frame height.
     * @return true if the command was accepted by the transport/device.
     */
    bool SetAfFreeSizeAndPositionBestEffort(double height_percent,
                                            double width_percent,
                                            double x_percent,
                                            double y_percent) override;

    /**
     * @brief Best-effort position key setting.
     *
     * Sony example code sets this to HOSTPC (0x01) before remote touch / AF
     * point operations.
     * @param raw_key Position key raw value.
     * @return true if the command was accepted by the transport/device.
     */
    bool SetPositionKeyBestEffort(std::uint8_t raw_key);

    /**
     * @brief Best-effort focus mode request for Sony vendor values.
     * @param raw_mode Sony focus mode raw value (for example MF / AF_S / AF_C).
     * @return true if the command was accepted by the transport/device.
     */
    bool SetFocusModeBestEffort(std::uint32_t raw_mode) override;

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
        ISimpleCamCtrl::FocusPositionInfo focus_position;
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

    /**
     * @brief Start/stop and run the polling worker thread.
     */
    bool StartPollingWorker();
    void StopPollingWorker();
    void PollingWorkerLoop();

    /**
     * @brief Internal helper for polling worker (timeout-aware caller should use std::future).
     */
    bool UpdateStatusInternal();

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
    enum class ConnectionState : int {
        Disconnected,   ///< No transport, or session closed/failed.
        TransportReady, ///< Transport injected, but PTP session not yet established.
        SessionOpen     ///< PTP session active and ready.
    };

    mutable std::timed_mutex api_mutex_; ///< Main lock ensuring thread safety across all public APIs and underlying transport calls.
    mutable std::mutex cache_mutex_;       ///< cache lock used by getters and worker-safe cache updates.
    StateCache cache_;

    std::atomic<ConnectionState> connection_state_{ConnectionState::Disconnected};

    // Worker thread for periodic status polling.
    std::atomic<bool> polling_worker_running_{false};
    std::thread polling_worker_thread_;
    std::mutex polling_worker_mutex_;
    std::condition_variable polling_worker_cv_;
    std::future<bool> polling_worker_future_; // In-flight UpdateStatus operation

    // Indicates whether the local `cache_` has been successfully populated with data
    // from the device at least once (e.g., via UpdateStatus).
    // Note: A session can be open (ConnectionState::SessionOpen) while the cache
    // is still empty. This flag prevents returning uninitialized default values
    // if getters are called immediately after Connect() but before UpdateStatus().
    bool has_status_ = false;

    std::vector<std::uint32_t> cached_status_params_;
    bool has_cached_status_params_ = false;

    std::string friendly_name_;

    PTPControl *ptp_ = nullptr;
    DataManager *data_mgr_ = nullptr;
    IPTPTransportPtr transport_;
};
