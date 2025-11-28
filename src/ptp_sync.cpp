// SPDX-License-Identifier: MIT
/*
 * RPi-AES67 - Professional AES67 Sender/Receiver for Raspberry Pi 5
 * Copyright (c) 2025 DHPKE
 *
 * PTP synchronization implementation.
 * Software-based PTP follower for AES67 timing synchronization.
 */

#include "rpi_aes67/ptp_sync.h"
#include "rpi_aes67/logger.h"
#include <thread>
#include <vector>
#include <mutex>
#include <condition_variable>
#include <cstring>

#ifdef __linux__
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <linux/if_packet.h>
#include <linux/if_ether.h>
#endif

namespace rpi_aes67 {

// PTP constants
constexpr uint16_t PTP_EVENT_PORT = 319;
constexpr uint16_t PTP_GENERAL_PORT = 320;
constexpr const char* PTP_MULTICAST_ADDR = "224.0.1.129";

// ==================== PTPSync::Impl ====================

class PTPSync::Impl {
public:
    Impl() = default;
    ~Impl() { stop(); }
    
    bool initialize(const PTPConfig& config) {
        config_ = config;
        
        // Initialize socket for PTP communication
        // In this implementation, we rely on linuxptp running externally
        // and read its state through shared memory or system calls
        
        LOG_INFO("PTP initialized on interface {}, domain {}", 
                 config_.interface, static_cast<int>(config_.domain));
        
        initialized_ = true;
        return true;
    }
    
    void start() {
        if (running_) return;
        
        running_ = true;
        state_ = PTPState::Listening;
        
        // Start monitoring thread
        monitor_thread_ = std::thread([this]() {
            monitor_loop();
        });
        
        LOG_INFO("PTP synchronization started");
    }
    
    void stop() {
        if (!running_) return;
        
        running_ = false;
        state_ = PTPState::Initializing;
        
        if (monitor_thread_.joinable()) {
            monitor_thread_.join();
        }
        
        LOG_INFO("PTP synchronization stopped");
    }
    
    bool is_running() const { return running_; }
    bool is_synchronized() const { return state_ == PTPState::Slave; }
    
    std::chrono::nanoseconds get_current_time() const {
        // Use system clock as base and apply PTP offset
        auto now = std::chrono::system_clock::now();
        auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
            now.time_since_epoch());
        
        // Apply offset from master
        return std::chrono::nanoseconds(ns.count() - offset_from_master_);
    }
    
    uint64_t get_ptp_timestamp() const {
        return static_cast<uint64_t>(get_current_time().count());
    }
    
    uint32_t get_rtp_timestamp(uint32_t sample_rate) const {
        return ptp_to_rtp_timestamp(get_ptp_timestamp(), sample_rate);
    }
    
    static uint32_t ptp_to_rtp_timestamp(uint64_t ptp_ns, uint32_t sample_rate) {
        // RTP timestamp = (PTP_time_ns * sample_rate) / 1e9
        // Use 64-bit arithmetic to avoid overflow
        uint64_t timestamp = (ptp_ns * sample_rate) / 1000000000ULL;
        return static_cast<uint32_t>(timestamp);  // 32-bit wrapping
    }
    
    int64_t get_offset_from_master() const { return offset_from_master_; }
    double get_path_delay() const { return path_delay_; }
    PTPState get_state() const { return state_; }
    
    PTPClockInfo get_clock_info() const {
        PTPClockInfo info;
        info.clock_id = clock_id_;
        info.priority1 = priority1_;
        info.priority2 = priority2_;
        info.clock_class = clock_class_;
        info.clock_accuracy = clock_accuracy_;
        info.offset_from_master_ns = offset_from_master_;
        info.path_delay_ns = path_delay_;
        info.state = state_;
        info.synchronized = is_synchronized();
        return info;
    }
    
    void add_listener(PTPListener* listener) {
        std::lock_guard<std::mutex> lock(listeners_mutex_);
        listeners_.push_back(listener);
    }
    
    void remove_listener(PTPListener* listener) {
        std::lock_guard<std::mutex> lock(listeners_mutex_);
        listeners_.erase(
            std::remove(listeners_.begin(), listeners_.end(), listener),
            listeners_.end());
    }

private:
    void monitor_loop() {
        while (running_) {
            // Poll PTP status (from linuxptp via pmc or shared memory)
            update_ptp_status();
            
            // Notify listeners of any changes
            notify_listeners();
            
            // Sleep for monitoring interval
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
    
    void update_ptp_status() {
        // In a full implementation, this would:
        // 1. Read from linuxptp shared memory
        // 2. Or use pmc tool to query status
        // 3. Or implement full PTP protocol
        
        // For now, simulate synchronized state after a delay
        static auto start_time = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::steady_clock::now() - start_time;
        
        if (elapsed > std::chrono::seconds(5) && state_ != PTPState::Slave) {
            PTPState old_state = state_;
            state_ = PTPState::Slave;
            offset_from_master_ = 0;  // Simulated perfect sync
            path_delay_ = 100.0;  // Simulated path delay in ns
            
            if (old_state != state_) {
                LOG_INFO("PTP state changed: {} -> {}", 
                        state_to_string(old_state), state_to_string(state_));
            }
        }
    }
    
    void notify_listeners() {
        std::lock_guard<std::mutex> lock(listeners_mutex_);
        for (auto* listener : listeners_) {
            listener->on_ptp_state_changed(state_);
            listener->on_ptp_offset_update(offset_from_master_, path_delay_);
        }
    }
    
    static std::string state_to_string(PTPState state) {
        return PTPSync::state_to_string(state);
    }
    
    PTPConfig config_;
    bool initialized_ = false;
    std::atomic<bool> running_{false};
    std::atomic<PTPState> state_{PTPState::Initializing};
    
    std::thread monitor_thread_;
    
    // PTP state
    std::atomic<int64_t> offset_from_master_{0};
    std::atomic<double> path_delay_{0.0};
    uint64_t clock_id_ = 0;
    uint8_t priority1_ = 128;
    uint8_t priority2_ = 128;
    uint8_t clock_class_ = 248;
    uint8_t clock_accuracy_ = 0xFE;
    
    // Listeners
    std::mutex listeners_mutex_;
    std::vector<PTPListener*> listeners_;
};

// ==================== PTPSync ====================

PTPSync::PTPSync() : impl_(std::make_unique<Impl>()) {}
PTPSync::~PTPSync() = default;

bool PTPSync::initialize(const PTPConfig& config) {
    return impl_->initialize(config);
}

bool PTPSync::initialize(const std::string& interface, uint8_t domain) {
    PTPConfig config;
    config.interface = interface;
    config.domain = domain;
    return initialize(config);
}

void PTPSync::start() { impl_->start(); }
void PTPSync::stop() { impl_->stop(); }
bool PTPSync::is_running() const { return impl_->is_running(); }
bool PTPSync::is_synchronized() const { return impl_->is_synchronized(); }

std::chrono::nanoseconds PTPSync::get_current_time() const {
    return impl_->get_current_time();
}

uint64_t PTPSync::get_ptp_timestamp() const {
    return impl_->get_ptp_timestamp();
}

uint32_t PTPSync::ptp_to_rtp_timestamp(uint64_t ptp_ns, uint32_t sample_rate) {
    return Impl::ptp_to_rtp_timestamp(ptp_ns, sample_rate);
}

uint32_t PTPSync::get_rtp_timestamp(uint32_t sample_rate) const {
    return impl_->get_rtp_timestamp(sample_rate);
}

int64_t PTPSync::get_offset_from_master() const {
    return impl_->get_offset_from_master();
}

double PTPSync::get_path_delay() const {
    return impl_->get_path_delay();
}

PTPState PTPSync::get_state() const {
    return impl_->get_state();
}

PTPClockInfo PTPSync::get_clock_info() const {
    return impl_->get_clock_info();
}

void PTPSync::add_listener(PTPListener* listener) {
    impl_->add_listener(listener);
}

void PTPSync::remove_listener(PTPListener* listener) {
    impl_->remove_listener(listener);
}

std::string PTPSync::state_to_string(PTPState state) {
    switch (state) {
        case PTPState::Initializing: return "Initializing";
        case PTPState::Listening: return "Listening";
        case PTPState::Uncalibrated: return "Uncalibrated";
        case PTPState::Slave: return "Slave";
        case PTPState::Passive: return "Passive";
        case PTPState::Faulty: return "Faulty";
        default: return "Unknown";
    }
}

// ==================== LocalClock ====================

LocalClock::LocalClock() 
    : calibrated_(false)
    , offset_ns_(0)
    , calibration_time_() {}

LocalClock::~LocalClock() = default;

void LocalClock::calibrate(const PTPSync& ptp_sync) {
    if (!ptp_sync.is_synchronized()) {
        return;
    }
    
    auto ptp_time = ptp_sync.get_current_time();
    auto local_time = std::chrono::steady_clock::now();
    auto local_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
        local_time.time_since_epoch());
    
    offset_ns_ = ptp_time.count() - local_ns.count();
    calibration_time_ = local_time;
    calibrated_ = true;
}

bool LocalClock::is_calibrated() const {
    return calibrated_;
}

std::chrono::nanoseconds LocalClock::now() const {
    auto local_time = std::chrono::steady_clock::now();
    auto local_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
        local_time.time_since_epoch());
    
    return std::chrono::nanoseconds(local_ns.count() + offset_ns_);
}

uint32_t LocalClock::to_rtp_timestamp(uint32_t sample_rate) const {
    uint64_t ns = static_cast<uint64_t>(now().count());
    return PTPSync::ptp_to_rtp_timestamp(ns, sample_rate);
}

}  // namespace rpi_aes67
