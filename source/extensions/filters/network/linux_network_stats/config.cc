#include "envoy/extensions/filters/network/linux_network_stats/v3/linux_network_stats.pb.h"
#include "envoy/extensions/filters/network/linux_network_stats/v3/linux_network_stats.pb.validate.h"
#include "envoy/registry/registry.h"
#include "envoy/server/filter_config.h"

#include "source/extensions/filters/network/common/factory_base.h"
#include "source/extensions/filters/network/linux_network_stats/filter.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace LinuxNetworkStats {

namespace {
const std::string FilterName = "envoy.filters.network.linux_network_stats";
Network::FilterFactoryCb createFilterFactory(
    const envoy::extensions::filters::network::linux_network_stats::v3::Config& config_proto,
    Stats::Scope& scope) {
#if defined(__linux__)
  ConfigConstSharedPtr config = std::make_shared<Config>(config_proto, scope);
  return [config](Network::FilterManager& filter_manager) -> void {
    filter_manager.addReadFilter(std::make_shared<Filter>(config));
  };
#else
  UNREFERENCED_PARAMETER(config_proto);
  UNREFERENCED_PARAMETER(factory_context);
  throw EnvoyException("linux_network_stats filter is not supported on this platform.");
#endif
}
} // namespace

namespace Downstream {
/**
 * Config registration for the filter. @see NamedNetworkFilterConfigFactory.
 */
class LinuxNetworkStatsConfigFactory
    : public Common::FactoryBase<
          envoy::extensions::filters::network::linux_network_stats::v3::Config> {
public:
  LinuxNetworkStatsConfigFactory() : FactoryBase(FilterName) {}

private:
  Network::FilterFactoryCb createFilterFactoryFromProtoTyped(
      const envoy::extensions::filters::network::linux_network_stats::v3::Config& config_proto,
      Server::Configuration::FactoryContext& factory_context) override {
    return createFilterFactory(config_proto, factory_context.scope());
  }
};

/**
 * Static registration for the filter. @see RegisterFactory.
 */
REGISTER_FACTORY(LinuxNetworkStatsConfigFactory,
                 Server::Configuration::NamedNetworkFilterConfigFactory);
} // namespace Downstream

namespace Upstream {
class LinuxNetworkStatsConfigFactory
    : public Common::UpstreamFactoryBase<
          envoy::extensions::filters::network::linux_network_stats::v3::Config> {
public:
  LinuxNetworkStatsConfigFactory() : UpstreamFactoryBase(FilterName) {}

private:
  Network::FilterFactoryCb createFilterFactoryFromProtoTyped(
      const envoy::extensions::filters::network::linux_network_stats::v3::Config& config_proto,
      Server::Configuration::CommonFactoryContext& factory_context) override {
    return createFilterFactory(config_proto, factory_context.scope());
  }
};

/**
 * Static registration for the filter. @see RegisterFactory.
 */
REGISTER_FACTORY(LinuxNetworkStatsConfigFactory,
                 Server::Configuration::NamedUpstreamNetworkFilterConfigFactory);
} // namespace Upstream

} // namespace LinuxNetworkStats
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
