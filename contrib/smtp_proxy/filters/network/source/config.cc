#include "contrib/smtp_proxy/filters/network/source/config.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace SmtpProxy {

/**
 * Config registration for the Smtp proxy filter. @see NamedNetworkFilterConfigFactory.
 */
Network::FilterFactoryCb
NetworkFilters::SmtpProxy::SmtpConfigFactory::createFilterFactoryFromProtoTyped(
    const envoy::extensions::filters::network::smtp_proxy::v3alpha::SmtpProxy& proto_config,
    Server::Configuration::FactoryContext& context) {
  ASSERT(!proto_config.stat_prefix().empty());

  SmtpFilterConfig::SmtpFilterConfigOptions config_options;
  config_options.stats_prefix_ = fmt::format("smtp.{}", proto_config.stat_prefix());
  if (proto_config.has_proxy_protocol_config()) {
    config_options.proxy_protocol_config_ = proto_config.proxy_protocol_config();
  }

  config_options.terminate_ssl_ = proto_config.terminate_ssl();
  config_options.upstream_ssl_ = proto_config.upstream_ssl();

  SmtpFilterConfigSharedPtr filter_config(
      std::make_shared<SmtpFilterConfig>(config_options, context.scope()));
  return [filter_config](Network::FilterManager& filter_manager) -> void {
    filter_manager.addFilter(std::make_shared<SmtpFilter>(filter_config));
  };
}

/**
 * Static registration for the Smtp proxy filter. @see RegisterFactory.
 */
REGISTER_FACTORY(SmtpConfigFactory, Server::Configuration::NamedNetworkFilterConfigFactory);

} // namespace SmtpProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
