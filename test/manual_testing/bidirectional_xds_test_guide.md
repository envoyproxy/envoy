# Bidirectional xDS Manual Testing Guide

This guide provides step-by-step instructions for manually testing the bidirectional xDS implementation.

## Prerequisites

```bash
# Build the necessary components
bazel build //examples/reverse-xds:bidirectional_main
bazel build //test/manual_testing:test_management_server
bazel test //test/extensions/config_subscription/grpc:bidirectional_grpc_mux_test
```

## Testing Levels

### 1. Unit Tests

Run the basic unit tests to verify core functionality:

```bash
# Run unit tests
bazel test //test/extensions/config_subscription/grpc:bidirectional_grpc_mux_test

# Run integration tests
bazel test //test/extensions/config_subscription/grpc:bidirectional_integration_test

# Run with verbose output
bazel test //test/extensions/config_subscription/grpc:bidirectional_grpc_mux_test --test_output=all
```

**Expected Output:**
```
[==========] Running 3 tests from 1 test suite.
[----------] Global test environment set-up.
[----------] 3 tests from ListenerStatusProviderTest
[ RUN      ] ListenerStatusProviderTest.BasicFunctionality
[       OK ] ListenerStatusProviderTest.BasicFunctionality (1 ms)
[ RUN      ] ListenerStatusProviderTest.ListenerFailure  
[       OK ] ListenerStatusProviderTest.ListenerFailure (0 ms)
[ RUN      ] ListenerStatusProviderTest.SelectiveResourceRetrieval
[       OK ] ListenerStatusProviderTest.SelectiveResourceRetrieval (1 ms)
[----------] 3 tests from ListenerStatusProviderTest (2 ms total)
```

### 2. Integration Tests

Test the complete bidirectional flow:

```bash
# Run integration tests
bazel test //test/extensions/config_subscription/grpc:bidirectional_integration_test --test_output=all
```

### 3. End-to-End Manual Testing

#### Step 1: Start the Test Management Server

```bash
# Terminal 1: Start the test management server
./bazel-bin/test/manual_testing/test_management_server --port=18000

# Expected output:
# Bidirectional ADS server listening on [::]:18000
# Waiting for Envoy clients to connect...
```

#### Step 2: Create Envoy Configuration

Create `test_envoy_config.yaml`:

```yaml
static_resources:
  listeners:
  - name: http_listener
    address:
      socket_address:
        address: 0.0.0.0
        port_value: 10000
    filter_chains:
    - filters:
      - name: envoy.filters.network.http_connection_manager
        typed_config:
          "@type": type.googleapis.com/envoy.extensions.filters.network.http_connection_manager.v3.HttpConnectionManager
          stat_prefix: ingress_http
          route_config:
            name: local_route
            virtual_hosts:
            - name: local_service
              domains: ["*"]
              routes:
              - match:
                  prefix: "/"
                direct_response:
                  status: 200
                  body:
                    inline_string: "Hello from Envoy!\n"
          http_filters:
          - name: envoy.filters.http.router

dynamic_resources:
  ads_config:
    api_type: GRPC
    transport_api_version: V3
    grpc_services:
    - envoy_grpc:
        cluster_name: ads_cluster

  clusters:
  - name: ads_cluster
    type: STRICT_DNS
    load_assignment:
      cluster_name: ads_cluster
      endpoints:
      - lb_endpoints:
        - endpoint:
            address:
              socket_address:
                address: 127.0.0.1
                port_value: 18000
    typed_extension_protocol_options:
      envoy.extensions.upstreams.http.v3.HttpProtocolOptions:
        "@type": type.googleapis.com/envoy.extensions.upstreams.http.v3.HttpProtocolOptions
        explicit_http_config:
          http2_protocol_options: {}

admin:
  address:
    socket_address:
      address: 127.0.0.1
      port_value: 9901
```

#### Step 3: Start Envoy with Bidirectional xDS

```bash
# Terminal 2: Start Envoy with bidirectional xDS
./bazel-bin/examples/reverse-xds/bidirectional_main -c test_envoy_config.yaml

# Expected output:
# [info] Setting up bidirectional xDS with proper integration
# [info] Generated 2 listener status resources for reverse xDS
# [info] Bidirectional xDS configuration ready
```

#### Step 4: Verify the Connection

In the management server terminal, you should see:

```
=== New client connected: grpc://127.0.0.1:xxxxx ===
Config request from grpc://127.0.0.1:xxxxx: type.googleapis.com/envoy.config.listener.v3.Listener
Requesting listener status from grpc://127.0.0.1:xxxxx

=== Received Status from grpc://127.0.0.1:xxxxx ===
Type: type.googleapis.com/envoy.admin.v3.ListenerReadinessStatus
Version: 3
Resources: 2 items
Resource 1:
  Type: type.googleapis.com/envoy.admin.v3.ListenerReadinessStatus
  Size: xxx bytes
Resource 2:
  Type: type.googleapis.com/envoy.admin.v3.ListenerReadinessStatus
  Size: xxx bytes
```

### 4. Functional Testing

#### Test Normal HTTP Traffic

```bash
# Verify Envoy is serving HTTP traffic
curl http://localhost:10000
# Expected: "Hello from Envoy!"
```

#### Test Admin Interface

```bash
# Check Envoy admin interface
curl http://localhost:9901/stats | grep listener
curl http://localhost:9901/config_dump
```

#### Test Listener State Changes

Simulate listener state changes and verify they're reported via reverse xDS:

```bash
# Terminal 3: Simulate listener failure (occupy port 10000)
nc -l 10000

# Check management server logs for updated status
# Terminal 4: Restart Envoy to trigger listener events
# Kill and restart the Envoy process
```

### 5. Advanced Testing Scenarios

#### Scenario 1: Multiple Listeners

Update the Envoy config to add more listeners:

```yaml
static_resources:
  listeners:
  - name: http_listener
    # ... existing config
  - name: https_listener
    address:
      socket_address:
        address: 0.0.0.0
        port_value: 10443
    # ... HTTPS config
```

#### Scenario 2: Selective Resource Requests

Modify the test management server to request specific listeners:

```python
# In test_management_server.py, modify the reverse request
reverse_request.add_resource_names("http_listener")  # Only request specific listener
```

#### Scenario 3: Resource Updates

Test that version increments work correctly:

1. Start with one listener
2. Add another listener via LDS update
3. Verify the version increments in reverse xDS responses

## Debugging and Troubleshooting

### Enable Debug Logging

```bash
# Run Envoy with debug logging
./bazel-bin/examples/reverse-xds/bidirectional_main -c test_envoy_config.yaml -l debug

# Look for these log lines:
# [debug] Received reverse xDS request from management server for type: type.googleapis.com/envoy.admin.v3.ListenerReadinessStatus
# [debug] Sent reverse xDS response for type: type.googleapis.com/envoy.admin.v3.ListenerReadinessStatus, version: 1
```

### Common Issues and Solutions

#### Issue: Management server doesn't receive reverse responses

**Solution:** Check that:
1. Envoy connects to the management server successfully
2. The `BidirectionalGrpcMuxFactory` is being used
3. Listener status providers are registered

#### Issue: Empty reverse xDS responses

**Solution:** Verify that:
1. Listener events are being triggered (`onListenerAdded`, `onListenerReady`)
2. The resource provider is returning non-empty resource lists
3. Proto serialization is working correctly

#### Issue: Version tracking not working

**Solution:** Check that:
1. `incrementVersion()` is called on status updates
2. Version info is thread-safe (uses proper locking)
3. Version comparison logic in the management server

### Monitoring and Metrics

Check these stats to monitor bidirectional xDS:

```bash
# General xDS stats
curl http://localhost:9901/stats | grep "^config\."

# Stream stats  
curl http://localhost:9901/stats | grep "grpc_mux"

# Custom reverse xDS stats (if implemented)
curl http://localhost:9901/stats | grep "reverse_xds"
```

## Expected Test Results

### Successful Test Indicators

1. **Unit tests pass**: All basic functionality tests complete successfully
2. **Connection established**: Management server shows client connection
3. **Normal xDS works**: Envoy receives configuration via ADS
4. **Reverse xDS works**: Management server receives status via reverse requests
5. **Version tracking**: Version numbers increment on status changes
6. **Selective requests**: Specific resource requests return correct subsets
7. **Resource accuracy**: Status data matches actual Envoy state

### Test Summary Checklist

- [ ] Unit tests pass
- [ ] Integration tests pass  
- [ ] ADS connection established
- [ ] Normal configuration delivery works
- [ ] Reverse status requests work
- [ ] Listener status accurately reflects Envoy state
- [ ] Version tracking increments correctly
- [ ] Selective resource requests work
- [ ] Error handling works (failed listeners, etc.)
- [ ] Stream remains stable under load

## Performance Testing

For performance testing, use:

```bash
# Test with multiple clients
for i in {1..10}; do
  ./bazel-bin/examples/reverse-xds/bidirectional_main -c test_envoy_config_$i.yaml &
done

# Monitor resource usage
top -p $(pgrep -f test_management_server)
```

This comprehensive testing approach ensures the bidirectional xDS implementation works correctly at all levels. 