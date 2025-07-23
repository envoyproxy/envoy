#!/bin/bash

# Test script for gRPC reverse tunnel handshake demonstration
# This script automates the testing process for the new gRPC-based handshake

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ENVOY_BINARY="${ENVOY_BINARY:-bazel-bin/source/exe/envoy-static}"

echo "🚀 gRPC Reverse Tunnel Handshake Test"
echo "======================================"

# Check if envoy binary exists
if [ ! -f "${ENVOY_BINARY}" ]; then
    echo "❌ Error: Envoy binary not found at ${ENVOY_BINARY}"
    echo "   Please build Envoy first: bazel build //source/exe:envoy-static"
    echo "   Or set ENVOY_BINARY environment variable to the correct path"
    exit 1
fi

# Kill any existing envoy processes
echo "🧹 Cleaning up existing processes..."
pkill -f "envoy-static.*cloud-envoy" || true
pkill -f "envoy-static.*on-prem-envoy" || true
sleep 2

# Start Python backend server in background
echo "🐍 Starting Python backend server..."
cd "${SCRIPT_DIR}/../"
python3 -c "
import http.server
import socketserver
import json
import urllib.parse
from datetime import datetime

class BackendHandler(http.server.SimpleHTTPRequestHandler):
    def do_GET(self):
        self.send_response(200)
        self.send_header('Content-type', 'application/json')
        self.end_headers()
        
        response = {
            'message': 'Hello from on-premises backend service!',
            'timestamp': datetime.now().isoformat(),
            'path': self.path,
            'method': 'GET',
            'headers': dict(self.headers)
        }
        
        self.wfile.write(json.dumps(response, indent=2).encode())
        
    def log_message(self, format, *args):
        print(f'[BACKEND] {format % args}')

PORT = 3000
with socketserver.TCPServer(('', PORT), BackendHandler) as httpd:
    print(f'Backend server running on port {PORT}')
    httpd.serve_forever()
" &
BACKEND_PID=$!
echo "   Backend server started with PID: ${BACKEND_PID}"

# Wait for backend to start
sleep 2

# Start Cloud Envoy (Acceptor)
echo "☁️  Starting Cloud Envoy (gRPC Acceptor)..."
cd "${SCRIPT_DIR}/../../"
"${ENVOY_BINARY}" \
    -c examples/reverse_connection_socket_interface/cloud-envoy-grpc.yaml \
    --concurrency 1 --use-dynamic-base-id -l trace \
    > /tmp/cloud-envoy.log 2>&1 &
CLOUD_PID=$!
echo "   Cloud Envoy started with PID: ${CLOUD_PID}"

# Wait for Cloud Envoy to start
echo "   Waiting for Cloud Envoy to initialize..."
sleep 5

# Check if Cloud Envoy started successfully
if ! kill -0 $CLOUD_PID 2>/dev/null; then
    echo "❌ Cloud Envoy failed to start. Check logs:"
    tail -20 /tmp/cloud-envoy.log
    kill $BACKEND_PID 2>/dev/null || true
    exit 1
fi

# Start On-Premises Envoy (Initiator) 
echo "🏢 Starting On-Premises Envoy (gRPC Initiator)..."
"${ENVOY_BINARY}" \
    -c examples/reverse_connection_socket_interface/on-prem-envoy-grpc.yaml \
    --concurrency 1 --use-dynamic-base-id -l trace \
    > /tmp/on-prem-envoy.log 2>&1 &
ONPREM_PID=$!
echo "   On-Premises Envoy started with PID: ${ONPREM_PID}"

# Wait for On-Premises Envoy to start and establish connections
echo "   Waiting for gRPC handshake to complete..."
sleep 10

# Check if On-Premises Envoy started successfully
if ! kill -0 $ONPREM_PID 2>/dev/null; then
    echo "❌ On-Premises Envoy failed to start. Check logs:"
    tail -20 /tmp/on-prem-envoy.log
    kill $CLOUD_PID $BACKEND_PID 2>/dev/null || true
    exit 1
fi

# Test the end-to-end flow
echo "🧪 Testing end-to-end reverse tunnel flow..."
echo "   Sending test request via reverse tunnel..."

HTTP_RESPONSE=$(curl -s -w "%{http_code}" \
    -H "x-remote-node-id: on-prem-node" \
    -H "x-dst-cluster-uuid: on-prem" \
    http://localhost:8085/on_prem_service)

HTTP_STATUS="${HTTP_RESPONSE: -3}"
RESPONSE_BODY="${HTTP_RESPONSE%???}"

echo "   HTTP Status: ${HTTP_STATUS}"

if [ "${HTTP_STATUS}" = "200" ]; then
    echo "✅ SUCCESS: Reverse tunnel working correctly!"
    echo "   Response received:"
    echo "${RESPONSE_BODY}" | jq . 2>/dev/null || echo "${RESPONSE_BODY}"
else
    echo "❌ FAILED: Unexpected HTTP status: ${HTTP_STATUS}"
    echo "   Response: ${RESPONSE_BODY}"
fi

# Check logs for gRPC handshake evidence
echo ""
echo "📋 Checking logs for gRPC handshake evidence..."

echo "   Cloud Envoy (Acceptor) logs:"
if grep -q "EstablishTunnel gRPC request" /tmp/cloud-envoy.log; then
    echo "   ✅ Found gRPC handshake requests in Cloud Envoy logs"
    grep "EstablishTunnel gRPC request" /tmp/cloud-envoy.log | tail -3
else
    echo "   ⚠️  No gRPC handshake requests found in Cloud Envoy logs"
fi

echo ""
echo "   On-Premises Envoy (Initiator) logs:"
if grep -q "gRPC reverse tunnel handshake" /tmp/on-prem-envoy.log; then
    echo "   ✅ Found gRPC handshake initiation in On-Premises Envoy logs"
    grep "gRPC reverse tunnel handshake" /tmp/on-prem-envoy.log | tail -3
else
    echo "   ⚠️  No gRPC handshake initiation found in On-Premises Envoy logs"
fi

# Performance test
echo ""
echo "🏃 Performance test (10 requests)..."
start_time=$(date +%s%N)
for i in {1..10}; do
    curl -s -H "x-remote-node-id: on-prem-node" \
         -H "x-dst-cluster-uuid: on-prem" \
         http://localhost:8085/on_prem_service > /dev/null
done
end_time=$(date +%s%N)
duration_ms=$(( (end_time - start_time) / 1000000 ))
avg_latency_ms=$(( duration_ms / 10 ))

echo "   Total time: ${duration_ms}ms"
echo "   Average latency: ${avg_latency_ms}ms per request"

# Cleanup function
cleanup() {
    echo ""
    echo "🧹 Cleaning up..."
    kill $ONPREM_PID $CLOUD_PID $BACKEND_PID 2>/dev/null || true
    sleep 2
    
    echo "   Log files available at:"
    echo "   - Cloud Envoy: /tmp/cloud-envoy.log"
    echo "   - On-Premises Envoy: /tmp/on-prem-envoy.log"
    echo ""
    echo "   To view detailed logs, run:"
    echo "   tail -f /tmp/cloud-envoy.log"
    echo "   tail -f /tmp/on-prem-envoy.log"
}

# Set trap for cleanup
trap cleanup EXIT INT TERM

echo ""
echo "🎉 Test completed! Press Ctrl+C to cleanup and exit."
echo "   Envoys will continue running for further testing..."

# Keep the script running
while true; do
    sleep 10
    
    # Check if processes are still running
    if ! kill -0 $CLOUD_PID 2>/dev/null; then
        echo "❌ Cloud Envoy died unexpectedly"
        break
    fi
    
    if ! kill -0 $ONPREM_PID 2>/dev/null; then
        echo "❌ On-Premises Envoy died unexpectedly"  
        break
    fi
    
    if ! kill -0 $BACKEND_PID 2>/dev/null; then
        echo "❌ Backend server died unexpectedly"
        break
    fi
done 