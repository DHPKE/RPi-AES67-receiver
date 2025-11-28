// SPDX-License-Identifier: MIT
/*
 * RPi-AES67 - Professional AES67 Sender/Receiver for Raspberry Pi 5
 * Copyright (c) 2025 DHPKE
 *
 * Logger implementation.
 */

#include "rpi_aes67/logger.h"
#include <algorithm>
#include <cctype>

namespace rpi_aes67 {

// Static members
std::string Logger::name_ = "rpi_aes67";
LogLevel Logger::level_ = LogLevel::Info;
std::string Logger::file_path_;
std::ofstream Logger::file_stream_;
std::mutex Logger::mutex_;
bool Logger::initialized_ = false;

void Logger::init(const std::string& name, LogLevel level, const std::string& file) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    name_ = name;
    level_ = level;
    file_path_ = file;
    
    if (!file.empty()) {
        file_stream_.open(file, std::ios::app);
        if (!file_stream_.is_open()) {
            std::cerr << "[ERROR] Failed to open log file: " << file << std::endl;
        }
    }
    
    initialized_ = true;
}

void Logger::set_level(LogLevel level) {
    level_ = level;
}

LogLevel Logger::get_level() {
    return level_;
}

LogLevel Logger::parse_level(const std::string& level_str) {
    std::string lower = level_str;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    
    if (lower == "trace") return LogLevel::Trace;
    if (lower == "debug") return LogLevel::Debug;
    if (lower == "info") return LogLevel::Info;
    if (lower == "warning" || lower == "warn") return LogLevel::Warning;
    if (lower == "error") return LogLevel::Error;
    if (lower == "critical" || lower == "fatal") return LogLevel::Critical;
    if (lower == "off" || lower == "none") return LogLevel::Off;
    
    return LogLevel::Info;  // Default
}

std::string Logger::level_to_string(LogLevel level) {
    switch (level) {
        case LogLevel::Trace: return "TRACE";
        case LogLevel::Debug: return "DEBUG";
        case LogLevel::Info: return "INFO";
        case LogLevel::Warning: return "WARN";
        case LogLevel::Error: return "ERROR";
        case LogLevel::Critical: return "CRIT";
        case LogLevel::Off: return "OFF";
        default: return "UNKNOWN";
    }
}

std::string Logger::get_timestamp() {
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()) % 1000;
    
    std::tm tm_buf{};
#ifdef _WIN32
    localtime_s(&tm_buf, &time);
#else
    localtime_r(&time, &tm_buf);
#endif
    
    std::ostringstream oss;
    oss << std::put_time(&tm_buf, "%Y-%m-%d %H:%M:%S")
        << '.' << std::setfill('0') << std::setw(3) << ms.count();
    
    return oss.str();
}

void Logger::do_log(LogLevel level, const std::string& message) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    std::ostringstream oss;
    oss << "[" << get_timestamp() << "] "
        << "[" << level_to_string(level) << "] "
        << "[" << name_ << "] "
        << message;
    
    std::string formatted = oss.str();
    
    // Output to console
    if (level >= LogLevel::Error) {
        std::cerr << formatted << std::endl;
    } else {
        std::cout << formatted << std::endl;
    }
    
    // Output to file if configured
    if (file_stream_.is_open()) {
        file_stream_ << formatted << std::endl;
        file_stream_.flush();
    }
}

}  // namespace rpi_aes67
