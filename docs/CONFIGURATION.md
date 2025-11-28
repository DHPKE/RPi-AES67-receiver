# Configuration Reference

This document describes all configuration options for RPi-AES67.

## Configuration File

Default location: `/etc/rpi-aes67/config.json`

## Full Example

```json
{
  "node": {
    "id": "550e8400-e29b-41d4-a716-446655440000",
    "label": "RPi5 AES67 Node",
    "description": "Professional AES67 Sender/Receiver",
    "tags": {
      "location": "Studio A",
      "device_type": "raspberry_pi_5"
    }
  },
  "senders": [
    {
      "id": "sender-1",
      "label": "Main Output",
      "description": "Primary audio output stream",
      "channels": 2,
      "sample_rate": 48000,
      "bit_depth": 24,
      "multicast_ip": "239.69.1.1",
      "port": 5004,
      "payload_type": 97,
      "pipewire_source": "alsa_input.usb-device",
      "enabled": true,
      "packet_time_us": 1000
    }
  ],
  "receivers": [
    {
      "id": "receiver-1",
      "label": "Main Input",
      "description": "Primary audio input stream",
      "channels": 2,
      "sample_rates": [44100, 48000, 96000],
      "bit_depths": [16, 24],
      "pipewire_sink": "alsa_output.platform-sound",
      "enabled": true
    }
  ],
  "network": {
    "interface": "eth0",
    "ptp_domain": 0,
    "registry_url": "http://nmos-registry.local:3000",
    "enable_mdns": true,
    "node_port": 8080,
    "connection_port": 8081
  },
  "audio": {
    "buffer_size_ms": 5.0,
    "jitter_buffer_ms": 10.0,
    "buffer_frames": 256,
    "enable_sample_rate_conversion": true
  },
  "logging": {
    "level": "info",
    "file": "/var/log/rpi-aes67.log",
    "enable_console": true
  }
}
```

## Node Configuration

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `id` | string | auto-generated | UUID for this node |
| `label` | string | "RPi5 AES67 Node" | Human-readable name |
| `description` | string | "" | Description text |
| `tags` | object | {} | Key-value metadata tags |

## Sender Configuration

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `id` | string | required | Unique sender ID |
| `label` | string | "" | Human-readable name |
| `description` | string | "" | Description text |
| `channels` | integer | 2 | Number of audio channels (1-64) |
| `sample_rate` | integer | 48000 | Sample rate (44100, 48000, 96000) |
| `bit_depth` | integer | 24 | Bits per sample (16, 24, 32) |
| `multicast_ip` | string | "239.69.1.1" | Destination multicast IP |
| `port` | integer | 5004 | RTP destination port |
| `payload_type` | integer | 97 | RTP payload type (96-127) |
| `pipewire_source` | string | "" | PipeWire source device name |
| `enabled` | boolean | true | Enable this sender |
| `packet_time_us` | integer | 1000 | Packet time in microseconds |

### AES67 Packet Time

The `packet_time_us` must be one of:
- 125 (125µs)
- 250 (250µs)
- 333 (333µs)
- 1000 (1ms) - **mandatory for AES67**
- 4000 (4ms)

## Receiver Configuration

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `id` | string | required | Unique receiver ID |
| `label` | string | "" | Human-readable name |
| `description` | string | "" | Description text |
| `channels` | integer | 2 | Maximum supported channels |
| `sample_rates` | array | [44100, 48000, 96000] | Supported sample rates |
| `bit_depths` | array | [16, 24] | Supported bit depths |
| `pipewire_sink` | string | "" | PipeWire sink device name |
| `enabled` | boolean | true | Enable this receiver |

## Network Configuration

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `interface` | string | "eth0" | Network interface for AES67 |
| `ptp_domain` | integer | 0 | PTP domain number (0-127) |
| `registry_url` | string | "" | NMOS registry URL (empty = peer-to-peer) |
| `enable_mdns` | boolean | true | Enable mDNS discovery |
| `node_port` | integer | 8080 | HTTP API port for NMOS Node API |
| `connection_port` | integer | 8081 | HTTP API port for Connection API |

## Audio Processing Configuration

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `buffer_size_ms` | float | 5.0 | Audio buffer size in milliseconds |
| `jitter_buffer_ms` | float | 10.0 | Jitter buffer target delay |
| `buffer_frames` | integer | 256 | Buffer size in audio frames |
| `enable_sample_rate_conversion` | boolean | true | Allow sample rate conversion |

### Buffer Sizing

- Lower values = lower latency, higher risk of dropouts
- Higher values = more stable, higher latency

Recommended values:
- **Low latency**: `buffer_size_ms: 2.0`, `jitter_buffer_ms: 5.0`
- **Balanced** (default): `buffer_size_ms: 5.0`, `jitter_buffer_ms: 10.0`
- **High stability**: `buffer_size_ms: 10.0`, `jitter_buffer_ms: 20.0`

## Logging Configuration

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `level` | string | "info" | Log level |
| `file` | string | "" | Log file path (empty = no file) |
| `enable_console` | boolean | true | Output to console |

### Log Levels

- `trace`: Very detailed debugging
- `debug`: Debug information
- `info`: Normal operation
- `warning`: Potential issues
- `error`: Errors
- `critical`: Fatal errors
- `off`: Disable logging

## Environment Variables

Configuration can also be set via environment variables:

| Variable | Description |
|----------|-------------|
| `RPI_AES67_CONFIG` | Configuration file path |
| `RPI_AES67_LOG_LEVEL` | Override log level |
| `RPI_AES67_INTERFACE` | Override network interface |

## Multiple Senders/Receivers

You can configure multiple senders and receivers:

```json
{
  "senders": [
    {"id": "sender-1", "label": "Output 1", "multicast_ip": "239.69.1.1", "port": 5004},
    {"id": "sender-2", "label": "Output 2", "multicast_ip": "239.69.1.2", "port": 5004},
    {"id": "sender-3", "label": "Output 3", "multicast_ip": "239.69.1.3", "port": 5004}
  ],
  "receivers": [
    {"id": "receiver-1", "label": "Input 1"},
    {"id": "receiver-2", "label": "Input 2"},
    {"id": "receiver-3", "label": "Input 3"}
  ]
}
```

## PipeWire Device Names

To find available PipeWire devices:

```bash
# List all audio sinks (outputs)
pw-cli list-objects | grep -A5 "type=PipeWire:Interface:Node"

# Or use wpctl
wpctl status
```

Common device names:
- `alsa_output.platform-soc_sound.stereo-fallback` - Built-in audio
- `alsa_output.usb-*` - USB audio devices
- `alsa_input.usb-*` - USB microphones
