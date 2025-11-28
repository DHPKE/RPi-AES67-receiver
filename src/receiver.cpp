// SPDX-License-Identifier: MIT
/*
 * RPi-AES67 - Professional AES67 Sender/Receiver for Raspberry Pi 5
 * Copyright (c) 2025 DHPKE
 *
 * AES67 Receiver implementation.
 */

#include "rpi_aes67/receiver.h"
#include "rpi_aes67/logger.h"
#include <thread>
#include <mutex>
#include <condition_variable>
#include <sstream>
#include <regex>
#include <cstring>
#include <queue>
#include <map>

#ifdef __linux__
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <poll.h>
#endif

namespace rpi_aes67 {

// RTP header structure
struct RTPHeader {
    uint8_t cc:4;
    uint8_t x:1;
    uint8_t p:1;
    uint8_t v:2;
    uint8_t pt:7;
    uint8_t m:1;
    uint16_t seq;
    uint32_t ts;
    uint32_t ssrc;
};

// ==================== JitterBuffer::Impl ====================

class JitterBuffer::Impl {
public:
    Impl() = default;
    explicit Impl(const Config& config) : config_(config) {}
    
    bool push(const uint8_t* data, size_t size, uint16_t sequence, uint32_t timestamp) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        if (packets_.size() >= config_.max_packets) {
            // Buffer full, drop oldest
            packets_.erase(packets_.begin());
        }
        
        Packet pkt;
        pkt.data.assign(data, data + size);
        pkt.sequence = sequence;
        pkt.timestamp = timestamp;
        pkt.arrival_time = std::chrono::steady_clock::now();
        
        // Insert in order by timestamp
        auto it = packets_.begin();
        while (it != packets_.end() && it->timestamp < timestamp) {
            ++it;
        }
        packets_.insert(it, std::move(pkt));
        
        return true;
    }
    
    bool pop(uint8_t* data, size_t max_size, size_t& size, uint32_t& timestamp) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        if (packets_.empty()) {
            return false;
        }
        
        // Check if we should wait for more packets (jitter buffer delay)
        auto now = std::chrono::steady_clock::now();
        auto first_arrival = packets_.front().arrival_time;
        auto delay = std::chrono::duration_cast<std::chrono::milliseconds>(
            now - first_arrival).count();
        
        if (delay < config_.target_delay_ms && packets_.size() < 3) {
            return false;  // Wait for more buffering
        }
        
        const auto& pkt = packets_.front();
        size = std::min(max_size, pkt.data.size());
        std::memcpy(data, pkt.data.data(), size);
        timestamp = pkt.timestamp;
        
        packets_.erase(packets_.begin());
        return true;
    }
    
    double get_level() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return static_cast<double>(packets_.size()) / config_.max_packets;
    }
    
    double get_latency_ms() const {
        std::lock_guard<std::mutex> lock(mutex_);
        if (packets_.empty()) return 0.0;
        
        auto now = std::chrono::steady_clock::now();
        auto delay = std::chrono::duration_cast<std::chrono::milliseconds>(
            now - packets_.front().arrival_time).count();
        return static_cast<double>(delay);
    }
    
    void reset() {
        std::lock_guard<std::mutex> lock(mutex_);
        packets_.clear();
    }
    
private:
    struct Packet {
        std::vector<uint8_t> data;
        uint16_t sequence;
        uint32_t timestamp;
        std::chrono::steady_clock::time_point arrival_time;
    };
    
    Config config_;
    mutable std::mutex mutex_;
    std::vector<Packet> packets_;
};

// ==================== JitterBuffer ====================

JitterBuffer::JitterBuffer() : impl_(std::make_unique<Impl>()) {}
JitterBuffer::JitterBuffer(const Config& config) : impl_(std::make_unique<Impl>(config)) {}
JitterBuffer::~JitterBuffer() = default;

bool JitterBuffer::push(const uint8_t* data, size_t size, uint16_t sequence, uint32_t timestamp) {
    return impl_->push(data, size, sequence, timestamp);
}

bool JitterBuffer::pop(uint8_t* data, size_t max_size, size_t& size, uint32_t& timestamp) {
    return impl_->pop(data, max_size, size, timestamp);
}

double JitterBuffer::get_level() const { return impl_->get_level(); }
double JitterBuffer::get_latency_ms() const { return impl_->get_latency_ms(); }
void JitterBuffer::reset() { impl_->reset(); }

// ==================== SDPParser ====================

SDPInfo SDPParser::parse(const std::string& sdp) {
    SDPInfo info;
    std::istringstream stream(sdp);
    std::string line;
    
    while (std::getline(stream, line)) {
        // Remove carriage return if present
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        
        if (line.empty()) continue;
        
        // Session name
        if (line.substr(0, 2) == "s=") {
            info.session_name = line.substr(2);
        }
        // Origin
        else if (line.substr(0, 2) == "o=") {
            // o=<username> <session-id> <session-version> IN IP4 <address>
            std::regex origin_regex(R"(o=\S+\s+(\d+)\s+\d+\s+IN\s+IP4\s+(\S+))");
            std::smatch matches;
            if (std::regex_search(line, matches, origin_regex)) {
                info.session_id = matches[1];
                info.origin_address = matches[2];
            }
        }
        // Connection information
        else if (line.substr(0, 2) == "c=") {
            std::regex conn_regex(R"(c=IN\s+IP4\s+([0-9.]+))");
            std::smatch matches;
            if (std::regex_search(line, matches, conn_regex)) {
                info.source_ip = matches[1];
            }
        }
        // Media description
        else if (line.substr(0, 2) == "m=") {
            std::regex media_regex(R"(m=audio\s+(\d+)\s+RTP/AVP\s+(\d+))");
            std::smatch matches;
            if (std::regex_search(line, matches, media_regex)) {
                info.port = static_cast<uint16_t>(std::stoi(matches[1]));
                info.payload_type = static_cast<uint8_t>(std::stoi(matches[2]));
            }
        }
        // RTP map
        else if (line.substr(0, 9) == "a=rtpmap:") {
            std::regex rtpmap_regex(R"(a=rtpmap:(\d+)\s+(\w+)/(\d+)/(\d+))");
            std::smatch matches;
            if (std::regex_search(line, matches, rtpmap_regex)) {
                info.encoding = matches[2];
                info.format.sample_rate = static_cast<uint32_t>(std::stoi(matches[3]));
                info.format.channels = static_cast<uint8_t>(std::stoi(matches[4]));
                
                // Determine bit depth from encoding
                if (info.encoding == "L16") {
                    info.format.bit_depth = 16;
                } else if (info.encoding == "L24") {
                    info.format.bit_depth = 24;
                } else if (info.encoding == "L32") {
                    info.format.bit_depth = 32;
                }
            }
        }
        // Packet time
        else if (line.substr(0, 8) == "a=ptime:") {
            double ptime = std::stod(line.substr(8));
            info.packet_time_us = static_cast<uint32_t>(ptime * 1000);
        }
        // PTP clock reference
        else if (line.substr(0, 12) == "a=ts-refclk:") {
            if (line.find("ptp=IEEE1588") != std::string::npos) {
                // Extract PTP clock ID if present
                std::regex ptp_regex(R"(ptp=IEEE1588-\d+:([0-9A-Fa-f:-]+))");
                std::smatch matches;
                if (std::regex_search(line, matches, ptp_regex)) {
                    info.ptp_clock_id = matches[1];
                }
            }
        }
    }
    
    // Validate
    info.is_valid = !info.source_ip.empty() && info.port > 0 && 
                    info.format.sample_rate > 0 && info.format.channels > 0;
    
    return info;
}

bool SDPParser::validate_aes67(const SDPInfo& info) {
    if (!info.is_valid) return false;
    
    // AES67 requirements:
    // - Sample rate: 48000 Hz (mandatory), 96000 Hz, 44100 Hz also allowed
    // - Bit depth: 16, 24, or 32 bit linear PCM
    // - Packet time: 1ms (mandatory), 125µs, 250µs, 333µs, 4ms also allowed
    
    bool valid_sample_rate = info.format.sample_rate == 44100 ||
                            info.format.sample_rate == 48000 ||
                            info.format.sample_rate == 96000;
    
    bool valid_bit_depth = info.format.bit_depth == 16 ||
                          info.format.bit_depth == 24 ||
                          info.format.bit_depth == 32;
    
    bool valid_encoding = info.encoding == "L16" ||
                         info.encoding == "L24" ||
                         info.encoding == "L32";
    
    return valid_sample_rate && valid_bit_depth && valid_encoding;
}

AudioFormat SDPParser::extract_format(const SDPInfo& info) {
    return info.format;
}

// ==================== AES67Receiver::Impl ====================

class AES67Receiver::Impl {
public:
    Impl() = default;
    ~Impl() { stop(); }
    
    bool configure(const ReceiverConfig& config) {
        config_ = config;
        LOG_INFO("Receiver {} configured: {}", config_.id, config_.label);
        return true;
    }
    
    bool configure(const ReceiverConfig& config, const AudioProcessingConfig& audio_config) {
        config_ = config;
        audio_config_ = audio_config;
        
        // Configure jitter buffer
        JitterBuffer::Config jb_config;
        jb_config.target_delay_ms = static_cast<uint32_t>(audio_config.jitter_buffer_ms);
        jb_config.min_delay_ms = static_cast<uint32_t>(audio_config.buffer_size_ms);
        jb_config.max_delay_ms = jb_config.target_delay_ms * 5;
        jitter_buffer_ = std::make_unique<JitterBuffer>(jb_config);
        
        LOG_INFO("Receiver {} configured with jitter buffer {}ms", 
                 config_.id, audio_config.jitter_buffer_ms);
        return true;
    }
    
    void set_audio_sink(std::shared_ptr<PipeWireOutput> sink) {
        audio_sink_ = std::move(sink);
    }
    
    void set_ptp_sync(std::shared_ptr<PTPSync> ptp) {
        ptp_sync_ = std::move(ptp);
    }
    
    bool initialize() {
        if (initialized_) return true;
        
        // Initialize jitter buffer if not configured
        if (!jitter_buffer_) {
            jitter_buffer_ = std::make_unique<JitterBuffer>();
        }
        
        // Initialize audio sink if provided
        if (audio_sink_) {
            if (!audio_sink_->initialize()) {
                LOG_ERROR("Failed to initialize audio sink");
                return false;
            }
        }
        
        initialized_ = true;
        state_ = ReceiverState::Stopped;
        LOG_INFO("Receiver {} initialized", config_.id);
        return true;
    }
    
    bool connect(const std::string& sdp) {
        sdp_info_ = SDPParser::parse(sdp);
        
        if (!sdp_info_.is_valid) {
            LOG_ERROR("Invalid SDP");
            return false;
        }
        
        LOG_INFO("Parsed SDP: {}:{} {}ch {}Hz", 
                 sdp_info_.source_ip, sdp_info_.port,
                 sdp_info_.format.channels, sdp_info_.format.sample_rate);
        
        return connect_internal();
    }
    
    bool connect(const std::string& source_ip, uint16_t port, const AudioFormat& format) {
        sdp_info_.source_ip = source_ip;
        sdp_info_.port = port;
        sdp_info_.format = format.is_valid() ? format : AudioFormat{};
        sdp_info_.is_valid = true;
        
        return connect_internal();
    }
    
    void disconnect() {
        if (state_ == ReceiverState::Receiving) {
            stop();
        }
        
#ifdef __linux__
        if (socket_fd_ >= 0) {
            close(socket_fd_);
            socket_fd_ = -1;
        }
#endif
        
        connected_ = false;
        state_ = ReceiverState::Stopped;
        LOG_INFO("Receiver {} disconnected", config_.id);
    }
    
    bool start() {
        if (!connected_) {
            LOG_ERROR("Receiver not connected");
            return false;
        }
        
        if (state_ == ReceiverState::Receiving) return true;
        
        // Open audio sink
        if (audio_sink_ && sdp_info_.format.is_valid()) {
            if (!audio_sink_->open(config_.pipewire_sink, sdp_info_.format)) {
                LOG_ERROR("Failed to open audio sink");
                return false;
            }
            audio_sink_->start();
        }
        
        // Start receive thread
        running_ = true;
        receive_thread_ = std::thread([this]() { receive_loop(); });
        
        // Start playout thread
        playout_thread_ = std::thread([this]() { playout_loop(); });
        
        state_ = ReceiverState::Receiving;
        stats_.start_time = std::chrono::steady_clock::now();
        
        LOG_INFO("Receiver {} started", config_.id);
        notify_state_change();
        return true;
    }
    
    void stop() {
        if (state_ != ReceiverState::Receiving) return;
        
        running_ = false;
        
        if (receive_thread_.joinable()) {
            receive_thread_.join();
        }
        
        if (playout_thread_.joinable()) {
            playout_thread_.join();
        }
        
        if (audio_sink_) {
            audio_sink_->stop();
        }
        
        jitter_buffer_->reset();
        
        state_ = ReceiverState::Listening;
        LOG_INFO("Receiver {} stopped", config_.id);
        notify_state_change();
    }
    
    bool is_running() const { return state_ == ReceiverState::Receiving; }
    bool is_connected() const { return connected_; }
    ReceiverState get_state() const { return state_; }
    
    std::string get_id() const { return config_.id; }
    std::string get_label() const { return config_.label; }
    ReceiverConfig get_config() const { return config_; }
    ReceiverStatistics get_statistics() const { return stats_; }
    AudioFormat get_audio_format() const { return sdp_info_.format; }
    SDPInfo get_sdp_info() const { return sdp_info_; }
    std::string get_sender_id() const { return sender_id_; }
    
    void set_state_callback(std::function<void(ReceiverState)> callback) {
        state_callback_ = std::move(callback);
    }
    
    bool is_healthy() const {
        if (state_ != ReceiverState::Receiving) return true;
        
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
            now - stats_.last_packet_time).count();
        
        return elapsed < 5;
    }
    
    void recover() {
        LOG_INFO("Attempting to recover receiver {}", config_.id);
        stop();
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        start();
    }
    
private:
    bool connect_internal() {
#ifdef __linux__
        // Create UDP socket
        socket_fd_ = socket(AF_INET, SOCK_DGRAM, 0);
        if (socket_fd_ < 0) {
            LOG_ERROR("Failed to create socket");
            return false;
        }
        
        // Allow address reuse
        int reuse = 1;
        setsockopt(socket_fd_, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
        
        // Bind to port
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(sdp_info_.port);
        addr.sin_addr.s_addr = INADDR_ANY;
        
        if (bind(socket_fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
            LOG_ERROR("Failed to bind socket to port {}", sdp_info_.port);
            close(socket_fd_);
            socket_fd_ = -1;
            return false;
        }
        
        // Join multicast group if multicast address
        unsigned long ip = ntohl(inet_addr(sdp_info_.source_ip.c_str()));
        if ((ip & 0xF0000000) == 0xE0000000) {  // 224.0.0.0 - 239.255.255.255
            struct ip_mreq mreq{};
            inet_pton(AF_INET, sdp_info_.source_ip.c_str(), &mreq.imr_multiaddr);
            mreq.imr_interface.s_addr = INADDR_ANY;
            
            if (setsockopt(socket_fd_, IPPROTO_IP, IP_ADD_MEMBERSHIP, 
                          &mreq, sizeof(mreq)) < 0) {
                LOG_WARNING("Failed to join multicast group {}", sdp_info_.source_ip);
            }
        }
        
        // Set receive buffer size
        int bufsize = 2 * 1024 * 1024;  // 2MB
        setsockopt(socket_fd_, SOL_SOCKET, SO_RCVBUF, &bufsize, sizeof(bufsize));
#endif
        
        connected_ = true;
        state_ = ReceiverState::Listening;
        LOG_INFO("Receiver {} connected to {}:{}", 
                 config_.id, sdp_info_.source_ip, sdp_info_.port);
        return true;
    }
    
    void receive_loop() {
        std::vector<uint8_t> buffer(65536);
        
        while (running_) {
#ifdef __linux__
            pollfd pfd{};
            pfd.fd = socket_fd_;
            pfd.events = POLLIN;
            
            int ret = poll(&pfd, 1, 100);  // 100ms timeout
            if (ret <= 0) continue;
            
            ssize_t received = recv(socket_fd_, buffer.data(), buffer.size(), 0);
            if (received <= 0) continue;
            
            process_rtp_packet(buffer.data(), received);
#endif
        }
    }
    
    void process_rtp_packet(const uint8_t* data, size_t size) {
        if (size < sizeof(RTPHeader)) return;
        
        const RTPHeader* header = reinterpret_cast<const RTPHeader*>(data);
        
        // Verify RTP version
        if (header->v != 2) return;
        
        uint16_t sequence = ntohs(header->seq);
        uint32_t timestamp = ntohl(header->ts);
        
        // Extract payload
        size_t header_size = sizeof(RTPHeader) + (header->cc * 4);
        if (header->x) {
            // Handle extension header
            if (size < header_size + 4) return;
            uint16_t ext_length = ntohs(*reinterpret_cast<const uint16_t*>(data + header_size + 2));
            header_size += 4 + ext_length * 4;
        }
        
        if (size <= header_size) return;
        
        const uint8_t* payload = data + header_size;
        size_t payload_size = size - header_size;
        
        // Add to jitter buffer
        jitter_buffer_->push(payload, payload_size, sequence, timestamp);
        
        // Update statistics
        stats_.packets_received++;
        stats_.bytes_received += size;
        stats_.last_sequence_number = sequence;
        stats_.last_rtp_timestamp = timestamp;
        stats_.last_packet_time = std::chrono::steady_clock::now();
        stats_.buffer_level = jitter_buffer_->get_level();
        
        // Check for packet loss
        if (last_sequence_valid_) {
            int16_t diff = static_cast<int16_t>(sequence - last_sequence_ - 1);
            if (diff > 0) {
                stats_.packets_lost += diff;
            } else if (diff < -1) {
                stats_.packets_out_of_order++;
            }
        }
        last_sequence_ = sequence;
        last_sequence_valid_ = true;
    }
    
    void playout_loop() {
        std::vector<uint8_t> buffer(8192);
        
        while (running_) {
            size_t size;
            uint32_t timestamp;
            
            if (jitter_buffer_->pop(buffer.data(), buffer.size(), size, timestamp)) {
                // Send to audio output
                if (audio_sink_) {
                    audio_sink_->write(buffer.data(), size);
                }
            } else {
                // No data available, sleep briefly
                std::this_thread::sleep_for(std::chrono::microseconds(500));
            }
        }
    }
    
    void notify_state_change() {
        if (state_callback_) {
            state_callback_(state_);
        }
    }
    
    ReceiverConfig config_;
    AudioProcessingConfig audio_config_;
    SDPInfo sdp_info_;
    
    bool initialized_ = false;
    std::atomic<bool> running_{false};
    std::atomic<bool> connected_{false};
    ReceiverState state_ = ReceiverState::Stopped;
    
    std::shared_ptr<PipeWireOutput> audio_sink_;
    std::shared_ptr<PTPSync> ptp_sync_;
    std::unique_ptr<JitterBuffer> jitter_buffer_;
    
    std::string sender_id_;
    
#ifdef __linux__
    int socket_fd_ = -1;
#endif
    
    std::thread receive_thread_;
    std::thread playout_thread_;
    
    ReceiverStatistics stats_{};
    std::function<void(ReceiverState)> state_callback_;
    
    uint16_t last_sequence_ = 0;
    bool last_sequence_valid_ = false;
};

// ==================== AES67Receiver ====================

AES67Receiver::AES67Receiver() : impl_(std::make_unique<Impl>()) {}
AES67Receiver::~AES67Receiver() = default;

bool AES67Receiver::configure(const ReceiverConfig& config) { return impl_->configure(config); }
bool AES67Receiver::configure(const ReceiverConfig& config, const AudioProcessingConfig& audio_config) {
    return impl_->configure(config, audio_config);
}
void AES67Receiver::set_audio_sink(std::shared_ptr<PipeWireOutput> sink) { impl_->set_audio_sink(std::move(sink)); }
void AES67Receiver::set_ptp_sync(std::shared_ptr<PTPSync> ptp) { impl_->set_ptp_sync(std::move(ptp)); }
bool AES67Receiver::initialize() { return impl_->initialize(); }
bool AES67Receiver::connect(const std::string& sdp) { return impl_->connect(sdp); }
bool AES67Receiver::connect(const std::string& source_ip, uint16_t port, const AudioFormat& format) {
    return impl_->connect(source_ip, port, format);
}
void AES67Receiver::disconnect() { impl_->disconnect(); }
bool AES67Receiver::start() { return impl_->start(); }
void AES67Receiver::stop() { impl_->stop(); }
bool AES67Receiver::is_running() const { return impl_->is_running(); }
bool AES67Receiver::is_connected() const { return impl_->is_connected(); }
ReceiverState AES67Receiver::get_state() const { return impl_->get_state(); }
std::string AES67Receiver::get_id() const { return impl_->get_id(); }
std::string AES67Receiver::get_label() const { return impl_->get_label(); }
ReceiverConfig AES67Receiver::get_config() const { return impl_->get_config(); }
ReceiverStatistics AES67Receiver::get_statistics() const { return impl_->get_statistics(); }
AudioFormat AES67Receiver::get_audio_format() const { return impl_->get_audio_format(); }
SDPInfo AES67Receiver::get_sdp_info() const { return impl_->get_sdp_info(); }
std::string AES67Receiver::get_sender_id() const { return impl_->get_sender_id(); }
void AES67Receiver::register_with_nmos(std::shared_ptr<NMOSNode> /*node*/) { /* TODO */ }
void AES67Receiver::unregister_from_nmos() { /* TODO */ }
void AES67Receiver::set_state_callback(StateCallback callback) { impl_->set_state_callback(std::move(callback)); }
bool AES67Receiver::is_healthy() const { return impl_->is_healthy(); }
void AES67Receiver::recover() { impl_->recover(); }

}  // namespace rpi_aes67
