#!/bin/bash
set -e

echo "=== Docksmith Ubuntu Setup Script ==="

echo "1. Installing Dependencies..."
sudo apt-get update
sudo apt-get install -y build-essential cmake nlohmann-json3-dev libstdc++-11-dev

echo "2. Building Docksmith..."
cmake -S . -B build
cmake --build build --parallel $(nproc)

echo "3. Setting up Offline Base Image (~/.docksmith/layers/)..."
mkdir -p ~/.docksmith/layers
mkdir -p ~/.docksmith/images
mkdir -p /tmp/docksmith-fix-staging/bin
mkdir -p /tmp/docksmith-fix-staging/tmp

cp /bin/busybox /tmp/docksmith-fix-staging/bin/busybox
chmod +x /tmp/docksmith-fix-staging/bin/busybox

(cd /tmp/docksmith-fix-staging/bin && for app in sh cat echo printf ls; do ln -s busybox "$app"; done)

tar --sort=name --mtime='@0' --owner=0 --group=0 --numeric-owner \
    -cf ~/.docksmith/layers/baselayer0000000000000000000000000000000.tar \
    -C /tmp/docksmith-fix-staging .

rm -rf /tmp/docksmith-fix-staging

echo "=== Setup Complete! ==="
echo "You can now run commands like:"
echo "sudo HOME=\$HOME ./build/docksmith images"
echo "sudo HOME=\$HOME ./build/docksmith build -t sample:latest sample-app"
echo "sudo HOME=\$HOME ./build/docksmith run sample:latest"
