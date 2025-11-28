// SPDX-License-Identifier: MIT
/*
 * RPi-AES67 - Professional AES67 Sender/Receiver for Raspberry Pi 5
 * Copyright (c) 2025 DHPKE
 *
 * AES67 Receiver - Receives and plays back AES67/RTP audio streams.
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
 * @brief Receiver statistics
 */
struct ReceiverStatistics {
    uint64_t packets_received = 0;
    uint64_t packets_lost = 0;
    uint64_t packets_out_of_order = 0;
    uint64_t bytes_received = 0;
    uint64_t rtcp_reports_received = 0;
    uint32_t last_sequence_number = 0;
    uint32_t last_rtp_timestamp = 0;
    double jitter_ms = 0.0;
    double latency_ms = 0.0;
    double buffer_level = 0.0;  // 0.0 - 1.0
    bool ptp_synchronized = false;
    double bitrate_kbps = 0.0;
    uint64_t overruns = 0;
    uint64_t underruns = 0;
    std::chrono::steady_clock::time_point start_time;
    std::chrono::steady_clock::time_point last_packet_time;
};

/**
 * @brief Receiver state enumeration
 */
enum class ReceiverState {
    Stopped,
    Initializing,
    Listening,
    Receiving,
    Error
};

/**
 * @brief Parsed SDP information
 */
struct SDPInfo {
    std::string session_name;
    std::string session_id;
    std::string origin_address;
    std::string source_ip;
    uint16_t port = 0;
    uint8_t payload_type = 0;
    AudioFormat format;
    std::string encoding;
    uint32_t packet_time_us = 1000;  // Packet time in microseconds
    std::string ptp_clock_id;
    bool is_valid = false;
};

/**
 * @brief AES67 Receiver class
 * 
 * Receives AES67-compliant RTP audio streams with jitter buffering
 * and PTP-synchronized playout timing.
 */
class AES67Receiver {
public:
    AES67Receiver();
    ~AES67Receiver();
    
    // Non-copyable, non-movable
    AES67Receiver(const AES67Receiver&) = delete;
    AES67Receiver& operator=(const AES67Receiver&) = delete;
    AES67Receiver(AES67Receiver&&) = delete;
    AES67Receiver& operator=(AES67Receiver&&) = delete;
    
    /**
     * @brief Configure the receiver
     * @param config Receiver configuration
     * @return true on success
     */
    bool configure(const ReceiverConfig& config);
    
    /**
     * @brief Configure with audio processing settings
     * @param config Receiver configuration
     * @param audio_config Audio processing configuration
     * @return true on success
     */
    bool configure(const ReceiverConfig& config, const AudioProcessingConfig& audio_config);
    
    /**
     * @brief Set the audio sink for playback
     * @param sink PipeWire output sink
     */
    void set_audio_sink(std::shared_ptr<PipeWireOutput> sink);
    
    /**
     * @brief Set PTP synchronization reference
     * @param ptp PTP synchronization instance
     */
    void set_ptp_sync(std::shared_ptr<PTPSync> ptp);
    
    /**
     * @brief Initialize the receiver
     * @return true on success
     */
    bool initialize();
    
    /**
     * @brief Connect to a stream using SDP
     * @param sdp SDP description string
     * @return true on success
     */
    bool connect(const std::string& sdp);
    
    /**
     * @brief Connect to a stream using transport parameters
     * @param source_ip Source/multicast IP address
     * @param port RTP port
     * @param format Expected audio format (optional - can be auto-detected)
     * @return true on success
     */
    bool connect(const std::string& source_ip, uint16_t port, 
                 const AudioFormat& format = AudioFormat{});
    
    /**
     * @brief Disconnect from current stream
     */
    void disconnect();
    
    /**
     * @brief Start receiving (must call connect first)
     * @return true on success
     */
    bool start();
    
    /**
     * @brief Stop receiving
     */
    void stop();
    
    /**
     * @brief Check if receiver is running
     */
    [[nodiscard]] bool is_running() const;
    
    /**
     * @brief Check if connected to a stream
     */
    [[nodiscard]] bool is_connected() const;
    
    /**
     * @brief Get current state
     */
    [[nodiscard]] ReceiverState get_state() const;
    
    /**
     * @brief Get the receiver ID
     */
    [[nodiscard]] std::string get_id() const;
    
    /**
     * @brief Get the receiver label
     */
    [[nodiscard]] std::string get_label() const;
    
    /**
     * @brief Get receiver configuration
     */
    [[nodiscard]] ReceiverConfig get_config() const;
    
    /**
     * @brief Get receiver statistics
     */
    [[nodiscard]] ReceiverStatistics get_statistics() const;
    
    /**
     * @brief Get current audio format (from connected stream)
     */
    [[nodiscard]] AudioFormat get_audio_format() const;
    
    /**
     * @brief Get parsed SDP info (if connected via SDP)
     */
    [[nodiscard]] SDPInfo get_sdp_info() const;
    
    /**
     * @brief Get current sender ID (from NMOS connection)
     */
    [[nodiscard]] std::string get_sender_id() const;
    
    /**
     * @brief Register receiver with NMOS node
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
    using StateCallback = std::function<void(ReceiverState)>;
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
 * @brief SDP parser for AES67 streams
 */
class SDPParser {
public:
    /**
     * @brief Parse SDP string
     * @param sdp SDP string
     * @return Parsed SDP information
     */
    static SDPInfo parse(const std::string& sdp);
    
    /**
     * @brief Validate SDP for AES67 compliance
     * @param info Parsed SDP info
     * @return true if valid AES67 SDP
     */
    static bool validate_aes67(const SDPInfo& info);
    
    /**
     * @brief Extract audio format from SDP info
     * @param info Parsed SDP info
     * @return Audio format
     */
    static AudioFormat extract_format(const SDPInfo& info);
};

/**
 * @brief Jitter buffer for RTP packet reordering and timing
 */
class JitterBuffer {
public:
    /**
     * @brief Jitter buffer configuration
     */
    struct Config {
        uint32_t target_delay_ms = 10;
        uint32_t min_delay_ms = 5;
        uint32_t max_delay_ms = 50;
        uint32_t max_packets = 1000;
    };
    
    JitterBuffer();
    explicit JitterBuffer(const Config& config);
    ~JitterBuffer();
    
    /**
     * @brief Add a packet to the buffer
     * @param data Packet data
     * @param size Packet size
     * @param sequence RTP sequence number
     * @param timestamp RTP timestamp
     * @return true if added successfully
     */
    bool push(const uint8_t* data, size_t size, uint16_t sequence, uint32_t timestamp);
    
    /**
     * @brief Get next packet for playout
     * @param data Output buffer
     * @param max_size Maximum buffer size
     * @param size Output: actual size written
     * @param timestamp Output: RTP timestamp
     * @return true if packet available
     */
    bool pop(uint8_t* data, size_t max_size, size_t& size, uint32_t& timestamp);
    
    /**
     * @brief Get current buffer level (0.0 - 1.0)
     */
    [[nodiscard]] double get_level() const;
    
    /**
     * @brief Get current latency in milliseconds
     */
    [[nodiscard]] double get_latency_ms() const;
    
    /**
     * @brief Reset the buffer
     */
    void reset();
    
private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace rpi_aes67
