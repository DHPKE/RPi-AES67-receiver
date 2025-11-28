# API Reference

This document describes the programming API for RPi-AES67.

## Core Classes

### Config

Configuration loading and management.

```cpp
#include "rpi_aes67/config.h"

// Load from file
rpi_aes67::Config config = rpi_aes67::Config::load_from_file("/etc/rpi-aes67/config.json");

// Load from string
rpi_aes67::Config config = rpi_aes67::Config::load_from_string(json_string);

// Get default configuration
rpi_aes67::Config config = rpi_aes67::Config::get_default();

// Validate configuration
if (!config.validate()) {
    // Handle invalid config
}

// Save to file
config.save_to_file("/path/to/output.json");
```

### AES67Sender

Audio stream transmission.

```cpp
#include "rpi_aes67/sender.h"

// Create and configure sender
auto sender = std::make_shared<rpi_aes67::AES67Sender>();

rpi_aes67::SenderConfig config;
config.id = "sender-1";
config.label = "Main Output";
config.multicast_ip = "239.69.1.1";
config.port = 5004;
config.channels = 2;
config.sample_rate = 48000;
config.bit_depth = 24;

sender->configure(config);

// Set audio source (optional)
auto audio_input = std::make_shared<rpi_aes67::PipeWireInput>();
audio_input->initialize();
sender->set_audio_source(audio_input);

// Set PTP synchronization
auto ptp = std::make_shared<rpi_aes67::PTPSync>();
ptp->initialize("eth0");
sender->set_ptp_sync(ptp);

// Initialize and start
sender->initialize();
sender->start();

// Get SDP for distribution
std::string sdp = sender->generate_sdp();

// Get statistics
auto stats = sender->get_statistics();
std::cout << "Packets sent: " << stats.packets_sent << std::endl;

// Stop
sender->stop();
```

### AES67Receiver

Audio stream reception.

```cpp
#include "rpi_aes67/receiver.h"

// Create and configure receiver
auto receiver = std::make_shared<rpi_aes67::AES67Receiver>();

rpi_aes67::ReceiverConfig config;
config.id = "receiver-1";
config.label = "Main Input";
config.channels = 2;
config.sample_rates = {48000, 96000};
config.bit_depths = {16, 24};

rpi_aes67::AudioProcessingConfig audio_config;
audio_config.jitter_buffer_ms = 10.0;

receiver->configure(config, audio_config);

// Set audio output (optional)
auto audio_output = std::make_shared<rpi_aes67::PipeWireOutput>();
audio_output->initialize();
receiver->set_audio_sink(audio_output);

// Initialize
receiver->initialize();

// Connect using SDP
std::string sdp = "v=0\r\n...";  // SDP from sender
receiver->connect(sdp);

// Or connect using IP/port
receiver->connect("239.69.1.1", 5004);

// Start receiving
receiver->start();

// Get statistics
auto stats = receiver->get_statistics();
std::cout << "Packets received: " << stats.packets_received << std::endl;
std::cout << "Packets lost: " << stats.packets_lost << std::endl;

// Stop and disconnect
receiver->stop();
receiver->disconnect();
```

### NMOSNode

NMOS IS-04/IS-05 implementation.

```cpp
#include "rpi_aes67/nmos_node.h"

// Create NMOS node
auto nmos_node = std::make_shared<rpi_aes67::NMOSNode>();

rpi_aes67::NodeConfig node_config;
node_config.label = "My AES67 Node";

rpi_aes67::NetworkConfig network_config;
network_config.interface = "eth0";
network_config.node_port = 8080;

nmos_node->initialize(node_config, network_config);
nmos_node->start();

// Register resources
std::string sender_id = nmos_node->register_sender(sender);
std::string receiver_id = nmos_node->register_receiver(receiver);

// Enable registry registration
nmos_node->enable_registration("http://registry.local:3000");

// Set connection callback
nmos_node->set_connection_callback([](const rpi_aes67::ConnectionRequest& req) {
    rpi_aes67::ConnectionResponse response;
    // Handle connection request
    return response;
});

// Get API URL
std::cout << "Node API: " << nmos_node->get_api_url() << std::endl;

// Stop
nmos_node->stop();
```

### PTPSync

PTP synchronization.

```cpp
#include "rpi_aes67/ptp_sync.h"

// Create and initialize
auto ptp = std::make_shared<rpi_aes67::PTPSync>();
ptp->initialize("eth0", 0);  // interface, domain

// Start synchronization
ptp->start();

// Check status
if (ptp->is_synchronized()) {
    std::cout << "PTP synchronized" << std::endl;
    std::cout << "Offset: " << ptp->get_offset_from_master() << " ns" << std::endl;
}

// Get current PTP time
auto ptp_time = ptp->get_current_time();
uint64_t ptp_ns = ptp->get_ptp_timestamp();

// Convert to RTP timestamp
uint32_t rtp_ts = ptp->get_rtp_timestamp(48000);  // For 48kHz

// Stop
ptp->stop();
```

### PipeWireInput / PipeWireOutput

Audio I/O with PipeWire.

```cpp
#include "rpi_aes67/pipewire_io.h"

// Initialize PipeWire
rpi_aes67::PipeWireManager::instance().initialize();

// Audio input
auto input = std::make_shared<rpi_aes67::PipeWireInput>();
input->initialize();

rpi_aes67::AudioFormat format;
format.sample_rate = 48000;
format.channels = 2;
format.bit_depth = 24;

input->open("", format);  // Empty string = default device
input->set_callback([](const rpi_aes67::AudioBuffer& buffer) {
    // Process audio data
});
input->start();

// Audio output
auto output = std::make_shared<rpi_aes67::PipeWireOutput>();
output->initialize();
output->open("", format);
output->start();

// Write audio data
output->write(data, size);

// Cleanup
input->stop();
output->stop();
rpi_aes67::PipeWireManager::instance().shutdown();
```

## Data Structures

### AudioFormat

```cpp
struct AudioFormat {
    uint32_t sample_rate = 48000;  // 44100, 48000, 96000
    uint8_t channels = 2;          // 1-64
    uint8_t bit_depth = 24;        // 16, 24, 32
    
    uint32_t bytes_per_sample() const;
    uint32_t bytes_per_frame() const;
    std::string encoding_name() const;  // "L16", "L24", "L32"
    bool is_valid() const;
};
```

### SenderStatistics

```cpp
struct SenderStatistics {
    uint64_t packets_sent;
    uint64_t bytes_sent;
    uint64_t rtcp_reports_sent;
    uint64_t sequence_number;
    uint32_t rtp_timestamp;
    double bitrate_kbps;
    uint64_t underruns;
    std::chrono::steady_clock::time_point start_time;
    std::chrono::steady_clock::time_point last_packet_time;
};
```

### ReceiverStatistics

```cpp
struct ReceiverStatistics {
    uint64_t packets_received;
    uint64_t packets_lost;
    uint64_t packets_out_of_order;
    uint64_t bytes_received;
    uint64_t rtcp_reports_received;
    uint32_t last_sequence_number;
    uint32_t last_rtp_timestamp;
    double jitter_ms;
    double latency_ms;
    double buffer_level;     // 0.0 - 1.0
    bool ptp_synchronized;
    double bitrate_kbps;
    uint64_t overruns;
    uint64_t underruns;
    std::chrono::steady_clock::time_point start_time;
    std::chrono::steady_clock::time_point last_packet_time;
};
```

### SDPInfo

```cpp
struct SDPInfo {
    std::string session_name;
    std::string session_id;
    std::string origin_address;
    std::string source_ip;
    uint16_t port;
    uint8_t payload_type;
    AudioFormat format;
    std::string encoding;
    uint32_t packet_time_us;
    std::string ptp_clock_id;
    bool is_valid;
};
```

## SDP Utilities

### SDPParser

```cpp
#include "rpi_aes67/receiver.h"

std::string sdp = "v=0\r\n...";

// Parse SDP
rpi_aes67::SDPInfo info = rpi_aes67::SDPParser::parse(sdp);

if (info.is_valid) {
    std::cout << "Stream: " << info.source_ip << ":" << info.port << std::endl;
    std::cout << "Format: " << info.format.sample_rate << "Hz "
              << (int)info.format.channels << "ch" << std::endl;
}

// Validate for AES67 compliance
if (rpi_aes67::SDPParser::validate_aes67(info)) {
    std::cout << "AES67 compliant" << std::endl;
}
```

### SDPGenerator

```cpp
#include "rpi_aes67/sender.h"

// Generate SDP from sender config
rpi_aes67::SenderConfig config;
config.multicast_ip = "239.69.1.1";
config.port = 5004;
config.sample_rate = 48000;
config.channels = 2;
config.bit_depth = 24;

std::string sdp = rpi_aes67::SDPGenerator::generate(
    config, 
    session_id, 
    origin_ip
);
```

## Error Handling

Most methods return `bool` for success/failure or use exceptions:

```cpp
try {
    auto config = rpi_aes67::Config::load_from_file("/path/to/config.json");
} catch (const std::runtime_error& e) {
    std::cerr << "Error: " << e.what() << std::endl;
}

// Or check return values
if (!receiver->connect(sdp)) {
    std::cerr << "Failed to connect" << std::endl;
}
```

## Logging

```cpp
#include "rpi_aes67/logger.h"

// Initialize logger
rpi_aes67::Logger::init("my_app", rpi_aes67::LogLevel::Info, "/var/log/myapp.log");

// Log messages
LOG_TRACE("Trace message");
LOG_DEBUG("Debug: value = {}", value);
LOG_INFO("Application started");
LOG_WARNING("Potential issue");
LOG_ERROR("Error occurred: {}", error_message);
LOG_CRITICAL("Fatal error!");

// Change log level at runtime
rpi_aes67::Logger::set_level(rpi_aes67::LogLevel::Debug);
```
