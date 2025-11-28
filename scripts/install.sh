#!/bin/bash

set -e

echo "==================================="
echo "RPi-AES67 Installation"
echo "Professional AES67 Sender/Receiver"
echo "==================================="

if [ "$EUID" -ne 0 ]; then 
    echo "Please run as root (use sudo)"
    exit 1
fi

# Check if running on Raspberry Pi
echo "Checking system..."
TOTAL_RAM=$(free -m | awk '/^Mem:/{print $2}')
echo "Total RAM: ${TOTAL_RAM}MB"

# Add swap if needed (for compilation)
if [ ! -f /swapfile ]; then
    echo "Creating 4GB swap file for compilation..."
    fallocate -l 4G /swapfile
    chmod 600 /swapfile
    mkswap /swapfile
    swapon /swapfile
    echo "Swap activated"
fi

echo "Updating system packages..."
apt-get update

echo "Installing system dependencies..."
apt-get install -y \
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

echo ""
echo "==================================="
echo "Building RPi-AES67..."
echo "==================================="
rm -rf build
mkdir -p build
cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
make install
cd ..
echo "✓ RPi-AES67 built and installed"

echo ""
echo "==================================="
echo "Setting up configuration..."
echo "==================================="
mkdir -p /etc/rpi-aes67
if [ ! -f /etc/rpi-aes67/config.json ]; then
    if [ -f config/config.json ]; then
        cp config/config.json /etc/rpi-aes67/
        echo "✓ Configuration copied"
    else
        echo "⚠ config/config.json not found, skipping"
    fi
fi

echo "Creating service user..."
if ! id -u aes67 > /dev/null 2>&1; then
    useradd -r -s /bin/false aes67
    usermod -aG audio aes67
    echo "✓ User 'aes67' created"
else
    echo "✓ User 'aes67' already exists"
fi

echo "Installing systemd services..."
for service in aes67-sender.service aes67-receiver.service aes67-bidirectional.service; do
    if [ -f systemd/$service ]; then
        cp systemd/$service /etc/systemd/system/
        echo "✓ Installed $service"
    fi
done
systemctl daemon-reload

echo "Configuring PipeWire..."
mkdir -p /etc/pipewire
cat > /etc/pipewire/pipewire.conf << 'EOF'
context.properties = {
    default.clock.rate = 48000
    default.clock.quantum = 256
    default.clock.min-quantum = 64
    default.clock.max-quantum = 2048
}
EOF
echo "✓ PipeWire configured"

echo "Enabling services..."
systemctl enable pipewire 2>/dev/null || true
systemctl enable wireplumber 2>/dev/null || true

# Configure real-time audio settings
echo "Configuring real-time audio settings..."
if ! grep -q "@audio - rtprio 95" /etc/security/limits.conf; then
    echo "@audio - rtprio 95" >> /etc/security/limits.conf
    echo "@audio - memlock unlimited" >> /etc/security/limits.conf
    echo "✓ Real-time audio settings configured"
fi

# Make swap permanent
if ! grep -q "/swapfile" /etc/fstab; then
    echo '/swapfile none swap sw 0 0' >> /etc/fstab
    echo "✓ Swap made permanent"
fi

echo ""
echo "==================================="
echo "Installation Complete!"
echo "==================================="
echo ""
echo "✓ All dependencies installed"
echo "✓ RPi-AES67 built and installed"
echo "✓ SystemD services installed"
echo ""
echo "Next steps:"
echo "1. Configure network: sudo ./scripts/setup_network.sh eth0"
echo "2. Setup PTP: sudo ./scripts/setup_ptp.sh eth0"
echo "3. Edit configuration: /etc/rpi-aes67/config.json"
echo "4. Start service:"
echo "   - For bidirectional: sudo systemctl start aes67-bidirectional"
echo "   - For sender only: sudo systemctl start aes67-sender"
echo "   - For receiver only: sudo systemctl start aes67-receiver"
echo "5. Check status: sudo systemctl status aes67-bidirectional"
echo ""
echo "Access NMOS API at: http://localhost:8080/x-nmos/node/v1.3/"
echo ""
