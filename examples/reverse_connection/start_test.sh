#!/bin/bash

# Test script for reverse connection feature
set -e

echo "Starting reverse connection test setup..."

# Kill any existing processes
pkill -f backend_service.py || true
pkill -f envoy-static || true
sleep 2

echo "1. Starting backend service on port 7070..."
python3 backend_service.py &
BACKEND_PID=$!
sleep 2

echo "2. Starting cloud Envoy on port 9000 (API) and 8085 (egress)..."
../../bazel-bin/source/exe/envoy-static -c cloud-envoy.yaml --use-dynamic-base-id &
CLOUD_PID=$!
sleep 3

echo "3. Starting on-prem Envoy on port 9001 (API) and 8081 (ingress)..."
../../bazel-bin/source/exe/envoy-static -c on-prem-envoy.yaml --use-dynamic-base-id &
ONPREM_PID=$!
sleep 5

echo "4. Testing the setup..."
echo "   Backend service: http://localhost:7070/on_prem_service"
echo "   Cloud Envoy API: http://localhost:9000/"
echo "   On-prem Envoy API: http://localhost:9001/"
echo "   Cloud Envoy egress: http://localhost:8085/on_prem_service"
echo "   On-prem ingress: http://localhost:8081/on_prem_service"

# Test reverse connection API
echo ""
echo "Testing reverse connection APIs..."
echo "Cloud connected/accepted nodes:"
curl -s http://localhost:9000/ | jq '.' || curl -s http://localhost:9000/

echo ""
echo "On-prem connected/accepted nodes:"
curl -s http://localhost:9001/ | jq '.' || curl -s http://localhost:9001/

echo ""
echo "All services started successfully!"
echo "PIDs: Backend=$BACKEND_PID, Cloud=$CLOUD_PID, OnPrem=$ONPREM_PID"
echo ""
echo "To stop all services, run: kill $BACKEND_PID $CLOUD_PID $ONPREM_PID"

# Keep the script running
wait 