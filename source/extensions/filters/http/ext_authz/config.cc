#include "extensions/filters/http/ext_authz/config.h"

#include <chrono>
#include <string>

#include "envoy/config/common/matcher/v3/matcher.pb.h"
#include "envoy/config/core/v3/grpc_service.pb.h"
#include "envoy/extensions/filters/http/ext_authz/v3/ext_authz.pb.h"
#include "envoy/extensions/filters/http/ext_authz/v3/ext_authz.pb.validate.h"
#include "envoy/registry/registry.h"

#include "common/common/macros.h"
#include "common/protobuf/utility.h"

#include "extensions/common/matcher/matcher.h"
#include "extensions/filters/common/ext_authz/ext_authz_grpc_impl.h"
#include "extensions/filters/common/ext_authz/ext_authz_http_impl.h"
#include "extensions/filters/http/ext_authz/ext_authz.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ExtAuthz {

bool validate(const envoy::config::common::matcher::v3::MatchPredicate& match) {
  switch (match.rule_case()) {
  case envoy::config::common::matcher::v3::MatchPredicate::RuleCase::kOrMatch:
    return std::all_of(match.or_match().rules().begin(), match.or_match().rules().end(), validate);
  case envoy::config::common::matcher::v3::MatchPredicate::RuleCase::kAndMatch:
    return std::all_of(match.and_match().rules().begin(), match.and_match().rules().end(),
                       validate);
  case envoy::config::common::matcher::v3::MatchPredicate::RuleCase::kNotMatch:
    return validate(match.not_match());
  case envoy::config::common::matcher::v3::MatchPredicate::RuleCase::kHttpResponseHeadersMatch:
    FALLTHRU;
  case envoy::config::common::matcher::v3::MatchPredicate::RuleCase::kHttpResponseTrailersMatch:
    FALLTHRU;
  case envoy::config::common::matcher::v3::MatchPredicate::RuleCase::kHttpResponseGenericBodyMatch:
    return false;
  default:
    return true;
  }
}

Http::FilterFactoryCb ExtAuthzFilterConfig::createFilterFactoryFromProtoTyped(
    const envoy::extensions::filters::http::ext_authz::v3::ExtAuthz& proto_config,
    const std::string& stats_prefix, Server::Configuration::FactoryContext& context) {
  if (proto_config.has_match() && !validate(proto_config.match())) {
    throw EnvoyException(fmt::format("found unsupported response rule in match:\n{}",
                                     proto_config.match().DebugString()));
    ;
  }

  const auto filter_config =
      std::make_shared<FilterConfig>(proto_config, context.localInfo(), context.scope(),
                                     context.runtime(), context.httpContext(), stats_prefix);
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
      callbacks.addStreamDecoderFilter(Http::StreamDecoderFilterSharedPtr{
          std::make_shared<Filter>(filter_config, std::move(client))});
    };
  } else {
    // gRPC client.
    const uint32_t timeout_ms =
        PROTOBUF_GET_MS_OR_DEFAULT(proto_config.grpc_service(), timeout, DefaultTimeout);
    callback = [grpc_service = proto_config.grpc_service(), &context, filter_config, timeout_ms,
                transport_api_version = proto_config.transport_api_version(),
                use_alpha = proto_config.hidden_envoy_deprecated_use_alpha()](
                   Http::FilterChainFactoryCallbacks& callbacks) {
      const auto async_client_factory =
          context.clusterManager().grpcAsyncClientManager().factoryForGrpcService(
              grpc_service, context.scope(), true);
      auto client = std::make_unique<Filters::Common::ExtAuthz::GrpcClientImpl>(
          async_client_factory->create(), std::chrono::milliseconds(timeout_ms),
          transport_api_version, use_alpha);
      callbacks.addStreamDecoderFilter(Http::StreamDecoderFilterSharedPtr{
          std::make_shared<Filter>(filter_config, std::move(client))});
    };
  }

  return callback;
};

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
