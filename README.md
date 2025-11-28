# RPi-AES67 - Professional AES67 Sender/Receiver for Raspberry Pi 5

A professional-grade, bidirectional AES67 audio over IP implementation with full NMOS IS-04/IS-05 support, optimized for Raspberry Pi 5.

## Features

### AES67 Audio Streaming
- ✅ **Bidirectional Operation**: Simultaneous send and receive
- ✅ **AES67 Compliant**: Full AES67/RAVENNA/ST2110-30 compatibility
- ✅ **Multiple Sample Rates**: 44.1kHz, 48kHz, 96kHz
- ✅ **Multiple Bit Depths**: 16-bit, 24-bit, 32-bit linear PCM
- ✅ **Multi-Channel**: Up to 64 channels per stream
- ✅ **PTP Synchronized**: IEEE 1588-2019 PTPv2 for network timing

### NMOS Support
- ✅ **IS-04 Discovery & Registration**: Automatic device discovery
- ✅ **IS-05 Connection Management**: Staged/active connection model
- ✅ **mDNS/DNS-SD**: Peer-to-peer discovery
- ✅ **Registry Support**: Works with NMOS registries

### Audio Integration
- ✅ **PipeWire Integration**: Modern Linux audio with low latency
- ✅ **Multiple Streams**: 4+ simultaneous senders/receivers
- ✅ **Jitter Buffering**: Adaptive buffering for smooth playout
- ✅ **Auto Recovery**: Automatic reconnection on failures

## Architecture

```
┌─────────────────────────────────────────────────────────────────┐
│                      NMOS Controller                             │
└─────────────────────────────┬───────────────────────────────────┘
                              │ IS-04/IS-05
┌─────────────────────────────▼───────────────────────────────────┐
│                        NMOSNode                                  │
│     IS-04 Registration & Discovery │ IS-05 Connection Mgmt     │
└──────────────────┬──────────────────────────┬───────────────────┘
                   │                          │
┌──────────────────▼──────────┐ ┌─────────────▼──────────────────┐
│      AES67Sender            │ │        AES67Receiver           │
│  ┌──────────────────────┐   │ │  ┌──────────────────────────┐  │
│  │ PipeWire Input       │   │ │  │ SDP Parser               │  │
│  │ RTP Packetizer       │   │ │  │ RTP Depacketizer         │  │
│  │ SDP Generator        │   │ │  │ Jitter Buffer            │  │
│  │ PTP Timestamps       │   │ │  │ PipeWire Output          │  │
│  └──────────────────────┘   │ │  └──────────────────────────┘  │
└─────────────────────────────┘ └────────────────────────────────┘
                   │                          │
┌──────────────────▼──────────────────────────▼──────────────────┐
│                       PTPSync                                   │
│            IEEE 1588-2019 PTP Follower                         │
└────────────────────────────────────────────────────────────────┘
```

## Quick Start

### Prerequisites

- Raspberry Pi 5 with Raspberry Pi OS 64-bit (Bookworm+)
- Ethernet connection to AES67 network
- PTP master clock on the network

### Installation

```bash
# Clone repository
git clone https://github.com/DHPKE/RPi-AES67-receiver.git
cd RPi-AES67-receiver

# Install dependencies
sudo apt-get update
sudo apt-get install -y \
    build-essential cmake git pkg-config \
    nlohmann-json3-dev \
    libpipewire-0.3-dev pipewire wireplumber \
    linuxptp ethtool

# Build
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j4

# Install
sudo make install
```

### Network Setup

```bash
# Configure network for AES67
sudo ./scripts/setup_network.sh eth0

# Setup PTP synchronization
sudo ./scripts/setup_ptp.sh eth0
```

### Configuration

Edit `/etc/rpi-aes67/config.json`:

```json
{
  "node": {
    "label": "RPi5 AES67 Node",
    "description": "Professional AES67 Sender/Receiver"
  },
  "senders": [
    {
      "id": "sender-1",
      "label": "Main Output",
      "multicast_ip": "239.69.1.1",
      "port": 5004,
      "channels": 2,
      "sample_rate": 48000,
      "bit_depth": 24
    }
  ],
  "receivers": [
    {
      "id": "receiver-1",
      "label": "Main Input",
      "channels": 2,
      "sample_rates": [48000, 96000]
    }
  ],
  "network": {
    "interface": "eth0",
    "ptp_domain": 0,
    "registry_url": "http://nmos-registry.local:3000"
  }
}
```

### Running

```bash
# Bidirectional mode (default)
rpi-aes67 -c /etc/rpi-aes67/config.json

# Sender only
rpi-aes67 -c /etc/rpi-aes67/config.json -m sender

# Receiver only
rpi-aes67 -c /etc/rpi-aes67/config.json -m receiver

# With verbose logging
rpi-aes67 -v
```

### Systemd Service

```bash
# Enable bidirectional service
sudo systemctl enable aes67-bidirectional
sudo systemctl start aes67-bidirectional

# Check status
sudo systemctl status aes67-bidirectional

# View logs
journalctl -u aes67-bidirectional -f
```

## NMOS API

Access the NMOS Node API at `http://raspberry-pi-ip:8080`:

- `/x-nmos/node/v1.3/` - Node API
- `/x-nmos/node/v1.3/self` - Node information
- `/x-nmos/node/v1.3/senders` - Registered senders
- `/x-nmos/node/v1.3/receivers` - Registered receivers
- `/x-nmos/connection/v1.1/` - Connection API

### Manual Connection

```bash
# Connect receiver to a sender
curl -X PATCH http://localhost:8080/x-nmos/connection/v1.1/single/receivers/receiver-1/staged \
  -H "Content-Type: application/json" \
  -d '{
    "sender_id": "sender-uuid",
    "transport_params": [{
      "source_ip": "239.69.1.1",
      "destination_port": 5004,
      "rtp_enabled": true
    }]
  }'
```

## Examples

### Simple Receiver

```cpp
#include "rpi_aes67/receiver.h"

auto receiver = std::make_shared<rpi_aes67::AES67Receiver>();
receiver->configure({.id = "rx-1", .label = "My Receiver"});
receiver->initialize();
receiver->connect("239.69.1.1", 5004);
receiver->start();
```

### Simple Sender

```cpp
#include "rpi_aes67/sender.h"

auto sender = std::make_shared<rpi_aes67::AES67Sender>();
sender->configure({
    .id = "tx-1",
    .label = "My Sender",
    .multicast_ip = "239.69.1.1",
    .port = 5004
});
sender->initialize();
sender->start();

std::cout << sender->generate_sdp();
```

## Documentation

- [Architecture](docs/ARCHITECTURE.md) - System design and components
- [Building](docs/BUILDING.md) - Build instructions
- [Configuration](docs/CONFIGURATION.md) - Configuration reference
- [API Reference](docs/API.md) - Programming API

## Performance

Optimized for Raspberry Pi 5:

- **Latency**: < 10ms end-to-end on local network
- **Jitter**: < 1ms with PTP synchronization
- **Streams**: 4+ simultaneous senders/receivers
- **CPU Usage**: < 20% for stereo 48kHz streams
- **Stability**: 24+ hours continuous operation

## Network Requirements

- **Ethernet**: 100Mbps+ recommended (1Gbps for multiple streams)
- **PTP**: IEEE 1588-2019 compliant master clock
- **Multicast**: Proper IGMP snooping configuration
- **Buffer**: Large network buffers configured

## Troubleshooting

### No Audio Output

```bash
# Check PipeWire
systemctl --user status pipewire

# Check audio devices
pw-cli list-objects | grep Audio
```

### PTP Not Syncing

```bash
# Check PTP status
sudo pmc -u -b 0 'GET CURRENT_DATA_SET'

# Check for hardware timestamping
ethtool -T eth0
```

### NMOS Not Registering

```bash
# Check mDNS
avahi-browse -a | grep nmos

# Check registry connectivity
curl http://registry-ip:3000/x-nmos/registration/v1.3/
```

## License

MIT License - see [LICENSE](LICENSE)

## References

- [AES67 Standard](http://www.aes.org/publications/standards/search.cfm?docID=96)
- [NMOS Specifications](https://specs.amwa.tv/nmos/)
- [IEEE 1588 PTP](https://standards.ieee.org/standard/1588-2019.html)
- [PipeWire Documentation](https://docs.pipewire.org/)
- [ravennakit](https://github.com/soundondigital/ravennakit) - Architecture inspiration

## Support

- GitHub Issues: https://github.com/DHPKE/RPi-AES67-receiver/issues
- Discussions: https://github.com/DHPKE/RPi-AES67-receiver/discussions
