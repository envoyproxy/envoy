#pragma once

#include "source/extensions/filters/network/common/factory_base.h"
#include "source/extensions/filters/network/well_known_names.h"

#include "contrib/envoy/extensions/filters/network/ldap_proxy/v3alpha/ldap_proxy.pb.h"
#include "contrib/envoy/extensions/filters/network/ldap_proxy/v3alpha/ldap_proxy.pb.validate.h"
#include "contrib/ldap_proxy/filters/network/source/ldap_filter.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace LdapProxy {

class LdapConfigFactory
    : public Common::FactoryBase<
          envoy::extensions::filters::network::ldap_proxy::v3alpha::LdapProxy> {
public:
  LdapConfigFactory() : FactoryBase{NetworkFilterNames::get().LdapProxy} {}

private:
  Network::FilterFactoryCb createFilterFactoryFromProtoTyped(
      const envoy::extensions::filters::network::ldap_proxy::v3alpha::LdapProxy& proto_config,
      Server::Configuration::FactoryContext& context) override;
};

} // namespace LdapProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
