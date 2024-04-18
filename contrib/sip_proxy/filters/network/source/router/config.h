#pragma once

#include "contrib/envoy/extensions/filters/network/sip_proxy/router/v3alpha/router.pb.h"
#include "contrib/envoy/extensions/filters/network/sip_proxy/router/v3alpha/router.pb.validate.h"
#include "contrib/sip_proxy/filters/network/source/filters/factory_base.h"
#include "contrib/sip_proxy/filters/network/source/filters/well_known_names.h"
#include "contrib/sip_proxy/filters/network/source/router/router.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace SipProxy {
namespace Router {

class RouterFilterConfigImpl : public RouterFilterConfig {
public:
  RouterFilterConfigImpl(
      const envoy::extensions::filters::network::sip_proxy::router::v3alpha::Router& config,
      const std::string& stat_prefix, Server::Configuration::FactoryContext& context)
      : stats_(generateStats(stat_prefix, context.scope())) {
    UNREFERENCED_PARAMETER(config);
  }

  RouterStats& stats() override { return stats_; }

  static RouterStats generateStats(const std::string& prefix, Stats::Scope& scope) {
    return RouterStats{ALL_SIP_ROUTER_STATS(POOL_COUNTER_PREFIX(scope, prefix),
                                            POOL_GAUGE_PREFIX(scope, prefix),
                                            POOL_HISTOGRAM_PREFIX(scope, prefix))};
  }

private:
  RouterStats stats_;
};

class RouterFilterConfigFactory
    : public SipFilters::FactoryBase<
          envoy::extensions::filters::network::sip_proxy::router::v3alpha::Router> {
public:
  RouterFilterConfigFactory() : FactoryBase(SipFilters::SipFilterNames::get().ROUTER) {}

private:
  SipFilters::FilterFactoryCb createFilterFactoryFromProtoTyped(
      const envoy::extensions::filters::network::sip_proxy::router::v3alpha::Router& proto_config,
      const std::string& stat_prefix, Server::Configuration::FactoryContext& context) override;
};

} // namespace Router
} // namespace SipProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
