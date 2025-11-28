// SPDX-License-Identifier: MIT
/*
 * RPi-AES67 - Professional AES67 Sender/Receiver for Raspberry Pi 5
 * Copyright (c) 2025 DHPKE
 *
 * AES67 Sender implementation.
 */

#include "rpi_aes67/sender.h"
#include "rpi_aes67/logger.h"
#include <thread>
#include <mutex>
#include <condition_variable>
#include <sstream>
#include <iomanip>
#include <cstring>
#include <random>

#ifdef __linux__
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#endif

namespace rpi_aes67 {

// RTP header structure
struct RTPHeader {
    uint8_t cc:4;       // CSRC count
    uint8_t x:1;        // Extension flag
    uint8_t p:1;        // Padding flag
    uint8_t v:2;        // Version (2)
    uint8_t pt:7;       // Payload type
    uint8_t m:1;        // Marker bit
    uint16_t seq;       // Sequence number
    uint32_t ts;        // Timestamp
    uint32_t ssrc;      // Synchronization source
};

// ==================== AES67Sender::Impl ====================

class AES67Sender::Impl {
public:
    Impl() = default;
    ~Impl() { stop(); }
    
    bool configure(const SenderConfig& config) {
        config_ = config;
        
        format_.sample_rate = config.sample_rate;
        format_.channels = config.channels;
        format_.bit_depth = config.bit_depth;
        
        // Generate random SSRC
        std::random_device rd;
        ssrc_ = rd();
        
        LOG_INFO("Sender {} configured: {}ch {}Hz {}bit -> {}:{}", 
                config_.id, config_.channels, config_.sample_rate, 
                config_.bit_depth, config_.multicast_ip, config_.port);
        
        return true;
    }
    
    void set_audio_source(std::shared_ptr<PipeWireInput> source) {
        audio_source_ = std::move(source);
    }
    
    void set_ptp_sync(std::shared_ptr<PTPSync> ptp) {
        ptp_sync_ = std::move(ptp);
    }
    
    bool initialize() {
        if (initialized_) return true;
        
        // Initialize audio source if provided
        if (audio_source_) {
            if (!audio_source_->initialize()) {
                LOG_ERROR("Failed to initialize audio source");
                return false;
            }
            
            if (!audio_source_->open(config_.pipewire_source, format_)) {
                LOG_ERROR("Failed to open audio source");
                return false;
            }
            
            // Set callback for audio data
            audio_source_->set_callback([this](const AudioBuffer& buffer) {
                on_audio_data(buffer);
            });
        }
        
        initialized_ = true;
        state_ = SenderState::Stopped;
        LOG_INFO("Sender {} initialized", config_.id);
        return true;
    }
    
    bool start() {
        if (state_ == SenderState::Running) return true;
        if (!initialized_) {
            if (!initialize()) return false;
        }
        
        // Create UDP socket
#ifdef __linux__
        socket_fd_ = socket(AF_INET, SOCK_DGRAM, 0);
        if (socket_fd_ < 0) {
            LOG_ERROR("Failed to create socket");
            return false;
        }
        
        // Enable multicast
        int ttl = 32;
        setsockopt(socket_fd_, IPPROTO_IP, IP_MULTICAST_TTL, &ttl, sizeof(ttl));
        
        // Set destination address
        memset(&dest_addr_, 0, sizeof(dest_addr_));
        dest_addr_.sin_family = AF_INET;
        dest_addr_.sin_port = htons(config_.port);
        inet_pton(AF_INET, config_.multicast_ip.c_str(), &dest_addr_.sin_addr);
#endif
        
        // Start audio source
        if (audio_source_) {
            audio_source_->start();
        }
        
        // Start transmission thread
        running_ = true;
        state_ = SenderState::Running;
        stats_.start_time = std::chrono::steady_clock::now();
        
        LOG_INFO("Sender {} started", config_.id);
        notify_state_change();
        
        return true;
    }
    
    void stop() {
        if (state_ != SenderState::Running) return;
        
        running_ = false;
        
        if (audio_source_) {
            audio_source_->stop();
        }
        
#ifdef __linux__
        if (socket_fd_ >= 0) {
            close(socket_fd_);
            socket_fd_ = -1;
        }
#endif
        
        state_ = SenderState::Stopped;
        LOG_INFO("Sender {} stopped", config_.id);
        notify_state_change();
    }
    
    bool is_running() const { return state_ == SenderState::Running; }
    SenderState get_state() const { return state_; }
    
    std::string generate_sdp() const {
        return SDPGenerator::generate(config_, session_id_, origin_address_);
    }
    
    std::string get_id() const { return config_.id; }
    std::string get_label() const { return config_.label; }
    SenderConfig get_config() const { return config_; }
    SenderStatistics get_statistics() const { return stats_; }
    AudioFormat get_audio_format() const { return format_; }
    std::string get_multicast_ip() const { return config_.multicast_ip; }
    uint16_t get_port() const { return config_.port; }
    
    void set_state_callback(std::function<void(SenderState)> callback) {
        state_callback_ = std::move(callback);
    }
    
    bool is_healthy() const {
        if (state_ != SenderState::Running) return true;
        
        // Check for recent activity
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
            now - stats_.last_packet_time).count();
        
        return elapsed < 5;  // Healthy if packets sent within 5 seconds
    }
    
    void recover() {
        LOG_INFO("Attempting to recover sender {}", config_.id);
        stop();
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        start();
    }
    
private:
    void on_audio_data(const AudioBuffer& buffer) {
        if (!running_) return;
        
        // Calculate samples per packet based on packet time
        uint32_t samples_per_packet = (config_.sample_rate * config_.packet_time_us) / 1000000;
        size_t bytes_per_packet = samples_per_packet * format_.bytes_per_frame();
        
        // Get RTP timestamp
        uint32_t rtp_timestamp;
        if (ptp_sync_ && ptp_sync_->is_synchronized()) {
            rtp_timestamp = ptp_sync_->get_rtp_timestamp(config_.sample_rate);
        } else {
            // Use local timestamp
            rtp_timestamp = stats_.rtp_timestamp;
        }
        
        // Send packets
        const uint8_t* data = buffer.data;
        size_t remaining = buffer.size;
        
        while (remaining >= bytes_per_packet) {
            send_rtp_packet(data, bytes_per_packet, rtp_timestamp);
            
            data += bytes_per_packet;
            remaining -= bytes_per_packet;
            rtp_timestamp += samples_per_packet;
        }
        
        stats_.rtp_timestamp = rtp_timestamp;
    }
    
    void send_rtp_packet(const uint8_t* data, size_t size, uint32_t timestamp) {
        // Build RTP packet
        std::vector<uint8_t> packet(sizeof(RTPHeader) + size);
        
        RTPHeader* header = reinterpret_cast<RTPHeader*>(packet.data());
        header->v = 2;
        header->p = 0;
        header->x = 0;
        header->cc = 0;
        header->m = 0;
        header->pt = config_.payload_type;
        header->seq = htons(static_cast<uint16_t>(stats_.sequence_number++));
        header->ts = htonl(timestamp);
        header->ssrc = htonl(ssrc_);
        
        // Copy audio data
        std::memcpy(packet.data() + sizeof(RTPHeader), data, size);
        
        // Send packet
#ifdef __linux__
        if (socket_fd_ >= 0) {
            ssize_t sent = sendto(socket_fd_, packet.data(), packet.size(), 0,
                                  reinterpret_cast<sockaddr*>(&dest_addr_),
                                  sizeof(dest_addr_));
            if (sent > 0) {
                stats_.packets_sent++;
                stats_.bytes_sent += sent;
                stats_.last_packet_time = std::chrono::steady_clock::now();
            }
        }
#endif
    }
    
    void notify_state_change() {
        if (state_callback_) {
            state_callback_(state_);
        }
    }
    
    SenderConfig config_;
    AudioFormat format_;
    bool initialized_ = false;
    std::atomic<bool> running_{false};
    SenderState state_ = SenderState::Stopped;
    
    std::shared_ptr<PipeWireInput> audio_source_;
    std::shared_ptr<PTPSync> ptp_sync_;
    
    uint32_t ssrc_ = 0;
    uint64_t session_id_ = 0;
    std::string origin_address_ = "0.0.0.0";
    
#ifdef __linux__
    int socket_fd_ = -1;
    sockaddr_in dest_addr_{};
#endif
    
    SenderStatistics stats_{};
    std::function<void(SenderState)> state_callback_;
};

// ==================== AES67Sender ====================

AES67Sender::AES67Sender() : impl_(std::make_unique<Impl>()) {}
AES67Sender::~AES67Sender() = default;

bool AES67Sender::configure(const SenderConfig& config) { return impl_->configure(config); }
void AES67Sender::set_audio_source(std::shared_ptr<PipeWireInput> source) { impl_->set_audio_source(std::move(source)); }
void AES67Sender::set_ptp_sync(std::shared_ptr<PTPSync> ptp) { impl_->set_ptp_sync(std::move(ptp)); }
bool AES67Sender::initialize() { return impl_->initialize(); }
bool AES67Sender::start() { return impl_->start(); }
void AES67Sender::stop() { impl_->stop(); }
bool AES67Sender::is_running() const { return impl_->is_running(); }
SenderState AES67Sender::get_state() const { return impl_->get_state(); }
std::string AES67Sender::generate_sdp() const { return impl_->generate_sdp(); }
std::string AES67Sender::get_id() const { return impl_->get_id(); }
std::string AES67Sender::get_label() const { return impl_->get_label(); }
SenderConfig AES67Sender::get_config() const { return impl_->get_config(); }
SenderStatistics AES67Sender::get_statistics() const { return impl_->get_statistics(); }
AudioFormat AES67Sender::get_audio_format() const { return impl_->get_audio_format(); }
std::string AES67Sender::get_multicast_ip() const { return impl_->get_multicast_ip(); }
uint16_t AES67Sender::get_port() const { return impl_->get_port(); }
void AES67Sender::register_with_nmos(std::shared_ptr<NMOSNode> /*node*/) { /* TODO */ }
void AES67Sender::unregister_from_nmos() { /* TODO */ }
void AES67Sender::set_state_callback(StateCallback callback) { impl_->set_state_callback(std::move(callback)); }
bool AES67Sender::is_healthy() const { return impl_->is_healthy(); }
void AES67Sender::recover() { impl_->recover(); }

// ==================== SDPGenerator ====================

std::string SDPGenerator::generate(
    const SenderConfig& config,
    uint64_t session_id,
    const std::string& origin_address) {
    
    AudioFormat format;
    format.sample_rate = config.sample_rate;
    format.channels = config.channels;
    format.bit_depth = config.bit_depth;
    
    return generate(config.multicast_ip, config.port, config.payload_type,
                   format, config.label, session_id, origin_address);
}

std::string SDPGenerator::generate(
    const std::string& multicast_ip,
    uint16_t port,
    uint8_t payload_type,
    const AudioFormat& format,
    const std::string& session_name,
    uint64_t session_id,
    const std::string& origin_address) {
    
    std::ostringstream sdp;
    
    // v= Protocol version
    sdp << "v=0\r\n";
    
    // o= Origin
    // o=<username> <session-id> <session-version> <nettype> <addrtype> <address>
    sdp << "o=- " << session_id << " " << session_id 
        << " IN IP4 " << origin_address << "\r\n";
    
    // s= Session name
    sdp << "s=" << session_name << "\r\n";
    
    // c= Connection information
    // c=<nettype> <addrtype> <connection-address>
    sdp << "c=IN IP4 " << multicast_ip << "/32\r\n";
    
    // t= Timing
    sdp << "t=0 0\r\n";
    
    // m= Media description
    // m=<media> <port> <proto> <fmt>
    sdp << "m=audio " << port << " RTP/AVP " << static_cast<int>(payload_type) << "\r\n";
    
    // a=rtpmap
    // a=rtpmap:<payload type> <encoding name>/<clock rate>/<channels>
    sdp << "a=rtpmap:" << static_cast<int>(payload_type) << " "
        << format.encoding_name() << "/" << format.sample_rate 
        << "/" << static_cast<int>(format.channels) << "\r\n";
    
    // a=ptime (AES67 requires 1ms packet time)
    sdp << "a=ptime:1\r\n";
    
    // a=ts-refclk (PTP clock reference for AES67)
    sdp << "a=ts-refclk:ptp=IEEE1588-2008\r\n";
    
    // a=mediaclk
    sdp << "a=mediaclk:direct=0\r\n";
    
    return sdp.str();
}

}  // namespace rpi_aes67
