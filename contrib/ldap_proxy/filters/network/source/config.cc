#include "contrib/ldap_proxy/filters/network/source/config.h"

#include "fmt/format.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace LdapProxy {

Network::FilterFactoryCb LdapConfigFactory::createFilterFactoryFromProtoTyped(
    const envoy::extensions::filters::network::ldap_proxy::v3alpha::LdapProxy& proto_config,
    Server::Configuration::FactoryContext& context) {
  ASSERT(!proto_config.stat_prefix().empty());

  using StartTlsMode = envoy::extensions::filters::network::ldap_proxy::v3alpha::StartTlsMode;
  const bool force_upstream_starttls = 
      (proto_config.upstream_starttls_mode() == StartTlsMode::ALWAYS);

  const std::string stats_prefix = fmt::format("ldap.{}.", proto_config.stat_prefix());
  Stats::ScopeSharedPtr scope = context.scope().createScope(stats_prefix);

  return [scope, force_upstream_starttls](Network::FilterManager& filter_manager) -> void {
    filter_manager.addFilter(std::make_shared<LdapFilter>(*scope, force_upstream_starttls));
  };
}

REGISTER_FACTORY(LdapConfigFactory, Server::Configuration::NamedNetworkFilterConfigFactory);

} // namespace LdapProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
