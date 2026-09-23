#!/bin/bash
set -euo pipefail

echo "Updating apt repositories..."
sudo apt-get update

echo "Installing C++ and build dependencies..."
sudo apt-get install -y build-essential cmake pkg-config libcpp-httplib-dev \
  libssl-dev freetds-dev zlib1g-dev nlohmann-json3-dev wget tar

JDK_VERSION="${JDK_VERSION:-26}"
JDK_URL="${JDK_URL:-https://download.oracle.com/java/${JDK_VERSION}/latest/jdk-${JDK_VERSION}_linux-x64_bin.tar.gz}"

echo "Checking for JDK ${JDK_VERSION}..."
if java -version 2>&1 | grep -q "${JDK_VERSION}\."; then
    echo "JDK ${JDK_VERSION} is already installed."
else
    : "${JDK_SHA256:?Set JDK_SHA256 to the official SHA-256 for the exact JDK archive before running this script.}"
    tmp_jdk_archive="$(mktemp /tmp/agora-jdk.XXXXXX.tar.gz)"
    trap 'rm -f "$tmp_jdk_archive"' EXIT
    echo "Downloading and verifying JDK ${JDK_VERSION}..."
    wget --https-only --secure-protocol=TLSv1_2 "$JDK_URL" -O "$tmp_jdk_archive"
    printf '%s  %s\n' "$JDK_SHA256" "$tmp_jdk_archive" | sha256sum --check --status
    sudo mkdir -p /usr/lib/jvm
    sudo tar -xzf "$tmp_jdk_archive" -C /usr/lib/jvm
    
    # Extract the exact directory name created
    JDK_DIR=$(find /usr/lib/jvm -maxdepth 1 -mindepth 1 -type d -name "jdk-${JDK_VERSION}*" -print -quit)
    if [[ -z "$JDK_DIR" || ! -x "$JDK_DIR/bin/java" ]]; then
        echo "Unable to find the extracted JDK directory." >&2
        exit 1
    fi
    
    sudo update-alternatives --install /usr/bin/java java "$JDK_DIR/bin/java" 100
    sudo update-alternatives --install /usr/bin/javac javac "$JDK_DIR/bin/javac" 100
    sudo update-alternatives --set java "$JDK_DIR/bin/java"
    sudo update-alternatives --set javac "$JDK_DIR/bin/javac"
    echo "JDK ${JDK_VERSION} installed successfully."
fi

echo "Updating git submodules..."
git submodule update --init --recursive

echo "All dependencies installed successfully!"
