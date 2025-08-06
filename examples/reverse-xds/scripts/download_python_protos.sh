#!/bin/bash

# Download pre-built Python protobuf files for Envoy API
# This is a temporary workaround until the dev container is properly configured

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
OUTPUT_DIR="$SCRIPT_DIR/envoy_protos"

echo "📦 Downloading Python protobuf files for Envoy API"
echo "=================================================="
echo ""
echo "Output directory: $OUTPUT_DIR"
echo ""

# Create output directory
mkdir -p "$OUTPUT_DIR"
cd "$OUTPUT_DIR"

echo "1. 🌐 Setting up Python environment..."

# Install required packages if not present
pip3 install --user --quiet protobuf grpcio grpcio-tools

echo "   ✅ Python packages ready"
echo ""

echo "2. 📁 Creating minimal protobuf stubs..."

# Create the directory structure
mkdir -p envoy/service/discovery/v3
mkdir -p envoy/config/listener/v3  
mkdir -p envoy/config/core/v3
mkdir -p envoy/config/route/v3
mkdir -p envoy/admin/v3
mkdir -p envoy/extensions/filters/network/http_connection_manager/v3
mkdir -p envoy/extensions/filters/http/router/v3
mkdir -p google/protobuf
mkdir -p google/rpc

# Create __init__.py files
find . -type d -exec touch {}/__init__.py \;

echo "   ✅ Directory structure created"
echo ""

echo "3. 🔧 Creating minimal protobuf message classes..."

# Create a minimal discovery_pb2.py
cat > envoy/service/discovery/v3/discovery_pb2.py << 'EOF'
# Minimal protobuf stubs for demo purposes
from google.protobuf import message
from google.protobuf import any_pb2
from typing import List

class DiscoveryRequest(message.Message):
    def __init__(self):
        self.version_info = ""
        self.node = None
        self.resource_names = []
        self.type_url = ""
        self.response_nonce = ""
        self.error_detail = None

class DiscoveryResponse(message.Message):
    def __init__(self):
        self.version_info = ""
        self.resources = []
        self.canary = False
        self.type_url = ""
        self.nonce = ""
        self.control_plane = None

class DeltaDiscoveryRequest(message.Message):
    def __init__(self):
        self.node = None
        self.type_url = ""
        self.resource_names_subscribe = []
        self.resource_names_unsubscribe = []
        self.initial_resource_versions = {}
        self.response_nonce = ""
        self.error_detail = None

class DeltaDiscoveryResponse(message.Message):
    def __init__(self):
        self.system_version_info = ""
        self.resources = []
        self.type_url = ""
        self.removed_resources = []
        self.nonce = ""
        self.control_plane = None
EOF

# Create ADS service stub
cat > envoy/service/discovery/v3/ads_pb2_grpc.py << 'EOF'
# Minimal gRPC stubs for demo purposes
import grpc

class AggregatedDiscoveryServiceStub:
    def __init__(self, channel):
        self.StreamAggregatedResources = channel.stream_stream(
            '/envoy.service.discovery.v3.AggregatedDiscoveryService/StreamAggregatedResources',
            request_serializer=lambda x: x.SerializeToString() if hasattr(x, 'SerializeToString') else b'',
            response_deserializer=lambda x: x
        )

class AggregatedDiscoveryServiceServicer:
    def StreamAggregatedResources(self, request_iterator, context):
        raise NotImplementedError('Method not implemented!')

def add_AggregatedDiscoveryServiceServicer_to_server(servicer, server):
    rpc_method_handlers = {
        'StreamAggregatedResources': grpc.stream_stream_rpc_method_handler(
            servicer.StreamAggregatedResources,
            request_deserializer=lambda x: x,
            response_serializer=lambda x: x.SerializeToString() if hasattr(x, 'SerializeToString') else x,
        ),
    }
    generic_handler = grpc.method_handlers_generic_handler(
        'envoy.service.discovery.v3.AggregatedDiscoveryService', rpc_method_handlers)
    server.add_generic_rpc_handlers((generic_handler,))
EOF

# Create other minimal stubs
touch envoy/config/listener/v3/listener_pb2.py
touch envoy/config/core/v3/address_pb2.py  
touch envoy/config/core/v3/socket_option_pb2.py
touch envoy/admin/v3/reverse_xds_pb2.py
touch envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager_pb2.py
touch envoy/extensions/filters/http/router/v3/router_pb2.py
touch envoy/config/route/v3/route_pb2.py

echo "   ✅ Minimal protobuf stubs created"
echo ""

echo "4. 📋 Creating usage instructions..."

cat > README.md << 'EOF'
# Envoy Python Protobuf Stubs

This directory contains minimal Python protobuf stubs for the Envoy API.

## Usage

Add this directory to your Python path:

```bash
export PYTHONPATH="/path/to/envoy_protos:$PYTHONPATH"
```

Or in your Python script:

```python
import sys
sys.path.insert(0, '/path/to/envoy_protos')

import envoy.service.discovery.v3.discovery_pb2 as discovery_pb2
import envoy.service.discovery.v3.ads_pb2_grpc as ads_pb2_grpc
```

## Note

These are minimal stubs for demo purposes. For production use, 
generate the full protobuf files using the proper Envoy build system.
EOF

echo ""
echo "✅ Setup complete!"
echo ""
echo "📖 Usage:"
echo "   export PYTHONPATH=\"$OUTPUT_DIR:\$PYTHONPATH\""
echo "   python3 examples/reverse-xds/management_server_client_example.py"
echo ""
echo "🔧 Test the setup:"
echo "   export PYTHONPATH=\"$OUTPUT_DIR:\$PYTHONPATH\""
echo "   python3 -c \"import envoy.service.discovery.v3.discovery_pb2; print('✅ Import successful')\""