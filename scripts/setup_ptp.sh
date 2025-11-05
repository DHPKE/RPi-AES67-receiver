#!/bin/bash

set -e

if [ "$EUID" -ne 0 ]; then 
    echo "Please run as root"
    exit 1
fi

INTERFACE=${1:-eth0}

echo "Configuring PTP on $INTERFACE..."

mkdir -p /etc/linuxptp

cat > /etc/linuxptp/ptp4l.conf << EOF
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
network_transport UDPv4
delay_mechanism E2E
time_stamping hardware
tsproc_mode filter
delay_filter moving_median
delay_filter_length 10
summary_interval 0
step_threshold 0.0
first_step_threshold 0.00002
max_frequency 900000000

[$INTERFACE]
EOF

cat > /etc/systemd/system/ptp4l.service << EOF
[Unit]
Description=PTP Boundary Clock
After=network.target

[Service]
Type=simple
ExecStart=/usr/sbin/ptp4l -f /etc/linuxptp/ptp4l.conf -i $INTERFACE -m
Restart=always
RestartSec=5

[Install]
WantedBy=multi-user.target
EOF

systemctl daemon-reload
systemctl enable ptp4l
systemctl start ptp4l

echo "PTP configuration complete"
echo "Check sync status with: sudo pmc -u -b 0 'GET CURRENT_DATA_SET'"