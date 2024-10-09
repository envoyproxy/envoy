#include "source/extensions/filters/http/ext_proc/config.h"

#include "source/extensions/filters/common/expr/evaluator.h"
#include "source/extensions/filters/http/ext_proc/client_impl.h"
#include "source/extensions/filters/http/ext_proc/ext_proc.h"
#include "source/extensions/filters/http/ext_proc/http_client/http_client_impl.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ExternalProcessing {

absl::StatusOr<Http::FilterFactoryCb>
ExternalProcessingFilterConfig::createFilterFactoryFromProtoTyped(
    const envoy::extensions::filters::http::ext_proc::v3::ExternalProcessor& proto_config,
    const std::string& stats_prefix, DualInfo dual_info,
    Server::Configuration::ServerFactoryContext& context) {
  const uint32_t message_timeout_ms =
      PROTOBUF_GET_MS_OR_DEFAULT(proto_config, message_timeout, DefaultMessageTimeoutMs);
  const uint32_t max_message_timeout_ms =
      PROTOBUF_GET_MS_OR_DEFAULT(proto_config, max_message_timeout, DefaultMaxMessageTimeoutMs);
  const auto filter_config = std::make_shared<FilterConfig>(
      proto_config, std::chrono::milliseconds(message_timeout_ms), max_message_timeout_ms,
      dual_info.scope, stats_prefix, dual_info.is_upstream,
      Envoy::Extensions::Filters::Common::Expr::getBuilder(context), context);
  if (proto_config.has_grpc_service()) {
    return [filter_config = std::move(filter_config), &context,
            dual_info](Http::FilterChainFactoryCallbacks& callbacks) {
      auto client = std::make_unique<ExternalProcessorClientImpl>(
          context.clusterManager().grpcAsyncClientManager(), dual_info.scope);
      callbacks.addStreamFilter(
          Http::StreamFilterSharedPtr{std::make_shared<Filter>(filter_config, std::move(client))});
    };
  } else {
    return [proto_config = std::move(proto_config), filter_config = std::move(filter_config),
            &context](Http::FilterChainFactoryCallbacks& callbacks) {
      auto client = std::make_unique<ExtProcHttpClient>(proto_config, context);
      callbacks.addStreamFilter(
          Http::StreamFilterSharedPtr{std::make_shared<Filter>(filter_config, std::move(client))});
    };
  }
}

Router::RouteSpecificFilterConfigConstSharedPtr
ExternalProcessingFilterConfig::createRouteSpecificFilterConfigTyped(
    const envoy::extensions::filters::http::ext_proc::v3::ExtProcPerRoute& proto_config,
    Server::Configuration::ServerFactoryContext&, ProtobufMessage::ValidationVisitor&) {
  return std::make_shared<FilterConfigPerRoute>(proto_config);
}

// This method will only be called when the filter is in downstream.
Http::FilterFactoryCb
ExternalProcessingFilterConfig::createFilterFactoryFromProtoWithServerContextTyped(
    const envoy::extensions::filters::http::ext_proc::v3::ExternalProcessor& proto_config,
    const std::string& stats_prefix, Server::Configuration::ServerFactoryContext& server_context) {
  const uint32_t message_timeout_ms =
      PROTOBUF_GET_MS_OR_DEFAULT(proto_config, message_timeout, DefaultMessageTimeoutMs);
  const uint32_t max_message_timeout_ms =
      PROTOBUF_GET_MS_OR_DEFAULT(proto_config, max_message_timeout, DefaultMaxMessageTimeoutMs);
  const auto filter_config = std::make_shared<FilterConfig>(
      proto_config, std::chrono::milliseconds(message_timeout_ms), max_message_timeout_ms,
      server_context.scope(), stats_prefix, false,
      Envoy::Extensions::Filters::Common::Expr::getBuilder(server_context), server_context);

  if (proto_config.has_grpc_service()) {
    return [filter_config = std::move(filter_config),
            &server_context](Http::FilterChainFactoryCallbacks& callbacks) {
      auto client = std::make_unique<ExternalProcessorClientImpl>(
          server_context.clusterManager().grpcAsyncClientManager(), server_context.scope());
      callbacks.addStreamFilter(
          Http::StreamFilterSharedPtr{std::make_shared<Filter>(filter_config, std::move(client))});
    };
  } else {
    return [proto_config = std::move(proto_config), filter_config = std::move(filter_config),
            &server_context](Http::FilterChainFactoryCallbacks& callbacks) {
      auto client = std::make_unique<ExtProcHttpClient>(proto_config, server_context);
      callbacks.addStreamFilter(
          Http::StreamFilterSharedPtr{std::make_shared<Filter>(filter_config, std::move(client))});
    };
  }
}

LEGACY_REGISTER_FACTORY(ExternalProcessingFilterConfig,
                        Server::Configuration::NamedHttpFilterConfigFactory, "envoy.ext_proc");
LEGACY_REGISTER_FACTORY(UpstreamExternalProcessingFilterConfig,
                        Server::Configuration::UpstreamHttpFilterConfigFactory, "envoy.ext_proc");

} // namespace ExternalProcessing
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
