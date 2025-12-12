#include "source/extensions/filters/http/ext_proc/config.h"

#include "source/extensions/filters/common/expr/evaluator.h"
#include "source/extensions/filters/http/ext_proc/client_impl.h"
#include "source/extensions/filters/http/ext_proc/ext_proc.h"
#include "source/extensions/filters/http/ext_proc/http_client/http_client_impl.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ExternalProcessing {

namespace {

absl::Status verifyProcessingModeConfig(
    const envoy::extensions::filters::http::ext_proc::v3::ExternalProcessor& config) {
  const auto& processing_mode = config.processing_mode();
  if (config.has_http_service()) {
    // In case http_service configured, the processing mode can only support sending headers.
    if (processing_mode.request_body_mode() !=
            envoy::extensions::filters::http::ext_proc::v3::ProcessingMode::NONE ||
        processing_mode.response_body_mode() !=
            envoy::extensions::filters::http::ext_proc::v3::ProcessingMode::NONE ||
        processing_mode.request_trailer_mode() ==
            envoy::extensions::filters::http::ext_proc::v3::ProcessingMode::SEND ||
        processing_mode.response_trailer_mode() ==
            envoy::extensions::filters::http::ext_proc::v3::ProcessingMode::SEND) {
      return absl::InvalidArgumentError(
          "If the ext_proc filter is configured with http_service instead of gRPC service, "
          "then the processing modes of this filter can not be configured to send body or "
          "trailer.");
    }
  }

  if ((processing_mode.request_body_mode() ==
       envoy::extensions::filters::http::ext_proc::v3::ProcessingMode::FULL_DUPLEX_STREAMED) &&
      (processing_mode.request_trailer_mode() !=
       envoy::extensions::filters::http::ext_proc::v3::ProcessingMode::SEND)) {
    return absl::InvalidArgumentError(
        "If the ext_proc filter has the request_body_mode set to FULL_DUPLEX_STREAMED, "
        "then the request_trailer_mode has to be set to SEND");
  }

  if ((processing_mode.response_body_mode() ==
       envoy::extensions::filters::http::ext_proc::v3::ProcessingMode::FULL_DUPLEX_STREAMED) &&
      (processing_mode.response_trailer_mode() !=
       envoy::extensions::filters::http::ext_proc::v3::ProcessingMode::SEND)) {
    return absl::InvalidArgumentError(
        "If the ext_proc filter has the response_body_mode set to FULL_DUPLEX_STREAMED, "
        "then the response_trailer_mode has to be set to SEND");
  }

  // Do not support fail open for FULL_DUPLEX_STREAMED body mode.
  if (((processing_mode.request_body_mode() ==
        envoy::extensions::filters::http::ext_proc::v3::ProcessingMode::FULL_DUPLEX_STREAMED) ||
       (processing_mode.response_body_mode() ==
        envoy::extensions::filters::http::ext_proc::v3::ProcessingMode::FULL_DUPLEX_STREAMED)) &&
      config.failure_mode_allow()) {
    return absl::InvalidArgumentError(
        "If the ext_proc filter has either the request_body_mode or the response_body_mode set "
        "to FULL_DUPLEX_STREAMED, then the failure_mode_allow has to be left as false");
  }

  return absl::OkStatus();
}

absl::Status verifyFilterConfig(
    const envoy::extensions::filters::http::ext_proc::v3::ExternalProcessor& config) {
  if (config.has_grpc_service() == config.has_http_service()) {
    return absl::InvalidArgumentError(
        "One and only one of grpc_service or http_service must be configured");
  }

  if (config.disable_clear_route_cache() &&
      (config.route_cache_action() !=
       envoy::extensions::filters::http::ext_proc::v3::ExternalProcessor::DEFAULT)) {
    return absl::InvalidArgumentError("disable_clear_route_cache and route_cache_action can not "
                                      "be set to none-default at the same time.");
  }

  return verifyProcessingModeConfig(config);
}

} // namespace

absl::StatusOr<Http::FilterFactoryCb>
ExternalProcessingFilterConfig::createFilterFactoryFromProtoTyped(
    const envoy::extensions::filters::http::ext_proc::v3::ExternalProcessor& proto_config,
    const std::string& stats_prefix, DualInfo dual_info,
    Server::Configuration::ServerFactoryContext& context) {
  // Verify configuration before creating FilterConfig
  absl::Status result = verifyFilterConfig(proto_config);
  if (!result.ok()) {
    return result;
  }

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
      auto client = createExternalProcessorClient(context.clusterManager().grpcAsyncClientManager(),
                                                  dual_info.scope);
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

absl::StatusOr<Router::RouteSpecificFilterConfigConstSharedPtr>
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
  // Verify configuration before creating FilterConfig
  absl::Status result = verifyFilterConfig(proto_config);
  if (!result.ok()) {
    throw EnvoyException(std::string(result.message()));
  }

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
      auto client = createExternalProcessorClient(
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
