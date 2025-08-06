#!/bin/bash

# Test Go Installation Script
# This script verifies that Go is properly installed and configured

set -e

echo "🧪 Testing Go Installation"
echo "=========================="
echo ""

# Set Go environment to avoid toolchain issues
export GOTOOLCHAIN=local

# Test 1: Check Go binary
echo "1. 🔍 Checking Go binary..."
if command -v go &> /dev/null; then
    echo "   ✅ Go found at: $(which go)"
    echo "   📋 Version: $(go version)"
else
    echo "   ❌ Go not found in PATH"
    exit 1
fi
echo ""

# Test 2: Check environment variables
echo "2. 🌍 Checking Go environment..."
echo "   GOROOT: $(go env GOROOT)"
echo "   GOPATH: $(go env GOPATH)"
echo "   GOPROXY: $(go env GOPROXY)"
echo "   GOTOOLCHAIN: $(go env GOTOOLCHAIN)"
echo ""

# Test 3: Test module operations
echo "3. 📦 Testing module operations..."
TEMP_DIR=$(mktemp -d)
cd "$TEMP_DIR"

echo "   Creating test module..."
GOTOOLCHAIN=local go mod init test-module
echo 'package main

import "fmt"

func main() {
    fmt.Println("Hello, Go!")
}' > main.go

echo "   Building test program..."
GOTOOLCHAIN=local go build -o test-program main.go

echo "   Running test program..."
./test-program

cd - > /dev/null
rm -rf "$TEMP_DIR"
echo "   ✅ Module operations successful"
echo ""

echo "🎉 Go installation test completed successfully!"
echo ""
echo "📋 Summary:"
echo "   • Go binary: ✅ Available"
echo "   • Environment: ✅ Configured"  
echo "   • Module ops: ✅ Working"
echo "   • Ready for development: ✅"