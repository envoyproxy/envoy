#pragma once

#include "envoy/registry/registry.h"

#include "source/common/config/utility.h"
#include "source/common/router/string_accessor_impl.h"
#include "source/extensions/filters/udp/udp_proxy/session_filters/factory_base.h"

#include "test/extensions/filters/udp/udp_proxy/session_filters/psc_setter.pb.h"
#include "test/extensions/filters/udp/udp_proxy/session_filters/psc_setter.pb.validate.h"
#include "test/test_common/utility.h"

namespace Envoy {
namespace Extensions {
namespace UdpFilters {
namespace UdpProxy {
namespace SessionFilters {
namespace PerSessionCluster {

using PerSessionClusterSetterFilterConfig =
    test::extensions::filters::udp::udp_proxy::session_filters::PerSessionClusterSetterFilterConfig;

using ReadFilter = Network::UdpSessionReadFilter;
using ReadFilterStatus = Network::UdpSessionReadFilterStatus;
using ReadFilterCallbacks = Network::UdpSessionReadFilterCallbacks;

class PerSessionClusterSetterFilter : public virtual ReadFilter {
public:
  PerSessionClusterSetterFilter(const std::string cluster) : cluster_(cluster) {}

  ReadFilterStatus onNewSession() override {
    read_callbacks_->streamInfo().filterState()->setData(
        "envoy.udp_proxy.cluster", std::make_shared<UdpProxy::PerSessionCluster>(cluster_),
        StreamInfo::FilterState::StateType::Mutable);
    return ReadFilterStatus::Continue;
  }

  ReadFilterStatus onData(Network::UdpRecvData&) override { return ReadFilterStatus::Continue; }

  void initializeReadFilterCallbacks(ReadFilterCallbacks& callbacks) override {
    read_callbacks_ = &callbacks;
  }

private:
  const std::string cluster_;
  ReadFilterCallbacks* read_callbacks_;
};

class PerSessionClusterSetterFilterConfigFactory
    : public FactoryBase<PerSessionClusterSetterFilterConfig> {
public:
  PerSessionClusterSetterFilterConfigFactory() : FactoryBase("test.udp_session.psc_setter") {}

private:
  FilterFactoryCb
  createFilterFactoryFromProtoTyped(const PerSessionClusterSetterFilterConfig& config,
                                    Server::Configuration::FactoryContext&) override {
    return [config](Network::UdpSessionFilterChainFactoryCallbacks& callbacks) -> void {
      callbacks.addReadFilter(std::make_unique<PerSessionClusterSetterFilter>(config.cluster()));
    };
  }
};

static Registry::RegisterFactory<PerSessionClusterSetterFilterConfigFactory,
                                 NamedUdpSessionFilterConfigFactory>
    register_psc_setter_udp_session_read_filter_;

} // namespace PerSessionCluster
} // namespace SessionFilters
} // namespace UdpProxy
} // namespace UdpFilters
} // namespace Extensions
} // namespace Envoy
