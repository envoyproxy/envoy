#pragma once

#include <queue>

#include "envoy/registry/registry.h"

#include "source/common/config/utility.h"
#include "source/common/router/string_accessor_impl.h"
#include "source/extensions/filters/udp/udp_proxy/session_filters/factory_base.h"

#include "test/extensions/filters/udp/udp_proxy/session_filters/buffer_filter.pb.h"
#include "test/extensions/filters/udp/udp_proxy/session_filters/buffer_filter.pb.validate.h"
#include "test/test_common/utility.h"

namespace Envoy {
namespace Extensions {
namespace UdpFilters {
namespace UdpProxy {
namespace SessionFilters {

using Filter = Network::UdpSessionFilter;
using ReadFilterStatus = Network::UdpSessionReadFilterStatus;
using WriteFilterStatus = Network::UdpSessionWriteFilterStatus;
using ReadFilterCallbacks = Network::UdpSessionReadFilterCallbacks;
using WriteFilterCallbacks = Network::UdpSessionWriteFilterCallbacks;

using BufferingFilterConfig =
    test::extensions::filters::udp::udp_proxy::session_filters::BufferingFilterConfig;

using BufferedDatagramPtr = std::unique_ptr<Network::UdpRecvData>;

class BufferingSessionFilter : public Filter {
public:
  BufferingSessionFilter(int downstream_datagrams_to_buffer, int upstream_datagrams_to_buffer,
                         bool continue_after_inject)
      : downstream_datagrams_to_buffer_(downstream_datagrams_to_buffer),
        upstream_datagrams_to_buffer_(upstream_datagrams_to_buffer),
        continue_after_inject_(continue_after_inject) {}

  void initializeReadFilterCallbacks(ReadFilterCallbacks& callbacks) override {
    read_callbacks_ = &callbacks;
    // Verify that the filter is able to access the stream info.
    callbacks.streamInfo().filterState()->setData(
        "test.read", std::make_shared<Router::StringAccessorImpl>("val"),
        Envoy::StreamInfo::FilterState::StateType::Mutable);
  }

  void initializeWriteFilterCallbacks(WriteFilterCallbacks& callbacks) override {
    write_callbacks_ = &callbacks;
    // Verify that the filter is able to access the stream info.
    callbacks.streamInfo().filterState()->setData(
        "test.write", std::make_shared<Router::StringAccessorImpl>("val"),
        Envoy::StreamInfo::FilterState::StateType::Mutable);
  }

  ReadFilterStatus onNewSession() override { return ReadFilterStatus::Continue; }

  ReadFilterStatus onData(Network::UdpRecvData& data) override {
    if (downstream_buffer_.size() < downstream_datagrams_to_buffer_) {
      bufferRead(data);
      return ReadFilterStatus::StopIteration;
    }

    // There's no async callback, so we use the next datagram as a trigger to flush the buffer.
    while (!downstream_buffer_.empty()) {
      BufferedDatagramPtr buffered_datagram = std::move(downstream_buffer_.front());
      downstream_buffer_.pop();
      read_callbacks_->injectDatagramToFilterChain(*buffered_datagram);
    }

    if (continue_after_inject_) {
      return ReadFilterStatus::Continue;
    }

    bufferRead(data);
    return ReadFilterStatus::StopIteration;
  }

  WriteFilterStatus onWrite(Network::UdpRecvData& data) override {
    if (upstream_buffer_.size() < upstream_datagrams_to_buffer_) {
      bufferWrite(data);
      return WriteFilterStatus::StopIteration;
    }

    // There's no async callback, so we use the next datagram as a trigger to flush the buffer.
    while (!upstream_buffer_.empty()) {
      BufferedDatagramPtr buffered_datagram = std::move(upstream_buffer_.front());
      upstream_buffer_.pop();
      write_callbacks_->injectDatagramToFilterChain(*buffered_datagram);
    }

    if (continue_after_inject_) {
      return WriteFilterStatus::Continue;
    }

    bufferWrite(data);
    return WriteFilterStatus::StopIteration;
  }

private:
  void bufferRead(Network::UdpRecvData& data) {
    auto buffered_datagram = std::make_unique<Network::UdpRecvData>();
    buffered_datagram->addresses_ = {std::move(data.addresses_.local_),
                                     std::move(data.addresses_.peer_)};
    buffered_datagram->buffer_ = std::move(data.buffer_);
    buffered_datagram->receive_time_ = data.receive_time_;
    downstream_buffer_.push(std::move(buffered_datagram));
  }

  void bufferWrite(Network::UdpRecvData& data) {
    auto buffered_datagram = std::make_unique<Network::UdpRecvData>();
    buffered_datagram->addresses_ = {std::move(data.addresses_.local_),
                                     std::move(data.addresses_.peer_)};
    buffered_datagram->buffer_ = std::move(data.buffer_);
    buffered_datagram->receive_time_ = data.receive_time_;
    upstream_buffer_.push(std::move(buffered_datagram));
  }

  ReadFilterCallbacks* read_callbacks_;
  WriteFilterCallbacks* write_callbacks_;
  uint32_t downstream_datagrams_to_buffer_;
  uint32_t upstream_datagrams_to_buffer_;
  bool continue_after_inject_{false};
  std::queue<BufferedDatagramPtr> downstream_buffer_;
  std::queue<BufferedDatagramPtr> upstream_buffer_;
};

class BufferingSessionFilterConfigFactory : public FactoryBase<BufferingFilterConfig> {
public:
  BufferingSessionFilterConfigFactory() : FactoryBase("test.udp_session.buffer") {}

private:
  FilterFactoryCb
  createFilterFactoryFromProtoTyped(const BufferingFilterConfig& config,
                                    Server::Configuration::FactoryContext&) override {
    return [config](Network::UdpSessionFilterChainFactoryCallbacks& callbacks) -> void {
      callbacks.addFilter(std::make_shared<BufferingSessionFilter>(
          config.downstream_datagrams_to_buffer(), config.upstream_datagrams_to_buffer(),
          config.continue_after_inject()));
    };
  }
};

static Registry::RegisterFactory<BufferingSessionFilterConfigFactory,
                                 NamedUdpSessionFilterConfigFactory>
    register_buffer_udp_session_filter_;

} // namespace SessionFilters
} // namespace UdpProxy
} // namespace UdpFilters
} // namespace Extensions
} // namespace Envoy
