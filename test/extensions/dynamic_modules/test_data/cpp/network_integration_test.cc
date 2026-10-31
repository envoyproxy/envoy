#include <cassert>

#include "source/extensions/dynamic_modules/sdk/cpp/sdk_network.h"

namespace Envoy {
namespace DynamicModules {

class FlowControlFilter : public NetworkFilter {
public:
  explicit FlowControlFilter(NetworkFilterHandle& handle) : handle_(handle) {}

  NetworkFilterStatus onNewConnection() override { return NetworkFilterStatus::Continue; }

  NetworkFilterStatus onRead(NetworkBuffer&, bool) override {
    if (!reads_disabled_) {
      const auto disable_status = handle_.readDisable(true);
      assert(disable_status == NetworkReadDisableStatus::TransitionedToReadDisabled);
      assert(!handle_.readEnabled());
      reads_disabled_ = true;

      const auto enable_status = handle_.readDisable(false);
      assert(enable_status == NetworkReadDisableStatus::TransitionedToReadEnabled);
      assert(handle_.readEnabled());
    }
    return NetworkFilterStatus::Continue;
  }

  NetworkFilterStatus onWrite(NetworkBuffer&, bool) override {
    return NetworkFilterStatus::Continue;
  }

  void onEvent(NetworkConnectionEvent) override {}
  void onDestroy() override {}

private:
  NetworkFilterHandle& handle_;
  bool reads_disabled_{false};
};

class FlowControlFactory : public NetworkFilterFactory {
public:
  std::unique_ptr<NetworkFilter> create(NetworkFilterHandle& handle) override {
    return std::make_unique<FlowControlFilter>(handle);
  }
};

class FlowControlConfigFactory : public NetworkFilterConfigFactory {
public:
  std::unique_ptr<NetworkFilterFactory> create(NetworkFilterConfigHandle&,
                                               std::string_view) override {
    return std::make_unique<FlowControlFactory>();
  }
};

REGISTER_NETWORK_FILTER_CONFIG_FACTORY(FlowControlConfigFactory, "flow_control");

class ConnectionStateFilter : public NetworkFilter {
public:
  explicit ConnectionStateFilter(NetworkFilterHandle& handle) : handle_(handle) {}

  NetworkFilterStatus onNewConnection() override {
    assert(handle_.getConnectionState() == NetworkConnectionState::Open);
    return NetworkFilterStatus::Continue;
  }

  NetworkFilterStatus onRead(NetworkBuffer&, bool) override {
    assert(handle_.getConnectionState() == NetworkConnectionState::Open);
    return NetworkFilterStatus::Continue;
  }

  NetworkFilterStatus onWrite(NetworkBuffer&, bool) override {
    assert(handle_.getConnectionState() == NetworkConnectionState::Open);
    return NetworkFilterStatus::Continue;
  }

  void onEvent(NetworkConnectionEvent) override {}
  void onDestroy() override {}

private:
  NetworkFilterHandle& handle_;
};

class ConnectionStateFactory : public NetworkFilterFactory {
public:
  std::unique_ptr<NetworkFilter> create(NetworkFilterHandle& handle) override {
    return std::make_unique<ConnectionStateFilter>(handle);
  }
};

class ConnectionStateConfigFactory : public NetworkFilterConfigFactory {
public:
  std::unique_ptr<NetworkFilterFactory> create(NetworkFilterConfigHandle&,
                                               std::string_view) override {
    return std::make_unique<ConnectionStateFactory>();
  }
};

REGISTER_NETWORK_FILTER_CONFIG_FACTORY(ConnectionStateConfigFactory, "connection_state");

class HalfCloseFilter : public NetworkFilter {
public:
  explicit HalfCloseFilter(NetworkFilterHandle& handle) : handle_(handle) {}

  NetworkFilterStatus onNewConnection() override {
    assert(handle_.isHalfCloseEnabled());
    return NetworkFilterStatus::Continue;
  }

  NetworkFilterStatus onRead(NetworkBuffer&, bool) override {
    handle_.enableHalfClose(false);
    assert(!handle_.isHalfCloseEnabled());
    handle_.enableHalfClose(true);
    assert(handle_.isHalfCloseEnabled());
    return NetworkFilterStatus::Continue;
  }

  NetworkFilterStatus onWrite(NetworkBuffer&, bool) override {
    return NetworkFilterStatus::Continue;
  }

  void onEvent(NetworkConnectionEvent) override {}
  void onDestroy() override {}

private:
  NetworkFilterHandle& handle_;
};

class HalfCloseFactory : public NetworkFilterFactory {
public:
  std::unique_ptr<NetworkFilter> create(NetworkFilterHandle& handle) override {
    return std::make_unique<HalfCloseFilter>(handle);
  }
};

class HalfCloseConfigFactory : public NetworkFilterConfigFactory {
public:
  std::unique_ptr<NetworkFilterFactory> create(NetworkFilterConfigHandle&,
                                               std::string_view) override {
    return std::make_unique<HalfCloseFactory>();
  }
};

REGISTER_NETWORK_FILTER_CONFIG_FACTORY(HalfCloseConfigFactory, "half_close");

class BufferLimitsFilter : public NetworkFilter {
public:
  explicit BufferLimitsFilter(NetworkFilterHandle& handle) : handle_(handle) {}

  NetworkFilterStatus onNewConnection() override {
    initial_buffer_limit_ = handle_.getBufferLimit();
    handle_.setBufferLimits(32768);
    assert(handle_.getBufferLimit() == 32768);
    return NetworkFilterStatus::Continue;
  }

  NetworkFilterStatus onRead(NetworkBuffer&, bool) override {
    return NetworkFilterStatus::Continue;
  }

  NetworkFilterStatus onWrite(NetworkBuffer&, bool) override {
    return NetworkFilterStatus::Continue;
  }

  void onEvent(NetworkConnectionEvent) override {}
  void onDestroy() override {}

  void onAboveWriteBufferHighWatermark() override { above_high_watermark_called_ = true; }
  void onBelowWriteBufferLowWatermark() override { below_low_watermark_called_ = true; }

private:
  NetworkFilterHandle& handle_;
  inline static bool above_high_watermark_called_{false};
  inline static bool below_low_watermark_called_{false};
  inline static uint32_t initial_buffer_limit_{0};
};

class BufferLimitsFactory : public NetworkFilterFactory {
public:
  std::unique_ptr<NetworkFilter> create(NetworkFilterHandle& handle) override {
    return std::make_unique<BufferLimitsFilter>(handle);
  }
};

class BufferLimitsConfigFactory : public NetworkFilterConfigFactory {
public:
  std::unique_ptr<NetworkFilterFactory> create(NetworkFilterConfigHandle&,
                                               std::string_view) override {
    return std::make_unique<BufferLimitsFactory>();
  }
};

REGISTER_NETWORK_FILTER_CONFIG_FACTORY(BufferLimitsConfigFactory, "buffer_limits");

class DynamicMetadataFilter : public NetworkFilter {
public:
  explicit DynamicMetadataFilter(NetworkFilterHandle& handle) : handle_(handle) {}

  NetworkFilterStatus onNewConnection() override {
    // A serialized google.protobuf.Struct with "k" set to "v".
    static constexpr char serialized_struct[] = {0x0a, 0x08, 0x0a, 0x01, 0x6b,
                                                 0x12, 0x03, 0x1a, 0x01, 0x76};
    // Stop the chain instead of asserting, since assert is compiled out in opt builds.
    if (!handle_.setMetadataStruct(
            "dynamic_modules.struct",
            std::string_view(serialized_struct, sizeof(serialized_struct))) ||
        handle_.getMetadataString("dynamic_modules.struct", "k") != "v") {
      return NetworkFilterStatus::Stop;
    }

    // A serialized google.protobuf.Any with type URL "t/x" and value 0x01 0x02.
    static constexpr char serialized_any[] = {0x0a, 0x03, 0x74, 0x2f, 0x78, 0x12, 0x02, 0x01, 0x02};
    if (!handle_.setTypedMetadata("dynamic_modules.typed",
                                  std::string_view(serialized_any, sizeof(serialized_any)))) {
      return NetworkFilterStatus::Stop;
    }

    // A struct that does not parse and an Any without a type URL are rejected.
    if (handle_.setMetadataStruct("dynamic_modules.struct", std::string_view("\x0f", 1)) ||
        handle_.setTypedMetadata("dynamic_modules.typed", {})) {
      return NetworkFilterStatus::Stop;
    }
    return NetworkFilterStatus::Continue;
  }

  NetworkFilterStatus onRead(NetworkBuffer&, bool) override {
    return NetworkFilterStatus::Continue;
  }

  NetworkFilterStatus onWrite(NetworkBuffer&, bool) override {
    return NetworkFilterStatus::Continue;
  }

  void onEvent(NetworkConnectionEvent) override {}
  void onDestroy() override {}

private:
  NetworkFilterHandle& handle_;
};

class DynamicMetadataFactory : public NetworkFilterFactory {
public:
  std::unique_ptr<NetworkFilter> create(NetworkFilterHandle& handle) override {
    return std::make_unique<DynamicMetadataFilter>(handle);
  }
};

class DynamicMetadataConfigFactory : public NetworkFilterConfigFactory {
public:
  std::unique_ptr<NetworkFilterFactory> create(NetworkFilterConfigHandle&,
                                               std::string_view) override {
    return std::make_unique<DynamicMetadataFactory>();
  }
};

REGISTER_NETWORK_FILTER_CONFIG_FACTORY(DynamicMetadataConfigFactory, "dynamic_metadata");

} // namespace DynamicModules
} // namespace Envoy
