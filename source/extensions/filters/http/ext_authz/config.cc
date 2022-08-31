#include "source/extensions/filters/http/ext_authz/config.h"

#include <chrono>
#include <string>

#include "envoy/config/core/v3/grpc_service.pb.h"
#include "envoy/extensions/filters/http/ext_authz/v3/ext_authz.pb.h"
#include "envoy/extensions/filters/http/ext_authz/v3/ext_authz.pb.validate.h"
#include "envoy/registry/registry.h"

#include "source/common/config/utility.h"
#include "source/common/protobuf/utility.h"
#include "source/extensions/filters/common/ext_authz/ext_authz_grpc_impl.h"
#include "source/extensions/filters/common/ext_authz/ext_authz_http_impl.h"
#include "source/extensions/filters/http/ext_authz/ext_authz.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ExtAuthz {

Http::FilterFactoryCb ExtAuthzFilterConfig::createFilterFactoryFromProtoTyped(
    const envoy::extensions::filters::http::ext_authz::v3::ExtAuthz& proto_config,
    const std::string& stats_prefix, Server::Configuration::FactoryContext& context) {
  const auto filter_config = std::make_shared<FilterConfig>(
      proto_config, context.scope(), context.runtime(), context.httpContext(), stats_prefix,
      context.getServerFactoryContext().bootstrap());
  // The callback is created in main thread and executed in worker thread, variables except factory
  // context must be captured by value into the callback.
  Http::FilterFactoryCb callback;

  if (proto_config.has_http_service()) {
    // Raw HTTP client.
    const uint32_t timeout_ms = PROTOBUF_GET_MS_OR_DEFAULT(proto_config.http_service().server_uri(),
                                                           timeout, DefaultTimeout);
    const auto client_config =
        std::make_shared<Extensions::Filters::Common::ExtAuthz::ClientConfig>(
            proto_config, timeout_ms, proto_config.http_service().path_prefix());
    callback = [filter_config, client_config,
                &context](Http::FilterChainFactoryCallbacks& callbacks) {
      auto client = std::make_unique<Extensions::Filters::Common::ExtAuthz::RawHttpClientImpl>(
          context.clusterManager(), client_config);
      callbacks.addStreamFilter(std::make_shared<Filter>(filter_config, std::move(client)));
    };
  } else if (proto_config.grpc_service().has_google_grpc()) {
    // Google gRPC client.
    const uint32_t timeout_ms =
        PROTOBUF_GET_MS_OR_DEFAULT(proto_config.grpc_service(), timeout, DefaultTimeout);

    Config::Utility::checkTransportVersion(proto_config);
    callback = [&context, filter_config, timeout_ms,
                proto_config](Http::FilterChainFactoryCallbacks& callbacks) {
      auto client = std::make_unique<Filters::Common::ExtAuthz::GrpcClientImpl>(
          context.clusterManager().grpcAsyncClientManager().getOrCreateRawAsyncClient(
              proto_config.grpc_service(), context.scope(), true),
          std::chrono::milliseconds(timeout_ms));
      callbacks.addStreamFilter(std::make_shared<Filter>(filter_config, std::move(client)));
    };
  } else {
    // Envoy gRPC client.
    const uint32_t timeout_ms =
        PROTOBUF_GET_MS_OR_DEFAULT(proto_config.grpc_service(), timeout, DefaultTimeout);
    Config::Utility::checkTransportVersion(proto_config);
    callback = [grpc_service = proto_config.grpc_service(), &context, filter_config,
                timeout_ms](Http::FilterChainFactoryCallbacks& callbacks) {
      Grpc::RawAsyncClientSharedPtr raw_client =
          context.clusterManager().grpcAsyncClientManager().getOrCreateRawAsyncClient(
              grpc_service, context.scope(), true);
      auto client = std::make_unique<Filters::Common::ExtAuthz::GrpcClientImpl>(
          raw_client, std::chrono::milliseconds(timeout_ms));
      callbacks.addStreamFilter(std::make_shared<Filter>(filter_config, std::move(client)));
    };
  }

  return callback;
}

Router::RouteSpecificFilterConfigConstSharedPtr
ExtAuthzFilterConfig::createRouteSpecificFilterConfigTyped(
    const envoy::extensions::filters::http::ext_authz::v3::ExtAuthzPerRoute& proto_config,
    Server::Configuration::ServerFactoryContext&, ProtobufMessage::ValidationVisitor&) {
  return std::make_shared<FilterConfigPerRoute>(proto_config);
}

/**
 * Static registration for the external authorization filter. @see RegisterFactory.
 */
REGISTER_FACTORY(ExtAuthzFilterConfig,
                 Server::Configuration::NamedHttpFilterConfigFactory){"envoy.ext_authz"};

} // namespace ExtAuthz
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
