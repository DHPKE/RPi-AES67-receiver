// SPDX-License-Identifier: MIT
/*
 * RPi-AES67 - Professional AES67 Sender/Receiver for Raspberry Pi 5
 * Copyright (c) 2025 DHPKE
 *
 * NMOS Node implementation - IS-04/IS-05.
 */

#include "rpi_aes67/nmos_node.h"
#include "rpi_aes67/sender.h"
#include "rpi_aes67/receiver.h"
#include "rpi_aes67/logger.h"
#include <thread>
#include <mutex>
#include <condition_variable>
#include <sstream>
#include <iomanip>
#include <random>
#include <map>

// Simple HTTP server using Boost.Beast would be ideal, but for simplicity
// we'll use a basic socket-based implementation for now

#ifdef __linux__
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <poll.h>
#endif

namespace rpi_aes67 {

// ==================== UUIDGenerator ====================

std::string UUIDGenerator::generate() {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<uint32_t> dis(0, 0xFFFFFFFF);
    
    std::stringstream ss;
    ss << std::hex << std::setfill('0');
    ss << std::setw(8) << dis(gen) << "-";
    ss << std::setw(4) << (dis(gen) & 0xFFFF) << "-";
    ss << std::setw(4) << ((dis(gen) & 0x0FFF) | 0x4000) << "-";  // Version 4
    ss << std::setw(4) << ((dis(gen) & 0x3FFF) | 0x8000) << "-";  // Variant
    ss << std::setw(4) << (dis(gen) & 0xFFFF);
    ss << std::setw(8) << dis(gen);
    
    return ss.str();
}

std::string UUIDGenerator::generate_named(const std::string& /*namespace_uuid*/, 
                                          const std::string& /*name*/) {
    // Simplified - just generate a random UUID for now
    return generate();
}

bool UUIDGenerator::is_valid(const std::string& uuid) {
    // Simple validation: check format xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx
    if (uuid.length() != 36) return false;
    
    for (size_t i = 0; i < uuid.length(); ++i) {
        char c = uuid[i];
        if (i == 8 || i == 13 || i == 18 || i == 23) {
            if (c != '-') return false;
        } else {
            if (!std::isxdigit(c)) return false;
        }
    }
    
    return true;
}

// ==================== NMOSNode::Impl ====================

class NMOSNode::Impl {
public:
    Impl() = default;
    ~Impl() { stop(); }
    
    bool initialize(const NodeConfig& config, const NetworkConfig& network) {
        node_config_ = config;
        network_config_ = network;
        
        // Generate IDs if not provided
        if (node_id_.empty()) {
            node_id_ = UUIDGenerator::generate();
        }
        if (device_id_.empty()) {
            device_id_ = UUIDGenerator::generate();
        }
        
        LOG_INFO("NMOS Node initialized: {}", node_config_.label);
        LOG_INFO("Node ID: {}", node_id_);
        LOG_INFO("Device ID: {}", device_id_);
        
        initialized_ = true;
        return true;
    }
    
    bool start() {
        if (state_ == NMOSNodeState::Running) return true;
        
        // Start HTTP server
        if (!start_http_server()) {
            LOG_ERROR("Failed to start HTTP server");
            return false;
        }
        
        state_ = NMOSNodeState::Running;
        
        // Register with registry if configured
        if (!network_config_.registry_url.empty()) {
            register_with_registry();
        }
        
        LOG_INFO("NMOS Node started on port {}", network_config_.node_port);
        notify_state_change();
        return true;
    }
    
    void stop() {
        if (state_ == NMOSNodeState::Stopped) return;
        
        running_ = false;
        
        // Unregister from registry
        if (registered_) {
            unregister_from_registry();
        }
        
        // Stop HTTP server
        stop_http_server();
        
        state_ = NMOSNodeState::Stopped;
        LOG_INFO("NMOS Node stopped");
        notify_state_change();
    }
    
    bool is_running() const { return state_ == NMOSNodeState::Running; }
    NMOSNodeState get_state() const { return state_; }
    
    std::string register_sender(std::shared_ptr<AES67Sender> sender) {
        std::lock_guard<std::mutex> lock(resources_mutex_);
        
        std::string id = sender->get_id();
        if (id.empty()) {
            id = UUIDGenerator::generate();
        }
        
        NMOSSender nmos_sender;
        nmos_sender.id = id;
        nmos_sender.label = sender->get_label();
        nmos_sender.device_id = device_id_;
        nmos_sender.transport = "urn:x-nmos:transport:rtp.mcast";
        
        senders_[id] = nmos_sender;
        sender_objects_[id] = sender;
        
        LOG_INFO("Registered sender: {} ({})", nmos_sender.label, id);
        return id;
    }
    
    void unregister_sender(const std::string& sender_id) {
        std::lock_guard<std::mutex> lock(resources_mutex_);
        
        senders_.erase(sender_id);
        sender_objects_.erase(sender_id);
        
        LOG_INFO("Unregistered sender: {}", sender_id);
    }
    
    std::string register_receiver(std::shared_ptr<AES67Receiver> receiver) {
        std::lock_guard<std::mutex> lock(resources_mutex_);
        
        std::string id = receiver->get_id();
        if (id.empty()) {
            id = UUIDGenerator::generate();
        }
        
        NMOSReceiver nmos_receiver;
        nmos_receiver.id = id;
        nmos_receiver.label = receiver->get_label();
        nmos_receiver.device_id = device_id_;
        nmos_receiver.transport = "urn:x-nmos:transport:rtp.mcast";
        
        receivers_[id] = nmos_receiver;
        receiver_objects_[id] = receiver;
        
        LOG_INFO("Registered receiver: {} ({})", nmos_receiver.label, id);
        return id;
    }
    
    void unregister_receiver(const std::string& receiver_id) {
        std::lock_guard<std::mutex> lock(resources_mutex_);
        
        receivers_.erase(receiver_id);
        receiver_objects_.erase(receiver_id);
        
        LOG_INFO("Unregistered receiver: {}", receiver_id);
    }
    
    std::vector<NMOSSender> get_senders() const {
        std::lock_guard<std::mutex> lock(resources_mutex_);
        std::vector<NMOSSender> result;
        for (const auto& [id, sender] : senders_) {
            result.push_back(sender);
        }
        return result;
    }
    
    std::vector<NMOSReceiver> get_receivers() const {
        std::lock_guard<std::mutex> lock(resources_mutex_);
        std::vector<NMOSReceiver> result;
        for (const auto& [id, receiver] : receivers_) {
            result.push_back(receiver);
        }
        return result;
    }
    
    void enable_registration(const std::string& registry_url) {
        network_config_.registry_url = registry_url;
        if (state_ == NMOSNodeState::Running && !registered_) {
            register_with_registry();
        }
    }
    
    void disable_registration() {
        if (registered_) {
            unregister_from_registry();
        }
        network_config_.registry_url.clear();
    }
    
    bool is_registered() const { return registered_; }
    
    void enable_mdns(bool enable) {
        network_config_.enable_mdns = enable;
        // TODO: Start/stop mDNS announcements
    }
    
    void enable_peer_to_peer() {
        disable_registration();
        // Enable mDNS for peer-to-peer discovery
        enable_mdns(true);
    }
    
    void reregister() {
        if (!network_config_.registry_url.empty()) {
            unregister_from_registry();
            register_with_registry();
        }
    }
    
    void set_connection_callback(ConnectionCallback callback) {
        connection_callback_ = std::move(callback);
    }
    
    ConnectionResponse connect_to_sender(const std::string& /*sender_id*/,
                                         const std::string& receiver_id,
                                         const TransportParams& params) {
        std::lock_guard<std::mutex> lock(resources_mutex_);
        
        ConnectionResponse response;
        
        auto it = receiver_objects_.find(receiver_id);
        if (it == receiver_objects_.end()) {
            response.error_message = "Receiver not found";
            return response;
        }
        
        auto& receiver = it->second;
        
        // Connect using transport params
        std::string source = params.multicast_ip.empty() ? params.source_ip : params.multicast_ip;
        if (receiver->connect(source, params.destination_port)) {
            receiver->start();
            response.success = true;
            response.state = NMOSConnectionState::Active;
            response.active_params = params;
        } else {
            response.error_message = "Failed to connect";
        }
        
        return response;
    }
    
    bool disconnect_receiver(const std::string& receiver_id) {
        std::lock_guard<std::mutex> lock(resources_mutex_);
        
        auto it = receiver_objects_.find(receiver_id);
        if (it == receiver_objects_.end()) {
            return false;
        }
        
        it->second->disconnect();
        return true;
    }
    
    TransportParams get_staged_params(const std::string& receiver_id) const {
        std::lock_guard<std::mutex> lock(resources_mutex_);
        auto it = staged_params_.find(receiver_id);
        if (it != staged_params_.end()) {
            return it->second;
        }
        return TransportParams{};
    }
    
    TransportParams get_active_params(const std::string& receiver_id) const {
        std::lock_guard<std::mutex> lock(resources_mutex_);
        auto it = active_params_.find(receiver_id);
        if (it != active_params_.end()) {
            return it->second;
        }
        return TransportParams{};
    }
    
    bool stage_connection(const std::string& receiver_id, const TransportParams& params) {
        std::lock_guard<std::mutex> lock(resources_mutex_);
        staged_params_[receiver_id] = params;
        return true;
    }
    
    ConnectionResponse activate_connection(const std::string& receiver_id) {
        ConnectionResponse response;
        
        std::lock_guard<std::mutex> lock(resources_mutex_);
        
        auto staged_it = staged_params_.find(receiver_id);
        if (staged_it == staged_params_.end()) {
            response.error_message = "No staged parameters";
            return response;
        }
        
        auto receiver_it = receiver_objects_.find(receiver_id);
        if (receiver_it == receiver_objects_.end()) {
            response.error_message = "Receiver not found";
            return response;
        }
        
        auto& receiver = receiver_it->second;
        const auto& params = staged_it->second;
        
        // Connect
        std::string source = params.multicast_ip.empty() ? params.source_ip : params.multicast_ip;
        if (receiver->connect(source, params.destination_port)) {
            receiver->start();
            active_params_[receiver_id] = params;
            response.success = true;
            response.state = NMOSConnectionState::Active;
            response.active_params = params;
        } else {
            response.error_message = "Failed to activate connection";
        }
        
        return response;
    }
    
    std::string get_node_id() const { return node_id_; }
    std::string get_device_id() const { return device_id_; }
    NodeConfig get_node_config() const { return node_config_; }
    
    std::string get_api_url() const {
        std::ostringstream oss;
        oss << "http://localhost:" << network_config_.node_port << "/x-nmos/node/v1.3";
        return oss.str();
    }
    
    std::string get_health_url() const {
        return get_api_url() + "/health/nodes/" + node_id_;
    }
    
    void set_state_callback(std::function<void(NMOSNodeState)> callback) {
        state_callback_ = std::move(callback);
    }
    
    void set_registration_callback(std::function<void(bool)> callback) {
        registration_callback_ = std::move(callback);
    }

private:
    bool start_http_server() {
#ifdef __linux__
        server_fd_ = socket(AF_INET, SOCK_STREAM, 0);
        if (server_fd_ < 0) {
            LOG_ERROR("Failed to create HTTP server socket");
            return false;
        }
        
        int opt = 1;
        setsockopt(server_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
        
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(network_config_.node_port);
        addr.sin_addr.s_addr = INADDR_ANY;
        
        if (bind(server_fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
            LOG_ERROR("Failed to bind HTTP server to port {}", network_config_.node_port);
            close(server_fd_);
            server_fd_ = -1;
            return false;
        }
        
        if (listen(server_fd_, 10) < 0) {
            LOG_ERROR("Failed to listen on HTTP server socket");
            close(server_fd_);
            server_fd_ = -1;
            return false;
        }
        
        running_ = true;
        server_thread_ = std::thread([this]() { http_server_loop(); });
#endif
        return true;
    }
    
    void stop_http_server() {
#ifdef __linux__
        if (server_fd_ >= 0) {
            shutdown(server_fd_, SHUT_RDWR);
            close(server_fd_);
            server_fd_ = -1;
        }
#endif
        
        if (server_thread_.joinable()) {
            server_thread_.join();
        }
    }
    
    void http_server_loop() {
#ifdef __linux__
        while (running_) {
            pollfd pfd{};
            pfd.fd = server_fd_;
            pfd.events = POLLIN;
            
            int ret = poll(&pfd, 1, 1000);  // 1s timeout
            if (ret <= 0) continue;
            
            sockaddr_in client_addr{};
            socklen_t client_len = sizeof(client_addr);
            
            int client_fd = accept(server_fd_, 
                                   reinterpret_cast<sockaddr*>(&client_addr),
                                   &client_len);
            if (client_fd < 0) continue;
            
            // Handle request in a simple blocking way
            handle_http_request(client_fd);
            
            close(client_fd);
        }
#endif
    }
    
    void handle_http_request(int client_fd) {
        char buffer[4096];
        ssize_t received = recv(client_fd, buffer, sizeof(buffer) - 1, 0);
        if (received <= 0) return;
        
        buffer[received] = '\0';
        std::string request(buffer);
        
        // Parse request line
        std::string method, path;
        std::istringstream iss(request);
        iss >> method >> path;
        
        LOG_DEBUG("HTTP {} {}", method, path);
        
        std::string response;
        
        if (path.find("/x-nmos/node/v1.3") == 0) {
            response = handle_node_api(method, path);
        } else if (path.find("/x-nmos/connection/v1.1") == 0) {
            response = handle_connection_api(method, path, request);
        } else {
            response = "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n\r\n";
        }
        
        send(client_fd, response.c_str(), response.length(), 0);
    }
    
    std::string handle_node_api(const std::string& /*method*/, const std::string& path) {
        std::ostringstream response;
        std::string body;
        
        if (path == "/x-nmos/node/v1.3" || path == "/x-nmos/node/v1.3/") {
            body = R"(["self/", "senders/", "receivers/", "devices/", "sources/", "flows/"])";
        } else if (path == "/x-nmos/node/v1.3/self") {
            body = generate_self_json();
        } else if (path.find("/x-nmos/node/v1.3/senders") == 0) {
            body = generate_senders_json();
        } else if (path.find("/x-nmos/node/v1.3/receivers") == 0) {
            body = generate_receivers_json();
        } else {
            response << "HTTP/1.1 404 Not Found\r\n";
            response << "Content-Length: 0\r\n\r\n";
            return response.str();
        }
        
        response << "HTTP/1.1 200 OK\r\n";
        response << "Content-Type: application/json\r\n";
        response << "Content-Length: " << body.length() << "\r\n";
        response << "\r\n";
        response << body;
        
        return response.str();
    }
    
    std::string handle_connection_api(const std::string& method, 
                                      const std::string& path,
                                      const std::string& request) {
        std::ostringstream response;
        std::string body;
        
        if (method == "PATCH" && path.find("/staged") != std::string::npos) {
            // Handle connection staging
            // Extract receiver ID from path
            // Parse JSON body and stage connection
            
            // Find JSON body
            size_t body_start = request.find("\r\n\r\n");
            if (body_start != std::string::npos) {
                std::string json_body = request.substr(body_start + 4);
                // TODO: Parse JSON and stage connection
            }
            
            body = R"({"master_enable": true})";
        } else {
            body = "{}";
        }
        
        response << "HTTP/1.1 200 OK\r\n";
        response << "Content-Type: application/json\r\n";
        response << "Content-Length: " << body.length() << "\r\n";
        response << "\r\n";
        response << body;
        
        return response.str();
    }
    
    std::string generate_self_json() const {
        std::ostringstream json;
        json << "{";
        json << "\"id\": \"" << node_id_ << "\",";
        json << "\"label\": \"" << node_config_.label << "\",";
        json << "\"description\": \"" << node_config_.description << "\",";
        json << "\"version\": \"v1.3\",";
        json << "\"hostname\": \"rpi5-aes67\",";
        json << "\"api\": {\"versions\": [\"v1.0\", \"v1.1\", \"v1.2\", \"v1.3\"]},";
        json << "\"services\": [],";
        json << "\"clocks\": [{\"name\": \"clk0\", \"ref_type\": \"ptp\"}],";
        json << "\"interfaces\": [{\"name\": \"" << network_config_.interface << "\"}]";
        json << "}";
        return json.str();
    }
    
    std::string generate_senders_json() const {
        std::lock_guard<std::mutex> lock(resources_mutex_);
        
        std::ostringstream json;
        json << "[";
        
        bool first = true;
        for (const auto& [id, sender] : senders_) {
            if (!first) json << ",";
            first = false;
            
            json << "{";
            json << "\"id\": \"" << sender.id << "\",";
            json << "\"label\": \"" << sender.label << "\",";
            json << "\"device_id\": \"" << sender.device_id << "\",";
            json << "\"transport\": \"" << sender.transport << "\"";
            json << "}";
        }
        
        json << "]";
        return json.str();
    }
    
    std::string generate_receivers_json() const {
        std::lock_guard<std::mutex> lock(resources_mutex_);
        
        std::ostringstream json;
        json << "[";
        
        bool first = true;
        for (const auto& [id, receiver] : receivers_) {
            if (!first) json << ",";
            first = false;
            
            json << "{";
            json << "\"id\": \"" << receiver.id << "\",";
            json << "\"label\": \"" << receiver.label << "\",";
            json << "\"device_id\": \"" << receiver.device_id << "\",";
            json << "\"transport\": \"" << receiver.transport << "\"";
            json << "}";
        }
        
        json << "]";
        return json.str();
    }
    
    void register_with_registry() {
        // TODO: Implement IS-04 registration
        LOG_INFO("Registering with NMOS registry: {}", network_config_.registry_url);
        registered_ = true;
        state_ = NMOSNodeState::Registered;
        
        if (registration_callback_) {
            registration_callback_(true);
        }
    }
    
    void unregister_from_registry() {
        // TODO: Implement IS-04 unregistration
        LOG_INFO("Unregistering from NMOS registry");
        registered_ = false;
        
        if (registration_callback_) {
            registration_callback_(false);
        }
    }
    
    void notify_state_change() {
        if (state_callback_) {
            state_callback_(state_);
        }
    }
    
    NodeConfig node_config_;
    NetworkConfig network_config_;
    
    bool initialized_ = false;
    std::atomic<bool> running_{false};
    std::atomic<bool> registered_{false};
    NMOSNodeState state_ = NMOSNodeState::Stopped;
    
    std::string node_id_;
    std::string device_id_;
    
    // Resources
    mutable std::mutex resources_mutex_;
    std::map<std::string, NMOSSender> senders_;
    std::map<std::string, NMOSReceiver> receivers_;
    std::map<std::string, std::shared_ptr<AES67Sender>> sender_objects_;
    std::map<std::string, std::shared_ptr<AES67Receiver>> receiver_objects_;
    
    // Connection state
    std::map<std::string, TransportParams> staged_params_;
    std::map<std::string, TransportParams> active_params_;
    
    // HTTP server
#ifdef __linux__
    int server_fd_ = -1;
#endif
    std::thread server_thread_;
    
    // Callbacks
    ConnectionCallback connection_callback_;
    std::function<void(NMOSNodeState)> state_callback_;
    std::function<void(bool)> registration_callback_;
};

// ==================== NMOSNode ====================

NMOSNode::NMOSNode() : impl_(std::make_unique<Impl>()) {}
NMOSNode::~NMOSNode() = default;

bool NMOSNode::initialize(const NodeConfig& config) {
    return impl_->initialize(config, NetworkConfig{});
}

bool NMOSNode::initialize(const NodeConfig& config, const NetworkConfig& network) {
    return impl_->initialize(config, network);
}

bool NMOSNode::start() { return impl_->start(); }
void NMOSNode::stop() { impl_->stop(); }
bool NMOSNode::is_running() const { return impl_->is_running(); }
NMOSNodeState NMOSNode::get_state() const { return impl_->get_state(); }

std::string NMOSNode::register_sender(std::shared_ptr<AES67Sender> sender) {
    return impl_->register_sender(std::move(sender));
}

void NMOSNode::unregister_sender(const std::string& sender_id) {
    impl_->unregister_sender(sender_id);
}

std::string NMOSNode::register_receiver(std::shared_ptr<AES67Receiver> receiver) {
    return impl_->register_receiver(std::move(receiver));
}

void NMOSNode::unregister_receiver(const std::string& receiver_id) {
    impl_->unregister_receiver(receiver_id);
}

std::vector<NMOSSender> NMOSNode::get_senders() const { return impl_->get_senders(); }
std::vector<NMOSReceiver> NMOSNode::get_receivers() const { return impl_->get_receivers(); }

void NMOSNode::enable_registration(const std::string& registry_url) {
    impl_->enable_registration(registry_url);
}

void NMOSNode::disable_registration() { impl_->disable_registration(); }
bool NMOSNode::is_registered() const { return impl_->is_registered(); }
void NMOSNode::enable_mdns(bool enable) { impl_->enable_mdns(enable); }
void NMOSNode::enable_peer_to_peer() { impl_->enable_peer_to_peer(); }
void NMOSNode::reregister() { impl_->reregister(); }

void NMOSNode::set_connection_callback(ConnectionCallback callback) {
    impl_->set_connection_callback(std::move(callback));
}

ConnectionResponse NMOSNode::connect_to_sender(const std::string& sender_id,
                                               const std::string& receiver_id,
                                               const TransportParams& params) {
    return impl_->connect_to_sender(sender_id, receiver_id, params);
}

bool NMOSNode::disconnect_receiver(const std::string& receiver_id) {
    return impl_->disconnect_receiver(receiver_id);
}

TransportParams NMOSNode::get_staged_params(const std::string& receiver_id) const {
    return impl_->get_staged_params(receiver_id);
}

TransportParams NMOSNode::get_active_params(const std::string& receiver_id) const {
    return impl_->get_active_params(receiver_id);
}

bool NMOSNode::stage_connection(const std::string& receiver_id, const TransportParams& params) {
    return impl_->stage_connection(receiver_id, params);
}

ConnectionResponse NMOSNode::activate_connection(const std::string& receiver_id) {
    return impl_->activate_connection(receiver_id);
}

std::string NMOSNode::get_node_id() const { return impl_->get_node_id(); }
std::string NMOSNode::get_device_id() const { return impl_->get_device_id(); }
NodeConfig NMOSNode::get_node_config() const { return impl_->get_node_config(); }
std::string NMOSNode::get_api_url() const { return impl_->get_api_url(); }
std::string NMOSNode::get_health_url() const { return impl_->get_health_url(); }

void NMOSNode::set_state_callback(StateCallback callback) {
    impl_->set_state_callback(std::move(callback));
}

void NMOSNode::set_registration_callback(RegistrationCallback callback) {
    impl_->set_registration_callback(std::move(callback));
}

}  // namespace rpi_aes67
