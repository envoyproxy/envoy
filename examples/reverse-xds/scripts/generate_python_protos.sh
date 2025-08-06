#!/bin/bash

# Generate Python protobuf files using Bazel (the official Envoy way)
# This uses Envoy's build system to properly generate all dependencies

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ENVOY_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
OUTPUT_DIR="$SCRIPT_DIR/python_protos"

echo "🔧 Generating Python protobuf files using Bazel"
echo "==============================================="
echo ""
echo "Envoy root: $ENVOY_ROOT"
echo "Output dir: $OUTPUT_DIR"
echo ""

cd "$ENVOY_ROOT"

# Create output directory
mkdir -p "$OUTPUT_DIR"

echo "1. 🏗️  Building Python protobuf targets with Bazel..."

# List of Bazel targets for Python protobufs we need
TARGETS=(
    "@envoy_api//envoy/service/discovery/v3:pkg_py_proto"
    "@envoy_api//envoy/config/listener/v3:pkg_py_proto"
    "@envoy_api//envoy/config/core/v3:pkg_py_proto"
    "@envoy_api//envoy/admin/v3:pkg_py_proto"
    "@envoy_api//envoy/extensions/filters/network/http_connection_manager/v3:pkg_py_proto"
    "@envoy_api//envoy/extensions/filters/http/router/v3:pkg_py_proto"
    "@envoy_api//envoy/config/route/v3:pkg_py_proto"
)

# Build each target
for target in "${TARGETS[@]}"; do
    echo "  Building: $target"
    if bazel build "$target" 2>/dev/null; then
        echo "    ✅ Built successfully"
    else
        echo "    ❌ Failed to build (target may not exist in this repo)"
    fi
done

echo ""
echo "2. 📁 Checking for generated files..."

# Find the bazel output directory
BAZEL_BIN=$(bazel info bazel-bin 2>/dev/null)
if [[ -d "$BAZEL_BIN" ]]; then
    echo "  Bazel bin directory: $BAZEL_BIN"
    
    # Look for Python files
    PYTHON_FILES=$(find "$BAZEL_BIN" -name "*_pb2.py" 2>/dev/null | head -10)
    if [[ -n "$PYTHON_FILES" ]]; then
        echo "  Found Python protobuf files:"
        echo "$PYTHON_FILES" | sed 's/^/    /'
        
        echo ""
        echo "3. 📦 Copying files to output directory..."
        
        # Copy the files maintaining directory structure
        find "$BAZEL_BIN" -name "*_pb2.py" -exec cp {} "$OUTPUT_DIR" \; 2>/dev/null || true
        find "$BAZEL_BIN" -name "*_pb2_grpc.py" -exec cp {} "$OUTPUT_DIR" \; 2>/dev/null || true
        
        # Create __init__.py files
        find "$OUTPUT_DIR" -type d -exec touch {}/__init__.py \;
        
        echo "  ✅ Files copied to $OUTPUT_DIR"
    else
        echo "  ⚠️  No Python protobuf files found in Bazel output"
    fi
else
    echo "  ❌ Could not find Bazel bin directory"
fi

echo ""
echo "4. 🎯 Alternative: Use external API repository"
echo ""
echo "If the above didn't work, try cloning the Envoy API repository:"
echo "  git clone https://github.com/envoyproxy/data-plane-api.git"
echo "  cd data-plane-api"
echo "  # Generate Python files from there"
echo ""

echo "5. 💡 Workaround for demo"
echo ""
echo "For now, you can use the simplified management server:"
echo "  python3 examples/reverse-xds/simple_management_server.py"
echo ""
echo "Or modify the management server to use simpler protobuf messages."

echo ""
echo "📖 Next steps:"
echo "   1. Check if files were generated in: $OUTPUT_DIR"
echo "   2. If successful, add to Python path:"
echo "      export PYTHONPATH=\"$OUTPUT_DIR:\$PYTHONPATH\""
echo "   3. Run the management server:"
echo "      python3 examples/reverse-xds/management_server_client_example.py"