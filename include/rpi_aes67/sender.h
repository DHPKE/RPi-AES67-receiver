// SPDX-License-Identifier: MIT
/*
 * RPi-AES67 - Professional AES67 Sender/Receiver for Raspberry Pi 5
 * Copyright (c) 2025 DHPKE
 *
 * AES67 Sender - Transmits AES67/RTP audio streams.
 * Based on ravennakit architecture patterns.
 */

#pragma once

#include "config.h"
#include "pipewire_io.h"
#include "ptp_sync.h"
#include <string>
#include <memory>
#include <atomic>
#include <functional>
#include <cstdint>

namespace rpi_aes67 {

// Forward declarations
class NMOSNode;

/**
 * @brief Sender statistics
 */
struct SenderStatistics {
    uint64_t packets_sent = 0;
    uint64_t bytes_sent = 0;
    uint64_t rtcp_reports_sent = 0;
    uint64_t sequence_number = 0;
    uint32_t rtp_timestamp = 0;
    double bitrate_kbps = 0.0;
    uint64_t underruns = 0;
    std::chrono::steady_clock::time_point start_time;
    std::chrono::steady_clock::time_point last_packet_time;
};

/**
 * @brief Sender state enumeration
 */
enum class SenderState {
    Stopped,
    Initializing,
    Running,
    Error
};

/**
 * @brief AES67 Sender class
 * 
 * Transmits AES67-compliant RTP audio streams with PTP-synchronized timestamps.
 * Captures audio from PipeWire and packetizes for network transmission.
 */
class AES67Sender {
public:
    AES67Sender();
    ~AES67Sender();
    
    // Non-copyable, non-movable
    AES67Sender(const AES67Sender&) = delete;
    AES67Sender& operator=(const AES67Sender&) = delete;
    AES67Sender(AES67Sender&&) = delete;
    AES67Sender& operator=(AES67Sender&&) = delete;
    
    /**
     * @brief Configure the sender
     * @param config Sender configuration
     * @return true on success
     */
    bool configure(const SenderConfig& config);
    
    /**
     * @brief Set the audio source for capture
     * @param source PipeWire input source
     */
    void set_audio_source(std::shared_ptr<PipeWireInput> source);
    
    /**
     * @brief Set PTP synchronization reference
     * @param ptp PTP synchronization instance
     */
    void set_ptp_sync(std::shared_ptr<PTPSync> ptp);
    
    /**
     * @brief Initialize the sender
     * @return true on success
     */
    bool initialize();
    
    /**
     * @brief Start streaming
     * @return true on success
     */
    bool start();
    
    /**
     * @brief Stop streaming
     */
    void stop();
    
    /**
     * @brief Check if sender is running
     */
    [[nodiscard]] bool is_running() const;
    
    /**
     * @brief Get current state
     */
    [[nodiscard]] SenderState get_state() const;
    
    /**
     * @brief Generate SDP description for this sender
     * @return SDP string
     */
    [[nodiscard]] std::string generate_sdp() const;
    
    /**
     * @brief Get the sender ID
     */
    [[nodiscard]] std::string get_id() const;
    
    /**
     * @brief Get the sender label
     */
    [[nodiscard]] std::string get_label() const;
    
    /**
     * @brief Get sender configuration
     */
    [[nodiscard]] SenderConfig get_config() const;
    
    /**
     * @brief Get sender statistics
     */
    [[nodiscard]] SenderStatistics get_statistics() const;
    
    /**
     * @brief Get current audio format
     */
    [[nodiscard]] AudioFormat get_audio_format() const;
    
    /**
     * @brief Get multicast IP address
     */
    [[nodiscard]] std::string get_multicast_ip() const;
    
    /**
     * @brief Get RTP port number
     */
    [[nodiscard]] uint16_t get_port() const;
    
    /**
     * @brief Register sender with NMOS node
     * @param node NMOS node instance
     */
    void register_with_nmos(std::shared_ptr<NMOSNode> node);
    
    /**
     * @brief Unregister from NMOS node
     */
    void unregister_from_nmos();
    
    /**
     * @brief Set callback for state changes
     */
    using StateCallback = std::function<void(SenderState)>;
    void set_state_callback(StateCallback callback);
    
    /**
     * @brief Check health status
     */
    [[nodiscard]] bool is_healthy() const;
    
    /**
     * @brief Attempt recovery from error state
     */
    void recover();

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

/**
 * @brief SDP generator for AES67 streams
 */
class SDPGenerator {
public:
    /**
     * @brief Generate SDP for an audio stream
     * @param sender_config Sender configuration
     * @param session_id Session ID (typically PTP-based)
     * @param origin_address Origin IP address
     * @return SDP string
     */
    static std::string generate(
        const SenderConfig& sender_config,
        uint64_t session_id,
        const std::string& origin_address);
    
    /**
     * @brief Generate SDP for a given audio format
     * @param multicast_ip Multicast destination IP
     * @param port RTP port
     * @param payload_type RTP payload type
     * @param format Audio format
     * @param session_name Session name
     * @param session_id Session ID
     * @param origin_address Origin IP
     * @return SDP string
     */
    static std::string generate(
        const std::string& multicast_ip,
        uint16_t port,
        uint8_t payload_type,
        const AudioFormat& format,
        const std::string& session_name,
        uint64_t session_id,
        const std::string& origin_address);
};

}  // namespace rpi_aes67
