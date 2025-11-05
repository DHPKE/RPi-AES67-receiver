#pragma once

#include <string>
#include <memory>
#include <atomic>
#include <gst/gst.h>
#include "config_manager.h"

class PipeWireBridge;

class AES67Receiver {
public:
    AES67Receiver(const ReceiverConfig& config, const AudioConfig& audio_config);
    ~AES67Receiver();

    void initialize();
    bool start(const std::string& sdp);
    void stop();
    bool is_active() const { return active_; }
    bool is_healthy() const;
    void recover();
    std::string get_id() const { return config_.id; }
    std::string get_label() const { return config_.label; }
    ReceiverConfig get_config() const { return config_; }

    struct Statistics {
        uint64_t packets_received;
        uint64_t packets_lost;
        uint64_t bytes_received;
        double current_latency_ms;
        double buffer_level;
        bool ptp_synchronized;
    };
    Statistics get_statistics() const;

private:
    ReceiverConfig config_;
    AudioConfig audio_config_;
    std::atomic<bool> active_;
    std::atomic<bool> initialized_;
    GstElement* pipeline_;
    GstElement* rtpbin_;
    GstElement* udpsrc_;
    GstElement* depayloader_;
    GstElement* audioconvert_;
    GstElement* audioresample_;
    GstElement* appsink_;
    std::unique_ptr<PipeWireBridge> pipewire_bridge_;

    struct SDPInfo {
        std::string source_ip;
        uint16_t port;
        uint8_t payload_type;
        uint32_t sample_rate;
        uint8_t channels;
        uint8_t bit_depth;
        std::string encoding;
    };
    SDPInfo sdp_info_;
    mutable Statistics stats_;

    bool parse_sdp(const std::string& sdp, SDPInfo& info);
    bool build_pipeline();
    void cleanup_pipeline();
    
    static GstFlowReturn on_new_sample(GstElement* sink, gpointer user_data);
    static void on_pad_added(GstElement* element, GstPad* pad, gpointer user_data);
    static gboolean on_bus_message(GstBus* bus, GstMessage* message, gpointer user_data);
};