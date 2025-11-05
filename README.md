# AES67 Audio Receiver with NMOS for Raspberry Pi 5

A professional-grade AES67/RTP audio receiver with full NMOS IS-04/IS-05 compatibility, optimized for Raspberry Pi 5.

## Features

- ✅ **AES67 Compliant**: Full AES67/RTP audio streaming support
- ✅ **NMOS Compatible**: IS-04 (Discovery), IS-05 (Connection Management)
- ✅ **PTP Synchronized**: IEEE 1588 PTPv2 for network timing
- ✅ **PipeWire Integration**: Modern Linux audio with low latency
- ✅ **Multi-Channel**: Support for stereo and multichannel audio (up to 8 channels)
- ✅ **Multiple Sample Rates**: 44.1kHz, 48kHz, 96kHz
- ✅ **Production Ready**: Systemd service, auto-recovery, logging

## Quick Start

### Installation

```bash
git clone https://github.com/DHPKE/RPi-AES67-receiver.git
cd RPi-AES67-receiver
sudo ./scripts/install.sh
sudo ./scripts/setup_network.sh eth0
sudo ./scripts/setup_ptp.sh
sudo systemctl enable aes67-receiver
sudo systemctl start aes67-receiver
```