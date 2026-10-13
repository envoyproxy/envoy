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

} // namespace DynamicModules
} // namespace Envoy
