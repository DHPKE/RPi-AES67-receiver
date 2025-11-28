// SPDX-License-Identifier: MIT
/*
 * RPi-AES67 - Simple Sender Example
 * 
 * Demonstrates basic AES67 stream transmission with PipeWire input.
 */

#include <iostream>
#include <csignal>
#include <thread>
#include <atomic>

#include "rpi_aes67/config.h"
#include "rpi_aes67/logger.h"
#include "rpi_aes67/sender.h"
#include "rpi_aes67/pipewire_io.h"
#include "rpi_aes67/ptp_sync.h"

using namespace rpi_aes67;

static std::atomic<bool> g_running{true};

void signal_handler(int) {
    g_running = false;
}

int main(int argc, char* argv[]) {
    // Initialize logger
    Logger::init("simple_sender", LogLevel::Info);
    
    // Parse arguments
    std::string multicast_ip = "239.69.1.1";
    uint16_t port = 5004;
    
    if (argc > 1) {
        multicast_ip = argv[1];
    }
    if (argc > 2) {
        port = static_cast<uint16_t>(std::stoi(argv[2]));
    }
    
    LOG_INFO("Simple AES67 Sender Example");
    LOG_INFO("Sending to {}:{}", multicast_ip, port);
    
    // Setup signal handlers
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);
    
    try {
        // Initialize PipeWire
        if (!PipeWireManager::instance().initialize()) {
            LOG_WARNING("PipeWire not available, no audio input");
        }
        
        // Initialize PTP
        auto ptp_sync = std::make_shared<PTPSync>();
        if (ptp_sync->initialize("eth0")) {
            ptp_sync->start();
            LOG_INFO("PTP synchronization started");
        }
        
        // Create sender
        auto sender = std::make_shared<AES67Sender>();
        
        SenderConfig config;
        config.id = "simple-sender";
        config.label = "Simple Sender";
        config.multicast_ip = multicast_ip;
        config.port = port;
        config.channels = 2;
        config.sample_rate = 48000;
        config.bit_depth = 24;
        config.payload_type = 97;
        
        if (!sender->configure(config)) {
            LOG_ERROR("Failed to configure sender");
            return 1;
        }
        
        // Set up audio input (optional)
        auto audio_input = std::make_shared<PipeWireInput>();
        if (audio_input->initialize()) {
            sender->set_audio_source(audio_input);
        }
        
        sender->set_ptp_sync(ptp_sync);
        
        if (!sender->initialize()) {
            LOG_ERROR("Failed to initialize sender");
            return 1;
        }
        
        // Start sending
        if (!sender->start()) {
            LOG_ERROR("Failed to start sender");
            return 1;
        }
        
        LOG_INFO("Sender started. Press Ctrl+C to stop.");
        
        // Print SDP
        std::cout << "\n=== SDP ===\n" << sender->generate_sdp() << "===========\n\n";
        
        // Main loop - print statistics
        while (g_running) {
            std::this_thread::sleep_for(std::chrono::seconds(5));
            
            auto stats = sender->get_statistics();
            LOG_INFO("Stats: {} packets sent, {} bytes, {:.1f} kbps",
                     stats.packets_sent,
                     stats.bytes_sent,
                     stats.bitrate_kbps);
        }
        
        // Cleanup
        LOG_INFO("Stopping sender...");
        sender->stop();
        ptp_sync->stop();
        PipeWireManager::instance().shutdown();
        
        LOG_INFO("Done.");
        return 0;
        
    } catch (const std::exception& e) {
        LOG_ERROR("Error: {}", e.what());
        return 1;
    }
}
