// SPDX-License-Identifier: MIT
/*
 * RPi-AES67 - Professional AES67 Sender/Receiver for Raspberry Pi 5
 * Copyright (c) 2025 DHPKE
 *
 * NMOS Node - IS-04/IS-05 implementation for device discovery and connection management.
 * Based on ravennakit architecture patterns.
 */

#pragma once

#include "config.h"
#include <string>
#include <memory>
#include <functional>
#include <vector>
#include <map>
#include <cstdint>

namespace rpi_aes67 {

// Forward declarations
class AES67Sender;
class AES67Receiver;

/**
 * @brief NMOS resource types
 */
enum class NMOSResourceType {
    Node,
    Device,
    Source,
    Flow,
    Sender,
    Receiver
};

/**
 * @brief NMOS connection state
 */
enum class NMOSConnectionState {
    Disconnected,
    Staged,
    Active
};

/**
 * @brief NMOS node state
 */
enum class NMOSNodeState {
    Stopped,
    Starting,
    Running,
    Registered,
    Error
};

/**
 * @brief NMOS resource base information
 */
struct NMOSResource {
    std::string id;
    std::string label;
    std::string description;
    std::map<std::string, std::string> tags;
    std::string version;  // API version
};

/**
 * @brief NMOS sender resource
 */
struct NMOSSender : public NMOSResource {
    std::string flow_id;
    std::string device_id;
    std::string manifest_href;  // SDP URL
    std::string transport;
    std::vector<std::string> interface_bindings;
    bool subscription_active = false;
    std::string subscription_receiver_id;
};

/**
 * @brief NMOS receiver resource
 */
struct NMOSReceiver : public NMOSResource {
    std::string device_id;
    std::string transport;
    std::vector<std::string> interface_bindings;
    bool subscription_active = false;
    std::string subscription_sender_id;
    NMOSConnectionState connection_state = NMOSConnectionState::Disconnected;
};

/**
 * @brief IS-05 transport parameters for AES67/RTP
 */
struct TransportParams {
    std::string source_ip;
    std::string multicast_ip;
    std::string interface_ip;
    uint16_t destination_port = 0;
    uint16_t source_port = 0;
    bool rtp_enabled = true;
    std::string fec_enabled;
    std::string fec_destination_ip;
    std::string fec_mode;
    uint16_t fec_1d_destination_port = 0;
    uint16_t fec_2d_destination_port = 0;
    std::string rtcp_enabled;
    std::string rtcp_destination_ip;
    uint16_t rtcp_destination_port = 0;
};

/**
 * @brief IS-05 connection request
 */
struct ConnectionRequest {
    std::string sender_id;
    std::string receiver_id;
    bool master_enable = true;
    std::string activation_mode;  // "activate_immediate", "activate_scheduled_absolute", "activate_scheduled_relative"
    std::string requested_time;
    TransportParams transport_params;
    std::string transport_file;  // SDP content
    std::string transport_file_type = "application/sdp";
};

/**
 * @brief IS-05 connection response
 */
struct ConnectionResponse {
    bool success = false;
    std::string error_message;
    NMOSConnectionState state = NMOSConnectionState::Disconnected;
    TransportParams active_params;
};

/**
 * @brief Connection request callback type
 */
using ConnectionCallback = std::function<ConnectionResponse(const ConnectionRequest&)>;

/**
 * @brief NMOS Node class
 * 
 * Implements AMWA NMOS IS-04 (Discovery & Registration) and IS-05 (Device Connection Management)
 * for AES67 senders and receivers.
 */
class NMOSNode {
public:
    NMOSNode();
    ~NMOSNode();
    
    // Non-copyable, non-movable
    NMOSNode(const NMOSNode&) = delete;
    NMOSNode& operator=(const NMOSNode&) = delete;
    NMOSNode(NMOSNode&&) = delete;
    NMOSNode& operator=(NMOSNode&&) = delete;
    
    /**
     * @brief Initialize the NMOS node
     * @param config Node configuration
     * @return true on success
     */
    bool initialize(const NodeConfig& config);
    
    /**
     * @brief Initialize with network configuration
     * @param config Node configuration
     * @param network Network configuration
     * @return true on success
     */
    bool initialize(const NodeConfig& config, const NetworkConfig& network);
    
    /**
     * @brief Start the NMOS node (HTTP API, registration, etc.)
     * @return true on success
     */
    bool start();
    
    /**
     * @brief Stop the NMOS node
     */
    void stop();
    
    /**
     * @brief Check if node is running
     */
    [[nodiscard]] bool is_running() const;
    
    /**
     * @brief Get current node state
     */
    [[nodiscard]] NMOSNodeState get_state() const;
    
    // ==================== Resource Registration ====================
    
    /**
     * @brief Register a sender with the NMOS node
     * @param sender AES67 sender to register
     * @return Sender resource ID
     */
    std::string register_sender(std::shared_ptr<AES67Sender> sender);
    
    /**
     * @brief Unregister a sender
     * @param sender_id Sender resource ID
     */
    void unregister_sender(const std::string& sender_id);
    
    /**
     * @brief Register a receiver with the NMOS node
     * @param receiver AES67 receiver to register
     * @return Receiver resource ID
     */
    std::string register_receiver(std::shared_ptr<AES67Receiver> receiver);
    
    /**
     * @brief Unregister a receiver
     * @param receiver_id Receiver resource ID
     */
    void unregister_receiver(const std::string& receiver_id);
    
    /**
     * @brief Get all registered senders
     */
    [[nodiscard]] std::vector<NMOSSender> get_senders() const;
    
    /**
     * @brief Get all registered receivers
     */
    [[nodiscard]] std::vector<NMOSReceiver> get_receivers() const;
    
    // ==================== IS-04 Registration & Discovery ====================
    
    /**
     * @brief Enable registration with NMOS registry
     * @param registry_url Registry base URL (e.g., "http://registry.local:3000")
     */
    void enable_registration(const std::string& registry_url);
    
    /**
     * @brief Disable registration (peer-to-peer mode only)
     */
    void disable_registration();
    
    /**
     * @brief Check if registered with registry
     */
    [[nodiscard]] bool is_registered() const;
    
    /**
     * @brief Enable mDNS/DNS-SD for registry discovery
     * @param enable true to enable
     */
    void enable_mdns(bool enable);
    
    /**
     * @brief Enable peer-to-peer mode (no registry)
     */
    void enable_peer_to_peer();
    
    /**
     * @brief Force re-registration with registry
     */
    void reregister();
    
    // ==================== IS-05 Connection Management ====================
    
    /**
     * @brief Set callback for receiver connection requests
     * @param callback Connection request handler
     */
    void set_connection_callback(ConnectionCallback callback);
    
    /**
     * @brief Make a connection request to a sender
     * @param sender_id Target sender ID
     * @param receiver_id Our receiver ID
     * @param params Transport parameters (optional - use sender's SDP)
     * @return Connection response
     */
    ConnectionResponse connect_to_sender(const std::string& sender_id,
                                         const std::string& receiver_id,
                                         const TransportParams& params = TransportParams{});
    
    /**
     * @brief Disconnect a receiver from its sender
     * @param receiver_id Receiver ID to disconnect
     * @return true on success
     */
    bool disconnect_receiver(const std::string& receiver_id);
    
    /**
     * @brief Get staged connection parameters for a receiver
     * @param receiver_id Receiver ID
     * @return Transport parameters
     */
    [[nodiscard]] TransportParams get_staged_params(const std::string& receiver_id) const;
    
    /**
     * @brief Get active connection parameters for a receiver
     * @param receiver_id Receiver ID
     * @return Transport parameters
     */
    [[nodiscard]] TransportParams get_active_params(const std::string& receiver_id) const;
    
    /**
     * @brief Stage connection parameters
     * @param receiver_id Receiver ID
     * @param params Transport parameters
     * @return true on success
     */
    bool stage_connection(const std::string& receiver_id, const TransportParams& params);
    
    /**
     * @brief Activate staged connection
     * @param receiver_id Receiver ID
     * @return Connection response
     */
    ConnectionResponse activate_connection(const std::string& receiver_id);
    
    // ==================== Node Information ====================
    
    /**
     * @brief Get node ID
     */
    [[nodiscard]] std::string get_node_id() const;
    
    /**
     * @brief Get device ID
     */
    [[nodiscard]] std::string get_device_id() const;
    
    /**
     * @brief Get node configuration
     */
    [[nodiscard]] NodeConfig get_node_config() const;
    
    /**
     * @brief Get HTTP API base URL
     */
    [[nodiscard]] std::string get_api_url() const;
    
    /**
     * @brief Get node health/heartbeat URL
     */
    [[nodiscard]] std::string get_health_url() const;
    
    // ==================== State Callbacks ====================
    
    /**
     * @brief Set callback for node state changes
     */
    using StateCallback = std::function<void(NMOSNodeState)>;
    void set_state_callback(StateCallback callback);
    
    /**
     * @brief Set callback for registration state changes
     */
    using RegistrationCallback = std::function<void(bool registered)>;
    void set_registration_callback(RegistrationCallback callback);

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

/**
 * @brief UUID generator for NMOS resources
 */
class UUIDGenerator {
public:
    /**
     * @brief Generate a random UUID (v4)
     */
    static std::string generate();
    
    /**
     * @brief Generate a name-based UUID (v5)
     * @param namespace_uuid Namespace UUID
     * @param name Name string
     */
    static std::string generate_named(const std::string& namespace_uuid, const std::string& name);
    
    /**
     * @brief Validate UUID format
     */
    static bool is_valid(const std::string& uuid);
};

}  // namespace rpi_aes67
