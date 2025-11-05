#include <iostream>
#include <memory>
#include <signal.h>
#include <thread>
#include "nmos_node.h"
#include "aes67_receiver.h"
#include "config_manager.h"
#include "logger.h"

volatile sig_atomic_t running = 1;

void signal_handler(int signum) {
    Logger::info("Received signal " + std::to_string(signum) + ", shutting down...");
    running = 0;
}

int main(int argc, char* argv[]) {
    try {
        Logger::init("aes67-receiver");
        Logger::info("Starting AES67 NMOS Receiver for Raspberry Pi 5");

        // Load configuration
        std::string config_path = "/etc/aes67-receiver/config.json";
        if (argc > 1) {
            config_path = argv[1];
        }

        auto config = ConfigManager::load(config_path);
        Logger::info("Configuration loaded from " + config_path);

        // Setup signal handlers
        signal(SIGINT, signal_handler);
        signal(SIGTERM, signal_handler);

        // Initialize NMOS node
        auto nmos_node = std::make_shared<NmosNode>(config);
        nmos_node->start();
        Logger::info("NMOS node started");

        // Initialize AES67 receivers based on configuration
        std::vector<std::shared_ptr<AES67Receiver>> receivers;
        for (const auto& receiver_config : config.receivers) {
            auto receiver = std::make_shared<AES67Receiver>(receiver_config, config.audio);
            receiver->initialize();
            receivers.push_back(receiver);
            
            // Register receiver with NMOS node
            nmos_node->register_receiver(receiver);
            Logger::info("Receiver '" + receiver_config.label + "' initialized");
        }

        Logger::info("All receivers initialized. Running...");
        
        // Main loop
        while (running) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
            
            // Periodic health check
            for (const auto& receiver : receivers) {
                if (!receiver->is_healthy()) {
                    Logger::warning("Receiver " + receiver->get_id() + " unhealthy, attempting recovery");
                    receiver->recover();
                }
            }
        }

        // Cleanup
        Logger::info("Stopping receivers...");
        for (auto& receiver : receivers) {
            receiver->stop();
        }

        Logger::info("Stopping NMOS node...");
        nmos_node->stop();

        Logger::info("Shutdown complete");
        return 0;

    } catch (const std::exception& e) {
        Logger::error("Fatal error: " + std::string(e.what()));
        return 1;
    }
}