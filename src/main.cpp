// SPDX-License-Identifier: MIT
/*
 * RPi-AES67 - Professional AES67 Sender/Receiver for Raspberry Pi 5
 * Copyright (c) 2025 DHPKE
 *
 * Main entry point for the AES67 node application.
 * Supports bidirectional operation with NMOS IS-04/IS-05.
 */

#include <iostream>
#include <memory>
#include <csignal>
#include <thread>
#include <atomic>
#include <getopt.h>

#include "rpi_aes67/config.h"
#include "rpi_aes67/logger.h"
#include "rpi_aes67/ptp_sync.h"
#include "rpi_aes67/pipewire_io.h"
#include "rpi_aes67/sender.h"
#include "rpi_aes67/receiver.h"
#include "rpi_aes67/nmos_node.h"

using namespace rpi_aes67;

// Global flag for signal handling
static std::atomic<bool> g_running{true};

void signal_handler(int signum) {
    LOG_INFO("Received signal {}, shutting down...", signum);
    g_running = false;
}

void print_usage(const char* program_name) {
    std::cout << "RPi-AES67 - Professional AES67 Sender/Receiver for Raspberry Pi 5\n\n";
    std::cout << "Usage: " << program_name << " [options]\n\n";
    std::cout << "Options:\n";
    std::cout << "  -c, --config <path>    Configuration file path\n";
    std::cout << "                         (default: /etc/rpi-aes67/config.json)\n";
    std::cout << "  -m, --mode <mode>      Operation mode: sender, receiver, or bidirectional\n";
    std::cout << "                         (default: bidirectional)\n";
    std::cout << "  -v, --verbose          Enable verbose logging\n";
    std::cout << "  -h, --help             Show this help message\n";
    std::cout << "\nExamples:\n";
    std::cout << "  " << program_name << " -c /etc/rpi-aes67/config.json\n";
    std::cout << "  " << program_name << " -m receiver -v\n";
}

enum class OperationMode {
    Sender,
    Receiver,
    Bidirectional
};

int main(int argc, char* argv[]) {
    // Default configuration
    std::string config_path = "/etc/rpi-aes67/config.json";
    OperationMode mode = OperationMode::Bidirectional;
    LogLevel log_level = LogLevel::Info;
    
    // Parse command line arguments
    static struct option long_options[] = {
        {"config", required_argument, nullptr, 'c'},
        {"mode", required_argument, nullptr, 'm'},
        {"verbose", no_argument, nullptr, 'v'},
        {"help", no_argument, nullptr, 'h'},
        {nullptr, 0, nullptr, 0}
    };
    
    int opt;
    while ((opt = getopt_long(argc, argv, "c:m:vh", long_options, nullptr)) != -1) {
        switch (opt) {
            case 'c':
                config_path = optarg;
                break;
            case 'm':
                if (std::string(optarg) == "sender") {
                    mode = OperationMode::Sender;
                } else if (std::string(optarg) == "receiver") {
                    mode = OperationMode::Receiver;
                } else if (std::string(optarg) == "bidirectional") {
                    mode = OperationMode::Bidirectional;
                } else {
                    std::cerr << "Unknown mode: " << optarg << "\n";
                    return 1;
                }
                break;
            case 'v':
                log_level = LogLevel::Debug;
                break;
            case 'h':
                print_usage(argv[0]);
                return 0;
            default:
                print_usage(argv[0]);
                return 1;
        }
    }
    
    try {
        // Initialize logger
        Logger::init("rpi-aes67", log_level);
        LOG_INFO("Starting RPi-AES67 - Professional AES67 Sender/Receiver");
        LOG_INFO("Mode: {}", mode == OperationMode::Sender ? "Sender" :
                            mode == OperationMode::Receiver ? "Receiver" : "Bidirectional");
        
        // Load configuration
        Config config;
        try {
            config = Config::load_from_file(config_path);
            LOG_INFO("Configuration loaded from {}", config_path);
        } catch (const std::exception& e) {
            LOG_WARNING("Could not load config from {}: {}", config_path, e.what());
            LOG_INFO("Using default configuration");
            config = Config::get_default();
        }
        
        // Validate configuration
        if (!config.validate()) {
            LOG_ERROR("Invalid configuration");
            return 1;
        }
        
        // Setup signal handlers
        std::signal(SIGINT, signal_handler);
        std::signal(SIGTERM, signal_handler);
        
        // Initialize PipeWire
        if (!PipeWireManager::instance().initialize()) {
            LOG_WARNING("PipeWire initialization failed, audio may not work");
        }
        
        // Initialize PTP synchronization
        auto ptp_sync = std::make_shared<PTPSync>();
        if (!ptp_sync->initialize(config.network.interface, config.network.ptp_domain)) {
            LOG_WARNING("PTP initialization failed, using local clock");
        } else {
            ptp_sync->start();
            LOG_INFO("PTP synchronization started on {} (domain {})", 
                     config.network.interface, config.network.ptp_domain);
        }
        
        // Initialize NMOS node
        auto nmos_node = std::make_shared<NMOSNode>();
        if (!nmos_node->initialize(config.node, config.network)) {
            LOG_ERROR("Failed to initialize NMOS node");
            return 1;
        }
        
        // Start NMOS node
        if (!nmos_node->start()) {
            LOG_ERROR("Failed to start NMOS node");
            return 1;
        }
        LOG_INFO("NMOS node started at {}", nmos_node->get_api_url());
        
        // Enable registry registration if configured
        if (!config.network.registry_url.empty()) {
            nmos_node->enable_registration(config.network.registry_url);
        }
        
        // Initialize senders
        std::vector<std::shared_ptr<AES67Sender>> senders;
        if (mode == OperationMode::Sender || mode == OperationMode::Bidirectional) {
            for (const auto& sender_config : config.senders) {
                if (!sender_config.enabled) continue;
                
                auto sender = std::make_shared<AES67Sender>();
                
                if (!sender->configure(sender_config)) {
                    LOG_ERROR("Failed to configure sender {}", sender_config.id);
                    continue;
                }
                
                // Set up audio input if configured
                if (!sender_config.pipewire_source.empty()) {
                    auto audio_input = std::make_shared<PipeWireInput>();
                    if (audio_input->initialize()) {
                        sender->set_audio_source(audio_input);
                    }
                }
                
                sender->set_ptp_sync(ptp_sync);
                
                if (!sender->initialize()) {
                    LOG_ERROR("Failed to initialize sender {}", sender_config.id);
                    continue;
                }
                
                // Register with NMOS
                nmos_node->register_sender(sender);
                
                // Start sender
                if (sender->start()) {
                    LOG_INFO("Sender '{}' started: {} -> {}:{}", 
                             sender_config.label,
                             sender_config.pipewire_source.empty() ? "no input" : sender_config.pipewire_source,
                             sender_config.multicast_ip, sender_config.port);
                    senders.push_back(sender);
                }
            }
        }
        
        // Initialize receivers
        std::vector<std::shared_ptr<AES67Receiver>> receivers;
        if (mode == OperationMode::Receiver || mode == OperationMode::Bidirectional) {
            for (const auto& receiver_config : config.receivers) {
                if (!receiver_config.enabled) continue;
                
                auto receiver = std::make_shared<AES67Receiver>();
                
                if (!receiver->configure(receiver_config, config.audio)) {
                    LOG_ERROR("Failed to configure receiver {}", receiver_config.id);
                    continue;
                }
                
                // Set up audio output if configured
                if (!receiver_config.pipewire_sink.empty()) {
                    auto audio_output = std::make_shared<PipeWireOutput>();
                    if (audio_output->initialize()) {
                        receiver->set_audio_sink(audio_output);
                    }
                }
                
                receiver->set_ptp_sync(ptp_sync);
                
                if (!receiver->initialize()) {
                    LOG_ERROR("Failed to initialize receiver {}", receiver_config.id);
                    continue;
                }
                
                // Register with NMOS
                nmos_node->register_receiver(receiver);
                
                LOG_INFO("Receiver '{}' initialized and waiting for connection", 
                         receiver_config.label);
                receivers.push_back(receiver);
            }
        }
        
        // Summary
        LOG_INFO("Initialized {} sender(s) and {} receiver(s)", 
                 senders.size(), receivers.size());
        LOG_INFO("System running. Press Ctrl+C to stop.");
        
        // Main loop
        while (g_running) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
            
            // Periodic health check
            for (const auto& sender : senders) {
                if (!sender->is_healthy()) {
                    LOG_WARNING("Sender {} unhealthy, attempting recovery", sender->get_id());
                    sender->recover();
                }
            }
            
            for (const auto& receiver : receivers) {
                if (!receiver->is_healthy()) {
                    LOG_WARNING("Receiver {} unhealthy, attempting recovery", receiver->get_id());
                    receiver->recover();
                }
            }
            
            // Check PTP sync status periodically
            static int ptp_check_counter = 0;
            if (++ptp_check_counter >= 60) {  // Every minute
                ptp_check_counter = 0;
                if (ptp_sync->is_running()) {
                    LOG_DEBUG("PTP status: {} (offset: {} ns)", 
                             PTPSync::state_to_string(ptp_sync->get_state()),
                             ptp_sync->get_offset_from_master());
                }
            }
        }
        
        // Cleanup
        LOG_INFO("Shutting down...");
        
        // Stop senders
        for (auto& sender : senders) {
            sender->stop();
        }
        senders.clear();
        
        // Stop receivers
        for (auto& receiver : receivers) {
            receiver->disconnect();
        }
        receivers.clear();
        
        // Stop NMOS node
        nmos_node->stop();
        
        // Stop PTP
        ptp_sync->stop();
        
        // Shutdown PipeWire
        PipeWireManager::instance().shutdown();
        
        LOG_INFO("Shutdown complete");
        return 0;
        
    } catch (const std::exception& e) {
        LOG_CRITICAL("Fatal error: {}", e.what());
        return 1;
    }
}