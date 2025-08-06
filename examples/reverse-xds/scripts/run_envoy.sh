#!/bin/bash

# Bidirectional xDS Envoy Startup Script
# This script starts Envoy with bidirectional xDS support

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ENVOY_BIN="${SCRIPT_DIR}/../../../bazel-bin/source/exe/envoy-static"
CONFIG_FILE="${SCRIPT_DIR}/../config/envoy_bidirectional_config.yaml"

echo "🚀 Bidirectional xDS Envoy Client"
echo "================================="
echo ""

# Check if Envoy binary exists
if [ ! -f "$ENVOY_BIN" ]; then
    echo "❌ Envoy binary not found at: $ENVOY_BIN"
    echo "Please build Envoy first:"
    echo "  cd $(dirname $SCRIPT_DIR)/.. && bazel build //source/exe:envoy"
    exit 1
fi

# Check if config file exists
if [ ! -f "$CONFIG_FILE" ]; then
    echo "❌ Config file not found at: $CONFIG_FILE"
    exit 1
fi

echo "1. 🔍 Checking management server availability..."

# Check if management server is running
if ! nc -z localhost 18000 2>/dev/null; then
    echo "⚠️  Management server not detected on port 18000"
    echo "   Start it first with:"
    echo "   ./examples/reverse-xds/scripts/run_go_server.sh"
    echo ""
    echo "   Continuing anyway (Envoy will retry connection)..."
else
    echo "   ✅ Management server detected on port 18000"
fi
echo ""

echo "2. 🚀 Starting Envoy with bidirectional xDS support..."
echo "   Envoy binary: $ENVOY_BIN"
echo "   Config file: $CONFIG_FILE"
echo ""

echo "📋 Envoy Configuration:"
echo "   • Admin interface: http://localhost:9901"
echo "   • xDS server: localhost:18000"
echo "   • Dynamic listener: port 8080 (created via xDS)"
echo ""

echo "🎯 Expected Flow:"
echo "   1. Connect to management server via ADS"
echo "   2. Request listener/cluster configuration"
echo "   3. Receive dynamic listener config for port 8080"
echo "   4. Create and bind HTTP listener"
echo "   5. Report listener status via reverse xDS"
echo "   6. Serve traffic on http://localhost:8080"
echo ""

echo "🔗 Test URLs (after listener is ready):"
echo "   • Dynamic Listener: curl http://localhost:8080/"
echo "   • Admin Interface: curl http://localhost:9901/"
echo "   • Listener Status: curl http://localhost:9901/listeners"
echo ""

echo "Press Ctrl+C to stop Envoy..."
echo ""
echo "--- Envoy Logs ---"

# Start Envoy with bidirectional xDS support
exec "$ENVOY_BIN" -c "$CONFIG_FILE" --log-level info