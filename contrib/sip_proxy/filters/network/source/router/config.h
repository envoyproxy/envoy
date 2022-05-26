#pragma once

#include "contrib/envoy/extensions/filters/network/sip_proxy/router/v3alpha/router.pb.h"
#include "contrib/envoy/extensions/filters/network/sip_proxy/router/v3alpha/router.pb.validate.h"
#include "contrib/sip_proxy/filters/network/source/filters/factory_base.h"
#include "contrib/sip_proxy/filters/network/source/filters/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace SipProxy {
namespace Router {

class RouterFilterConfig
    : public SipFilters::FactoryBase<
          envoy::extensions::filters::network::sip_proxy::router::v3alpha::Router> {
public:
  RouterFilterConfig() : FactoryBase(SipFilters::SipFilterNames::get().ROUTER) {}

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
