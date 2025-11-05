#pragma once

#include <string>
#include <atomic>
#include <pipewire/pipewire.h>
#include "config_manager.h"

class PipeWireBridge {
public:
    explicit PipeWireBridge(const AudioConfig& config);
    ~PipeWireBridge();

    bool initialize();
    bool start(uint32_t sample_rate, uint8_t channels);
    void stop();
    
    bool write_audio(const void* data, size_t size);
    
    bool is_connected() const { return connected_; }
    void reconnect();

private:
    AudioConfig config_;
    std::atomic<bool> connected_;
    std::atomic<bool> running_;

    pw_thread_loop* loop_;
    pw_context* context_;
    pw_core* core_;
    pw_stream* stream_;

    uint32_t sample_rate_;
    uint8_t channels_;

    static void on_state_changed(void* data, enum pw_stream_state old, 
                                enum pw_stream_state state, const char* error);
    static void on_process(void* data);
};