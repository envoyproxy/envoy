#!/bin/bash

# Bidirectional xDS Demo Coordinator Script
# This script helps you run the separate management server and Envoy scripts

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
MGMT_SCRIPT="${SCRIPT_DIR}/run_go_server.sh"
ENVOY_SCRIPT="${SCRIPT_DIR}/run_envoy.sh"

echo "🚀 Bidirectional xDS Demo Coordinator"
echo "====================================="
echo ""

# Check if scripts exist
if [ ! -f "$MGMT_SCRIPT" ]; then
    echo "❌ Management server script not found at: $MGMT_SCRIPT"
    exit 1
fi

if [ ! -f "$ENVOY_SCRIPT" ]; then
    echo "❌ Envoy script not found at: $ENVOY_SCRIPT"
    exit 1
fi

echo "📋 Available Options:"
echo ""
echo "   1️⃣  Run Management Server Only:"
echo "      $MGMT_SCRIPT"
echo ""
echo "   2️⃣  Run Envoy Only:"
echo "      $ENVOY_SCRIPT"
echo ""
echo "   3️⃣  Run Both (Recommended for Demo):"
echo "      Use two terminals for separate logs"
echo ""

echo "🎯 Recommended Demo Flow:"
echo ""
echo "   Terminal 1: Start Management Server"
echo "   $ ./examples/reverse-xds/run_management_server.sh"
echo ""
echo "   Terminal 2: Start Envoy (after server is ready)"
echo "   $ ./examples/reverse-xds/run_envoy.sh"
echo ""

echo "💡 Benefits of Separate Terminals:"
echo "   • Clear separation of logs"
echo "   • Independent process control"
echo "   • Better debugging experience"
echo "   • Easy to restart individual components"
echo ""

read -p "Would you like to run both automatically in background? (y/N): " choice
case "$choice" in 
  y|Y ) 
    echo ""
    echo "🚀 Starting both services in background..."
    
    # Function to cleanup background processes
    cleanup() {
        echo ""
        echo "🧹 Cleaning up..."
        if [ ! -z "$MGMT_PID" ]; then
            kill $MGMT_PID 2>/dev/null || true
            echo "  Stopped management server (PID: $MGMT_PID)"
        fi
        if [ ! -z "$ENVOY_PID" ]; then
            kill $ENVOY_PID 2>/dev/null || true
            echo "  Stopped Envoy (PID: $ENVOY_PID)"
        fi
        echo "✅ Cleanup complete"
    }

    # Set up cleanup on script exit
    trap cleanup EXIT INT TERM

    echo "1. 🖥️  Starting management server..."
    "$MGMT_SCRIPT" > mgmt_server.log 2>&1 &
    MGMT_PID=$!
    echo "   Management server started (PID: $MGMT_PID)"
    echo "   Logs: tail -f mgmt_server.log"
    echo ""

    # Wait for management server to start
    echo "2. ⏳ Waiting for management server to be ready..."
    sleep 5

    echo "3. 🚀 Starting Envoy..."
    "$ENVOY_SCRIPT" > envoy.log 2>&1 &
    ENVOY_PID=$!
    echo "   Envoy started (PID: $ENVOY_PID)"
    echo "   Logs: tail -f envoy.log"
    echo ""

    echo "🎯 Demo Flow:"
    echo "   1. Envoy will connect to management server via ADS"
    echo "   2. Management server will send dynamic listener config"
    echo "   3. Envoy will create listener on port 8080"
    echo "   4. Management server will request listener status via reverse xDS"
    echo "   5. Envoy will report listener readiness"
    echo "   6. Dynamic listener will be ready for traffic!"
    echo ""

    echo "🔗 Test URLs:"
    echo "   • Envoy Admin:      http://localhost:9901"
    echo "   • Dynamic Listener: http://localhost:8080/"
    echo "   • Test Command:     curl http://localhost:8080/"
    echo ""

    echo "📋 Log Monitoring:"
    echo "   • Management Server: tail -f mgmt_server.log"
    echo "   • Envoy:            tail -f envoy.log"
    echo "   • Both:             tail -f mgmt_server.log envoy.log"
    echo ""

    echo "Press Ctrl+C to stop both services..."
    echo ""

    # Wait for both processes
    wait
    ;;
  * ) 
    echo ""
    echo "ℹ️  Use the individual scripts for better log visibility:"
    echo ""
    echo "   # Terminal 1"
    echo "   $ ./examples/reverse-xds/run_management_server.sh"
    echo ""
    echo "   # Terminal 2 (after server is ready)"
    echo "   $ ./examples/reverse-xds/run_envoy.sh"
    echo ""
    echo "   # Test (after both are running)"
    echo "   $ curl http://localhost:8080/"
    echo ""
    ;;
esac