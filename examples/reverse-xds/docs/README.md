# Bidirectional xDS Demo

This demo showcases Envoy's bidirectional xDS capability, where a management server can both send configuration to Envoy (normal xDS) and request status information from Envoy (reverse xDS) using the same ADS stream.

## Directory Structure

```
examples/reverse-xds/
├── config/           # Envoy configuration files
├── docs/            # Documentation (this README)
├── scripts/         # Run scripts and utilities  
├── server/          # Go-based management server
└── old-python-server/  # Legacy Python implementation (archived)
```

## Features Demonstrated

- 🎯 **Dynamic Listener Creation**: Management server creates a listener on port 8080
- 📊 **Real-time Status Monitoring**: Server monitors listener readiness via reverse xDS  
- 🔄 **Bidirectional Communication**: Same stream for both configuration and status
- ⚡ **Live Updates**: No restarts needed for dynamic configuration

## Prerequisites

1. **Build Envoy** with bidirectional xDS support:
   ```bash
   bazel build //source/exe:envoy
   ```

2. **Go Dependencies** for the management server:
   ```bash
   # Go 1.21+ required - dependencies managed via go.mod
   cd examples/reverse-xds/server
   go mod download
   ```

## Quick Start

### Option 1: Separate Terminals (Recommended)
```bash
# Terminal 1: Start Go Management Server 
./examples/reverse-xds/scripts/run_go_server.sh

# Terminal 2: Start Envoy (after server is ready)
./examples/reverse-xds/scripts/run_envoy.sh
```

### Option 2: Interactive Demo Script
```bash
# Run the coordinator script with options
./examples/reverse-xds/scripts/run_bidirectional_demo.sh
```

### Option 3: Manual Setup

1. **Build Go Management Server:**
   ```bash
   cd examples/reverse-xds/server
   go mod download
   go build -o management-server main.go
   ```

2. **Start the Management Server:**
   ```bash
   cd examples/reverse-xds/server
   ./management-server
   ```

3. **Start Envoy** (in another terminal):
   ```bash
   ./bazel-bin/source/exe/envoy-static -c examples/reverse-xds/config/envoy_bidirectional_config.yaml --log-level info
   ```

## What Happens

1. **📡 Connection**: Envoy connects to management server on port 18000
2. **📋 LDS Request**: Envoy requests listener configuration via ADS
3. **🎯 Dynamic Config**: Server sends listener config for port 8080
4. **⚙️ Listener Creation**: Envoy creates and binds the HTTP listener
5. **🔍 Status Request**: Server requests listener status via reverse xDS
6. **📊 Status Response**: Envoy reports listener is ready at `0.0.0.0:8080`
7. **✅ Confirmation**: Server confirms listener is live
8. **🎉 Success**: Dynamic listener accepts traffic!

## Testing the Result

Once the listener is ready, test it:
```bash
# Test the dynamic listener
curl http://localhost:8080/

# Check Envoy admin interface
curl http://localhost:9901/listeners
curl http://localhost:9901/stats | grep dynamic_listener
```

## Key URLs

- **Envoy Admin Interface**: http://localhost:9901
- **Dynamic Listener**: http://localhost:8080
- **Management Server**: Port 18000 (gRPC)

## Configuration Details

### Envoy Configuration (`envoy_bidirectional_config.yaml`)
- Uses dynamic resources (LDS, CDS) via ADS
- Connects to management server on localhost:18000
- Enables bidirectional xDS features via node metadata
- Static cluster configuration for xDS connection

### Management Server Features
- Implements `AggregatedDiscoveryService`
- Sends dynamic HTTP listener configuration
- Monitors listener status via reverse xDS requests
- Provides real-time feedback on resource readiness

## Architecture Benefits

- **Single Stream**: Both directions use the same ADS connection
- **Existing Infrastructure**: Reuses standard xDS message types
- **Authentication**: Inherits existing gRPC authentication
- **Simplicity**: Much simpler than separate status servers
- **Real-time**: Immediate feedback on configuration changes

## Troubleshooting

1. **Connection Issues**: Check that management server is running on port 18000
2. **Build Issues**: Ensure Envoy was built with bidirectional xDS support
3. **Port Conflicts**: Make sure ports 8080, 9901, and 18000 are available
4. **Python Issues**: Verify gRPC and protobuf packages are installed

## Log Analysis

Watch for these key log messages:

**Management Server:**
- `New client connected`
- `Sending dynamic listener configuration`
- `Listener 'dynamic_listener' is now READY`

**Envoy:**
- `ads_config_source Established new gRPC bidi-stream`
- `lds: add/update listener 'dynamic_listener'`
- `listener_manager: Listener dynamic_listener added`

## Next Steps

This demo provides a foundation for:
- Production bidirectional xDS implementations
- Dynamic service mesh configuration
- Real-time infrastructure monitoring
- Advanced xDS control plane features