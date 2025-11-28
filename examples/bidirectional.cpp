// SPDX-License-Identifier: MIT
/*
 * RPi-AES67 - Bidirectional Example
 * 
 * Demonstrates simultaneous AES67 sending and receiving with NMOS support.
 */

#include <iostream>
#include <csignal>
#include <thread>
#include <atomic>

#include "rpi_aes67/config.h"
#include "rpi_aes67/logger.h"
#include "rpi_aes67/sender.h"
#include "rpi_aes67/receiver.h"
#include "rpi_aes67/nmos_node.h"
#include "rpi_aes67/pipewire_io.h"
#include "rpi_aes67/ptp_sync.h"

using namespace rpi_aes67;

static std::atomic<bool> g_running{true};

void signal_handler(int) {
    g_running = false;
}

int main(int argc, char* argv[]) {
    // Initialize logger
    Logger::init("bidirectional", LogLevel::Info);
    
    // Parse arguments for config file
    std::string config_path;
    if (argc > 1) {
        config_path = argv[1];
    }
    
    LOG_INFO("Bidirectional AES67 Example");
    
    // Setup signal handlers
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);
    
    try {
        // Load configuration
        Config config;
        if (!config_path.empty()) {
            config = Config::load_from_file(config_path);
            LOG_INFO("Loaded configuration from {}", config_path);
        } else {
            config = Config::get_default();
            LOG_INFO("Using default configuration");
        }
        
        // Initialize PipeWire
        PipeWireManager::instance().initialize();
        
        // Initialize PTP
        auto ptp_sync = std::make_shared<PTPSync>();
        if (ptp_sync->initialize(config.network.interface, config.network.ptp_domain)) {
            ptp_sync->start();
            LOG_INFO("PTP synchronization started");
        }
        
        // Initialize NMOS node
        auto nmos_node = std::make_shared<NMOSNode>();
        if (!nmos_node->initialize(config.node, config.network)) {
            LOG_ERROR("Failed to initialize NMOS node");
            return 1;
        }
        nmos_node->start();
        LOG_INFO("NMOS node started at {}", nmos_node->get_api_url());
        
        // Create sender
        std::shared_ptr<AES67Sender> sender;
        if (!config.senders.empty()) {
            const auto& sender_config = config.senders[0];
            
            sender = std::make_shared<AES67Sender>();
            sender->configure(sender_config);
            sender->set_ptp_sync(ptp_sync);
            
            auto audio_input = std::make_shared<PipeWireInput>();
            if (audio_input->initialize()) {
                sender->set_audio_source(audio_input);
            }
            
            sender->initialize();
            nmos_node->register_sender(sender);
            sender->start();
            
            LOG_INFO("Sender started: {}", sender_config.label);
            std::cout << "\n=== Sender SDP ===\n" << sender->generate_sdp() << "==================\n\n";
        }
        
        // Create receiver
        std::shared_ptr<AES67Receiver> receiver;
        if (!config.receivers.empty()) {
            const auto& receiver_config = config.receivers[0];
            
            receiver = std::make_shared<AES67Receiver>();
            receiver->configure(receiver_config, config.audio);
            receiver->set_ptp_sync(ptp_sync);
            
            auto audio_output = std::make_shared<PipeWireOutput>();
            if (audio_output->initialize()) {
                receiver->set_audio_sink(audio_output);
            }
            
            receiver->initialize();
            nmos_node->register_receiver(receiver);
            
            LOG_INFO("Receiver initialized: {}", receiver_config.label);
        }
        
        LOG_INFO("Bidirectional operation ready. Press Ctrl+C to stop.");
        LOG_INFO("Use NMOS controller to connect the receiver to a sender.");
        
        // Main loop
        while (g_running) {
            std::this_thread::sleep_for(std::chrono::seconds(10));
            
            // Print status
            if (sender && sender->is_running()) {
                auto stats = sender->get_statistics();
                LOG_INFO("Sender: {} packets sent", stats.packets_sent);
            }
            
            if (receiver && receiver->is_running()) {
                auto stats = receiver->get_statistics();
                LOG_INFO("Receiver: {} packets received, {} lost", 
                         stats.packets_received, stats.packets_lost);
            }
            
            LOG_INFO("PTP: {}", PTPSync::state_to_string(ptp_sync->get_state()));
        }
        
        // Cleanup
        LOG_INFO("Shutting down...");
        
        if (sender) sender->stop();
        if (receiver) {
            receiver->stop();
            receiver->disconnect();
        }
        nmos_node->stop();
        ptp_sync->stop();
        PipeWireManager::instance().shutdown();
        
        LOG_INFO("Done.");
        return 0;
        
    } catch (const std::exception& e) {
        LOG_ERROR("Error: {}", e.what());
        return 1;
    }
}
