# Building RPi-AES67

This document describes how to build RPi-AES67 from source.

## Prerequisites

### Raspberry Pi 5

- Raspberry Pi OS 64-bit (Bookworm or later)
- At least 4GB RAM recommended
- Ethernet connection

### Required Packages

```bash
sudo apt-get update
sudo apt-get install -y \
    build-essential \
    cmake \
    git \
    pkg-config \
    nlohmann-json3-dev \
    libpipewire-0.3-dev \
    pipewire \
    pipewire-audio-client-libraries \
    wireplumber \
    linuxptp \
    ethtool
```

## Building

### Standard Build

```bash
# Clone repository
git clone https://github.com/DHPKE/RPi-AES67-receiver.git
cd RPi-AES67-receiver

# Create build directory
mkdir build && cd build

# Configure
cmake .. -DCMAKE_BUILD_TYPE=Release

# Build
make -j4

# Install (optional)
sudo make install
```

### Build Options

| Option | Default | Description |
|--------|---------|-------------|
| `BUILD_EXAMPLES` | ON | Build example applications |
| `BUILD_TESTS` | OFF | Build unit tests |
| `ENABLE_PIPEWIRE` | ON | Enable PipeWire audio support |
| `CMAKE_BUILD_TYPE` | Release | Build type (Debug/Release) |

Example with options:

```bash
cmake .. \
    -DCMAKE_BUILD_TYPE=Debug \
    -DBUILD_EXAMPLES=ON \
    -DBUILD_TESTS=ON
```

### Cross-Compilation for Raspberry Pi 5

From a Linux development machine:

```bash
# Install cross-compilation toolchain
sudo apt-get install gcc-aarch64-linux-gnu g++-aarch64-linux-gnu

# Configure for cross-compilation
cmake .. \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_SYSTEM_NAME=Linux \
    -DCMAKE_SYSTEM_PROCESSOR=aarch64 \
    -DCMAKE_C_COMPILER=aarch64-linux-gnu-gcc \
    -DCMAKE_CXX_COMPILER=aarch64-linux-gnu-g++

# Build
make -j$(nproc)
```

## Installation

After building:

```bash
# Install binaries and libraries
sudo make install

# Create configuration directory
sudo mkdir -p /etc/rpi-aes67
sudo cp ../config/config.json /etc/rpi-aes67/

# Install systemd services
sudo cp ../systemd/*.service /etc/systemd/system/
sudo systemctl daemon-reload

# Create service user
sudo useradd -r -s /bin/false aes67
sudo usermod -aG audio aes67
```

## Running

### Direct Execution

```bash
# Bidirectional mode (default)
./rpi-aes67 -c /etc/rpi-aes67/config.json

# Sender only
./rpi-aes67 -c /etc/rpi-aes67/config.json -m sender

# Receiver only
./rpi-aes67 -c /etc/rpi-aes67/config.json -m receiver

# Verbose output
./rpi-aes67 -v
```

### Systemd Service

```bash
# Enable and start bidirectional service
sudo systemctl enable aes67-bidirectional
sudo systemctl start aes67-bidirectional

# Check status
sudo systemctl status aes67-bidirectional

# View logs
journalctl -u aes67-bidirectional -f
```

## Troubleshooting Build Issues

### Missing nlohmann-json

If nlohmann-json is not found:

```bash
# Manual installation
git clone https://github.com/nlohmann/json.git
cd json
mkdir build && cd build
cmake ..
sudo make install
```

### PipeWire Not Found

Ensure PipeWire development headers are installed:

```bash
sudo apt-get install libpipewire-0.3-dev
```

Or build without PipeWire:

```bash
cmake .. -DENABLE_PIPEWIRE=OFF
```

### Permission Issues

For running without root:

```bash
# Add user to audio group
sudo usermod -aG audio $USER

# Enable real-time scheduling
sudo sh -c 'echo "@audio - rtprio 95" >> /etc/security/limits.conf'
sudo sh -c 'echo "@audio - memlock unlimited" >> /etc/security/limits.conf'

# Log out and back in for changes to take effect
```
