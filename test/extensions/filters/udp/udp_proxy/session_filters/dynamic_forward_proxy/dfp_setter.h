#pragma once

#include "envoy/registry/registry.h"

#include "source/common/config/utility.h"
#include "source/common/router/string_accessor_impl.h"
#include "source/common/stream_info/uint32_accessor_impl.h"
#include "source/extensions/filters/udp/udp_proxy/session_filters/factory_base.h"

#include "test/extensions/filters/udp/udp_proxy/session_filters/dynamic_forward_proxy/dfp_setter.pb.h"
#include "test/extensions/filters/udp/udp_proxy/session_filters/dynamic_forward_proxy/dfp_setter.pb.validate.h"
#include "test/test_common/utility.h"

namespace Envoy {
namespace Extensions {
namespace UdpFilters {
namespace UdpProxy {
namespace SessionFilters {
namespace DynamicForwardProxy {

using DynamicForwardProxySetterFilterConfig = test::extensions::filters::udp::udp_proxy::
    session_filters::DynamicForwardProxySetterFilterConfig;

using ReadFilter = Network::UdpSessionReadFilter;
using ReadFilterStatus = Network::UdpSessionReadFilterStatus;
using ReadFilterCallbacks = Network::UdpSessionReadFilterCallbacks;

class DynamicForwardProxySetterFilter : public virtual ReadFilter {
public:
  DynamicForwardProxySetterFilter(const std::string host, uint32_t port)
      : host_(host), port_(port) {}

  ReadFilterStatus onNewSession() override {
    read_callbacks_->streamInfo().filterState()->setData(
        "envoy.upstream.dynamic_host", std::make_shared<Router::StringAccessorImpl>(host_),
        StreamInfo::FilterState::StateType::Mutable);
    read_callbacks_->streamInfo().filterState()->setData(
        "envoy.upstream.dynamic_port", std::make_shared<StreamInfo::UInt32AccessorImpl>(port_),
        StreamInfo::FilterState::StateType::Mutable);
    return ReadFilterStatus::Continue;
  }

  ReadFilterStatus onData(Network::UdpRecvData&) override { return ReadFilterStatus::Continue; }

  void initializeReadFilterCallbacks(ReadFilterCallbacks& callbacks) override {
    read_callbacks_ = &callbacks;
  }

private:
  const std::string host_;
  uint32_t port_;
  ReadFilterCallbacks* read_callbacks_;
};

class DynamicForwardProxySetterFilterConfigFactory
    : public FactoryBase<DynamicForwardProxySetterFilterConfig> {
public:
  DynamicForwardProxySetterFilterConfigFactory() : FactoryBase("test.udp_session.dfp_setter") {}

private:
  FilterFactoryCb
  createFilterFactoryFromProtoTyped(const DynamicForwardProxySetterFilterConfig& config,
                                    Server::Configuration::FactoryContext&) override {
    return [config](Network::UdpSessionFilterChainFactoryCallbacks& callbacks) -> void {
      callbacks.addReadFilter(
          std::make_unique<DynamicForwardProxySetterFilter>(config.host(), config.port()));
    };
  }
};

static Registry::RegisterFactory<DynamicForwardProxySetterFilterConfigFactory,
                                 NamedUdpSessionFilterConfigFactory>
    register_dfp_setter_udp_session_read_filter_;

} // namespace DynamicForwardProxy
} // namespace SessionFilters
} // namespace UdpProxy
} // namespace UdpFilters
} // namespace Extensions
} // namespace Envoy
