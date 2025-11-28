// SPDX-License-Identifier: MIT
/*
 * RPi-AES67 - Professional AES67 Sender/Receiver for Raspberry Pi 5
 * Copyright (c) 2025 DHPKE
 *
 * PipeWire audio I/O implementation.
 */

#include "rpi_aes67/pipewire_io.h"
#include "rpi_aes67/logger.h"
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <cstring>

#ifdef HAVE_PIPEWIRE
#include <pipewire/pipewire.h>
#include <spa/param/audio/format-utils.h>
#include <spa/param/props.h>
#endif

namespace rpi_aes67 {

// ==================== PipeWireInput::Impl ====================

class PipeWireInput::Impl {
public:
    Impl() = default;
    ~Impl() { close(); }
    
    bool initialize() {
#ifdef HAVE_PIPEWIRE
        pw_init(nullptr, nullptr);
        initialized_ = true;
        return true;
#else
        LOG_WARNING("PipeWire not available, input disabled");
        initialized_ = true;
        return true;
#endif
    }
    
    bool open(const std::string& device_name, const AudioFormat& format) {
        device_name_ = device_name;
        format_ = format;
        
#ifdef HAVE_PIPEWIRE
        // Create main loop
        loop_ = pw_thread_loop_new("pipewire-input", nullptr);
        if (!loop_) {
            LOG_ERROR("Failed to create PipeWire thread loop");
            return false;
        }
        
        // Create context
        context_ = pw_context_new(pw_thread_loop_get_loop(loop_), nullptr, 0);
        if (!context_) {
            LOG_ERROR("Failed to create PipeWire context");
            pw_thread_loop_destroy(loop_);
            loop_ = nullptr;
            return false;
        }
        
        // Start the loop
        pw_thread_loop_start(loop_);
        
        pw_thread_loop_lock(loop_);
        
        // Connect to core
        core_ = pw_context_connect(context_, nullptr, 0);
        if (!core_) {
            LOG_ERROR("Failed to connect to PipeWire");
            pw_thread_loop_unlock(loop_);
            close();
            return false;
        }
        
        // Create stream
        struct pw_properties *props = pw_properties_new(
            PW_KEY_MEDIA_TYPE, "Audio",
            PW_KEY_MEDIA_CATEGORY, "Capture",
            PW_KEY_MEDIA_ROLE, "Music",
            nullptr
        );
        
        if (!device_name.empty()) {
            pw_properties_set(props, PW_KEY_TARGET_OBJECT, device_name.c_str());
        }
        
        stream_ = pw_stream_new(core_, "AES67-Input", props);
        if (!stream_) {
            LOG_ERROR("Failed to create PipeWire stream");
            pw_thread_loop_unlock(loop_);
            close();
            return false;
        }
        
        // Set up stream callbacks
        static const struct pw_stream_events stream_events = {
            .version = PW_VERSION_STREAM_EVENTS,
            .state_changed = on_state_changed_wrapper,
            .process = on_process_wrapper,
        };
        
        pw_stream_add_listener(stream_, &stream_listener_, &stream_events, this);
        
        // Configure audio format
        uint8_t buffer[1024];
        struct spa_pod_builder b = SPA_POD_BUILDER_INIT(buffer, sizeof(buffer));
        
        enum spa_audio_format spa_format;
        switch (format_.bit_depth) {
            case 16: spa_format = SPA_AUDIO_FORMAT_S16_LE; break;
            case 24: spa_format = SPA_AUDIO_FORMAT_S24_LE; break;
            case 32: spa_format = SPA_AUDIO_FORMAT_S32_LE; break;
            default: spa_format = SPA_AUDIO_FORMAT_S24_LE;
        }
        
        const struct spa_pod *params[1];
        params[0] = spa_format_audio_raw_build(&b, SPA_PARAM_EnumFormat,
            &SPA_AUDIO_INFO_RAW_INIT(
                .format = spa_format,
                .channels = format_.channels,
                .rate = format_.sample_rate
            ));
        
        // Connect stream
        if (pw_stream_connect(stream_,
                PW_DIRECTION_INPUT,
                PW_ID_ANY,
                static_cast<pw_stream_flags>(
                    PW_STREAM_FLAG_AUTOCONNECT |
                    PW_STREAM_FLAG_MAP_BUFFERS |
                    PW_STREAM_FLAG_RT_PROCESS),
                params, 1) < 0) {
            LOG_ERROR("Failed to connect PipeWire stream");
            pw_thread_loop_unlock(loop_);
            close();
            return false;
        }
        
        pw_thread_loop_unlock(loop_);
#endif
        
        state_ = PipeWireState::Connected;
        LOG_INFO("PipeWire input opened: {}", device_name.empty() ? "default" : device_name);
        return true;
    }
    
    void close() {
#ifdef HAVE_PIPEWIRE
        if (loop_) {
            pw_thread_loop_stop(loop_);
        }
        
        if (stream_) {
            pw_stream_destroy(stream_);
            stream_ = nullptr;
        }
        
        if (core_) {
            pw_core_disconnect(core_);
            core_ = nullptr;
        }
        
        if (context_) {
            pw_context_destroy(context_);
            context_ = nullptr;
        }
        
        if (loop_) {
            pw_thread_loop_destroy(loop_);
            loop_ = nullptr;
        }
#endif
        state_ = PipeWireState::Disconnected;
    }
    
    void set_callback(AudioCallback callback) {
        std::lock_guard<std::mutex> lock(callback_mutex_);
        callback_ = std::move(callback);
    }
    
    bool start() {
        running_ = true;
        state_ = PipeWireState::Streaming;
        LOG_INFO("PipeWire input started");
        return true;
    }
    
    void stop() {
        running_ = false;
        state_ = PipeWireState::Connected;
        LOG_INFO("PipeWire input stopped");
    }
    
    bool is_running() const { return running_; }
    PipeWireState get_state() const { return state_; }
    AudioFormat get_format() const { return format_; }
    
private:
#ifdef HAVE_PIPEWIRE
    static void on_state_changed_wrapper(void* data, enum pw_stream_state old,
                                         enum pw_stream_state state, const char* error) {
        auto* impl = static_cast<Impl*>(data);
        impl->on_state_changed(old, state, error);
    }
    
    static void on_process_wrapper(void* data) {
        auto* impl = static_cast<Impl*>(data);
        impl->on_process();
    }
    
    void on_state_changed(enum pw_stream_state old, enum pw_stream_state state, const char* error) {
        LOG_DEBUG("PipeWire input state: {} -> {}", 
                  pw_stream_state_as_string(old),
                  pw_stream_state_as_string(state));
        
        if (state == PW_STREAM_STATE_ERROR) {
            LOG_ERROR("PipeWire input error: {}", error ? error : "unknown");
            state_ = PipeWireState::Error;
        }
    }
    
    void on_process() {
        if (!running_) return;
        
        struct pw_buffer* b = pw_stream_dequeue_buffer(stream_);
        if (!b) return;
        
        struct spa_buffer* buf = b->buffer;
        uint8_t* data = static_cast<uint8_t*>(buf->datas[0].data);
        
        if (data && callback_) {
            AudioBuffer audio_buf;
            audio_buf.data = data;
            audio_buf.size = buf->datas[0].chunk->size;
            audio_buf.frames = audio_buf.size / format_.bytes_per_frame();
            audio_buf.channels = format_.channels;
            audio_buf.sample_rate = format_.sample_rate;
            audio_buf.bits_per_sample = format_.bit_depth;
            
            std::lock_guard<std::mutex> lock(callback_mutex_);
            if (callback_) {
                callback_(audio_buf);
            }
        }
        
        pw_stream_queue_buffer(stream_, b);
    }
    
    pw_thread_loop* loop_ = nullptr;
    pw_context* context_ = nullptr;
    pw_core* core_ = nullptr;
    pw_stream* stream_ = nullptr;
    spa_hook stream_listener_{};
#endif
    
    bool initialized_ = false;
    std::atomic<bool> running_{false};
    PipeWireState state_ = PipeWireState::Disconnected;
    AudioFormat format_;
    std::string device_name_;
    
    std::mutex callback_mutex_;
    AudioCallback callback_;
};

// ==================== PipeWireOutput::Impl ====================

class PipeWireOutput::Impl {
public:
    Impl() = default;
    ~Impl() { close(); }
    
    bool initialize() {
#ifdef HAVE_PIPEWIRE
        pw_init(nullptr, nullptr);
        initialized_ = true;
        return true;
#else
        LOG_WARNING("PipeWire not available, output disabled");
        initialized_ = true;
        return true;
#endif
    }
    
    bool open(const std::string& device_name, const AudioFormat& format) {
        device_name_ = device_name;
        format_ = format;
        
#ifdef HAVE_PIPEWIRE
        // Create main loop
        loop_ = pw_thread_loop_new("pipewire-output", nullptr);
        if (!loop_) {
            LOG_ERROR("Failed to create PipeWire thread loop");
            return false;
        }
        
        // Create context
        context_ = pw_context_new(pw_thread_loop_get_loop(loop_), nullptr, 0);
        if (!context_) {
            LOG_ERROR("Failed to create PipeWire context");
            pw_thread_loop_destroy(loop_);
            loop_ = nullptr;
            return false;
        }
        
        // Start the loop
        pw_thread_loop_start(loop_);
        
        pw_thread_loop_lock(loop_);
        
        // Connect to core
        core_ = pw_context_connect(context_, nullptr, 0);
        if (!core_) {
            LOG_ERROR("Failed to connect to PipeWire");
            pw_thread_loop_unlock(loop_);
            close();
            return false;
        }
        
        // Create stream
        struct pw_properties *props = pw_properties_new(
            PW_KEY_MEDIA_TYPE, "Audio",
            PW_KEY_MEDIA_CATEGORY, "Playback",
            PW_KEY_MEDIA_ROLE, "Music",
            nullptr
        );
        
        if (!device_name.empty()) {
            pw_properties_set(props, PW_KEY_TARGET_OBJECT, device_name.c_str());
        }
        
        stream_ = pw_stream_new(core_, "AES67-Output", props);
        if (!stream_) {
            LOG_ERROR("Failed to create PipeWire stream");
            pw_thread_loop_unlock(loop_);
            close();
            return false;
        }
        
        // Set up stream callbacks
        static const struct pw_stream_events stream_events = {
            .version = PW_VERSION_STREAM_EVENTS,
            .state_changed = on_state_changed_wrapper,
            .process = on_process_wrapper,
        };
        
        pw_stream_add_listener(stream_, &stream_listener_, &stream_events, this);
        
        // Configure audio format
        uint8_t buffer[1024];
        struct spa_pod_builder b = SPA_POD_BUILDER_INIT(buffer, sizeof(buffer));
        
        enum spa_audio_format spa_format;
        switch (format_.bit_depth) {
            case 16: spa_format = SPA_AUDIO_FORMAT_S16_LE; break;
            case 24: spa_format = SPA_AUDIO_FORMAT_S24_LE; break;
            case 32: spa_format = SPA_AUDIO_FORMAT_S32_LE; break;
            default: spa_format = SPA_AUDIO_FORMAT_S24_LE;
        }
        
        const struct spa_pod *params[1];
        params[0] = spa_format_audio_raw_build(&b, SPA_PARAM_EnumFormat,
            &SPA_AUDIO_INFO_RAW_INIT(
                .format = spa_format,
                .channels = format_.channels,
                .rate = format_.sample_rate
            ));
        
        // Connect stream
        if (pw_stream_connect(stream_,
                PW_DIRECTION_OUTPUT,
                PW_ID_ANY,
                static_cast<pw_stream_flags>(
                    PW_STREAM_FLAG_AUTOCONNECT |
                    PW_STREAM_FLAG_MAP_BUFFERS |
                    PW_STREAM_FLAG_RT_PROCESS),
                params, 1) < 0) {
            LOG_ERROR("Failed to connect PipeWire stream");
            pw_thread_loop_unlock(loop_);
            close();
            return false;
        }
        
        pw_thread_loop_unlock(loop_);
#endif
        
        state_ = PipeWireState::Connected;
        connected_ = true;
        LOG_INFO("PipeWire output opened: {}", device_name.empty() ? "default" : device_name);
        return true;
    }
    
    void close() {
#ifdef HAVE_PIPEWIRE
        if (loop_) {
            pw_thread_loop_stop(loop_);
        }
        
        if (stream_) {
            pw_stream_destroy(stream_);
            stream_ = nullptr;
        }
        
        if (core_) {
            pw_core_disconnect(core_);
            core_ = nullptr;
        }
        
        if (context_) {
            pw_context_destroy(context_);
            context_ = nullptr;
        }
        
        if (loop_) {
            pw_thread_loop_destroy(loop_);
            loop_ = nullptr;
        }
#endif
        state_ = PipeWireState::Disconnected;
        connected_ = false;
    }
    
    bool start() {
        running_ = true;
        state_ = PipeWireState::Streaming;
        LOG_INFO("PipeWire output started");
        return true;
    }
    
    void stop() {
        running_ = false;
        state_ = PipeWireState::Connected;
        LOG_INFO("PipeWire output stopped");
    }
    
    size_t write(const AudioBuffer& buffer) {
        return write(buffer.data, buffer.size);
    }
    
    size_t write(const void* data, size_t size) {
        if (!running_) return 0;
        
        std::lock_guard<std::mutex> lock(write_mutex_);
        
#ifdef HAVE_PIPEWIRE
        // Queue data for PipeWire callback
        const uint8_t* src = static_cast<const uint8_t*>(data);
        write_buffer_.insert(write_buffer_.end(), src, src + size);
#endif
        
        return size;
    }
    
    bool is_running() const { return running_; }
    PipeWireState get_state() const { return state_; }
    AudioFormat get_format() const { return format_; }
    bool is_connected() const { return connected_; }
    
    void reconnect() {
        close();
        open(device_name_, format_);
        if (running_) {
            start();
        }
    }
    
    size_t get_available_frames() const {
        // Return available buffer space
        constexpr size_t MAX_BUFFER = 8192;
        std::lock_guard<std::mutex> lock(write_mutex_);
        size_t used = write_buffer_.size() / format_.bytes_per_frame();
        return MAX_BUFFER > used ? MAX_BUFFER - used : 0;
    }
    
private:
#ifdef HAVE_PIPEWIRE
    static void on_state_changed_wrapper(void* data, enum pw_stream_state old,
                                         enum pw_stream_state state, const char* error) {
        auto* impl = static_cast<Impl*>(data);
        impl->on_state_changed(old, state, error);
    }
    
    static void on_process_wrapper(void* data) {
        auto* impl = static_cast<Impl*>(data);
        impl->on_process();
    }
    
    void on_state_changed(enum pw_stream_state old, enum pw_stream_state state, const char* error) {
        LOG_DEBUG("PipeWire output state: {} -> {}",
                  pw_stream_state_as_string(old),
                  pw_stream_state_as_string(state));
        
        if (state == PW_STREAM_STATE_ERROR) {
            LOG_ERROR("PipeWire output error: {}", error ? error : "unknown");
            state_ = PipeWireState::Error;
        }
    }
    
    void on_process() {
        if (!running_) return;
        
        struct pw_buffer* b = pw_stream_dequeue_buffer(stream_);
        if (!b) return;
        
        struct spa_buffer* buf = b->buffer;
        uint8_t* data = static_cast<uint8_t*>(buf->datas[0].data);
        
        if (data) {
            std::lock_guard<std::mutex> lock(write_mutex_);
            
            size_t max_size = buf->datas[0].maxsize;
            size_t copy_size = std::min(max_size, write_buffer_.size());
            
            if (copy_size > 0) {
                std::memcpy(data, write_buffer_.data(), copy_size);
                write_buffer_.erase(write_buffer_.begin(), 
                                   write_buffer_.begin() + copy_size);
            } else {
                // Output silence
                std::memset(data, 0, max_size);
                copy_size = max_size;
            }
            
            buf->datas[0].chunk->offset = 0;
            buf->datas[0].chunk->stride = format_.bytes_per_frame();
            buf->datas[0].chunk->size = copy_size;
        }
        
        pw_stream_queue_buffer(stream_, b);
    }
    
    pw_thread_loop* loop_ = nullptr;
    pw_context* context_ = nullptr;
    pw_core* core_ = nullptr;
    pw_stream* stream_ = nullptr;
    spa_hook stream_listener_{};
#endif
    
    bool initialized_ = false;
    std::atomic<bool> running_{false};
    std::atomic<bool> connected_{false};
    PipeWireState state_ = PipeWireState::Disconnected;
    AudioFormat format_;
    std::string device_name_;
    
    mutable std::mutex write_mutex_;
    std::vector<uint8_t> write_buffer_;
};

// ==================== PipeWireInput ====================

PipeWireInput::PipeWireInput() : impl_(std::make_unique<Impl>()) {}
PipeWireInput::~PipeWireInput() = default;

bool PipeWireInput::initialize() { return impl_->initialize(); }
bool PipeWireInput::open(const std::string& device_name, const AudioFormat& format) {
    return impl_->open(device_name, format);
}
void PipeWireInput::close() { impl_->close(); }
void PipeWireInput::set_callback(AudioCallback callback) { impl_->set_callback(std::move(callback)); }
bool PipeWireInput::start() { return impl_->start(); }
void PipeWireInput::stop() { impl_->stop(); }
bool PipeWireInput::is_running() const { return impl_->is_running(); }
PipeWireState PipeWireInput::get_state() const { return impl_->get_state(); }
AudioFormat PipeWireInput::get_format() const { return impl_->get_format(); }

std::vector<PipeWireDevice> PipeWireInput::list_devices() {
    // NOTE: Device enumeration requires a running PipeWire context
    // For now, use 'pw-cli list-objects' or 'wpctl status' to discover devices
    // and specify them by name in the configuration file
    LOG_DEBUG("PipeWire device enumeration not yet implemented");
    return {};
}

// ==================== PipeWireOutput ====================

PipeWireOutput::PipeWireOutput() : impl_(std::make_unique<Impl>()) {}
PipeWireOutput::~PipeWireOutput() = default;

bool PipeWireOutput::initialize() { return impl_->initialize(); }
bool PipeWireOutput::open(const std::string& device_name, const AudioFormat& format) {
    return impl_->open(device_name, format);
}
void PipeWireOutput::close() { impl_->close(); }
bool PipeWireOutput::start() { return impl_->start(); }
void PipeWireOutput::stop() { impl_->stop(); }
size_t PipeWireOutput::write(const AudioBuffer& buffer) { return impl_->write(buffer); }
size_t PipeWireOutput::write(const void* data, size_t size) { return impl_->write(data, size); }
bool PipeWireOutput::is_running() const { return impl_->is_running(); }
PipeWireState PipeWireOutput::get_state() const { return impl_->get_state(); }
AudioFormat PipeWireOutput::get_format() const { return impl_->get_format(); }
bool PipeWireOutput::is_connected() const { return impl_->is_connected(); }
void PipeWireOutput::reconnect() { impl_->reconnect(); }
size_t PipeWireOutput::get_available_frames() const { return impl_->get_available_frames(); }

std::vector<PipeWireDevice> PipeWireOutput::list_devices() {
    // NOTE: Device enumeration requires a running PipeWire context
    // For now, use 'pw-cli list-objects' or 'wpctl status' to discover devices
    // and specify them by name in the configuration file
    LOG_DEBUG("PipeWire device enumeration not yet implemented");
    return {};
}

// ==================== PipeWireManager ====================

class PipeWireManager::Impl {
public:
    bool initialize() {
#ifdef HAVE_PIPEWIRE
        pw_init(nullptr, nullptr);
#endif
        initialized_ = true;
        LOG_INFO("PipeWire manager initialized");
        return true;
    }
    
    void shutdown() {
#ifdef HAVE_PIPEWIRE
        pw_deinit();
#endif
        initialized_ = false;
        LOG_INFO("PipeWire manager shutdown");
    }
    
    bool is_initialized() const { return initialized_; }
    
    std::vector<PipeWireDevice> list_sources() const {
        // NOTE: Device enumeration requires a full registry implementation
        // For now, use command-line tools to discover devices
        LOG_DEBUG("Source enumeration not yet implemented - use 'pw-cli list-objects'");
        return {};
    }
    
    std::vector<PipeWireDevice> list_sinks() const {
        // NOTE: Device enumeration requires a full registry implementation
        // For now, use command-line tools to discover devices
        LOG_DEBUG("Sink enumeration not yet implemented - use 'pw-cli list-objects'");
        return {};
    }
    
    std::optional<PipeWireDevice> find_device(const std::string& name) const {
        // NOTE: Device lookup requires a full registry implementation
        // For now, specify devices by name in the configuration file
        LOG_DEBUG("Device lookup not yet implemented for: {}", name);
        return std::nullopt;
    }
    
private:
    bool initialized_ = false;
};

PipeWireManager::PipeWireManager() : impl_(std::make_unique<Impl>()) {}
PipeWireManager::~PipeWireManager() = default;

PipeWireManager& PipeWireManager::instance() {
    static PipeWireManager instance;
    return instance;
}

bool PipeWireManager::initialize() { return impl_->initialize(); }
void PipeWireManager::shutdown() { impl_->shutdown(); }
bool PipeWireManager::is_initialized() const { return impl_->is_initialized(); }
std::vector<PipeWireDevice> PipeWireManager::list_sources() const { return impl_->list_sources(); }
std::vector<PipeWireDevice> PipeWireManager::list_sinks() const { return impl_->list_sinks(); }
std::optional<PipeWireDevice> PipeWireManager::find_device(const std::string& name) const {
    return impl_->find_device(name);
}

}  // namespace rpi_aes67
