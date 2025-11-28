// SPDX-License-Identifier: MIT
/*
 * RPi-AES67 - Professional AES67 Sender/Receiver for Raspberry Pi 5
 * Copyright (c) 2025 DHPKE
 *
 * PipeWire audio I/O integration.
 * Provides input capture and output playback using PipeWire.
 */

#pragma once

#include "config.h"
#include <string>
#include <memory>
#include <functional>
#include <atomic>
#include <cstdint>
#include <vector>

namespace rpi_aes67 {

/**
 * @brief Audio buffer for passing audio data
 */
struct AudioBuffer {
    uint8_t* data = nullptr;
    size_t size = 0;
    uint32_t frames = 0;
    uint32_t channels = 0;
    uint32_t sample_rate = 0;
    uint8_t bits_per_sample = 0;
    uint64_t timestamp = 0;  // PTP timestamp if available
    
    [[nodiscard]] size_t bytes_per_frame() const {
        return static_cast<size_t>(channels) * (bits_per_sample / 8);
    }
};

/**
 * @brief Callback type for audio data
 */
using AudioCallback = std::function<void(const AudioBuffer& buffer)>;

/**
 * @brief PipeWire connection state
 */
enum class PipeWireState {
    Disconnected,
    Connecting,
    Connected,
    Streaming,
    Error
};

/**
 * @brief PipeWire device information
 */
struct PipeWireDevice {
    uint32_t id;
    std::string name;
    std::string description;
    std::string media_class;  // "Audio/Source" or "Audio/Sink"
    uint32_t channels;
    uint32_t sample_rate;
    bool is_default;
};

/**
 * @brief PipeWire input (audio capture) class
 * 
 * Captures audio from PipeWire sources for AES67 transmission.
 */
class PipeWireInput {
public:
    PipeWireInput();
    ~PipeWireInput();
    
    // Non-copyable, non-movable
    PipeWireInput(const PipeWireInput&) = delete;
    PipeWireInput& operator=(const PipeWireInput&) = delete;
    PipeWireInput(PipeWireInput&&) = delete;
    PipeWireInput& operator=(PipeWireInput&&) = delete;
    
    /**
     * @brief Initialize PipeWire input
     * @return true on success
     */
    bool initialize();
    
    /**
     * @brief Open an audio source for capture
     * @param device_name Device name or empty for default
     * @param format Desired audio format
     * @return true on success
     */
    bool open(const std::string& device_name, const AudioFormat& format);
    
    /**
     * @brief Close the audio source
     */
    void close();
    
    /**
     * @brief Set audio callback
     * @param callback Callback function for audio data
     */
    void set_callback(AudioCallback callback);
    
    /**
     * @brief Start capturing audio
     */
    bool start();
    
    /**
     * @brief Stop capturing audio
     */
    void stop();
    
    /**
     * @brief Check if capturing is active
     */
    [[nodiscard]] bool is_running() const;
    
    /**
     * @brief Get current state
     */
    [[nodiscard]] PipeWireState get_state() const;
    
    /**
     * @brief Get current audio format
     */
    [[nodiscard]] AudioFormat get_format() const;
    
    /**
     * @brief List available input devices
     */
    [[nodiscard]] static std::vector<PipeWireDevice> list_devices();

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

/**
 * @brief PipeWire output (audio playback) class
 * 
 * Plays back received AES67 audio through PipeWire sinks.
 */
class PipeWireOutput {
public:
    PipeWireOutput();
    ~PipeWireOutput();
    
    // Non-copyable, non-movable
    PipeWireOutput(const PipeWireOutput&) = delete;
    PipeWireOutput& operator=(const PipeWireOutput&) = delete;
    PipeWireOutput(PipeWireOutput&&) = delete;
    PipeWireOutput& operator=(PipeWireOutput&&) = delete;
    
    /**
     * @brief Initialize PipeWire output
     * @return true on success
     */
    bool initialize();
    
    /**
     * @brief Open an audio sink for playback
     * @param device_name Device name or empty for default
     * @param format Desired audio format
     * @return true on success
     */
    bool open(const std::string& device_name, const AudioFormat& format);
    
    /**
     * @brief Close the audio sink
     */
    void close();
    
    /**
     * @brief Start playback
     */
    bool start();
    
    /**
     * @brief Stop playback
     */
    void stop();
    
    /**
     * @brief Write audio data to the output
     * @param buffer Audio data to write
     * @return Number of frames written
     */
    size_t write(const AudioBuffer& buffer);
    
    /**
     * @brief Write raw audio data
     * @param data Audio data pointer
     * @param size Size in bytes
     * @return Number of bytes written
     */
    size_t write(const void* data, size_t size);
    
    /**
     * @brief Check if playback is active
     */
    [[nodiscard]] bool is_running() const;
    
    /**
     * @brief Get current state
     */
    [[nodiscard]] PipeWireState get_state() const;
    
    /**
     * @brief Get current audio format
     */
    [[nodiscard]] AudioFormat get_format() const;
    
    /**
     * @brief Check if connected to PipeWire
     */
    [[nodiscard]] bool is_connected() const;
    
    /**
     * @brief Attempt reconnection
     */
    void reconnect();
    
    /**
     * @brief Get available buffer space in frames
     */
    [[nodiscard]] size_t get_available_frames() const;
    
    /**
     * @brief List available output devices
     */
    [[nodiscard]] static std::vector<PipeWireDevice> list_devices();

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

/**
 * @brief PipeWire manager for device enumeration and monitoring
 */
class PipeWireManager {
public:
    /**
     * @brief Get singleton instance
     */
    static PipeWireManager& instance();
    
    /**
     * @brief Initialize PipeWire
     * @return true on success
     */
    bool initialize();
    
    /**
     * @brief Shutdown PipeWire
     */
    void shutdown();
    
    /**
     * @brief Check if initialized
     */
    [[nodiscard]] bool is_initialized() const;
    
    /**
     * @brief List all audio sources (inputs)
     */
    [[nodiscard]] std::vector<PipeWireDevice> list_sources() const;
    
    /**
     * @brief List all audio sinks (outputs)
     */
    [[nodiscard]] std::vector<PipeWireDevice> list_sinks() const;
    
    /**
     * @brief Find device by name
     */
    [[nodiscard]] std::optional<PipeWireDevice> find_device(const std::string& name) const;

private:
    PipeWireManager();
    ~PipeWireManager();
    
    class Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace rpi_aes67
