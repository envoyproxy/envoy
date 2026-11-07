#include <cassert>

#include "source/extensions/dynamic_modules/sdk/cpp/sdk_listener.h"

namespace Envoy {
namespace DynamicModules {

class WriteToSocketFilter : public ListenerFilter {
public:
  explicit WriteToSocketFilter(ListenerFilterHandle& handle) : handle_(handle) {}

  ListenerFilterStatus onAccept() override {
    assert(handle_.getConnectionStartTimeMs() > 0);
    assert(handle_.getRemoteAddress().has_value());
    assert(handle_.getLocalAddress().has_value());
    handle_.setRequestedServerName("sdk.listener.test");
    handle_.setDetectedTransportProtocol("sdk_listener");
    return ListenerFilterStatus::Continue;
  }

private:
  ListenerFilterHandle& handle_;
};

class WriteToSocketFactory : public ListenerFilterFactory {
public:
  std::unique_ptr<ListenerFilter> create(ListenerFilterHandle& handle) override {
    return std::make_unique<WriteToSocketFilter>(handle);
  }
};

class WriteToSocketConfigFactory : public ListenerFilterConfigFactory {
public:
  std::unique_ptr<ListenerFilterFactory> create(ListenerFilterConfigHandle&,
                                                std::string_view) override {
    return std::make_unique<WriteToSocketFactory>();
  }
};

REGISTER_LISTENER_FILTER_CONFIG_FACTORY(WriteToSocketConfigFactory, "write_to_socket");

class BufferReadFilter : public ListenerFilter {
public:
  explicit BufferReadFilter(ListenerFilterHandle& handle) : handle_(handle) {}

  ListenerFilterStatus onAccept() override { return ListenerFilterStatus::StopIteration; }

  ListenerFilterStatus onData(size_t data_length) override {
    assert(data_length >= 4);
    const auto chunk = handle_.getBufferChunk();
    assert(chunk.has_value());
    assert(chunk->toStringView().substr(0, 4) == "ping");
    assert(handle_.currentMaxReadBytes() == 4);
    return ListenerFilterStatus::Continue;
  }

  size_t maxReadBytes() override { return 4; }

private:
  ListenerFilterHandle& handle_;
};

class BufferReadFactory : public ListenerFilterFactory {
public:
  std::unique_ptr<ListenerFilter> create(ListenerFilterHandle& handle) override {
    return std::make_unique<BufferReadFilter>(handle);
  }
};

class BufferReadConfigFactory : public ListenerFilterConfigFactory {
public:
  std::unique_ptr<ListenerFilterFactory> create(ListenerFilterConfigHandle&,
                                                std::string_view) override {
    return std::make_unique<BufferReadFactory>();
  }
};

REGISTER_LISTENER_FILTER_CONFIG_FACTORY(BufferReadConfigFactory, "buffer_read");

class DynamicMetadataFilter : public ListenerFilter {
public:
  explicit DynamicMetadataFilter(ListenerFilterHandle& handle) : handle_(handle) {}

  ListenerFilterStatus onAccept() override {
    // A serialized google.protobuf.Struct with "k" set to "v".
    static constexpr char serialized_struct[] = {0x0a, 0x08, 0x0a, 0x01, 0x6b,
                                                 0x12, 0x03, 0x1a, 0x01, 0x76};
    // Stop the chain instead of asserting, since assert is compiled out in opt builds.
    if (!handle_.setDynamicMetadataStruct(
            "dynamic_modules.struct",
            std::string_view(serialized_struct, sizeof(serialized_struct))) ||
        handle_.getDynamicMetadataString("dynamic_modules.struct", "k") != "v") {
      return ListenerFilterStatus::StopIteration;
    }

    // A serialized google.protobuf.Any with type URL "t/x" and value 0x01 0x02.
    static constexpr char serialized_any[] = {0x0a, 0x03, 0x74, 0x2f, 0x78, 0x12, 0x02, 0x01, 0x02};
    if (!handle_.setDynamicTypedMetadata(
            "dynamic_modules.typed", std::string_view(serialized_any, sizeof(serialized_any)))) {
      return ListenerFilterStatus::StopIteration;
    }

    // A struct that does not parse and an Any without a type URL are rejected.
    if (handle_.setDynamicMetadataStruct("dynamic_modules.struct", std::string_view("\x0f", 1)) ||
        handle_.setDynamicTypedMetadata("dynamic_modules.typed", {})) {
      return ListenerFilterStatus::StopIteration;
    }
    return ListenerFilterStatus::Continue;
  }

private:
  ListenerFilterHandle& handle_;
};

class DynamicMetadataFactory : public ListenerFilterFactory {
public:
  std::unique_ptr<ListenerFilter> create(ListenerFilterHandle& handle) override {
    return std::make_unique<DynamicMetadataFilter>(handle);
  }
};

class DynamicMetadataConfigFactory : public ListenerFilterConfigFactory {
public:
  std::unique_ptr<ListenerFilterFactory> create(ListenerFilterConfigHandle&,
                                                std::string_view) override {
    return std::make_unique<DynamicMetadataFactory>();
  }
};

REGISTER_LISTENER_FILTER_CONFIG_FACTORY(DynamicMetadataConfigFactory, "dynamic_metadata");

} // namespace DynamicModules
} // namespace Envoy
