#include "aes67_receiver.h"
#include "pipewire_bridge.h"
#include "logger.h"
#include <sstream>
#include <regex>

AES67Receiver::AES67Receiver(const ReceiverConfig& config, const AudioConfig& audio_config)
    : config_(config)
    , audio_config_(audio_config)
    , active_(false)
    , initialized_(false)
    , pipeline_(nullptr)
    , rtpbin_(nullptr)
    , udpsrc_(nullptr)
    , depayloader_(nullptr)
    , audioconvert_(nullptr)
    , audioresample_(nullptr)
    , appsink_(nullptr)
{
    memset(&stats_, 0, sizeof(stats_));
}

AES67Receiver::~AES67Receiver() {
    stop();
    cleanup_pipeline();
}

void AES67Receiver::initialize() {
    if (initialized_) {
        return;
    }

    // Initialize GStreamer
    if (!gst_is_initialized()) {
        gst_init(nullptr, nullptr);
    }

    // Initialize PipeWire bridge
    pipewire_bridge_ = std::make_unique<PipeWireBridge>(audio_config_);
    pipewire_bridge_->initialize();

    initialized_ = true;
    Logger::info("AES67Receiver " + config_.id + " initialized");
}

bool AES67Receiver::parse_sdp(const std::string& sdp, SDPInfo& info) {
    // Parse SDP for connection information
    // Example SDP:
    // v=0
    // o=- 123456 123456 IN IP4 192.168.1.100
    // s=AES67 Stream
    // c=IN IP4 239.69.1.1/32
    // t=0 0
    // m=audio 5004 RTP/AVP 97
    // a=rtpmap:97 L24/48000/2
    // a=ptime:1

    std::istringstream iss(sdp);
    std::string line;

    while (std::getline(iss, line)) {
        // Connection information
        if (line.substr(0, 2) == "c=") {
            std::regex conn_regex(R"(c=IN IP4 ([0-9.]+))");
            std::smatch matches;
            if (std::regex_search(line, matches, conn_regex)) {
                info.source_ip = matches[1];
            }
        }
        // Media description
        else if (line.substr(0, 2) == "m=") {
            std::regex media_regex(R"(m=audio (\d+) RTP/AVP (\d+))");
            std::smatch matches;
            if (std::regex_search(line, matches, media_regex)) {
                info.port = std::stoi(matches[1]);
                info.payload_type = std::stoi(matches[2]);
            }
        }
        // RTP map
        else if (line.substr(0, 7) == "a=rtpmap:") {
            std::regex rtpmap_regex(R"(a=rtpmap:(\d+) ([^/]+)/(\d+)/(\d+))");
            std::smatch matches;
            if (std::regex_search(line, matches, rtpmap_regex)) {
                info.encoding = matches[2];
                info.sample_rate = std::stoi(matches[3]);
                info.channels = std::stoi(matches[4]);
                
                // Determine bit depth from encoding
                if (info.encoding == "L24") {
                    info.bit_depth = 24;
                } else if (info.encoding == "L16") {
                    info.bit_depth = 16;
                }
            }
        }
    }

    // Validate parsed information
    if (info.source_ip.empty() || info.port == 0) {
        Logger::error("Invalid SDP: missing connection information");
        return false;
    }

    Logger::info("Parsed SDP: " + info.source_ip + ":" + std::to_string(info.port) + 
                 " " + info.encoding + "/" + std::to_string(info.sample_rate) + 
                 "/" + std::to_string(info.channels));
    return true;
}

bool AES67Receiver::build_pipeline() {
    cleanup_pipeline();

    // Create pipeline
    pipeline_ = gst_pipeline_new("aes67-receiver-pipeline");
    if (!pipeline_) {
        Logger::error("Failed to create GStreamer pipeline");
        return false;
    }

    // Create elements
    udpsrc_ = gst_element_factory_make("udpsrc", "udpsrc");
    rtpbin_ = gst_element_factory_make("rtpbin", "rtpbin");
    
    // Choose depayloader based on encoding
    const char* depayloader_name = "rtpL24depay";
    if (sdp_info_.encoding == "L16") {
        depayloader_name = "rtpL16depay";
    }
    depayloader_ = gst_element_factory_make(depayloader_name, "depayloader");
    
    audioconvert_ = gst_element_factory_make("audioconvert", "audioconvert");
    audioresample_ = gst_element_factory_make("audioresample", "audioresample");
    appsink_ = gst_element_factory_make("appsink", "appsink");

    if (!udpsrc_ || !rtpbin_ || !depayloader_ || !audioconvert_ || 
        !audioresample_ || !appsink_) {
        Logger::error("Failed to create GStreamer elements");
        cleanup_pipeline();
        return false;
    }

    // Configure udpsrc
    g_object_set(udpsrc_,
                 "address", sdp_info_.source_ip.c_str(),
                 "port", sdp_info_.port,
                 "caps", gst_caps_new_simple("application/x-rtp",
                                            "media", G_TYPE_STRING, "audio",
                                            "clock-rate", G_TYPE_INT, sdp_info_.sample_rate,
                                            "encoding-name", G_TYPE_STRING, sdp_info_.encoding.c_str(),
                                            "payload", G_TYPE_INT, sdp_info_.payload_type,
                                            nullptr),
                 nullptr);

    // Configure appsink
    g_object_set(appsink_,
                 "emit-signals", TRUE,
                 "sync", FALSE,
                 nullptr);
    
    GstCaps* sink_caps = gst_caps_new_simple("audio/x-raw",
                                             "format", G_TYPE_STRING, "S24LE",
                                             "rate", G_TYPE_INT, sdp_info_.sample_rate,
                                             "channels", G_TYPE_INT, sdp_info_.channels,
                                             nullptr);
    g_object_set(appsink_, "caps", sink_caps, nullptr);
    gst_caps_unref(sink_caps);

    // Connect callback
    g_signal_connect(appsink_, "new-sample", G_CALLBACK(on_new_sample), this);

    // Add elements to pipeline
    gst_bin_add_many(GST_BIN(pipeline_), udpsrc_, rtpbin_, depayloader_,
                     audioconvert_, audioresample_, appsink_, nullptr);

    // Link elements
    if (!gst_element_link(udpsrc_, rtpbin_) ||
        !gst_element_link(depayloader_, audioconvert_) ||
        !gst_element_link(audioconvert_, audioresample_) ||
        !gst_element_link(audioresample_, appsink_)) {
        Logger::error("Failed to link GStreamer elements");
        cleanup_pipeline();
        return false;
    }

    // Link rtpbin dynamically
    g_signal_connect(rtpbin_, "pad-added", G_CALLBACK(on_pad_added), this);

    // Setup bus watch
    GstBus* bus = gst_pipeline_get_bus(GST_PIPELINE(pipeline_));
    gst_bus_add_watch(bus, on_bus_message, this);
    gst_object_unref(bus);

    Logger::info("GStreamer pipeline built successfully");
    return true;
}

bool AES67Receiver::start(const std::string& sdp) {
    if (active_) {
        Logger::warning("Receiver already active");
        return true;
    }

    if (!initialized_) {
        Logger::error("Receiver not initialized");
        return false;
    }

    // Parse SDP
    if (!parse_sdp(sdp, sdp_info_)) {
        return false;
    }

    // Build pipeline
    if (!build_pipeline()) {
        return false;
    }

    // Start PipeWire bridge
    if (!pipewire_bridge_->start(sdp_info_.sample_rate, sdp_info_.channels)) {
        Logger::error("Failed to start PipeWire bridge");
        cleanup_pipeline();
        return false;
    }

    // Start pipeline
    GstStateChangeReturn ret = gst_element_set_state(pipeline_, GST_STATE_PLAYING);
    if (ret == GST_STATE_CHANGE_FAILURE) {
        Logger::error("Failed to start GStreamer pipeline");
        pipewire_bridge_->stop();
        cleanup_pipeline();
        return false;
    }

    active_ = true;
    Logger::info("AES67Receiver " + config_.id + " started");
    return true;
}

void AES67Receiver::stop() {
    if (!active_) {
        return;
    }

    active_ = false;

    // Stop pipeline
    if (pipeline_) {
        gst_element_set_state(pipeline_, GST_STATE_NULL);
    }

    // Stop PipeWire bridge
    if (pipewire_bridge_) {
        pipewire_bridge_->stop();
    }

    cleanup_pipeline();
    Logger::info("AES67Receiver " + config_.id + " stopped");
}

void AES67Receiver::cleanup_pipeline() {
    if (pipeline_) {
        gst_object_unref(pipeline_);
        pipeline_ = nullptr;
    }
    udpsrc_ = nullptr;
    rtpbin_ = nullptr;
    depayloader_ = nullptr;
    audioconvert_ = nullptr;
    audioresample_ = nullptr;
    appsink_ = nullptr;
}

GstFlowReturn AES67Receiver::on_new_sample(GstElement* sink, gpointer user_data) {
    auto* receiver = static_cast<AES67Receiver*>(user_data);
    
    GstSample* sample = nullptr;
    g_signal_emit_by_name(sink, "pull-sample", &sample);
    
    if (sample) {
        GstBuffer* buffer = gst_sample_get_buffer(sample);
        GstMapInfo map;
        
        if (gst_buffer_map(buffer, &map, GST_MAP_READ)) {
            // Send audio data to PipeWire
            receiver->pipewire_bridge_->write_audio(map.data, map.size);
            
            // Update statistics
            receiver->stats_.packets_received++;
            receiver->stats_.bytes_received += map.size;
            
            gst_buffer_unmap(buffer, &map);
        }
        
        gst_sample_unref(sample);
    }
    
    return GST_FLOW_OK;
}

void AES67Receiver::on_pad_added(GstElement* element, GstPad* pad, gpointer user_data) {
    auto* receiver = static_cast<AES67Receiver*>(user_data);
    
    GstPad* sinkpad = gst_element_get_static_pad(receiver->depayloader_, "sink");
    if (!gst_pad_is_linked(sinkpad)) {
        gst_pad_link(pad, sinkpad);
    }
    gst_object_unref(sinkpad);
}

gboolean AES67Receiver::on_bus_message(GstBus* bus, GstMessage* message, gpointer user_data) {
    auto* receiver = static_cast<AES67Receiver*>(user_data);
    
    switch (GST_MESSAGE_TYPE(message)) {
        case GST_MESSAGE_ERROR: {
            GError* err;
            gchar* debug;
            gst_message_parse_error(message, &err, &debug);
            Logger::error("GStreamer error: " + std::string(err->message));
            g_error_free(err);
            g_free(debug);
            break;
        }
        case GST_MESSAGE_WARNING: {
            GError* err;
            gchar* debug;
            gst_message_parse_warning(message, &err, &debug);
            Logger::warning("GStreamer warning: " + std::string(err->message));
            g_error_free(err);
            g_free(debug);
            break;
        }
        default:
            break;
    }
    
    return TRUE;
}

bool AES67Receiver::is_healthy() const {
    if (!active_) {
        return true; // Inactive receivers are considered healthy
    }
    
    // Check if pipeline is running
    if (pipeline_) {
        GstState state;
        gst_element_get_state(pipeline_, &state, nullptr, 0);
        if (state != GST_STATE_PLAYING) {
            return false;
        }
    }
    
    // Check PipeWire bridge
    if (pipewire_bridge_ && !pipewire_bridge_->is_connected()) {
        return false;
    }
    
    return true;
}

void AES67Receiver::recover() {
    Logger::info("Attempting to recover receiver " + config_.id);
    
    // Try to restart the pipeline
    if (pipeline_) {
        gst_element_set_state(pipeline_, GST_STATE_NULL);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        gst_element_set_state(pipeline_, GST_STATE_PLAYING);
    }
    
    // Reconnect PipeWire if needed
    if (pipewire_bridge_ && !pipewire_bridge_->is_connected()) {
        pipewire_bridge_->reconnect();
    }
}

AES67Receiver::Statistics AES67Receiver::get_statistics() const {
    return stats_;
}