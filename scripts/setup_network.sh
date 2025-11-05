#!/bin/bash

set -e

if [ "$EUID" -ne 0 ]; then 
    echo "Please run as root"
    exit 1
fi

INTERFACE=${1:-eth0}

echo "Configuring $INTERFACE for AES67..."

# Add multicast route
ip route add 239.0.0.0/8 dev $INTERFACE 2>/dev/null || true

# Increase receive buffers
sysctl -w net.core.rmem_max=134217728
sysctl -w net.core.rmem_default=134217728
sysctl -w net.core.wmem_max=134217728
sysctl -w net.core.wmem_default=134217728
sysctl -w net.core.netdev_max_backlog=5000

# Make changes persistent
cat >> /etc/sysctl.conf << EOF
# AES67 Network Settings
net.core.rmem_max=134217728
net.core.rmem_default=134217728
net.core.wmem_max=134217728
net.core.wmem_default=134217728
net.core.netdev_max_backlog=5000
EOF

# Disable energy-efficient ethernet
ethtool -s $INTERFACE eee off 2>/dev/null || true

echo "Network configuration complete for $INTERFACE"