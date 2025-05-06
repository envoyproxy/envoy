#include "source/extensions/filters/network/ext_proc/config.h"

#include <chrono>
#include <string>

#include "envoy/extensions/filters/network/ext_proc/v3/ext_proc.pb.h"
#include "envoy/extensions/filters/network/ext_proc/v3/ext_proc.pb.validate.h"
#include "envoy/network/connection.h"
#include "envoy/registry/registry.h"

#include "source/extensions/filters/network/ext_proc/ext_proc.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ExtProc {

namespace {

absl::Status verifyFilterConfig(
    const envoy::extensions::filters::network::ext_proc::v3::NetworkExternalProcessor& config) {
  if (!config.has_grpc_service()) {
    return absl::InvalidArgumentError("A grpc_service must be configured");
  }

  if (config.processing_mode().process_read() ==
          envoy::extensions::filters::network::ext_proc::v3::ProcessingMode::SKIP &&
      config.processing_mode().process_write() ==
          envoy::extensions::filters::network::ext_proc::v3::ProcessingMode::SKIP) {
    return absl::InvalidArgumentError(
        "both read and write paths are skipped, at least one must be enabled.");
  }
  return absl::OkStatus();
}

} // namespace

Network::FilterFactoryCb NetworkExtProcConfigFactory::createFilterFactoryFromProtoTyped(
    const envoy::extensions::filters::network::ext_proc::v3::NetworkExternalProcessor& proto_config,
    Server::Configuration::FactoryContext& context) {
  absl::Status result = verifyFilterConfig(proto_config);
  if (!result.ok()) {
    throw EnvoyException(std::string(result.message()));
  }

  ConfigConstSharedPtr ext_proc_config = std::make_shared<const Config>(proto_config);

  return [ext_proc_config, &context](Network::FilterManager& filter_manager) -> void {
    auto client = createExternalProcessorClient(
        context.serverFactoryContext().clusterManager().grpcAsyncClientManager(), context.scope());
    filter_manager.addFilter(
        std::make_shared<NetworkExtProcFilter>(ext_proc_config, std::move(client)));
  };
}

/**
 * Static registration for the external authorization filter. @see RegisterFactory.
 */
REGISTER_FACTORY(NetworkExtProcConfigFactory,
                 Server::Configuration::NamedNetworkFilterConfigFactory);

} // namespace ExtProc
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
