#!/bin/bash
set -e

echo "Updating apt repositories..."
sudo apt-get update

echo "Installing C++ and build dependencies..."
sudo apt-get install -y build-essential cmake pkg-config libcpp-httplib-dev \
  libssl-dev freetds-dev zlib1g-dev nlohmann-json3-dev wget tar

echo "Checking for JDK 26..."
if java -version 2>&1 | grep -q "26\."; then
    echo "JDK 26 is already installed."
else
    echo "Downloading and installing JDK 26..."
    wget https://download.oracle.com/java/26/latest/jdk-26_linux-x64_bin.tar.gz -O /tmp/jdk-26.tar.gz
    sudo mkdir -p /usr/lib/jvm
    sudo tar -xzf /tmp/jdk-26.tar.gz -C /usr/lib/jvm
    
    # Extract the exact directory name created
    JDK_DIR=$(ls -d /usr/lib/jvm/jdk-26* | head -n 1)
    
    sudo update-alternatives --install /usr/bin/java java $JDK_DIR/bin/java 100
    sudo update-alternatives --install /usr/bin/javac javac $JDK_DIR/bin/javac 100
    sudo update-alternatives --set java $JDK_DIR/bin/java
    sudo update-alternatives --set javac $JDK_DIR/bin/javac
    echo "JDK 26 installed successfully."
fi

echo "Updating git submodules..."
git submodule update --init --recursive

echo "All dependencies installed successfully!"
