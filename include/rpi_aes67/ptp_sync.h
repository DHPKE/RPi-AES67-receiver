// SPDX-License-Identifier: MIT
/*
 * RPi-AES67 - Professional AES67 Sender/Receiver for Raspberry Pi 5
 * Copyright (c) 2025 DHPKE
 *
 * PTP synchronization wrapper.
 * Provides software-based PTP follower functionality using ravennakit patterns.
 */

#pragma once

#include <string>
#include <memory>
#include <atomic>
#include <chrono>
#include <functional>
#include <cstdint>

namespace rpi_aes67 {

/**
 * @brief PTP synchronization status
 */
enum class PTPState {
    Initializing,
    Listening,
    Uncalibrated,
    Slave,       // Following master
    Passive,
    Faulty
};

/**
 * @brief PTP clock information
 */
struct PTPClockInfo {
    uint64_t clock_id;
    uint8_t priority1;
    uint8_t priority2;
    uint8_t clock_class;
    uint8_t clock_accuracy;
    int64_t offset_from_master_ns;
    double path_delay_ns;
    PTPState state;
    bool synchronized;
};

/**
 * @brief PTP synchronization callback interface
 */
class PTPListener {
public:
    virtual ~PTPListener() = default;
    
    /**
     * @brief Called when PTP synchronization state changes
     */
    virtual void on_ptp_state_changed(PTPState state) = 0;
    
    /**
     * @brief Called periodically with clock offset information
     */
    virtual void on_ptp_offset_update(int64_t offset_ns, double path_delay_ns) = 0;
};

/**
 * @brief PTP configuration
 */
struct PTPConfig {
    std::string interface = "eth0";
    uint8_t domain = 0;
    bool use_hardware_timestamps = false;
    uint32_t announce_interval_ms = 1000;
    uint32_t sync_interval_ms = 125;
};

/**
 * @brief PTP synchronization class
 * 
 * Provides software-based IEEE 1588-2019 PTP follower functionality.
 * Can operate as a virtual PTP follower for AES67 timing synchronization.
 */
class PTPSync {
public:
    PTPSync();
    ~PTPSync();
    
    // Non-copyable, non-movable
    PTPSync(const PTPSync&) = delete;
    PTPSync& operator=(const PTPSync&) = delete;
    PTPSync(PTPSync&&) = delete;
    PTPSync& operator=(PTPSync&&) = delete;
    
    /**
     * @brief Initialize PTP follower
     * @param config PTP configuration
     * @return true on success
     */
    bool initialize(const PTPConfig& config);
    
    /**
     * @brief Initialize with network interface name
     * @param interface Network interface name (e.g., "eth0")
     * @param domain PTP domain number
     * @return true on success
     */
    bool initialize(const std::string& interface, uint8_t domain = 0);
    
    /**
     * @brief Start PTP synchronization
     */
    void start();
    
    /**
     * @brief Stop PTP synchronization
     */
    void stop();
    
    /**
     * @brief Check if PTP is running
     */
    [[nodiscard]] bool is_running() const;
    
    /**
     * @brief Check if clock is synchronized to master
     */
    [[nodiscard]] bool is_synchronized() const;
    
    /**
     * @brief Get current PTP time
     * @return PTP timestamp in nanoseconds since epoch
     */
    [[nodiscard]] std::chrono::nanoseconds get_current_time() const;
    
    /**
     * @brief Get PTP time as 64-bit timestamp
     * @return PTP timestamp value
     */
    [[nodiscard]] uint64_t get_ptp_timestamp() const;
    
    /**
     * @brief Convert PTP timestamp to RTP timestamp
     * @param ptp_ns PTP time in nanoseconds
     * @param sample_rate Sample rate in Hz
     * @return RTP timestamp (32-bit wrapping)
     */
    [[nodiscard]] static uint32_t ptp_to_rtp_timestamp(uint64_t ptp_ns, uint32_t sample_rate);
    
    /**
     * @brief Get current RTP timestamp for given sample rate
     * @param sample_rate Sample rate in Hz
     * @return RTP timestamp (32-bit wrapping)
     */
    [[nodiscard]] uint32_t get_rtp_timestamp(uint32_t sample_rate) const;
    
    /**
     * @brief Get offset from master clock
     * @return Offset in nanoseconds (negative = local ahead of master)
     */
    [[nodiscard]] int64_t get_offset_from_master() const;
    
    /**
     * @brief Get mean path delay
     * @return Path delay in nanoseconds
     */
    [[nodiscard]] double get_path_delay() const;
    
    /**
     * @brief Get current PTP state
     */
    [[nodiscard]] PTPState get_state() const;
    
    /**
     * @brief Get clock information
     */
    [[nodiscard]] PTPClockInfo get_clock_info() const;
    
    /**
     * @brief Add a PTP state listener
     * @param listener Listener to add (caller retains ownership)
     */
    void add_listener(PTPListener* listener);
    
    /**
     * @brief Remove a PTP state listener
     * @param listener Listener to remove
     */
    void remove_listener(PTPListener* listener);
    
    /**
     * @brief Get state as string
     */
    [[nodiscard]] static std::string state_to_string(PTPState state);

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

/**
 * @brief Local clock with PTP calibration support
 * 
 * Provides a local monotonic clock that can be calibrated against PTP time.
 */
class LocalClock {
public:
    LocalClock();
    ~LocalClock();
    
    /**
     * @brief Calibrate local clock against PTP reference
     * @param ptp_sync PTP synchronization reference
     */
    void calibrate(const PTPSync& ptp_sync);
    
    /**
     * @brief Check if clock is calibrated
     */
    [[nodiscard]] bool is_calibrated() const;
    
    /**
     * @brief Get current time as nanoseconds
     */
    [[nodiscard]] std::chrono::nanoseconds now() const;
    
    /**
     * @brief Convert to RTP timestamp
     * @param sample_rate Sample rate in Hz
     */
    [[nodiscard]] uint32_t to_rtp_timestamp(uint32_t sample_rate) const;
    
private:
    std::atomic<bool> calibrated_;
    std::atomic<int64_t> offset_ns_;
    std::chrono::steady_clock::time_point calibration_time_;
};

}  // namespace rpi_aes67
