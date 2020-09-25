#include "extensions/filters/http/mutation/config.h"

#include <string>

#include "extensions/filters/http/mutation/mutation.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Mutation {

Http::FilterFactoryCb MutationFilterConfig::createFilterFactoryFromProtoTyped(
    const envoy::extensions::filters::http::mutation::v3::Mutation& proto_config,
    const std::string&, Server::Configuration::FactoryContext& context) {
  const auto filter_config = std::make_shared<FilterConfig>(proto_config);

  return [&context, &proto_config, filter_config](Http::FilterChainFactoryCallbacks& callbacks) {
    const auto client_factory =
        context.clusterManager().grpcAsyncClientManager().factoryForGrpcService(
            proto_config.grpc_service(), context.scope(), /* skip_cluster_check */ true);

    auto grpc_client = client_factory->create();

    callbacks.addStreamFilter(Http::StreamFilterSharedPtr{
        std::make_shared<Filter>(filter_config, std::move(grpc_client))});
  };
}

REGISTER_FACTORY(MutationFilterConfig,
                 Server::Configuration::NamedHttpFilterConfigFactory){"envoy.mutation"};

} // namespace Mutation
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy