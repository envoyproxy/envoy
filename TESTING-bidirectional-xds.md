# Testing Bidirectional xDS Implementation

This document provides a complete guide for testing the bidirectional xDS implementation at various levels.

## 🚀 Quick Start

Run all tests with one command:
```bash
./test/scripts/run_bidirectional_xds_tests.sh
```

## 📋 Testing Levels Overview

### 1. **Unit Tests** - Fast, focused functionality testing
### 2. **Integration Tests** - Complete bidirectional flow testing  
### 3. **Manual Testing** - End-to-end testing with real servers
### 4. **Build Verification** - Ensure all components compile

---

## 🧪 Testing Options

### Option 1: Automated Test Suite (Recommended)

**Run everything:**
```bash
./test/scripts/run_bidirectional_xds_tests.sh
```

**Run specific test levels:**
```bash
# Unit tests only
./test/scripts/run_bidirectional_xds_tests.sh --unit-only

# Build verification only  
./test/scripts/run_bidirectional_xds_tests.sh --build-only

# Prepare manual testing only
./test/scripts/run_bidirectional_xds_tests.sh --manual-only

# Verbose output
./test/scripts/run_bidirectional_xds_tests.sh --verbose
```

### Option 2: Individual Test Commands

**Unit Tests:**
```bash
# Core functionality tests
bazel test //test/extensions/config_subscription/grpc:bidirectional_grpc_mux_test

# Integration tests  
bazel test //test/extensions/config_subscription/grpc:bidirectional_integration_test

# Verbose output
bazel test //test/extensions/config_subscription/grpc:bidirectional_grpc_mux_test --test_output=all
```

**Build Tests:**
```bash
# Build all components
bazel build //source/extensions/config_subscription/grpc:bidirectional_grpc_mux_lib
bazel build //source/server:listener_status_provider_lib
bazel build //examples/reverse-xds:bidirectional_main
```

### Option 3: Manual End-to-End Testing

**Step 1: Start Management Server**
```bash
# Terminal 1
python3 test/manual_testing/test_management_server.py --port=18000
```

**Step 2: Start Envoy with Bidirectional xDS**
```bash
# Terminal 2 (after running automated tests to generate config)
./test/scripts/run_bidirectional_xds_tests.sh --manual-only
./test_output/run_manual_test.sh
```

**Step 3: Test Functionality**
```bash
# Terminal 3
curl http://localhost:10000  # Test HTTP traffic
curl http://localhost:9901/stats | grep listener  # Check Envoy stats
```

---

## 📊 Test Results and Verification

### ✅ Success Indicators

**Unit Tests:**
- All test cases pass
- No compilation errors
- Resource providers work correctly

**Integration Tests:**  
- Bidirectional message flow works
- Version tracking increments properly
- Resource filtering works correctly

**Manual Tests:**
- Management server connects to Envoy
- Normal xDS configuration delivery works
- Reverse xDS status requests succeed  
- Status data matches actual Envoy state

### 📈 Expected Output Examples

**Unit Test Success:**
```
[==========] Running 3 tests from 1 test suite.
[----------] 3 tests from ListenerStatusProviderTest
[ RUN      ] ListenerStatusProviderTest.BasicFunctionality
[       OK ] ListenerStatusProviderTest.BasicFunctionality (1 ms)
[ RUN      ] ListenerStatusProviderTest.ListenerFailure
[       OK ] ListenerStatusProviderTest.ListenerFailure (0 ms)  
[ RUN      ] ListenerStatusProviderTest.SelectiveResourceRetrieval
[       OK ] ListenerStatusProviderTest.SelectiveResourceRetrieval (1 ms)
[----------] 3 tests from ListenerStatusProviderTest (2 ms total)
```

**Manual Test Success (Management Server):**
```
🚀 Test Management Server started on [::]:18000
📡 Reverse xDS: ENABLED
⏳ Waiting for Envoy clients to connect...

✅ New client connected: envoy-45678
📥 Config request from envoy-45678: type.googleapis.com/envoy.config.listener.v3.Listener
📤 Sent config response: 1 resources
🔄 Sending reverse xDS request to envoy-45678

🎯 === Received Status from envoy-45678 ===
📋 Type: type.googleapis.com/envoy.admin.v3.ListenerReadinessStatus
🏷️  Version: 2
📦 Resources: 1 items

  📄 Resource 1:
     📋 Listener: http_listener
     ✅ Ready: true
     🌐 Address: 0.0.0.0:10000
     🔧 State: READY
🎯 === End Status Report ===
```

---

## 🔧 Debugging and Troubleshooting

### Debug Mode

Enable debug logging for detailed output:
```bash
# Unit tests with debug output
bazel test //test/extensions/config_subscription/grpc:bidirectional_grpc_mux_test --test_output=all

# Manual testing with debug logging
./bazel-bin/examples/reverse-xds/bidirectional_main -c test_config.yaml -l debug
```

### Common Issues

**Issue: Tests fail to build**
```bash
# Solution: Verify Bazel workspace
bazel info workspace
bazel clean
```

**Issue: Management server connection fails**
```bash
# Solution: Check port availability
netstat -ln | grep 18000
lsof -i :18000
```

**Issue: No reverse xDS responses**
```bash
# Solution: Verify provider registration and events
# Check Envoy logs for:
# [debug] Received reverse xDS request from management server  
# [debug] Sent reverse xDS response for type: ...
```

### Monitoring and Metrics

**Check Envoy Stats:**
```bash
# General xDS stats
curl http://localhost:9901/stats | grep "^config\."

# Stream connection stats
curl http://localhost:9901/stats | grep "grpc_mux" 

# Cluster stats
curl http://localhost:9901/stats | grep cluster
```

**Check Config Dump:**
```bash
curl http://localhost:9901/config_dump | jq '.configs[].dynamic_listeners'
```

---

## 📁 Test Artifacts and Files

### Generated Test Files (in `test_output/`)
- `test_envoy_config.yaml` - Test Envoy configuration
- `run_manual_test.sh` - Manual test execution script  
- `MANUAL_TEST_INSTRUCTIONS.md` - Detailed manual test guide
- `test_report.md` - Comprehensive test results report

### Core Test Files
- `test/extensions/config_subscription/grpc/bidirectional_grpc_mux_test.cc` - Unit tests
- `test/extensions/config_subscription/grpc/bidirectional_integration_test.cc` - Integration tests
- `test/manual_testing/test_management_server.py` - Test management server
- `test/manual_testing/bidirectional_xds_test_guide.md` - Detailed manual test guide

---

## 🎯 Test Coverage

### What's Tested

**✅ Core Functionality:**
- BidirectionalGrpcMuxImpl creation and registration
- ClientResourceProvider interface implementation
- ListenerStatusProvider lifecycle management
- Proto message serialization/deserialization

**✅ Integration:**
- Bidirectional message flow on same stream
- Version tracking and increments
- Selective resource requests
- Error handling for failed listeners

**✅ Manual Verification:**
- Real management server communication
- ADS stream establishment
- HTTP traffic serving
- Status reporting accuracy

### What's NOT Tested (Future Work)

**🔄 Areas for Future Testing:**
- Performance under high load
- Multiple concurrent clients
- Network partition scenarios
- Resource update stress testing
- Memory leak testing
- Cluster health status providers
- Configuration snapshot providers

---

## 📈 Performance Testing

For performance and stress testing:

```bash
# Multiple client simulation
for i in {1..10}; do
  ./bazel-bin/examples/reverse-xds/bidirectional_main -c test_config_$i.yaml &
done

# Monitor resource usage
top -p $(pgrep -f test_management_server)
htop -p $(pgrep -f bidirectional_main)

# Test with high frequency requests
python3 test/manual_testing/test_management_server.py --interval=0.1
```

---

## 🎉 Summary

The bidirectional xDS implementation includes comprehensive testing at multiple levels:

1. **✅ Automated Unit Tests** - Fast, reliable functionality verification
2. **✅ Integration Tests** - Complete bidirectional flow validation  
3. **✅ Manual Testing Tools** - Real-world scenario verification
4. **✅ Build Verification** - Compilation and dependency checks
5. **✅ Debug Support** - Detailed logging and troubleshooting

**Quick Test Command:**
```bash
# Run all tests with one command
./test/scripts/run_bidirectional_xds_tests.sh
```

**For detailed step-by-step manual testing, see:**
- `test/manual_testing/bidirectional_xds_test_guide.md`
- `test_output/MANUAL_TEST_INSTRUCTIONS.md` (generated after running tests)

The implementation provides a solid foundation for reverse xDS that's been thoroughly tested and verified to work correctly. 