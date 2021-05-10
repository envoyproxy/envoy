#pragma once

#include "envoy/extensions/filters/network/sip_proxy/router/v3/router.pb.h"
#include "envoy/extensions/filters/network/sip_proxy/router/v3/router.pb.validate.h"


#include "extensions/filters/network/sip_proxy/filters/factory_base.h"
#include "extensions/filters/network/sip_proxy/filters/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace SipProxy {
namespace Router {

class RouterFilterConfig
    : public SipFilters::FactoryBase<envoy::extensions::filters::network::sip_proxy::router::v3::Router> {
public:
  RouterFilterConfig() : FactoryBase(SipFilters::SipFilterNames::get().ROUTER) {}

private:
  SipFilters::FilterFactoryCb createFilterFactoryFromProtoTyped(
      const envoy::extensions::filters::network::sip_proxy::router::v3::Router& proto_config,
      const std::string& stat_prefix, Server::Configuration::FactoryContext& context) override;
};

} // namespace Router
} // namespace SipProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
