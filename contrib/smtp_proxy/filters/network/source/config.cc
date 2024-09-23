#include "contrib/smtp_proxy/filters/network/source/config.h"

#include "envoy/config/accesslog/v3/accesslog.pb.h"

#include "source/common/access_log/access_log_impl.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace SmtpProxy {

/**
 * Config registration for the SMTP Proxy filter. @see NamedNetworkFilterConfigFactory.
 */
Network::FilterFactoryCb
NetworkFilters::SmtpProxy::SmtpConfigFactory::createFilterFactoryFromProtoTyped(
    const envoy::extensions::filters::network::smtp_proxy::v3alpha::SmtpProxy& proto_config,
    Server::Configuration::FactoryContext& context) {
  ASSERT(!proto_config.stat_prefix().empty());

  SmtpFilterConfig::SmtpFilterConfigOptions config_options;
  config_options.stats_prefix_ = fmt::format("smtp.{}", proto_config.stat_prefix());
  config_options.downstream_tls_ = proto_config.downstream_tls();
  config_options.upstream_tls_ = proto_config.upstream_tls();
  config_options.protocol_inspection_ = proto_config.protocol_inspection();
  config_options.tracing_ = proto_config.tracing();
  for (const envoy::config::accesslog::v3::AccessLog& log_config : proto_config.access_log()) {
    config_options.access_logs_.emplace_back(
        AccessLog::AccessLogFactory::fromProto(log_config, context));
  }

  SmtpFilterConfigSharedPtr filter_config(
      std::make_shared<SmtpFilterConfig>(config_options, context.scope()));

  auto& server_context = context.serverFactoryContext();

  auto& time_source = server_context.mainThreadDispatcher().timeSource();
  return [filter_config, &time_source,
          &server_context](Network::FilterManager& filter_manager) -> void {
    filter_manager.addFilter(std::make_shared<SmtpFilter>(filter_config, time_source,
                                                          server_context.api().randomGenerator()));
  };
}

/**
 * Static registration for the SMTP Proxy filter. @see RegisterFactory.
 */
REGISTER_FACTORY(SmtpConfigFactory, Server::Configuration::NamedNetworkFilterConfigFactory);

} // namespace SmtpProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
