#!/bin/bash

# Bidirectional xDS Management Server (Go)
# =======================================

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SERVER_DIR="$SCRIPT_DIR/../server"

echo "🚀 Bidirectional xDS Management Server (Go)"
echo "=========================================="
echo ""

# Check if Go is available
if ! command -v go &> /dev/null; then
    echo "❌ Go is not installed or not in PATH"
    echo "   Please install Go 1.21+ and try again"
    exit 1
fi

echo "🔧 Go version: $(go version)"
echo ""

# Change to server directory
cd "$SERVER_DIR"

# Set Go environment to avoid toolchain issues
export GOTOOLCHAIN=local

# Initialize Go module if needed
if [ ! -f go.sum ]; then
    echo "📦 Initializing Go dependencies..."
    GOTOOLCHAIN=local go mod tidy
fi

echo "🔨 Building management server..."
GOTOOLCHAIN=local go build -o management-server main.go

echo "🚀 Starting management server on :18000..."
echo ""
echo "📋 Server Configuration:"
echo "   • Listen Address: :18000"
echo "   • Protocol: gRPC (bidirectional ADS)"
echo "   • Dynamic Listener: port 8080"
echo "   • Upstream Target: www.envoyproxy.io:443"
echo ""
echo "🎯 Test with Envoy client:"
echo "   ./scripts/run_envoy.sh"
echo ""
echo "Press Ctrl+C to stop..."
echo ""

./management-server