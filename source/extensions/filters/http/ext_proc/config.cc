#include "source/extensions/filters/http/ext_proc/config.h"

#include <string>

#include "source/extensions/filters/http/ext_proc/client_impl.h"
#include "source/extensions/filters/http/ext_proc/ext_proc.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ExternalProcessing {

Http::FilterFactoryCb ExternalProcessingFilterConfig::createFilterFactoryFromProtoTyped(
    const envoy::extensions::filters::http::ext_proc::v3alpha::ExternalProcessor& proto_config,
    const std::string& stats_prefix, Server::Configuration::FactoryContext& context) {
  const uint32_t message_timeout_ms =
      PROTOBUF_GET_MS_OR_DEFAULT(proto_config, message_timeout, kDefaultMessageTimeoutMs);
  const auto filter_config = std::make_shared<FilterConfig>(
      proto_config, std::chrono::milliseconds(message_timeout_ms), context.scope(), stats_prefix);

  return [filter_config, grpc_service = proto_config.grpc_service(),
          &context](Http::FilterChainFactoryCallbacks& callbacks) {
    auto client = std::make_unique<ExternalProcessorClientImpl>(
        context.clusterManager().grpcAsyncClientManager(), grpc_service, context.scope());

    callbacks.addStreamFilter(
        Http::StreamFilterSharedPtr{std::make_shared<Filter>(filter_config, std::move(client))});
  };
}

REGISTER_FACTORY(ExternalProcessingFilterConfig,
                 Server::Configuration::NamedHttpFilterConfigFactory){"envoy.ext_proc"};

} // namespace ExternalProcessing
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
