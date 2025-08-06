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
