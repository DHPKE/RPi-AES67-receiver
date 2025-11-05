#!/bin/bash

set -e

echo "==================================="
echo "AES67 NMOS Receiver Installation"
echo "==================================="

if [ "$EUID" -ne 0 ]; then 
    echo "Please run as root (use sudo)"
    exit 1
fi

echo "Updating system packages..."
apt-get update

echo "Installing dependencies..."
apt-get install -y \
    build-essential \
    cmake \
    git \
    libboost-all-dev \
    libssl-dev \
    libavahi-client-dev \
    nlohmann-json3-dev \
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
    ethtool \
    pkg-config \
    libwebsocketpp-dev \
    libcpprest-dev

echo "Building nmos-cpp..."
if [ ! -d "nmos-cpp" ]; then
    git clone --recurse-submodules https://github.com/sony/nmos-cpp.git
else
    cd nmos-cpp
    git pull
    git submodule update --init --recursive
    cd ..
fi

cd nmos-cpp/Development
mkdir -p build
cd build
cmake .. \
    -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_SHARED_LIBS=ON \
    -DCMAKE_INSTALL_PREFIX=/usr/local \
    -DWEBSOCKETPP_INCLUDE_DIR=/usr/include
make -j$(nproc)
make install
ldconfig
cd ../../..

echo "Building AES67 receiver..."
mkdir -p build
cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
make install
cd ..

echo "Setting up configuration..."
mkdir -p /etc/aes67-receiver
if [ ! -f /etc/aes67-receiver/config.json ]; then
    cp config/config.json /etc/aes67-receiver/
fi

echo "Creating service user..."
if ! id -u aes67 > /dev/null 2>&1; then
    useradd -r -s /bin/false aes67
    usermod -aG audio aes67
fi

echo "Installing systemd service..."
cp systemd/aes67-receiver.service /etc/systemd/system/
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

echo "Enabling services..."
systemctl enable pipewire
systemctl enable wireplumber

echo ""
echo "==================================="
echo "Installation complete!"
echo "==================================="
echo ""
echo "Next steps:"
echo "1. Configure network: sudo ./scripts/setup_network.sh eth0"
echo "2. Setup PTP: sudo ./scripts/setup_ptp.sh eth0"
echo "3. Edit configuration: /etc/aes67-receiver/config.json"
echo "4. Start service: sudo systemctl start aes67-receiver"
echo "5. Check status: sudo systemctl status aes67](#)

