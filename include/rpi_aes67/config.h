// SPDX-License-Identifier: MIT
/*
 * RPi-AES67 - Professional AES67 Sender/Receiver for Raspberry Pi 5
 * Copyright (c) 2025 DHPKE
 *
 * Configuration types and JSON-based configuration system.
 * Based on ravennakit architecture patterns.
 */

#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include <optional>
#include <nlohmann/json.hpp>

namespace rpi_aes67 {

/**
 * @brief Sample rate values supported for AES67 streams
 */
enum class SampleRate : uint32_t {
    Rate_44100 = 44100,
    Rate_48000 = 48000,
    Rate_96000 = 96000
};

/**
 * @brief Bit depth values supported for audio
 */
enum class BitDepth : uint8_t {
    Bits_16 = 16,
    Bits_24 = 24,
    Bits_32 = 32
};

/**
 * @brief Audio format specification
 */
struct AudioFormat {
    uint32_t sample_rate = 48000;
    uint8_t channels = 2;
    uint8_t bit_depth = 24;
    
    [[nodiscard]] uint32_t bytes_per_sample() const { return bit_depth / 8; }
    [[nodiscard]] uint32_t bytes_per_frame() const { return bytes_per_sample() * channels; }
    [[nodiscard]] std::string encoding_name() const;
    [[nodiscard]] bool is_valid() const;
};

/**
 * @brief Node identity and metadata configuration
 */
struct NodeConfig {
    std::string id;
    std::string label;
    std::string description;
    std::map<std::string, std::string> tags;
    
    // Generate defaults if not provided
    void set_defaults();
};

/**
 * @brief Configuration for a single AES67 sender
 */
struct SenderConfig {
    std::string id;
    std::string label;
    std::string description;
    uint8_t channels = 2;
    uint32_t sample_rate = 48000;
    uint8_t bit_depth = 24;
    std::string multicast_ip = "239.69.1.1";
    uint16_t port = 5004;
    uint8_t payload_type = 97;
    std::string pipewire_source;
    bool enabled = true;
    uint32_t packet_time_us = 1000;  // 1ms default for AES67
};

/**
 * @brief Configuration for a single AES67 receiver
 */
struct ReceiverConfig {
    std::string id;
    std::string label;
    std::string description;
    uint8_t channels = 2;
    std::vector<uint32_t> sample_rates = {44100, 48000, 96000};
    std::vector<uint8_t> bit_depths = {16, 24};
    std::string pipewire_sink;
    bool enabled = true;
};

/**
 * @brief Network configuration
 */
struct NetworkConfig {
    std::string interface = "eth0";
    uint8_t ptp_domain = 0;
    std::string registry_url;
    bool enable_mdns = true;
    uint16_t node_port = 8080;
    uint16_t connection_port = 8081;
};

/**
 * @brief Audio processing configuration
 */
struct AudioProcessingConfig {
    double buffer_size_ms = 5.0;
    double jitter_buffer_ms = 10.0;
    uint32_t buffer_frames = 256;
    bool enable_sample_rate_conversion = true;
};

/**
 * @brief Logging configuration
 */
struct LoggingConfig {
    std::string level = "info";
    std::string file;
    bool enable_console = true;
};

/**
 * @brief Complete application configuration
 */
struct Config {
    NodeConfig node;
    std::vector<SenderConfig> senders;
    std::vector<ReceiverConfig> receivers;
    NetworkConfig network;
    AudioProcessingConfig audio;
    LoggingConfig logging;
    
    /**
     * @brief Load configuration from JSON file
     * @param path Path to JSON configuration file
     * @return Config object
     * @throws std::runtime_error on parse error
     */
    static Config load_from_file(const std::string& path);
    
    /**
     * @brief Load configuration from JSON string
     * @param json_str JSON string
     * @return Config object
     */
    static Config load_from_string(const std::string& json_str);
    
    /**
     * @brief Save configuration to JSON file
     * @param path Output file path
     */
    void save_to_file(const std::string& path) const;
    
    /**
     * @brief Convert to JSON object
     */
    [[nodiscard]] nlohmann::json to_json() const;
    
    /**
     * @brief Validate the configuration
     * @return true if valid
     */
    [[nodiscard]] bool validate() const;
    
    /**
     * @brief Get default configuration
     */
    static Config get_default();
};

// JSON serialization for nlohmann::json
void to_json(nlohmann::json& j, const AudioFormat& f);
void from_json(const nlohmann::json& j, AudioFormat& f);

void to_json(nlohmann::json& j, const NodeConfig& c);
void from_json(const nlohmann::json& j, NodeConfig& c);

void to_json(nlohmann::json& j, const SenderConfig& c);
void from_json(const nlohmann::json& j, SenderConfig& c);

void to_json(nlohmann::json& j, const ReceiverConfig& c);
void from_json(const nlohmann::json& j, ReceiverConfig& c);

void to_json(nlohmann::json& j, const NetworkConfig& c);
void from_json(const nlohmann::json& j, NetworkConfig& c);

void to_json(nlohmann::json& j, const AudioProcessingConfig& c);
void from_json(const nlohmann::json& j, AudioProcessingConfig& c);

void to_json(nlohmann::json& j, const LoggingConfig& c);
void from_json(const nlohmann::json& j, LoggingConfig& c);

void to_json(nlohmann::json& j, const Config& c);
void from_json(const nlohmann::json& j, Config& c);

}  // namespace rpi_aes67
