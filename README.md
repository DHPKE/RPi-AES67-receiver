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

## Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                    NMOS Controller                          │
│                  (Discovery & Control)                      │
└────────────────────┬────────────────────────────────────────┘
                     │ IS-04/IS-05
┌────────────────────▼────────────────────────────────────────┐
│              NMOS Node (nmos-cpp)                           │
│  ┌──────────────────────────────────────────────────────┐   │
│  │  Registration & Discovery │ Connection Management    │   │
│  └───────────────┬──────────────────────┬────────────────┘   │
└──────────────────┼──────────────────────┼────────────────────┘
                   │                      │
┌──────────────────▼──────────────────────▼────────────────────┐
│              AES67 Receiver Manager                         │
│  ┌────────────────┐  ┌────────────────┐  ┌──────────────┐   │
│  │  SDP Parser    │  │  RTP Receiver  │  │ PTP Client   │   │
│  └────────────────┘  └────────────────┘  └──────────────┘   │
└──────────────────────────────┬──────────────────────────────┘
                               │
┌──────────────────────────────▼──────────────────────────────┐
│              GStreamer Audio Pipeline                       │
│    rtpbin → depay → audioconvert → audioresample            │
└──────────────────────────────┬──────────────────────────────┘
                               │
┌──────────────────────────────▼──────────────────────────────┐
│              PipeWire Audio Server                          │
│           (System Audio Output/Routing)                     │
└─────────────────────────────────────────────────────────────┘
```

## Quick Start

### Prerequisites

Raspberry Pi 5 with:
- Raspberry Pi OS 64-bit (Bookworm or later)
- Ethernet connection
- Internet access for installation

### Installation

```bash
# Clone repository
git clone https://github.com/DHPKE/aes67-nmos-receiver-rpi5.git
cd aes67-nmos-receiver-rpi5

# Run installation script (installs dependencies and builds)
sudo ./scripts/install.sh

# Configure network for AES67
sudo ./scripts/setup_network.sh eth0

# Setup PTP synchronization
sudo ./scripts/setup_ptp.sh

# Enable and start service
sudo systemctl enable aes67-receiver
sudo systemctl start aes67-receiver
```

### Configuration

Edit `/etc/aes67-receiver/config.json`:

```json
{
  "node": {
    "label": "RPi5 AES67 Receiver",
    "description": "AES67 Audio Receiver",
    "tags": {}
  },
  "receivers": [
    {
      "id": "receiver-1",
      "label": "Main Receiver",
      "channels": 2,
      "sample_rates": [48000, 96000],
      "bit_depths": [24]
    }
  ],
  "network": {
    "interface": "eth0",
    "registry_url": "http://nmos-registry.local:3000"
  },
  "audio": {
    "pipewire_node_name": "AES67-Receiver",
    "buffer_size": 256
  }
}
```

### Check Status

```bash
# Service status
sudo systemctl status aes67-receiver

# View logs
journalctl -u aes67-receiver -f

# Check NMOS registration
curl http://localhost:8080/x-nmos/node/v1.3/self

# Check PTP sync status
sudo pmc -u -b 0 'GET CURRENT_DATA_SET'
```

## Building from Source

### Dependencies

```bash
sudo apt-get update
sudo apt-get install -y \
    build-essential cmake git \
    libboost-all-dev \
    libssl-dev \
    libavahi-client-dev \
    nlohmann-json3-dev \
    libgstreamer1.0-dev \
    libgstreamer-plugins-base1.0-dev \
    gstreamer1.0-plugins-good \
    gstreamer1.0-plugins-bad \
    libpipewire-0.3-dev \
    pipewire pipewire-audio-client-libraries \
    linuxptp
```

### Build nmos-cpp

```bash
git clone https://github.com/sony/nmos-cpp.git
cd nmos-cpp
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j4
sudo make install
```

### Build AES67 Receiver

```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j4
sudo make install
```

## Usage

### Auto-Discovery Mode

The receiver automatically:
1. Registers with NMOS registry (via mDNS or configured URL)
2. Advertises receiver capabilities
3. Waits for connection from NMOS controller

### Manual Connection

For testing without NMOS controller:

```bash
# Send connection request
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

# Activate connection
curl -X PATCH http://localhost:8080/x-nmos/connection/v1.1/single/receivers/receiver-1/active \
  -H "Content-Type: application/json" \
  -d '{}'
```

## Network Configuration

### Multicast Configuration

```bash
# Add multicast route
sudo ip route add 239.0.0.0/8 dev eth0

# Increase receive buffers
sudo sysctl -w net.core.rmem_max=134217728
sudo sysctl -w net.core.rmem_default=134217728
```

### PTP Configuration

Edit `/etc/linuxptp/ptp4l.conf`:

```ini
[global]
priority1 128
priority2 128
domainNumber 0
clockClass 248
clockAccuracy 0xFE
offsetScaledLogVariance 0xFFFF
free_running 0
freq_est_interval 1
dscp_event 46
dscp_general 34
dataset_comparison G.8275.x
G.8275.defaultDS.localPriority 128
summary_interval 0
kernel_leap 1
check_fup_sync 0
clock_servo pi
servo_num_offset_values 10
servo_offset_threshold 200
write_phase_mode 0
pi_proportional_const 0.0
pi_integral_const 0.0
pi_proportional_scale 0.0
pi_proportional_exponent -0.3
pi_proportional_norm_max 0.7
pi_integral_scale 0.0
pi_integral_exponent 0.4
pi_integral_norm_max 0.3
step_threshold 0.0
first_step_threshold 0.00002
max_frequency 900000000
clock_type OC
network_transport UDPv4
delay_mechanism E2E
time_stamping hardware
tsproc_mode filter
delay_filter moving_median
delay_filter_length 10
egressLatency 0
ingressLatency 0
boundary_clock_jbod 0
```

## Monitoring

### Web Interface

Access NMOS Node API: `http://raspberry-pi-ip:8080`

Endpoints:
- `/x-nmos/node/v1.3/` - Node API
- `/x-nmos/connection/v1.1/` - Connection API

### Command Line Tools

```bash
# Monitor audio levels
pw-top

# Check PipeWire graph
pw-dump

# Network statistics
watch -n1 'netstat -suna | grep -i "packet receive errors"'

# PTP status
watch -n1 'sudo pmc -u -b 0 "GET CURRENT_DATA_SET"'
```

## Performance Tuning

### CPU Governor

```bash
# Set performance mode
echo performance | sudo tee /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor
```

### Real-time Priority

Edit `/etc/security/limits.conf`:

```
@audio - rtprio 95
@audio - memlock unlimited
```

### Network Tuning

```bash
# Increase network buffer sizes
sudo sysctl -w net.core.rmem_max=134217728
sudo sysctl -w net.core.wmem_max=134217728
sudo sysctl -w net.core.netdev_max_backlog=5000

# Disable IPv6 if not needed
sudo sysctl -w net.ipv6.conf.all.disable_ipv6=1
```

## Troubleshooting

### No NMOS Registration

```bash
# Check mDNS
avahi-browse -a

# Check registry connectivity
curl http://registry-ip:3000/x-nmos/registration/v1.3/

# Check logs
journalctl -u aes67-receiver | grep -i registration
```

### No Audio Output

```bash
# Check PipeWire
systemctl --user status pipewire

# Check audio routing
pw-link -i

# Test with dummy source
gst-launch-1.0 audiotestsrc ! autoaudiosink
```

### PTP Not Syncing

```bash
# Check PTP status
sudo pmc -u -b 0 'GET CURRENT_DATA_SET'

# Check network interface supports hardware timestamping
ethtool -T eth0

# Restart PTP
sudo systemctl restart ptp4l
```

### High Latency

1. Check buffer settings in config.json
2. Verify PTP synchronization
3. Check CPU governor (set to performance)
4. Reduce other system load

## API Documentation

See [API.md](docs/API.md) for complete REST API documentation.

## Architecture Details

See [ARCHITECTURE.md](docs/ARCHITECTURE.md) for in-depth technical documentation.

## Contributing

Contributions welcome! Please read [CONTRIBUTING.md](CONTRIBUTING.md) first.

## License

MIT License - see [LICENSE](LICENSE)

## References

- [AES67 Standard](http://www.aes.org/publications/standards/search.cfm?docID=96)
- [NMOS Specifications](https://specs.amwa.tv/nmos/)
- [nmos-cpp](https://github.com/sony/nmos-cpp)
- [PipeWire Documentation](https://docs.pipewire.org/)
- [IEEE 1588 PTP](https://standards.ieee.org/standard/1588-2019.html)

## Support

For issues and questions:
- GitHub Issues: https://github.com/DHPKE/aes67-nmos-receiver-rpi5/issues
- Discussions: https://github.com/DHPKE/aes67-nmos-receiver-rpi5/discussions
