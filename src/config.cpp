// SPDX-License-Identifier: MIT
/*
 * RPi-AES67 - Professional AES67 Sender/Receiver for Raspberry Pi 5
 * Copyright (c) 2025 DHPKE
 *
 * Configuration implementation.
 */

#include "rpi_aes67/config.h"
#include "rpi_aes67/logger.h"
#include <fstream>
#include <random>
#include <sstream>
#include <iomanip>
#include <chrono>

namespace rpi_aes67 {

// ==================== AudioFormat ====================

std::string AudioFormat::encoding_name() const {
    switch (bit_depth) {
        case 16: return "L16";
        case 24: return "L24";
        case 32: return "L32";
        default: return "L24";
    }
}

bool AudioFormat::is_valid() const {
    if (sample_rate != 44100 && sample_rate != 48000 && sample_rate != 96000) {
        return false;
    }
    if (channels == 0 || channels > 64) {
        return false;
    }
    if (bit_depth != 16 && bit_depth != 24 && bit_depth != 32) {
        return false;
    }
    return true;
}

// ==================== NodeConfig ====================

static std::string generate_uuid() {
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

void NodeConfig::set_defaults() {
    if (id.empty()) {
        id = generate_uuid();
    }
    if (label.empty()) {
        label = "RPi5 AES67 Node";
    }
    if (description.empty()) {
        description = "AES67 Sender/Receiver for Raspberry Pi 5";
    }
}

// ==================== Config ====================

Config Config::load_from_file(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        throw std::runtime_error("Cannot open configuration file: " + path);
    }
    
    nlohmann::json j;
    try {
        file >> j;
    } catch (const nlohmann::json::parse_error& e) {
        throw std::runtime_error("JSON parse error in " + path + ": " + e.what());
    }
    
    return j.get<Config>();
}

Config Config::load_from_string(const std::string& json_str) {
    nlohmann::json j = nlohmann::json::parse(json_str);
    return j.get<Config>();
}

void Config::save_to_file(const std::string& path) const {
    std::ofstream file(path);
    if (!file.is_open()) {
        throw std::runtime_error("Cannot open file for writing: " + path);
    }
    
    file << to_json().dump(2);
}

nlohmann::json Config::to_json() const {
    nlohmann::json j;
    ::rpi_aes67::to_json(j, *this);
    return j;
}

bool Config::validate() const {
    // Validate node config
    if (node.id.empty()) {
        return false;
    }
    
    // Validate senders
    for (const auto& sender : senders) {
        if (sender.id.empty()) {
            return false;
        }
        if (sender.port == 0 || sender.port > 65535) {
            return false;
        }
        if (sender.sample_rate != 44100 && sender.sample_rate != 48000 && sender.sample_rate != 96000) {
            return false;
        }
    }
    
    // Validate receivers
    for (const auto& receiver : receivers) {
        if (receiver.id.empty()) {
            return false;
        }
    }
    
    // Validate network config
    if (network.interface.empty()) {
        return false;
    }
    
    return true;
}

Config Config::get_default() {
    Config config;
    
    // Node defaults
    config.node.id = generate_uuid();
    config.node.label = "RPi5 AES67 Node";
    config.node.description = "Professional AES67 Sender/Receiver";
    config.node.tags["location"] = "Studio A";
    config.node.tags["device_type"] = "raspberry_pi_5";
    
    // Default sender
    SenderConfig sender;
    sender.id = "sender-1";
    sender.label = "Main Output";
    sender.description = "Primary audio output stream";
    sender.channels = 2;
    sender.sample_rate = 48000;
    sender.bit_depth = 24;
    sender.multicast_ip = "239.69.1.1";
    sender.port = 5004;
    sender.payload_type = 97;
    config.senders.push_back(sender);
    
    // Default receiver
    ReceiverConfig receiver;
    receiver.id = "receiver-1";
    receiver.label = "Main Input";
    receiver.description = "Primary audio input stream";
    receiver.channels = 2;
    receiver.sample_rates = {44100, 48000, 96000};
    receiver.bit_depths = {16, 24};
    config.receivers.push_back(receiver);
    
    // Network defaults
    config.network.interface = "eth0";
    config.network.ptp_domain = 0;
    config.network.enable_mdns = true;
    config.network.node_port = 8080;
    config.network.connection_port = 8081;
    
    // Audio defaults
    config.audio.buffer_size_ms = 5.0;
    config.audio.jitter_buffer_ms = 10.0;
    config.audio.buffer_frames = 256;
    
    // Logging defaults
    config.logging.level = "info";
    config.logging.enable_console = true;
    
    return config;
}

// ==================== JSON Serialization ====================

void to_json(nlohmann::json& j, const AudioFormat& f) {
    j = nlohmann::json{
        {"sample_rate", f.sample_rate},
        {"channels", f.channels},
        {"bit_depth", f.bit_depth}
    };
}

void from_json(const nlohmann::json& j, AudioFormat& f) {
    if (j.contains("sample_rate")) j.at("sample_rate").get_to(f.sample_rate);
    if (j.contains("channels")) j.at("channels").get_to(f.channels);
    if (j.contains("bit_depth")) j.at("bit_depth").get_to(f.bit_depth);
}

void to_json(nlohmann::json& j, const NodeConfig& c) {
    j = nlohmann::json{
        {"id", c.id},
        {"label", c.label},
        {"description", c.description},
        {"tags", c.tags}
    };
}

void from_json(const nlohmann::json& j, NodeConfig& c) {
    if (j.contains("id")) j.at("id").get_to(c.id);
    if (j.contains("label")) j.at("label").get_to(c.label);
    if (j.contains("description")) j.at("description").get_to(c.description);
    if (j.contains("tags")) j.at("tags").get_to(c.tags);
}

void to_json(nlohmann::json& j, const SenderConfig& c) {
    j = nlohmann::json{
        {"id", c.id},
        {"label", c.label},
        {"description", c.description},
        {"channels", c.channels},
        {"sample_rate", c.sample_rate},
        {"bit_depth", c.bit_depth},
        {"multicast_ip", c.multicast_ip},
        {"port", c.port},
        {"payload_type", c.payload_type},
        {"pipewire_source", c.pipewire_source},
        {"enabled", c.enabled},
        {"packet_time_us", c.packet_time_us}
    };
}

void from_json(const nlohmann::json& j, SenderConfig& c) {
    if (j.contains("id")) j.at("id").get_to(c.id);
    if (j.contains("label")) j.at("label").get_to(c.label);
    if (j.contains("description")) j.at("description").get_to(c.description);
    if (j.contains("channels")) j.at("channels").get_to(c.channels);
    if (j.contains("sample_rate")) j.at("sample_rate").get_to(c.sample_rate);
    if (j.contains("bit_depth")) j.at("bit_depth").get_to(c.bit_depth);
    if (j.contains("multicast_ip")) j.at("multicast_ip").get_to(c.multicast_ip);
    if (j.contains("port")) j.at("port").get_to(c.port);
    if (j.contains("payload_type")) j.at("payload_type").get_to(c.payload_type);
    if (j.contains("pipewire_source")) j.at("pipewire_source").get_to(c.pipewire_source);
    if (j.contains("enabled")) j.at("enabled").get_to(c.enabled);
    if (j.contains("packet_time_us")) j.at("packet_time_us").get_to(c.packet_time_us);
}

void to_json(nlohmann::json& j, const ReceiverConfig& c) {
    j = nlohmann::json{
        {"id", c.id},
        {"label", c.label},
        {"description", c.description},
        {"channels", c.channels},
        {"sample_rates", c.sample_rates},
        {"bit_depths", c.bit_depths},
        {"pipewire_sink", c.pipewire_sink},
        {"enabled", c.enabled}
    };
}

void from_json(const nlohmann::json& j, ReceiverConfig& c) {
    if (j.contains("id")) j.at("id").get_to(c.id);
    if (j.contains("label")) j.at("label").get_to(c.label);
    if (j.contains("description")) j.at("description").get_to(c.description);
    if (j.contains("channels")) j.at("channels").get_to(c.channels);
    if (j.contains("sample_rates")) j.at("sample_rates").get_to(c.sample_rates);
    if (j.contains("bit_depths")) j.at("bit_depths").get_to(c.bit_depths);
    if (j.contains("pipewire_sink")) j.at("pipewire_sink").get_to(c.pipewire_sink);
    if (j.contains("enabled")) j.at("enabled").get_to(c.enabled);
}

void to_json(nlohmann::json& j, const NetworkConfig& c) {
    j = nlohmann::json{
        {"interface", c.interface},
        {"ptp_domain", c.ptp_domain},
        {"registry_url", c.registry_url},
        {"enable_mdns", c.enable_mdns},
        {"node_port", c.node_port},
        {"connection_port", c.connection_port}
    };
}

void from_json(const nlohmann::json& j, NetworkConfig& c) {
    if (j.contains("interface")) j.at("interface").get_to(c.interface);
    if (j.contains("ptp_domain")) j.at("ptp_domain").get_to(c.ptp_domain);
    if (j.contains("registry_url")) j.at("registry_url").get_to(c.registry_url);
    if (j.contains("enable_mdns")) j.at("enable_mdns").get_to(c.enable_mdns);
    // Support both old and new config names
    if (j.contains("node_port")) j.at("node_port").get_to(c.node_port);
    if (j.contains("connection_port")) j.at("connection_port").get_to(c.connection_port);
    // Legacy support
    if (j.contains("use_mdns")) j.at("use_mdns").get_to(c.enable_mdns);
}

void to_json(nlohmann::json& j, const AudioProcessingConfig& c) {
    j = nlohmann::json{
        {"buffer_size_ms", c.buffer_size_ms},
        {"jitter_buffer_ms", c.jitter_buffer_ms},
        {"buffer_frames", c.buffer_frames},
        {"enable_sample_rate_conversion", c.enable_sample_rate_conversion}
    };
}

void from_json(const nlohmann::json& j, AudioProcessingConfig& c) {
    if (j.contains("buffer_size_ms")) j.at("buffer_size_ms").get_to(c.buffer_size_ms);
    if (j.contains("jitter_buffer_ms")) j.at("jitter_buffer_ms").get_to(c.jitter_buffer_ms);
    if (j.contains("buffer_frames")) j.at("buffer_frames").get_to(c.buffer_frames);
    // Legacy support for buffer_size
    if (j.contains("buffer_size")) j.at("buffer_size").get_to(c.buffer_frames);
    if (j.contains("enable_sample_rate_conversion")) {
        j.at("enable_sample_rate_conversion").get_to(c.enable_sample_rate_conversion);
    }
    // Legacy support for latency_ms
    if (j.contains("latency_ms")) j.at("latency_ms").get_to(c.buffer_size_ms);
}

void to_json(nlohmann::json& j, const LoggingConfig& c) {
    j = nlohmann::json{
        {"level", c.level},
        {"file", c.file},
        {"enable_console", c.enable_console}
    };
}

void from_json(const nlohmann::json& j, LoggingConfig& c) {
    if (j.contains("level")) j.at("level").get_to(c.level);
    if (j.contains("file")) j.at("file").get_to(c.file);
    if (j.contains("enable_console")) j.at("enable_console").get_to(c.enable_console);
}

void to_json(nlohmann::json& j, const Config& c) {
    j = nlohmann::json{
        {"node", c.node},
        {"senders", c.senders},
        {"receivers", c.receivers},
        {"network", c.network},
        {"audio", c.audio},
        {"logging", c.logging}
    };
}

void from_json(const nlohmann::json& j, Config& c) {
    if (j.contains("node")) j.at("node").get_to(c.node);
    if (j.contains("senders")) j.at("senders").get_to(c.senders);
    if (j.contains("receivers")) j.at("receivers").get_to(c.receivers);
    if (j.contains("network")) j.at("network").get_to(c.network);
    if (j.contains("audio")) j.at("audio").get_to(c.audio);
    if (j.contains("logging")) j.at("logging").get_to(c.logging);
    
    // Apply defaults
    c.node.set_defaults();
}

}  // namespace rpi_aes67
