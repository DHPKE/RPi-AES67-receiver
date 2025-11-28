// SPDX-License-Identifier: MIT
/*
 * RPi-AES67 - Professional AES67 Sender/Receiver for Raspberry Pi 5
 * Copyright (c) 2025 DHPKE
 *
 * Logging system with spdlog-like interface.
 */

#pragma once

#include <string>
#include <memory>
#include <sstream>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <fstream>
#include <mutex>
#include <ctime>

namespace rpi_aes67 {

/**
 * @brief Log level enumeration
 */
enum class LogLevel {
    Trace,
    Debug,
    Info,
    Warning,
    Error,
    Critical,
    Off
};

/**
 * @brief Simple logging class
 */
class Logger {
public:
    /**
     * @brief Initialize the logger
     * @param name Logger name
     * @param level Log level
     * @param file Optional log file path
     */
    static void init(const std::string& name = "rpi_aes67",
                     LogLevel level = LogLevel::Info,
                     const std::string& file = "");
    
    /**
     * @brief Set the log level
     * @param level New log level
     */
    static void set_level(LogLevel level);
    
    /**
     * @brief Get current log level
     */
    static LogLevel get_level();
    
    /**
     * @brief Parse log level from string
     */
    static LogLevel parse_level(const std::string& level_str);
    
    /**
     * @brief Log a trace message
     */
    template<typename... Args>
    static void trace(const std::string& fmt, Args&&... args) {
        log(LogLevel::Trace, fmt, std::forward<Args>(args)...);
    }
    
    /**
     * @brief Log a debug message
     */
    template<typename... Args>
    static void debug(const std::string& fmt, Args&&... args) {
        log(LogLevel::Debug, fmt, std::forward<Args>(args)...);
    }
    
    /**
     * @brief Log an info message
     */
    template<typename... Args>
    static void info(const std::string& fmt, Args&&... args) {
        log(LogLevel::Info, fmt, std::forward<Args>(args)...);
    }
    
    /**
     * @brief Log a warning message
     */
    template<typename... Args>
    static void warning(const std::string& fmt, Args&&... args) {
        log(LogLevel::Warning, fmt, std::forward<Args>(args)...);
    }
    
    /**
     * @brief Log an error message
     */
    template<typename... Args>
    static void error(const std::string& fmt, Args&&... args) {
        log(LogLevel::Error, fmt, std::forward<Args>(args)...);
    }
    
    /**
     * @brief Log a critical message
     */
    template<typename... Args>
    static void critical(const std::string& fmt, Args&&... args) {
        log(LogLevel::Critical, fmt, std::forward<Args>(args)...);
    }
    
private:
    static std::string name_;
    static LogLevel level_;
    static std::string file_path_;
    static std::ofstream file_stream_;
    static std::mutex mutex_;
    static bool initialized_;
    
    static std::string level_to_string(LogLevel level);
    static std::string get_timestamp();
    
    template<typename... Args>
    static void log(LogLevel level, const std::string& fmt, Args&&... args) {
        if (level < level_ || level_ == LogLevel::Off) {
            return;
        }
        
        std::string message = format_message(fmt, std::forward<Args>(args)...);
        do_log(level, message);
    }
    
    static void log(LogLevel level, const std::string& message) {
        if (level < level_ || level_ == LogLevel::Off) {
            return;
        }
        do_log(level, message);
    }
    
    static void do_log(LogLevel level, const std::string& message);
    
    // Simple format function (simplified - no full fmt-style formatting)
    template<typename T>
    static std::string to_string_helper(const T& value) {
        std::ostringstream oss;
        oss << value;
        return oss.str();
    }
    
    static std::string format_message(const std::string& fmt) {
        return fmt;
    }
    
    template<typename T, typename... Rest>
    static std::string format_message(const std::string& fmt, T&& first, Rest&&... rest) {
        std::string result;
        size_t pos = 0;
        size_t brace_pos = fmt.find("{}", pos);
        
        if (brace_pos != std::string::npos) {
            result = fmt.substr(0, brace_pos) + to_string_helper(first);
            result += format_message(fmt.substr(brace_pos + 2), std::forward<Rest>(rest)...);
        } else {
            result = fmt;
        }
        
        return result;
    }
};

// Convenience macros
#define LOG_TRACE(...) rpi_aes67::Logger::trace(__VA_ARGS__)
#define LOG_DEBUG(...) rpi_aes67::Logger::debug(__VA_ARGS__)
#define LOG_INFO(...) rpi_aes67::Logger::info(__VA_ARGS__)
#define LOG_WARNING(...) rpi_aes67::Logger::warning(__VA_ARGS__)
#define LOG_ERROR(...) rpi_aes67::Logger::error(__VA_ARGS__)
#define LOG_CRITICAL(...) rpi_aes67::Logger::critical(__VA_ARGS__)

}  // namespace rpi_aes67
