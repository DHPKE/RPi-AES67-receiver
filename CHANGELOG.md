# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [2.0.0] - 2025

### Added
- **Bidirectional Operation**: Full AES67 sender capability in addition to receiver
- **Native C++ Implementation**: Replaced GStreamer with native RTP handling
- **Jitter Buffer**: Adaptive jitter buffering for smooth audio playout
- **SDP Generator**: Generate AES67-compliant SDP for senders
- **Multiple Streams**: Support for 4+ simultaneous senders/receivers
- **Operation Modes**: Separate sender, receiver, and bidirectional modes
- **Example Applications**: simple_sender, simple_receiver, bidirectional
- **vcpkg Support**: Added vcpkg.json for dependency management
- **Comprehensive Documentation**: ARCHITECTURE.md, BUILDING.md, CONFIGURATION.md, API.md

### Changed
- **Project Name**: Renamed from aes67-nmos-receiver to rpi-aes67
- **Architecture**: Complete rewrite based on ravennakit patterns
- **Configuration**: New JSON configuration format with sender support
- **NMOS Implementation**: Native NMOS IS-04/IS-05 instead of nmos-cpp
- **PTP Synchronization**: Software-based PTP follower integration
- **SystemD Services**: Three separate services for different operation modes
- **Installation**: Simplified installation without nmos-cpp dependency

### Removed
- **GStreamer Dependency**: No longer required
- **nmos-cpp Dependency**: Replaced with native implementation
- **OpenSSL Dependency**: No longer required for core functionality
- **Boost Dependency**: No longer required

### Fixed
- Reduced installation time (no more 60-90 minute nmos-cpp build)
- Lower resource usage without GStreamer overhead
- Better PTP integration for accurate timestamps

## [1.0.0] - 2024

### Initial Release
- AES67 audio receiver
- NMOS IS-04/IS-05 support via nmos-cpp
- GStreamer-based RTP reception
- PipeWire audio output
- linuxptp PTP synchronization
- Systemd service support
