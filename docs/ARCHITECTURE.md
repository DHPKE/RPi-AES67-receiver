# Architecture

This document describes the architecture of RPi-AES67, a professional-grade AES67 sender/receiver implementation for Raspberry Pi 5.

## Overview

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                           NMOS Controller                                    │
│                        (Discovery & Control)                                 │
└───────────────────────────────┬─────────────────────────────────────────────┘
                                │ IS-04/IS-05 HTTP API
┌───────────────────────────────▼─────────────────────────────────────────────┐
│                           NMOSNode                                          │
│  ┌─────────────────────────────────────────────────────────────────────┐   │
│  │  IS-04 Registration & Discovery  │  IS-05 Connection Management     │   │
│  │  - Node/Device/Source/Flow       │  - Staged/Active parameters      │   │
│  │  - Sender/Receiver resources     │  - SDP-based connections         │   │
│  │  - mDNS/DNS-SD discovery         │  - Transport parameter handling  │   │
│  └───────────────────┬──────────────────────────┬──────────────────────┘   │
└──────────────────────┼──────────────────────────┼──────────────────────────┘
                       │                          │
┌──────────────────────▼──────────┐ ┌─────────────▼───────────────────────────┐
│       AES67Sender               │ │           AES67Receiver                 │
│  ┌────────────────────────┐     │ │  ┌────────────────────────┐             │
│  │  PipeWire Audio Input  │     │ │  │     SDP Parser         │             │
│  │  - Capture callback    │     │ │  │  - AES67 validation    │             │
│  │  - Sample rate support │     │ │  │  - Format extraction   │             │
│  └──────────┬─────────────┘     │ │  └──────────┬─────────────┘             │
│             │                   │ │             │                           │
│  ┌──────────▼─────────────┐     │ │  ┌──────────▼─────────────┐             │
│  │   RTP Packetizer       │     │ │  │  UDP Socket Receiver   │             │
│  │  - L16/L24 encoding    │     │ │  │  - Multicast support   │             │
│  │  - Timestamp from PTP  │     │ │  │  - Large buffer config │             │
│  │  - Sequence numbering  │     │ │  └──────────┬─────────────┘             │
│  └──────────┬─────────────┘     │ │             │                           │
│             │                   │ │  ┌──────────▼─────────────┐             │
│  ┌──────────▼─────────────┐     │ │  │    RTP Depacketizer    │             │
│  │   UDP Socket Sender    │     │ │  │  - Header parsing      │             │
│  │  - Multicast output    │     │ │  │  - Payload extraction  │             │
│  │  - TTL configuration   │     │ │  └──────────┬─────────────┘             │
│  └──────────┬─────────────┘     │ │             │                           │
│             │                   │ │  ┌──────────▼─────────────┐             │
│  ┌──────────▼─────────────┐     │ │  │    Jitter Buffer       │             │
│  │    SDP Generator       │     │ │  │  - Packet reordering   │             │
│  │  - AES67 compliant     │     │ │  │  - Delay management    │             │
│  │  - PTP clock reference │     │ │  │  - Loss concealment    │             │
│  └────────────────────────┘     │ │  └──────────┬─────────────┘             │
└─────────────────────────────────┘ │             │                           │
                                    │  ┌──────────▼─────────────┐             │
                                    │  │  PipeWire Audio Output │             │
                                    │  │  - Low-latency output  │             │
                                    │  │  - Format conversion   │             │
                                    │  └────────────────────────┘             │
                                    └─────────────────────────────────────────┘

┌─────────────────────────────────────────────────────────────────────────────┐
│                              PTPSync                                         │
│  ┌─────────────────────────────────────────────────────────────────────┐   │
│  │  Software PTP Follower (IEEE 1588-2019)                             │   │
│  │  - Clock synchronization with network master                         │   │
│  │  - RTP timestamp generation for AES67                               │   │
│  │  - Integration with linuxptp for hardware timestamping              │   │
│  └─────────────────────────────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────────────────────────────┘
```

## Components

### NMOSNode

The NMOS node implements:

- **IS-04 Discovery & Registration**
  - Automatic registration with NMOS registry
  - mDNS/DNS-SD for peer-to-peer discovery
  - Node, Device, Source, Flow, Sender, and Receiver resources
  - Heartbeat and health monitoring

- **IS-05 Connection Management**
  - Staged/Active connection model
  - SDP-based transport parameter handling
  - Automatic connection establishment
  - Bulk operations support

### AES67Sender

The sender captures audio and transmits as AES67-compliant RTP streams:

- **Audio Capture**: PipeWire integration for low-latency capture
- **RTP Packetization**: Standard L16/L24/L32 encoding
- **PTP Synchronization**: Timestamps aligned to network time
- **SDP Generation**: AES67-compliant session descriptions

### AES67Receiver

The receiver handles incoming AES67 streams and plays to audio output:

- **RTP Reception**: UDP multicast with large receive buffers
- **Jitter Buffer**: Adaptive buffering for smooth playout
- **SDP Parsing**: Automatic format detection from SDP
- **Audio Output**: PipeWire integration for flexible routing

### PTPSync

Software-based PTP follower for network time synchronization:

- **IEEE 1588-2019**: Compatible with AES67 PTP profile
- **linuxptp Integration**: Uses system PTP daemon when available
- **RTP Timestamp Generation**: Converts PTP time to RTP timestamps

### PipeWireIO

Audio I/O integration with modern Linux audio:

- **PipeWireInput**: Capture from any PipeWire source
- **PipeWireOutput**: Playback to any PipeWire sink
- **Device Discovery**: Automatic enumeration of audio devices
- **Format Support**: 16/24/32-bit, multiple sample rates

## Audio Flow

### Sender Path

```
PipeWire Source → Audio Callback → RTP Packetizer → UDP Multicast
                                         ↑
                              PTP Timestamp Generation
```

### Receiver Path

```
UDP Multicast → RTP Parser → Jitter Buffer → Audio Output → PipeWire Sink
                                   ↑
                        PTP-synchronized Playout
```

## Configuration

The system uses JSON configuration files for all settings:

```json
{
  "node": {
    "id": "uuid",
    "label": "Device Name",
    "description": "Description"
  },
  "senders": [...],
  "receivers": [...],
  "network": {
    "interface": "eth0",
    "ptp_domain": 0,
    "registry_url": "http://registry:3000"
  },
  "audio": {
    "buffer_size_ms": 5.0,
    "jitter_buffer_ms": 10.0
  }
}
```

## Thread Model

- **Main Thread**: Configuration, NMOS node, health monitoring
- **HTTP Server Thread**: Handles NMOS API requests
- **Receiver Threads**: One per active receiver (UDP receive + playout)
- **PTP Monitor Thread**: Tracks synchronization status
- **PipeWire Threads**: Managed by PipeWire for real-time audio

## Performance Considerations

### Raspberry Pi 5 Optimizations

- **CPU Affinity**: Audio threads pinned to performance cores
- **Real-time Priority**: SCHED_FIFO for audio processing
- **Lock-free Buffers**: Minimal blocking in audio path
- **Zero-copy Transfer**: Direct buffer access where possible

### Network Tuning

- **Large Receive Buffers**: 2MB+ for packet bursts
- **Multicast Optimization**: IGMP snooping aware
- **QoS Marking**: DSCP values for priority traffic
