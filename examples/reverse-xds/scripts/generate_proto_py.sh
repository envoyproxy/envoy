#!/bin/bash

# Script to generate Python protobuf files for the reverse-xds demo
# This generates the minimal set of protobuf files needed by the management server

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ENVOY_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
OUTPUT_DIR="$SCRIPT_DIR/generated"

echo "🔧 Generating Python protobuf files for reverse-xDS demo"
echo "======================================================="
echo ""
echo "Envoy root: $ENVOY_ROOT"
echo "Output dir: $OUTPUT_DIR"
echo ""

# Create output directory
mkdir -p "$OUTPUT_DIR"

# List of proto files we need
PROTO_FILES=(
    "api/envoy/service/discovery/v3/discovery.proto"
    "api/envoy/service/discovery/v3/ads.proto"
    "api/envoy/config/listener/v3/listener.proto"
    "api/envoy/config/core/v3/address.proto"
    "api/envoy/config/core/v3/socket_option.proto"
    "api/envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.proto"
    "api/envoy/extensions/filters/http/router/v3/router.proto"
    "api/envoy/config/route/v3/route.proto"
    "api/envoy/admin/v3/reverse_xds.proto"
)

echo "1. 📦 Generating Python protobuf files..."

# Change to Envoy root for proper imports
cd "$ENVOY_ROOT"

for proto_file in "${PROTO_FILES[@]}"; do
    if [[ -f "$proto_file" ]]; then
        echo "  Processing: $proto_file"
        python3 -m grpc_tools.protoc \
            --proto_path=. \
            --proto_path=api \
            --python_out="$OUTPUT_DIR" \
            --grpc_python_out="$OUTPUT_DIR" \
            "$proto_file"
    else
        echo "  ⚠️  Warning: $proto_file not found"
    fi
done

echo ""
echo "2. 🔧 Creating __init__.py files for Python packages..."

# Create __init__.py files for proper Python package structure
find "$OUTPUT_DIR" -type d -exec touch {}/__init__.py \;

echo ""
echo "3. 📝 Creating package information..."

# Create a simple info file
cat > "$OUTPUT_DIR/README.md" << 'EOF'
# Generated Python Protobuf Files

This directory contains Python protobuf files generated from Envoy API definitions.

## Files Generated:
- `envoy/service/discovery/v3/` - Discovery service (ADS, LDS, etc.)
- `envoy/config/listener/v3/` - Listener configuration
- `envoy/config/core/v3/` - Core types (addresses, etc.)
- `envoy/extensions/` - HTTP connection manager and router
- `envoy/admin/v3/` - Admin API including reverse xDS

## Usage:
Add this directory to your Python path:
```python
import sys
sys.path.insert(0, '/path/to/generated')

import envoy.service.discovery.v3.discovery_pb2 as discovery_pb2
```

## Regeneration:
Run `./generate_proto_py.sh` to regenerate these files.
EOF

echo ""
echo "4. 📋 Summary of generated files:"
find "$OUTPUT_DIR" -name "*.py" | grep -v __pycache__ | sort

echo ""
echo "✅ Python protobuf generation complete!"
echo ""
echo "📖 Usage:"
echo "   Add to your Python path: export PYTHONPATH=\"$OUTPUT_DIR:\$PYTHONPATH\""
echo "   Or add to your script: sys.path.insert(0, '$OUTPUT_DIR')"
echo ""
echo "🔗 Test the management server:"
echo "   export PYTHONPATH=\"$OUTPUT_DIR:\$PYTHONPATH\""
echo "   python3 examples/reverse-xds/management_server_client_example.py"