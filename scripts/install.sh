#!/bin/bash

set -e

echo "==================================="
echo "AES67 NMOS Receiver Installation"
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
    libssl-dev \
    libavahi-client-dev \
    libavahi-compat-libdnssd-dev \
    nlohmann-json3-dev \
    libwebsocketpp-dev \
    libcpprest-dev \
    libboost-all-dev \
    libboost-chrono-dev \
    libboost-date-time-dev \
    libboost-system-dev \
    libboost-thread-dev \
    libboost-filesystem-dev \
    libboost-regex-dev \
    libboost-random-dev \
    libboost-atomic-dev \
    libgstreamer1.0-dev \
    libgstreamer-plugins-base1.0-dev \
    gstreamer1.0-plugins-good \
    gstreamer1.0-plugins-bad \
    gstreamer1.0-plugins-ugly \
    gstreamer1.0-tools \
    libpipewire-0.3-dev \
    pipewire \
    pipewire-audio-client-libraries \
    wireplumber \
    linuxptp \
    ethtool

echo ""
echo "==================================="
echo "Building jwt-cpp..."
echo "==================================="
if [ ! -d "jwt-cpp" ]; then
    git clone https://github.com/Thalhammer/jwt-cpp.git
fi

cd jwt-cpp
rm -rf build
mkdir -p build
cd build
cmake .. \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX=/usr/local \
    -DJWT_BUILD_EXAMPLES=OFF \
    -DJWT_BUILD_TESTS=OFF
make -j1
make install
cd ../..
echo "✓ jwt-cpp installed"

echo ""
echo "==================================="
echo "Building nlohmann_json_schema_validator..."
echo "==================================="
if [ ! -d "json-schema-validator" ]; then
    git clone https://github.com/pboettch/json-schema-validator.git
fi

cd json-schema-validator
rm -rf build
mkdir -p build
cd build
cmake .. \
    -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_SHARED_LIBS=ON \
    -DCMAKE_INSTALL_PREFIX=/usr/local \
    -DBUILD_TESTS=OFF \
    -DBUILD_EXAMPLES=OFF
make -j1
make install
ldconfig
cd ../..
echo "✓ nlohmann_json_schema_validator installed"

echo ""
echo "==================================="
echo "Building nmos-cpp..."
echo "This will take 60-90 minutes..."
echo "==================================="
if [ ! -d "nmos-cpp" ]; then
    git clone --recurse-submodules https://github.com/sony/nmos-cpp.git
else
    cd nmos-cpp
    git pull
    git submodule update --init --recursive
    cd ..
fi

cd nmos-cpp/Development
rm -rf build
mkdir -p build
cd build

# Configure with proper Boost settings
cmake .. \
    -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_SHARED_LIBS=ON \
    -DCMAKE_INSTALL_PREFIX=/usr/local \
    -DWEBSOCKETPP_INCLUDE_DIR=/usr/include \
    -DBOOST_ROOT=/usr \
    -DBoost_NO_SYSTEM_PATHS=OFF \
    -DBoost_USE_STATIC_LIBS=OFF \
    -DBoost_USE_MULTITHREADED=ON

# Build with single job to avoid OOM
echo "Starting compilation (this is slow but stable)..."
make -j1

make install
ldconfig
cd ../../..
echo "✓ nmos-cpp installed"

echo ""
echo "==================================="
echo "Building AES67 receiver..."
echo "==================================="
rm -rf build
mkdir -p build
cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j1
make install
cd ..
echo "✓ AES67 receiver installed"

echo ""
echo "==================================="
echo "Setting up configuration..."
echo "==================================="
mkdir -p /etc/aes67-receiver
if [ ! -f /etc/aes67-receiver/config.json ]; then
    if [ -f config/config.json ]; then
        cp config/config.json /etc/aes67-receiver/
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

echo "Installing systemd service..."
if [ -f systemd/aes67-receiver.service ]; then
    cp systemd/aes67-receiver.service /etc/systemd/system/
    systemctl daemon-reload
    echo "✓ Systemd service installed"
else
    echo "⚠ systemd/aes67-receiver.service not found, skipping"
fi

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
echo "✓ nmos-cpp built and installed"
echo "✓ AES67 receiver built and installed"
echo "✓ 4GB swap file created and enabled"
echo ""
echo "Next steps:"
echo "1. Configure network: sudo ./scripts/setup_network.sh eth0"
echo "2. Setup PTP: sudo ./scripts/setup_ptp.sh eth0"
echo "3. Edit configuration: /etc/aes67-receiver/config.json"
echo "4. Start service: sudo systemctl start aes67-receiver"
echo "5. Check status: sudo systemctl status aes67-receiver"
echo ""
echo "Note: Swap file will persist after reboot"
echo ""