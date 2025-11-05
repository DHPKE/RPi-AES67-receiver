#!/bin/bash

# Install dependencies
apt-get install -y \
    ...
    libwebsocketpp-dev \
    libcpprest-dev \
    ...

# Clone the repository
if [ ! -d nmos-cpp ]; then
    git clone --recurse-submodules https://github.com/sony/nmos-cpp.git
else
    cd nmos-cpp
    git pull
    git submodule update --init --recursive
    cd ..
fi

cd nmos-cpp/Development

# Do other stuff...

cd ../../..