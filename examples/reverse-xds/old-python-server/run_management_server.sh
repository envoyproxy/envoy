#!/bin/bash

# Bidirectional xDS Management Server Startup Script
# This script installs dependencies and starts the management server

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
MANAGEMENT_SERVER="${SCRIPT_DIR}/management_server_client_example.py"
REQUIREMENTS_FILE="${SCRIPT_DIR}/requirements.txt"

echo "🖥️  Bidirectional xDS Management Server"
echo "======================================"
echo ""

# Check if management server exists
if [ ! -f "$MANAGEMENT_SERVER" ]; then
    echo "❌ Management server not found at: $MANAGEMENT_SERVER"
    exit 1
fi

# Check if requirements file exists
if [ ! -f "$REQUIREMENTS_FILE" ]; then
    echo "❌ Requirements file not found at: $REQUIREMENTS_FILE"
    exit 1
fi

echo "1. 📦 Installing Python dependencies..."
echo "   Requirements: $REQUIREMENTS_FILE"

# Install requirements
if ! pip install -r "$REQUIREMENTS_FILE" --quiet; then
    echo "❌ Failed to install Python dependencies"
    echo "   Make sure you have pip installed and try:"
    echo "   pip install -r $REQUIREMENTS_FILE"
    exit 1
fi

echo "   ✅ Dependencies installed successfully"
echo ""

echo "2. 🔧 Checking for Python protobuf files..."

# Check if we can import the required modules
if python3 -c "import envoy.service.discovery.v3.discovery_pb2" 2>/dev/null; then
    echo "   ✅ Envoy protobuf modules found"
    USE_FULL_SERVER=true
else
    echo "   ⚠️  Envoy protobuf modules not found"
    echo "   📦 Attempting to generate protobuf files..."
    
    # Try to generate protobuf files
    if [[ -f "$SCRIPT_DIR/generate_python_protos.sh" ]]; then
        echo "   Running: $SCRIPT_DIR/generate_python_protos.sh"
        if "$SCRIPT_DIR/generate_python_protos.sh" >/dev/null 2>&1; then
            echo "   ✅ Protobuf generation completed"
            USE_FULL_SERVER=true
        else
            echo "   ❌ Protobuf generation failed"
            USE_FULL_SERVER=false
        fi
    else
        USE_FULL_SERVER=false
    fi
fi

echo ""
echo "3. 🚀 Starting bidirectional xDS management server..."

if [[ "$USE_FULL_SERVER" == "true" ]]; then
    echo "   Using full server: $MANAGEMENT_SERVER"
    echo "   Listening on: http://localhost:18000"
    echo ""
    
    echo "📋 What this server does:"
    echo "   • Accepts ADS connections from Envoy"
    echo "   • Sends dynamic listener configuration (port 8080)"
    echo "   • Monitors listener status via reverse xDS"
    echo "   • Reports when listener is ready"
    echo ""
    
    echo "🔗 Connect Envoy using:"
    echo "   ./examples/reverse-xds/run_envoy.sh"
    echo ""
    
    echo "Press Ctrl+C to stop the server..."
    echo ""
    echo "--- Management Server Logs ---"
    
    # Start the full management server
    python3 "$MANAGEMENT_SERVER"
else
    echo "   Using simplified demo server"
    echo "   Note: This shows concepts but won't work with real Envoy"
    echo ""
    
    echo "💡 To fix protobuf issues:"
    echo "   1. Rebuild the dev container (recommended)"
    echo "   2. Or run: ./examples/reverse-xds/generate_python_protos.sh"
    echo ""
    
    echo "🔗 For now, running concept demo:"
    echo ""
    
    # Start the simplified server
    python3 "$SCRIPT_DIR/simple_management_server.py"
fi