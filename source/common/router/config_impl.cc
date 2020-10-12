#include "common/router/config_impl.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "envoy/config/core/v3/base.pb.h"
#include "envoy/config/route/v3/route.pb.h"
#include "envoy/config/route/v3/route_components.pb.h"
#include "envoy/http/header_map.h"
#include "envoy/runtime/runtime.h"
#include "envoy/type/matcher/v3/string.pb.h"
#include "envoy/type/v3/percent.pb.h"
#include "envoy/upstream/cluster_manager.h"
#include "envoy/upstream/upstream.h"

#include "common/common/assert.h"
#include "common/common/empty_string.h"
#include "common/common/fmt.h"
#include "common/common/hash.h"
#include "common/common/logger.h"
#include "common/common/regex.h"
#include "common/common/utility.h"
#include "common/config/metadata.h"
#include "common/config/utility.h"
#include "common/config/well_known_names.h"
#include "common/http/headers.h"
#include "common/http/path_utility.h"
#include "common/http/utility.h"
#include "common/protobuf/protobuf.h"
#include "common/protobuf/utility.h"
#include "common/router/reset_header_parser.h"
#include "common/router/retry_state_impl.h"
#include "common/runtime/runtime_features.h"
#include "common/tracing/http_tracer_impl.h"

#include "extensions/filters/http/common/utility.h"
#include "extensions/filters/http/well_known_names.h"

#include "absl/strings/match.h"

namespace Envoy {
namespace Router {
namespace {

const std::string DEPRECATED_ROUTER_NAME = "envoy.router";

} // namespace

std::string SslRedirector::newPath(const Http::RequestHeaderMap& headers) const {
  return Http::Utility::createSslRedirectPath(headers);
}

HedgePolicyImpl::HedgePolicyImpl(const envoy::config::route::v3::HedgePolicy& hedge_policy)
    : initial_requests_(PROTOBUF_GET_WRAPPED_OR_DEFAULT(hedge_policy, initial_requests, 1)),
      additional_request_chance_(hedge_policy.additional_request_chance()),
      hedge_on_per_try_timeout_(hedge_policy.hedge_on_per_try_timeout()) {}

HedgePolicyImpl::HedgePolicyImpl() : initial_requests_(1), hedge_on_per_try_timeout_(false) {}

RetryPolicyImpl::RetryPolicyImpl(const envoy::config::route::v3::RetryPolicy& retry_policy,
                                 ProtobufMessage::ValidationVisitor& validation_visitor)
    : retriable_headers_(
          Http::HeaderUtility::buildHeaderMatcherVector(retry_policy.retriable_headers())),
      retriable_request_headers_(
          Http::HeaderUtility::buildHeaderMatcherVector(retry_policy.retriable_request_headers())),
      validation_visitor_(&validation_visitor) {
  per_try_timeout_ =
      std::chrono::milliseconds(PROTOBUF_GET_MS_OR_DEFAULT(retry_policy, per_try_timeout, 0));
  num_retries_ = PROTOBUF_GET_WRAPPED_OR_DEFAULT(retry_policy, num_retries, 1);
  retry_on_ = RetryStateImpl::parseRetryOn(retry_policy.retry_on()).first;
  retry_on_ |= RetryStateImpl::parseRetryGrpcOn(retry_policy.retry_on()).first;

  for (const auto& host_predicate : retry_policy.retry_host_predicate()) {
    auto& factory = Envoy::Config::Utility::getAndCheckFactory<Upstream::RetryHostPredicateFactory>(
        host_predicate);
    auto config = Envoy::Config::Utility::translateToFactoryConfig(host_predicate,
                                                                   validation_visitor, factory);
    retry_host_predicate_configs_.emplace_back(factory, std::move(config));
  }

  const auto& retry_priority = retry_policy.retry_priority();
  if (!retry_priority.name().empty()) {
    auto& factory =
        Envoy::Config::Utility::getAndCheckFactory<Upstream::RetryPriorityFactory>(retry_priority);
    retry_priority_config_ =
        std::make_pair(&factory, Envoy::Config::Utility::translateToFactoryConfig(
                                     retry_priority, validation_visitor, factory));
  }

  auto host_selection_attempts = retry_policy.host_selection_retry_max_attempts();
  if (host_selection_attempts) {
    host_selection_attempts_ = host_selection_attempts;
  }

  for (auto code : retry_policy.retriable_status_codes()) {
    retriable_status_codes_.emplace_back(code);
  }

  if (retry_policy.has_retry_back_off()) {
    base_interval_ = std::chrono::milliseconds(
        PROTOBUF_GET_MS_REQUIRED(retry_policy.retry_back_off(), base_interval));
    if ((*base_interval_).count() < 1) {
      base_interval_ = std::chrono::milliseconds(1);
    }

    max_interval_ = PROTOBUF_GET_OPTIONAL_MS(retry_policy.retry_back_off(), max_interval);
    if (max_interval_) {
      // Apply the same rounding to max interval in case both are set to sub-millisecond values.
      if ((*max_interval_).count() < 1) {
        max_interval_ = std::chrono::milliseconds(1);
      }

      if ((*max_interval_).count() < (*base_interval_).count()) {
        throw EnvoyException(
            "retry_policy.max_interval must greater than or equal to the base_interval");
      }
    }
  }

  if (retry_policy.has_rate_limited_retry_back_off()) {
    reset_headers_ = ResetHeaderParserImpl::buildResetHeaderParserVector(
        retry_policy.rate_limited_retry_back_off().reset_headers());

    absl::optional<std::chrono::milliseconds> reset_max_interval =
        PROTOBUF_GET_OPTIONAL_MS(retry_policy.rate_limited_retry_back_off(), max_interval);
    if (reset_max_interval.has_value()) {
      std::chrono::milliseconds max_interval = reset_max_interval.value();
      if (max_interval.count() < 1) {
        max_interval = std::chrono::milliseconds(1);
      }
      reset_max_interval_ = max_interval;
    }
  }
}

std::vector<Upstream::RetryHostPredicateSharedPtr> RetryPolicyImpl::retryHostPredicates() const {
  std::vector<Upstream::RetryHostPredicateSharedPtr> predicates;

  for (const auto& config : retry_host_predicate_configs_) {
    predicates.emplace_back(config.first.createHostPredicate(*config.second, num_retries_));
  }

  return predicates;
}

Upstream::RetryPrioritySharedPtr RetryPolicyImpl::retryPriority() const {
  if (retry_priority_config_.first == nullptr) {
    return nullptr;
  }

  return retry_priority_config_.first->createRetryPriority(*retry_priority_config_.second,
                                                           *validation_visitor_, num_retries_);
}

InternalRedirectPolicyImpl::InternalRedirectPolicyImpl(
    const envoy::config::route::v3::InternalRedirectPolicy& policy_config,
    ProtobufMessage::ValidationVisitor& validator, absl::string_view current_route_name)
    : current_route_name_(current_route_name),
      redirect_response_codes_(buildRedirectResponseCodes(policy_config)),
      max_internal_redirects_(
          PROTOBUF_GET_WRAPPED_OR_DEFAULT(policy_config, max_internal_redirects, 1)),
      enabled_(true), allow_cross_scheme_redirect_(policy_config.allow_cross_scheme_redirect()) {
  for (const auto& predicate : policy_config.predicates()) {
    auto& factory =
        Envoy::Config::Utility::getAndCheckFactory<InternalRedirectPredicateFactory>(predicate);
    auto config = factory.createEmptyConfigProto();
    Envoy::Config::Utility::translateOpaqueConfig(predicate.typed_config(), {}, validator, *config);
    predicate_factories_.emplace_back(&factory, std::move(config));
  }
}

std::vector<InternalRedirectPredicateSharedPtr> InternalRedirectPolicyImpl::predicates() const {
  std::vector<InternalRedirectPredicateSharedPtr> predicates;
  for (const auto& predicate_factory : predicate_factories_) {
    predicates.emplace_back(predicate_factory.first->createInternalRedirectPredicate(
        *predicate_factory.second, current_route_name_));
  }
  return predicates;
}

absl::flat_hash_set<Http::Code> InternalRedirectPolicyImpl::buildRedirectResponseCodes(
    const envoy::config::route::v3::InternalRedirectPolicy& policy_config) const {
  if (policy_config.redirect_response_codes_size() == 0) {
    return absl::flat_hash_set<Http::Code>{Http::Code::Found};
  }
  absl::flat_hash_set<Http::Code> ret;
  std::for_each(policy_config.redirect_response_codes().begin(),
                policy_config.redirect_response_codes().end(), [&ret](uint32_t response_code) {
                  const absl::flat_hash_set<uint32_t> valid_redirect_response_code = {301, 302, 303,
                                                                                      307, 308};
                  if (valid_redirect_response_code.contains(response_code)) {
                    ret.insert(static_cast<Http::Code>(response_code));
                  }
                });
  return ret;
}

CorsPolicyImpl::CorsPolicyImpl(const envoy::config::route::v3::CorsPolicy& config,
                               Runtime::Loader& loader)
    : config_(config), loader_(loader), allow_methods_(config.allow_methods()),
      allow_headers_(config.allow_headers()), expose_headers_(config.expose_headers()),
      max_age_(config.max_age()),
      legacy_enabled_(config.has_hidden_envoy_deprecated_enabled()
                          ? config.hidden_envoy_deprecated_enabled().value()
                          : true) {
  for (const auto& origin : config.hidden_envoy_deprecated_allow_origin()) {
    envoy::type::matcher::v3::StringMatcher matcher_config;
    matcher_config.set_exact(origin);
    allow_origins_.push_back(std::make_unique<Matchers::StringMatcherImpl>(matcher_config));
  }
  for (const auto& regex : config.hidden_envoy_deprecated_allow_origin_regex()) {
    envoy::type::matcher::v3::StringMatcher matcher_config;
    matcher_config.set_hidden_envoy_deprecated_regex(regex);
    allow_origins_.push_back(std::make_unique<Matchers::StringMatcherImpl>(matcher_config));
  }
  for (const auto& string_match : config.allow_origin_string_match()) {
    allow_origins_.push_back(std::make_unique<Matchers::StringMatcherImpl>(string_match));
  }
  if (config.has_allow_credentials()) {
    allow_credentials_ = PROTOBUF_GET_WRAPPED_REQUIRED(config, allow_credentials);
  }
}

ShadowPolicyImpl::ShadowPolicyImpl(const RequestMirrorPolicy& config) {

  cluster_ = config.cluster();

  if (config.has_runtime_fraction()) {
    runtime_key_ = config.runtime_fraction().runtime_key();
    default_value_ = config.runtime_fraction().default_value();
  } else {
    runtime_key_ = config.hidden_envoy_deprecated_runtime_key();
    default_value_.set_numerator(0);
  }
  trace_sampled_ = PROTOBUF_GET_WRAPPED_OR_DEFAULT(config, trace_sampled, true);
}

DecoratorImpl::DecoratorImpl(const envoy::config::route::v3::Decorator& decorator)
    : operation_(decorator.operation()),
      propagate_(PROTOBUF_GET_WRAPPED_OR_DEFAULT(decorator, propagate, true)) {}

void DecoratorImpl::apply(Tracing::Span& span) const {
  if (!operation_.empty()) {
    span.setOperation(operation_);
  }
}

const std::string& DecoratorImpl::getOperation() const { return operation_; }

bool DecoratorImpl::propagate() const { return propagate_; }

RouteTracingImpl::RouteTracingImpl(const envoy::config::route::v3::Tracing& tracing) {
  if (!tracing.has_client_sampling()) {
    client_sampling_.set_numerator(100);
    client_sampling_.set_denominator(envoy::type::v3::FractionalPercent::HUNDRED);
  } else {
    client_sampling_ = tracing.client_sampling();
  }
  if (!tracing.has_random_sampling()) {
    random_sampling_.set_numerator(100);
    random_sampling_.set_denominator(envoy::type::v3::FractionalPercent::HUNDRED);
  } else {
    random_sampling_ = tracing.random_sampling();
  }
  if (!tracing.has_overall_sampling()) {
    overall_sampling_.set_numerator(100);
    overall_sampling_.set_denominator(envoy::type::v3::FractionalPercent::HUNDRED);
  } else {
    overall_sampling_ = tracing.overall_sampling();
  }
  for (const auto& tag : tracing.custom_tags()) {
    custom_tags_.emplace(tag.tag(), Tracing::HttpTracerUtility::createCustomTag(tag));
  }
}

const envoy::type::v3::FractionalPercent& RouteTracingImpl::getClientSampling() const {
  return client_sampling_;
}

const envoy::type::v3::FractionalPercent& RouteTracingImpl::getRandomSampling() const {
  return random_sampling_;
}

const envoy::type::v3::FractionalPercent& RouteTracingImpl::getOverallSampling() const {
  return overall_sampling_;
}
const Tracing::CustomTagMap& RouteTracingImpl::getCustomTags() const { return custom_tags_; }

RouteEntryImplBase::RouteEntryImplBase(const VirtualHostImpl& vhost,
                                       const envoy::config::route::v3::Route& route,
                                       Server::Configuration::ServerFactoryContext& factory_context,
                                       ProtobufMessage::ValidationVisitor& validator)
    : case_sensitive_(PROTOBUF_GET_WRAPPED_OR_DEFAULT(route.match(), case_sensitive, true)),
      prefix_rewrite_(route.route().prefix_rewrite()),
      host_rewrite_(route.route().host_rewrite_literal()), vhost_(vhost),
      auto_host_rewrite_(PROTOBUF_GET_WRAPPED_OR_DEFAULT(route.route(), auto_host_rewrite, false)),
      auto_host_rewrite_header_(!route.route().host_rewrite_header().empty()
                                    ? absl::optional<Http::LowerCaseString>(Http::LowerCaseString(
                                          route.route().host_rewrite_header()))
                                    : absl::nullopt),
      host_rewrite_path_regex_(
          route.route().has_host_rewrite_path_regex()
              ? Regex::Utility::parseRegex(route.route().host_rewrite_path_regex().pattern())
              : nullptr),
      host_rewrite_path_regex_substitution_(
          route.route().has_host_rewrite_path_regex()
              ? route.route().host_rewrite_path_regex().substitution()
              : ""),
      cluster_name_(route.route().cluster()), cluster_header_name_(route.route().cluster_header()),
      cluster_not_found_response_code_(ConfigUtility::parseClusterNotFoundResponseCode(
          route.route().cluster_not_found_response_code())),
      timeout_(PROTOBUF_GET_MS_OR_DEFAULT(route.route(), timeout, DEFAULT_ROUTE_TIMEOUT_MS)),
      idle_timeout_(PROTOBUF_GET_OPTIONAL_MS(route.route(), idle_timeout)),
      max_stream_duration_(
          PROTOBUF_GET_OPTIONAL_MS(route.route().max_stream_duration(), max_stream_duration)),
      grpc_timeout_header_max_(
          PROTOBUF_GET_OPTIONAL_MS(route.route().max_stream_duration(), grpc_timeout_header_max)),
      grpc_timeout_header_offset_(PROTOBUF_GET_OPTIONAL_MS(route.route().max_stream_duration(),
                                                           grpc_timeout_header_offset)),
      max_grpc_timeout_(PROTOBUF_GET_OPTIONAL_MS(route.route(), max_grpc_timeout)),
      grpc_timeout_offset_(PROTOBUF_GET_OPTIONAL_MS(route.route(), grpc_timeout_offset)),
      loader_(factory_context.runtime()), runtime_(loadRuntimeData(route.match())),
      scheme_redirect_(route.redirect().scheme_redirect()),
      host_redirect_(route.redirect().host_redirect()),
      port_redirect_(route.redirect().port_redirect()
                         ? ":" + std::to_string(route.redirect().port_redirect())
                         : ""),
      path_redirect_(route.redirect().path_redirect()),
      path_redirect_has_query_(path_redirect_.find('?') != absl::string_view::npos),
      enable_preserve_query_in_path_redirects_(Runtime::runtimeFeatureEnabled(
          "envoy.reloadable_features.preserve_query_string_in_path_redirects")),
      https_redirect_(route.redirect().https_redirect()),
      prefix_rewrite_redirect_(route.redirect().prefix_rewrite()),
      strip_query_(route.redirect().strip_query()),
      hedge_policy_(buildHedgePolicy(vhost.hedgePolicy(), route.route())),
      retry_policy_(buildRetryPolicy(vhost.retryPolicy(), route.route(), validator)),
      internal_redirect_policy_(
          buildInternalRedirectPolicy(route.route(), validator, route.name())),
      rate_limit_policy_(route.route().rate_limits()),
      priority_(ConfigUtility::parsePriority(route.route().priority())),
      config_headers_(Http::HeaderUtility::buildHeaderDataVector(route.match().headers())),
      total_cluster_weight_(
          PROTOBUF_GET_WRAPPED_OR_DEFAULT(route.route().weighted_clusters(), total_weight, 100UL)),
      request_headers_parser_(HeaderParser::configure(route.request_headers_to_add(),
                                                      route.request_headers_to_remove())),
      response_headers_parser_(HeaderParser::configure(route.response_headers_to_add(),
                                                       route.response_headers_to_remove())),
      retry_shadow_buffer_limit_(PROTOBUF_GET_WRAPPED_OR_DEFAULT(
          route, per_request_buffer_limit_bytes, vhost.retryShadowBufferLimit())),
      metadata_(route.metadata()), typed_metadata_(route.metadata()),
      match_grpc_(route.match().has_grpc()), opaque_config_(parseOpaqueConfig(route)),
      decorator_(parseDecorator(route)), route_tracing_(parseRouteTracing(route)),
      direct_response_code_(ConfigUtility::parseDirectResponseCode(route)),
      direct_response_body_(ConfigUtility::parseDirectResponseBody(route, factory_context.api())),
      per_filter_configs_(route.typed_per_filter_config(),
                          route.hidden_envoy_deprecated_per_filter_config(), factory_context,
                          validator),
      route_name_(route.name()), time_source_(factory_context.dispatcher().timeSource()) {
  if (route.route().has_metadata_match()) {
    const auto filter_it = route.route().metadata_match().filter_metadata().find(
        Envoy::Config::MetadataFilters::get().ENVOY_LB);
    if (filter_it != route.route().metadata_match().filter_metadata().end()) {
      metadata_match_criteria_ = std::make_unique<MetadataMatchCriteriaImpl>(filter_it->second);
    }
  }

  if (!route.route().request_mirror_policies().empty()) {
    if (route.route().has_hidden_envoy_deprecated_request_mirror_policy()) {
      // protobuf does not allow `oneof` to contain a field labeled `repeated`, so we do our own
      // xor-like check.
      // https://github.com/protocolbuffers/protobuf/issues/2592
      // The alternative solution suggested (wrapping the oneof in a repeated message) would still
      // break wire compatibility.
      // (see https://github.com/envoyproxy/envoy/issues/439#issuecomment-383622723)
      throw EnvoyException("Cannot specify both request_mirror_policy and request_mirror_policies");
    }
    for (const auto& mirror_policy_config : route.route().request_mirror_policies()) {
      shadow_policies_.push_back(std::make_unique<ShadowPolicyImpl>(mirror_policy_config));
    }
  } else if (route.route().has_hidden_envoy_deprecated_request_mirror_policy()) {
    shadow_policies_.push_back(std::make_unique<ShadowPolicyImpl>(
        route.route().hidden_envoy_deprecated_request_mirror_policy()));
  }

  // If this is a weighted_cluster, we create N internal route entries
  // (called WeightedClusterEntry), such that each object is a simple
  // single cluster, pointing back to the parent. Metadata criteria
  // from the weighted cluster (if any) are merged with and override
  // the criteria from the route.
  if (route.route().cluster_specifier_case() ==
      envoy::config::route::v3::RouteAction::ClusterSpecifierCase::kWeightedClusters) {
    ASSERT(total_cluster_weight_ > 0);

    uint64_t total_weight = 0UL;
    const std::string& runtime_key_prefix = route.route().weighted_clusters().runtime_key_prefix();

    for (const auto& cluster : route.route().weighted_clusters().clusters()) {
      auto cluster_entry = std::make_unique<WeightedClusterEntry>(
          this, runtime_key_prefix + "." + cluster.name(), factory_context, validator, cluster);
      weighted_clusters_.emplace_back(std::move(cluster_entry));
      total_weight += weighted_clusters_.back()->clusterWeight();
    }

    if (total_weight != total_cluster_weight_) {
      throw EnvoyException(fmt::format("Sum of weights in the weighted_cluster should add up to {}",
                                       total_cluster_weight_));
    }
  }

  for (const auto& query_parameter : route.match().query_parameters()) {
    config_query_parameters_.push_back(
        std::make_unique<ConfigUtility::QueryParameterMatcher>(query_parameter));
  }

  if (!route.route().hash_policy().empty()) {
    hash_policy_ = std::make_unique<Http::HashPolicyImpl>(route.route().hash_policy());
  }

  if (route.match().has_tls_context()) {
    tls_context_match_criteria_ =
        std::make_unique<TlsContextMatchCriteriaImpl>(route.match().tls_context());
  }

  // Returns true if include_vh_rate_limits is explicitly set to true otherwise it defaults to false
  // which is similar to VhRateLimitOptions::Override and will only use virtual host rate limits if
  // the route is empty
  include_vh_rate_limits_ =
      PROTOBUF_GET_WRAPPED_OR_DEFAULT(route.route(), include_vh_rate_limits, false);

  if (route.route().has_cors()) {
    cors_policy_ =
        std::make_unique<CorsPolicyImpl>(route.route().cors(), factory_context.runtime());
  }
  for (const auto& upgrade_config : route.route().upgrade_configs()) {
    const bool enabled = upgrade_config.has_enabled() ? upgrade_config.enabled().value() : true;
    const bool success =
        upgrade_map_
            .emplace(std::make_pair(
                Envoy::Http::LowerCaseString(upgrade_config.upgrade_type()).get(), enabled))
            .second;
    if (!success) {
      throw EnvoyException(absl::StrCat("Duplicate upgrade ", upgrade_config.upgrade_type()));
    }
    if (upgrade_config.upgrade_type() == Http::Headers::get().MethodValues.Connect) {
      connect_config_ = upgrade_config.connect_config();
    } else if (upgrade_config.has_connect_config()) {
      throw EnvoyException(absl::StrCat("Non-CONNECT upgrade type ", upgrade_config.upgrade_type(),
                                        " has ConnectConfig"));
    }
  }

  if (route.route().has_regex_rewrite()) {
    if (!prefix_rewrite_.empty()) {
      throw EnvoyException("Cannot specify both prefix_rewrite and regex_rewrite");
    }
    auto rewrite_spec = route.route().regex_rewrite();
    regex_rewrite_ = Regex::Utility::parseRegex(rewrite_spec.pattern());
    regex_rewrite_substitution_ = rewrite_spec.substitution();
  }

  if (enable_preserve_query_in_path_redirects_ && path_redirect_has_query_ && strip_query_) {
    ENVOY_LOG(warn,
              "`strip_query` is set to true, but `path_redirect` contains query string and it will "
              "not be stripped: {}",
              path_redirect_);
  }
}

bool RouteEntryImplBase::evaluateRuntimeMatch(const uint64_t random_value) const {
  return !runtime_ ? true
                   : loader_.snapshot().featureEnabled(runtime_->fractional_runtime_key_,
                                                       runtime_->fractional_runtime_default_,
                                                       random_value);
}

bool RouteEntryImplBase::evaluateTlsContextMatch(const StreamInfo::StreamInfo& stream_info) const {
  bool matches = true;

  if (!tlsContextMatchCriteria()) {
    return matches;
  }

  const TlsContextMatchCriteria& criteria = *tlsContextMatchCriteria();

  if (criteria.presented().has_value()) {
    const bool peer_presented = stream_info.downstreamSslConnection() &&
                                stream_info.downstreamSslConnection()->peerCertificatePresented();
    matches &= criteria.presented().value() == peer_presented;
  }

  if (criteria.validated().has_value()) {
    const bool peer_validated = stream_info.downstreamSslConnection() &&
                                stream_info.downstreamSslConnection()->peerCertificateValidated();
    matches &= criteria.validated().value() == peer_validated;
  }

  return matches;
}

bool RouteEntryImplBase::matchRoute(const Http::RequestHeaderMap& headers,
                                    const StreamInfo::StreamInfo& stream_info,
                                    uint64_t random_value) const {
  bool matches = true;

  matches &= evaluateRuntimeMatch(random_value);
  if (!matches) {
    // No need to waste further cycles calculating a route match.
    return false;
  }

  if (match_grpc_) {
    matches &= Grpc::Common::isGrpcRequestHeaders(headers);
  }

  matches &= Http::HeaderUtility::matchHeaders(headers, config_headers_);
  if (!config_query_parameters_.empty()) {
    Http::Utility::QueryParams query_parameters =
        Http::Utility::parseQueryString(headers.getPathValue());
    matches &= ConfigUtility::matchQueryParams(query_parameters, config_query_parameters_);
  }

  matches &= evaluateTlsContextMatch(stream_info);

  return matches;
}

const std::string& RouteEntryImplBase::clusterName() const { return cluster_name_; }

void RouteEntryImplBase::finalizeRequestHeaders(Http::RequestHeaderMap& headers,
                                                const StreamInfo::StreamInfo& stream_info,
                                                bool insert_envoy_original_path) const {
  if (!vhost_.globalRouteConfig().mostSpecificHeaderMutationsWins()) {
    // Append user-specified request headers from most to least specific: route-level headers,
    // virtual host level headers and finally global connection manager level headers.
    request_headers_parser_->evaluateHeaders(headers, stream_info);
    vhost_.requestHeaderParser().evaluateHeaders(headers, stream_info);
    vhost_.globalRouteConfig().requestHeaderParser().evaluateHeaders(headers, stream_info);
  } else {
    // Most specific mutations take precedence.
    vhost_.globalRouteConfig().requestHeaderParser().evaluateHeaders(headers, stream_info);
    vhost_.requestHeaderParser().evaluateHeaders(headers, stream_info);
    request_headers_parser_->evaluateHeaders(headers, stream_info);
  }

  if (!host_rewrite_.empty()) {
    headers.setHost(host_rewrite_);
  } else if (auto_host_rewrite_header_) {
    const auto header = headers.get(*auto_host_rewrite_header_);
    if (!header.empty()) {
      // This is an implicitly untrusted header, so per the API documentation only the first
      // value is used.
      const absl::string_view header_value = header[0]->value().getStringView();
      if (!header_value.empty()) {
        headers.setHost(header_value);
      }
    }
  } else if (host_rewrite_path_regex_ != nullptr) {
    const std::string path(headers.getPathValue());
    absl::string_view just_path(Http::PathUtil::removeQueryAndFragment(path));
    headers.setHost(
        host_rewrite_path_regex_->replaceAll(just_path, host_rewrite_path_regex_substitution_));
  }

  // Handle path rewrite
  if (!getPathRewrite().empty() || regex_rewrite_ != nullptr) {
    rewritePathHeader(headers, insert_envoy_original_path);
  }
}

void RouteEntryImplBase::finalizeResponseHeaders(Http::ResponseHeaderMap& headers,
                                                 const StreamInfo::StreamInfo& stream_info) const {
  if (!vhost_.globalRouteConfig().mostSpecificHeaderMutationsWins()) {
    // Append user-specified request headers from most to least specific: route-level headers,
    // virtual host level headers and finally global connection manager level headers.
    response_headers_parser_->evaluateHeaders(headers, stream_info);
    vhost_.responseHeaderParser().evaluateHeaders(headers, stream_info);
    vhost_.globalRouteConfig().responseHeaderParser().evaluateHeaders(headers, stream_info);
  } else {
    // Most specific mutations take precedence.
    vhost_.globalRouteConfig().responseHeaderParser().evaluateHeaders(headers, stream_info);
    vhost_.responseHeaderParser().evaluateHeaders(headers, stream_info);
    response_headers_parser_->evaluateHeaders(headers, stream_info);
  }
}

absl::optional<RouteEntryImplBase::RuntimeData>
RouteEntryImplBase::loadRuntimeData(const envoy::config::route::v3::RouteMatch& route_match) {
  absl::optional<RuntimeData> runtime;
  RuntimeData runtime_data;

  if (route_match.has_runtime_fraction()) {
    runtime_data.fractional_runtime_default_ = route_match.runtime_fraction().default_value();
    runtime_data.fractional_runtime_key_ = route_match.runtime_fraction().runtime_key();
    return runtime_data;
  }

  return runtime;
}

// finalizePathHeaders does the "standard" path rewriting, meaning that it
// handles the "prefix_rewrite" and "regex_rewrite" route actions, only one of
// which can be specified. The "matched_path" argument applies only to the
// prefix rewriting, and describes the portion of the path (excluding query
// parameters) that should be replaced by the rewrite. A "regex_rewrite"
// applies to the entire path (excluding query parameters), regardless of what
// portion was matched.
void RouteEntryImplBase::finalizePathHeader(Http::RequestHeaderMap& headers,
                                            absl::string_view matched_path,
                                            bool insert_envoy_original_path) const {
  const auto& rewrite = getPathRewrite();
  if (rewrite.empty() && regex_rewrite_ == nullptr) {
    // There are no rewrites configured. Just return.
    return;
  }

  // TODO(perf): can we avoid the string copy for the common case?
  std::string path(headers.getPathValue());
  if (insert_envoy_original_path) {
    headers.setEnvoyOriginalPath(path);
  }

  if (!rewrite.empty()) {
    ASSERT(case_sensitive_ ? absl::StartsWith(path, matched_path)
                           : absl::StartsWithIgnoreCase(path, matched_path));
    headers.setPath(path.replace(0, matched_path.size(), rewrite));
    return;
  }

  if (regex_rewrite_ != nullptr) {
    // Replace the entire path, but preserve the query parameters
    auto just_path(Http::PathUtil::removeQueryAndFragment(path));
    headers.setPath(path.replace(
        0, just_path.size(), regex_rewrite_->replaceAll(just_path, regex_rewrite_substitution_)));
    return;
  }
}

absl::string_view RouteEntryImplBase::processRequestHost(const Http::RequestHeaderMap& headers,
                                                         absl::string_view new_scheme,
                                                         absl::string_view new_port) const {

  absl::string_view request_host = headers.getHostValue();
  size_t host_end;
  if (request_host.empty()) {
    return request_host;
  }
  // Detect if IPv6 URI
  if (request_host[0] == '[') {
    host_end = request_host.rfind("]:");
    if (host_end != absl::string_view::npos) {
      host_end += 1; // advance to :
    }
  } else {
    host_end = request_host.rfind(":");
  }

  if (host_end != absl::string_view::npos) {
    absl::string_view request_port = request_host.substr(host_end);
    absl::string_view request_protocol = headers.getForwardedProtoValue();
    bool remove_port = !new_port.empty();

    if (new_scheme != request_protocol) {
      remove_port |= (request_protocol == Http::Headers::get().SchemeValues.Https.c_str()) &&
                     request_port == ":443";
      remove_port |= (request_protocol == Http::Headers::get().SchemeValues.Http.c_str()) &&
                     request_port == ":80";
    }

    if (remove_port) {
      return request_host.substr(0, host_end);
    }
  }

  return request_host;
}

std::string RouteEntryImplBase::newPath(const Http::RequestHeaderMap& headers) const {
  ASSERT(isDirectResponse());

  absl::string_view final_scheme;
  absl::string_view final_host;
  absl::string_view final_port;
  absl::string_view final_path;

  if (!scheme_redirect_.empty()) {
    final_scheme = scheme_redirect_.c_str();
  } else if (https_redirect_) {
    final_scheme = Http::Headers::get().SchemeValues.Https;
  } else {
    ASSERT(headers.ForwardedProto());
    final_scheme = headers.getForwardedProtoValue();
  }

  if (!port_redirect_.empty()) {
    final_port = port_redirect_.c_str();
  } else {
    final_port = "";
  }

  if (!host_redirect_.empty()) {
    final_host = host_redirect_.c_str();
  } else {
    ASSERT(headers.Host());
    final_host = processRequestHost(headers, final_scheme, final_port);
  }

  std::string final_path_value;
  if (enable_preserve_query_in_path_redirects_) {
    if (!path_redirect_.empty()) {
      // The path_redirect query string, if any, takes precedence over the request's query string,
      // and it will not be stripped regardless of `strip_query`.
      if (path_redirect_has_query_) {
        final_path = path_redirect_.c_str();
      } else {
        const absl::string_view current_path = headers.getPathValue();
        const size_t path_end = current_path.find('?');
        const bool current_path_has_query = path_end != absl::string_view::npos;
        if (current_path_has_query) {
          final_path_value = path_redirect_;
          final_path_value.append(current_path.data() + path_end, current_path.length() - path_end);
          final_path = final_path_value;
        } else {
          final_path = path_redirect_.c_str();
        }
      }
    } else {
      final_path = headers.getPathValue();
    }
    if (!path_redirect_has_query_ && strip_query_) {
      const size_t path_end = final_path.find('?');
      if (path_end != absl::string_view::npos) {
        final_path = final_path.substr(0, path_end);
      }
    }
  } else {
    if (!path_redirect_.empty()) {
      final_path = path_redirect_.c_str();
    } else {
      final_path = headers.getPathValue();
      if (strip_query_) {
        const size_t path_end = final_path.find("?");
        if (path_end != absl::string_view::npos) {
          final_path = final_path.substr(0, path_end);
        }
      }
    }
  }

  return fmt::format("{}://{}{}{}", final_scheme, final_host, final_port, final_path);
}

std::multimap<std::string, std::string>
RouteEntryImplBase::parseOpaqueConfig(const envoy::config::route::v3::Route& route) {
  std::multimap<std::string, std::string> ret;
  if (route.has_metadata()) {
    auto filter_metadata = route.metadata().filter_metadata().find(
        Extensions::HttpFilters::HttpFilterNames::get().Router);
    if (filter_metadata == route.metadata().filter_metadata().end()) {
      // TODO(zuercher): simply return `ret` when deprecated filter names are removed.
      filter_metadata = route.metadata().filter_metadata().find(DEPRECATED_ROUTER_NAME);
      if (filter_metadata == route.metadata().filter_metadata().end()) {
        return ret;
      }

      Extensions::Common::Utility::ExtensionNameUtil::checkDeprecatedExtensionName(
          "http filter", DEPRECATED_ROUTER_NAME,
          Extensions::HttpFilters::HttpFilterNames::get().Router);
    }
    for (const auto& it : filter_metadata->second.fields()) {
      if (it.second.kind_case() == ProtobufWkt::Value::kStringValue) {
        ret.emplace(it.first, it.second.string_value());
      }
    }
  }
  return ret;
}

HedgePolicyImpl RouteEntryImplBase::buildHedgePolicy(
    const absl::optional<envoy::config::route::v3::HedgePolicy>& vhost_hedge_policy,
    const envoy::config::route::v3::RouteAction& route_config) const {
  // Route specific policy wins, if available.
  if (route_config.has_hedge_policy()) {
    return HedgePolicyImpl(route_config.hedge_policy());
  }

  // If not, we fall back to the virtual host policy if there is one.
  if (vhost_hedge_policy) {
    return HedgePolicyImpl(vhost_hedge_policy.value());
  }

  // Otherwise, an empty policy will do.
  return HedgePolicyImpl();
}

RetryPolicyImpl RouteEntryImplBase::buildRetryPolicy(
    const absl::optional<envoy::config::route::v3::RetryPolicy>& vhost_retry_policy,
    const envoy::config::route::v3::RouteAction& route_config,
    ProtobufMessage::ValidationVisitor& validation_visitor) const {
  // Route specific policy wins, if available.
  if (route_config.has_retry_policy()) {
    return RetryPolicyImpl(route_config.retry_policy(), validation_visitor);
  }

  // If not, we fallback to the virtual host policy if there is one.
  if (vhost_retry_policy) {
    return RetryPolicyImpl(vhost_retry_policy.value(), validation_visitor);
  }

  // Otherwise, an empty policy will do.
  return RetryPolicyImpl();
}

InternalRedirectPolicyImpl RouteEntryImplBase::buildInternalRedirectPolicy(
    const envoy::config::route::v3::RouteAction& route_config,
    ProtobufMessage::ValidationVisitor& validator, absl::string_view current_route_name) const {
  if (route_config.has_internal_redirect_policy()) {
    return InternalRedirectPolicyImpl(route_config.internal_redirect_policy(), validator,
                                      current_route_name);
  }
  envoy::config::route::v3::InternalRedirectPolicy policy_config;
  switch (route_config.internal_redirect_action()) {
  case envoy::config::route::v3::RouteAction::HANDLE_INTERNAL_REDIRECT:
    break;
  case envoy::config::route::v3::RouteAction::PASS_THROUGH_INTERNAL_REDIRECT:
    FALLTHRU;
  default:
    return InternalRedirectPolicyImpl();
  }
  if (route_config.has_max_internal_redirects()) {
    *policy_config.mutable_max_internal_redirects() = route_config.max_internal_redirects();
  }
  return InternalRedirectPolicyImpl(policy_config, validator, current_route_name);
}

DecoratorConstPtr RouteEntryImplBase::parseDecorator(const envoy::config::route::v3::Route& route) {
  DecoratorConstPtr ret;
  if (route.has_decorator()) {
    ret = DecoratorConstPtr(new DecoratorImpl(route.decorator()));
  }
  return ret;
}

RouteTracingConstPtr
RouteEntryImplBase::parseRouteTracing(const envoy::config::route::v3::Route& route) {
  RouteTracingConstPtr ret;
  if (route.has_tracing()) {
    ret = RouteTracingConstPtr(new RouteTracingImpl(route.tracing()));
  }
  return ret;
}

const DirectResponseEntry* RouteEntryImplBase::directResponseEntry() const {
  // A route for a request can exclusively be a route entry, a direct response entry,
  // or a redirect entry.
  if (isDirectResponse()) {
    return this;
  } else {
    return nullptr;
  }
}

const RouteEntry* RouteEntryImplBase::routeEntry() const {
  // A route for a request can exclusively be a route entry, a direct response entry,
  // or a redirect entry.
  if (isDirectResponse()) {
    return nullptr;
  } else {
    return this;
  }
}

RouteConstSharedPtr RouteEntryImplBase::clusterEntry(const Http::HeaderMap& headers,
                                                     uint64_t random_value) const {
  // Gets the route object chosen from the list of weighted clusters
  // (if there is one) or returns self.
  if (weighted_clusters_.empty()) {
    if (!cluster_name_.empty() || isDirectResponse()) {
      return shared_from_this();
    } else {
      ASSERT(!cluster_header_name_.get().empty());
      const auto entry = headers.get(cluster_header_name_);
      std::string final_cluster_name;
      if (!entry.empty()) {
        // This is an implicitly untrusted header, so per the API documentation only the first
        // value is used.
        final_cluster_name = std::string(entry[0]->value().getStringView());
      }

      // NOTE: Though we return a shared_ptr here, the current ownership model assumes that
      //       the route table sticks around. See snapped_route_config_ in
      //       ConnectionManagerImpl::ActiveStream.
      return std::make_shared<DynamicRouteEntry>(this, final_cluster_name);
    }
  }

  return WeightedClusterUtil::pickCluster(weighted_clusters_, total_cluster_weight_, random_value,
                                          true);
}

void RouteEntryImplBase::validateClusters(Upstream::ClusterManager& cm) const {
  if (isDirectResponse()) {
    return;
  }

  // Currently, we verify that the cluster exists in the CM if we have an explicit cluster or
  // weighted cluster rule. We obviously do not verify a cluster_header rule. This means that
  // trying to use all CDS clusters with a static route table will not work. In the upcoming RDS
  // change we will make it so that dynamically loaded route tables do *not* perform CM checks.
  // In the future we might decide to also have a config option that turns off checks for static
  // route tables. This would enable the all CDS with static route table case.
  if (!cluster_name_.empty()) {
    if (!cm.get(cluster_name_)) {
      throw EnvoyException(fmt::format("route: unknown cluster '{}'", cluster_name_));
    }
  } else if (!weighted_clusters_.empty()) {
    for (const WeightedClusterEntrySharedPtr& cluster : weighted_clusters_) {
      if (!cm.get(cluster->clusterName())) {
        throw EnvoyException(
            fmt::format("route: unknown weighted cluster '{}'", cluster->clusterName()));
      }
    }
  }
}

const RouteSpecificFilterConfig*
RouteEntryImplBase::perFilterConfig(const std::string& name) const {
  return per_filter_configs_.get(name);
}

RouteEntryImplBase::WeightedClusterEntry::WeightedClusterEntry(
    const RouteEntryImplBase* parent, const std::string& runtime_key,
    Server::Configuration::ServerFactoryContext& factory_context,
    ProtobufMessage::ValidationVisitor& validator,
    const envoy::config::route::v3::WeightedCluster::ClusterWeight& cluster)
    : DynamicRouteEntry(parent, cluster.name()), runtime_key_(runtime_key),
      loader_(factory_context.runtime()),
      cluster_weight_(PROTOBUF_GET_WRAPPED_REQUIRED(cluster, weight)),
      request_headers_parser_(HeaderParser::configure(cluster.request_headers_to_add(),
                                                      cluster.request_headers_to_remove())),
      response_headers_parser_(HeaderParser::configure(cluster.response_headers_to_add(),
                                                       cluster.response_headers_to_remove())),
      per_filter_configs_(cluster.typed_per_filter_config(),
                          cluster.hidden_envoy_deprecated_per_filter_config(), factory_context,
                          validator) {
  if (cluster.has_metadata_match()) {
    const auto filter_it = cluster.metadata_match().filter_metadata().find(
        Envoy::Config::MetadataFilters::get().ENVOY_LB);
    if (filter_it != cluster.metadata_match().filter_metadata().end()) {
      if (parent->metadata_match_criteria_) {
        cluster_metadata_match_criteria_ =
            parent->metadata_match_criteria_->mergeMatchCriteria(filter_it->second);
      } else {
        cluster_metadata_match_criteria_ =
            std::make_unique<MetadataMatchCriteriaImpl>(filter_it->second);
      }
    }
  }
}

const RouteSpecificFilterConfig*
RouteEntryImplBase::WeightedClusterEntry::perFilterConfig(const std::string& name) const {
  const auto cfg = per_filter_configs_.get(name);
  return cfg != nullptr ? cfg : DynamicRouteEntry::perFilterConfig(name);
}

PrefixRouteEntryImpl::PrefixRouteEntryImpl(
    const VirtualHostImpl& vhost, const envoy::config::route::v3::Route& route,
    Server::Configuration::ServerFactoryContext& factory_context,
    ProtobufMessage::ValidationVisitor& validator)
    : RouteEntryImplBase(vhost, route, factory_context, validator), prefix_(route.match().prefix()),
      path_matcher_(Matchers::PathMatcher::createPrefix(prefix_, !case_sensitive_)) {}

void PrefixRouteEntryImpl::rewritePathHeader(Http::RequestHeaderMap& headers,
                                             bool insert_envoy_original_path) const {
  finalizePathHeader(headers, prefix_, insert_envoy_original_path);
}

RouteConstSharedPtr PrefixRouteEntryImpl::matches(const Http::RequestHeaderMap& headers,
                                                  const StreamInfo::StreamInfo& stream_info,
                                                  uint64_t random_value) const {
  if (RouteEntryImplBase::matchRoute(headers, stream_info, random_value) &&
      path_matcher_->match(headers.getPathValue())) {
    return clusterEntry(headers, random_value);
  }
  return nullptr;
}

PathRouteEntryImpl::PathRouteEntryImpl(const VirtualHostImpl& vhost,
                                       const envoy::config::route::v3::Route& route,
                                       Server::Configuration::ServerFactoryContext& factory_context,
                                       ProtobufMessage::ValidationVisitor& validator)
    : RouteEntryImplBase(vhost, route, factory_context, validator), path_(route.match().path()),
      path_matcher_(Matchers::PathMatcher::createExact(path_, !case_sensitive_)) {}

void PathRouteEntryImpl::rewritePathHeader(Http::RequestHeaderMap& headers,
                                           bool insert_envoy_original_path) const {
  finalizePathHeader(headers, path_, insert_envoy_original_path);
}

RouteConstSharedPtr PathRouteEntryImpl::matches(const Http::RequestHeaderMap& headers,
                                                const StreamInfo::StreamInfo& stream_info,
                                                uint64_t random_value) const {
  if (RouteEntryImplBase::matchRoute(headers, stream_info, random_value) &&
      path_matcher_->match(headers.getPathValue())) {
    return clusterEntry(headers, random_value);
  }

  return nullptr;
}

RegexRouteEntryImpl::RegexRouteEntryImpl(
    const VirtualHostImpl& vhost, const envoy::config::route::v3::Route& route,
    Server::Configuration::ServerFactoryContext& factory_context,
    ProtobufMessage::ValidationVisitor& validator)
    : RouteEntryImplBase(vhost, route, factory_context, validator) {
  // TODO(yangminzhu): Use PathMatcher once hidden_envoy_deprecated_regex is removed.
  if (route.match().path_specifier_case() ==
      envoy::config::route::v3::RouteMatch::PathSpecifierCase::kHiddenEnvoyDeprecatedRegex) {
    regex_ = Regex::Utility::parseStdRegexAsCompiledMatcher(
        route.match().hidden_envoy_deprecated_regex());
    regex_str_ = route.match().hidden_envoy_deprecated_regex();
  } else {
    ASSERT(route.match().path_specifier_case() ==
           envoy::config::route::v3::RouteMatch::PathSpecifierCase::kSafeRegex);
    regex_ = Regex::Utility::parseRegex(route.match().safe_regex());
    regex_str_ = route.match().safe_regex().regex();
  }
}

void RegexRouteEntryImpl::rewritePathHeader(Http::RequestHeaderMap& headers,
                                            bool insert_envoy_original_path) const {
  const absl::string_view path = Http::PathUtil::removeQueryAndFragment(headers.getPathValue());
  // TODO(yuval-k): This ASSERT can happen if the path was changed by a filter without clearing the
  // route cache. We should consider if ASSERT-ing is the desired behavior in this case.
  ASSERT(regex_->match(path));
  finalizePathHeader(headers, path, insert_envoy_original_path);
}

RouteConstSharedPtr RegexRouteEntryImpl::matches(const Http::RequestHeaderMap& headers,
                                                 const StreamInfo::StreamInfo& stream_info,
                                                 uint64_t random_value) const {
  if (RouteEntryImplBase::matchRoute(headers, stream_info, random_value)) {
    const absl::string_view path = Http::PathUtil::removeQueryAndFragment(headers.getPathValue());
    if (regex_->match(path)) {
      return clusterEntry(headers, random_value);
    }
  }
  return nullptr;
}

ConnectRouteEntryImpl::ConnectRouteEntryImpl(
    const VirtualHostImpl& vhost, const envoy::config::route::v3::Route& route,
    Server::Configuration::ServerFactoryContext& factory_context,
    ProtobufMessage::ValidationVisitor& validator)
    : RouteEntryImplBase(vhost, route, factory_context, validator) {}

void ConnectRouteEntryImpl::rewritePathHeader(Http::RequestHeaderMap& headers,
                                              bool insert_envoy_original_path) const {
  const absl::string_view path = Http::PathUtil::removeQueryAndFragment(headers.getPathValue());
  finalizePathHeader(headers, path, insert_envoy_original_path);
}

RouteConstSharedPtr ConnectRouteEntryImpl::matches(const Http::RequestHeaderMap& headers,
                                                   const StreamInfo::StreamInfo& stream_info,
                                                   uint64_t random_value) const {
  if (Http::HeaderUtility::isConnect(headers) &&
      RouteEntryImplBase::matchRoute(headers, stream_info, random_value)) {
    return clusterEntry(headers, random_value);
  }
  return nullptr;
}

VirtualHostImpl::VirtualHostImpl(const envoy::config::route::v3::VirtualHost& virtual_host,
                                 const ConfigImpl& global_route_config,
                                 Server::Configuration::ServerFactoryContext& factory_context,
                                 Stats::Scope& scope, ProtobufMessage::ValidationVisitor& validator,
                                 bool validate_clusters)
    : stat_name_pool_(factory_context.scope().symbolTable()),
      stat_name_(stat_name_pool_.add(virtual_host.name())),
      vcluster_scope_(scope.createScope(virtual_host.name() + ".vcluster")),
      rate_limit_policy_(virtual_host.rate_limits()), global_route_config_(global_route_config),
      request_headers_parser_(HeaderParser::configure(virtual_host.request_headers_to_add(),
                                                      virtual_host.request_headers_to_remove())),
      response_headers_parser_(HeaderParser::configure(virtual_host.response_headers_to_add(),
                                                       virtual_host.response_headers_to_remove())),
      per_filter_configs_(virtual_host.typed_per_filter_config(),
                          virtual_host.hidden_envoy_deprecated_per_filter_config(), factory_context,
                          validator),
      retry_shadow_buffer_limit_(PROTOBUF_GET_WRAPPED_OR_DEFAULT(
          virtual_host, per_request_buffer_limit_bytes, std::numeric_limits<uint32_t>::max())),
      include_attempt_count_in_request_(virtual_host.include_request_attempt_count()),
      include_attempt_count_in_response_(virtual_host.include_attempt_count_in_response()),
      virtual_cluster_catch_all_(stat_name_pool_, *vcluster_scope_) {

  switch (virtual_host.require_tls()) {
  case envoy::config::route::v3::VirtualHost::NONE:
    ssl_requirements_ = SslRequirements::None;
    break;
  case envoy::config::route::v3::VirtualHost::EXTERNAL_ONLY:
    ssl_requirements_ = SslRequirements::ExternalOnly;
    break;
  case envoy::config::route::v3::VirtualHost::ALL:
    ssl_requirements_ = SslRequirements::All;
    break;
  default:
    NOT_REACHED_GCOVR_EXCL_LINE;
  }

  // Retry and Hedge policies must be set before routes, since they may use them.
  if (virtual_host.has_retry_policy()) {
    retry_policy_ = virtual_host.retry_policy();
  }
  if (virtual_host.has_hedge_policy()) {
    hedge_policy_ = virtual_host.hedge_policy();
  }

  for (const auto& route : virtual_host.routes()) {
    switch (route.match().path_specifier_case()) {
    case envoy::config::route::v3::RouteMatch::PathSpecifierCase::kPrefix: {
      routes_.emplace_back(new PrefixRouteEntryImpl(*this, route, factory_context, validator));
      break;
    }
    case envoy::config::route::v3::RouteMatch::PathSpecifierCase::kPath: {
      routes_.emplace_back(new PathRouteEntryImpl(*this, route, factory_context, validator));
      break;
    }
    case envoy::config::route::v3::RouteMatch::PathSpecifierCase::kHiddenEnvoyDeprecatedRegex:
    case envoy::config::route::v3::RouteMatch::PathSpecifierCase::kSafeRegex: {
      routes_.emplace_back(new RegexRouteEntryImpl(*this, route, factory_context, validator));
      break;
    }
    case envoy::config::route::v3::RouteMatch::PathSpecifierCase::kConnectMatcher: {
      routes_.emplace_back(new ConnectRouteEntryImpl(*this, route, factory_context, validator));
      break;
    }
    default:
      NOT_REACHED_GCOVR_EXCL_LINE;
    }

    if (validate_clusters) {
      routes_.back()->validateClusters(factory_context.clusterManager());
      for (const auto& shadow_policy : routes_.back()->shadowPolicies()) {
        ASSERT(!shadow_policy->cluster().empty());
        if (!factory_context.clusterManager().get(shadow_policy->cluster())) {
          throw EnvoyException(
              fmt::format("route: unknown shadow cluster '{}'", shadow_policy->cluster()));
        }
      }
    }
  }

  for (const auto& virtual_cluster : virtual_host.virtual_clusters()) {
    virtual_clusters_.push_back(
        VirtualClusterEntry(virtual_cluster, stat_name_pool_, *vcluster_scope_));
  }

  if (virtual_host.has_cors()) {
    cors_policy_ = std::make_unique<CorsPolicyImpl>(virtual_host.cors(), factory_context.runtime());
  }
}

VirtualHostImpl::VirtualClusterEntry::VirtualClusterEntry(
    const envoy::config::route::v3::VirtualCluster& virtual_cluster, Stats::StatNamePool& pool,
    Stats::Scope& scope)
    : VirtualClusterBase(pool.add(virtual_cluster.name()),
                         scope.createScope(virtual_cluster.name())) {
  if (virtual_cluster.hidden_envoy_deprecated_pattern().empty() ==
      virtual_cluster.headers().empty()) {
    throw EnvoyException("virtual clusters must define either 'pattern' or 'headers'");
  }

  if (!virtual_cluster.hidden_envoy_deprecated_pattern().empty()) {
    envoy::config::route::v3::HeaderMatcher matcher_config;
    matcher_config.set_name(Http::Headers::get().Path.get());
    matcher_config.set_hidden_envoy_deprecated_regex_match(
        virtual_cluster.hidden_envoy_deprecated_pattern());
    headers_.push_back(std::make_unique<Http::HeaderUtility::HeaderData>(matcher_config));
  } else {
    ASSERT(!virtual_cluster.headers().empty());
    headers_ = Http::HeaderUtility::buildHeaderDataVector(virtual_cluster.headers());
  }

  if (virtual_cluster.hidden_envoy_deprecated_method() !=
      envoy::config::core::v3::METHOD_UNSPECIFIED) {
    envoy::config::route::v3::HeaderMatcher matcher_config;
    matcher_config.set_name(Http::Headers::get().Method.get());
    matcher_config.set_exact_match(envoy::config::core::v3::RequestMethod_Name(
        virtual_cluster.hidden_envoy_deprecated_method()));
    headers_.push_back(std::make_unique<Http::HeaderUtility::HeaderData>(matcher_config));
  }
}

const Config& VirtualHostImpl::routeConfig() const { return global_route_config_; }

const RouteSpecificFilterConfig* VirtualHostImpl::perFilterConfig(const std::string& name) const {
  return per_filter_configs_.get(name);
}

const VirtualHostImpl* RouteMatcher::findWildcardVirtualHost(
    const std::string& host, const RouteMatcher::WildcardVirtualHosts& wildcard_virtual_hosts,
    RouteMatcher::SubstringFunction substring_function) const {
  // We do a longest wildcard match against the host that's passed in
  // (e.g. "foo-bar.baz.com" should match "*-bar.baz.com" before matching "*.baz.com" for suffix
  // wildcards). This is done by scanning the length => wildcards map looking for every wildcard
  // whose size is < length.
  for (const auto& iter : wildcard_virtual_hosts) {
    const uint32_t wildcard_length = iter.first;
    const auto& wildcard_map = iter.second;
    // >= because *.foo.com shouldn't match .foo.com.
    if (wildcard_length >= host.size()) {
      continue;
    }
    const auto& match = wildcard_map.find(substring_function(host, wildcard_length));
    if (match != wildcard_map.end()) {
      return match->second.get();
    }
  }
  return nullptr;
}

RouteMatcher::RouteMatcher(const envoy::config::route::v3::RouteConfiguration& route_config,
                           const ConfigImpl& global_route_config,
                           Server::Configuration::ServerFactoryContext& factory_context,
                           ProtobufMessage::ValidationVisitor& validator, bool validate_clusters)
    : vhost_scope_(factory_context.scope().createScope("vhost")) {
  for (const auto& virtual_host_config : route_config.virtual_hosts()) {
    VirtualHostSharedPtr virtual_host(new VirtualHostImpl(virtual_host_config, global_route_config,
                                                          factory_context, *vhost_scope_, validator,
                                                          validate_clusters));
    for (const std::string& domain_name : virtual_host_config.domains()) {
      const std::string domain = Http::LowerCaseString(domain_name).get();
      bool duplicate_found = false;
      if ("*" == domain) {
        if (default_virtual_host_) {
          throw EnvoyException(fmt::format("Only a single wildcard domain is permitted in route {}",
                                           route_config.name()));
        }
        default_virtual_host_ = virtual_host;
      } else if (!domain.empty() && '*' == domain[0]) {
        duplicate_found = !wildcard_virtual_host_suffixes_[domain.size() - 1]
                               .emplace(domain.substr(1), virtual_host)
                               .second;
      } else if (!domain.empty() && '*' == domain[domain.size() - 1]) {
        duplicate_found = !wildcard_virtual_host_prefixes_[domain.size() - 1]
                               .emplace(domain.substr(0, domain.size() - 1), virtual_host)
                               .second;
      } else {
        duplicate_found = !virtual_hosts_.emplace(domain, virtual_host).second;
      }
      if (duplicate_found) {
        throw EnvoyException(fmt::format("Only unique values for domains are permitted. Duplicate "
                                         "entry of domain {} in route {}",
                                         domain, route_config.name()));
      }
    }
  }
}

RouteConstSharedPtr VirtualHostImpl::getRouteFromEntries(const RouteCallback& cb,
                                                         const Http::RequestHeaderMap& headers,
                                                         const StreamInfo::StreamInfo& stream_info,
                                                         uint64_t random_value) const {
  // No x-forwarded-proto header. This normally only happens when ActiveStream::decodeHeaders
  // bails early (as it rejects a request), so there is no routing is going to happen anyway.
  const auto* forwarded_proto_header = headers.ForwardedProto();
  if (forwarded_proto_header == nullptr) {
    return nullptr;
  }

  // First check for ssl redirect.
  if (ssl_requirements_ == SslRequirements::All && forwarded_proto_header->value() != "https") {
    return SSL_REDIRECT_ROUTE;
  } else if (ssl_requirements_ == SslRequirements::ExternalOnly &&
             forwarded_proto_header->value() != "https" &&
             !Http::HeaderUtility::isEnvoyInternalRequest(headers)) {
    return SSL_REDIRECT_ROUTE;
  }

  // Check for a route that matches the request.
  for (auto route = routes_.begin(); route != routes_.end(); ++route) {
    if (!headers.Path() && !(*route)->supportsPathlessHeaders()) {
      continue;
    }

    RouteConstSharedPtr route_entry = (*route)->matches(headers, stream_info, random_value);
    if (nullptr == route_entry) {
      continue;
    }

    if (cb) {
      RouteEvalStatus eval_status = (std::next(route) == routes_.end())
                                        ? RouteEvalStatus::NoMoreRoutes
                                        : RouteEvalStatus::HasMoreRoutes;
      RouteMatchStatus match_status = cb(route_entry, eval_status);
      if (match_status == RouteMatchStatus::Accept) {
        return route_entry;
      }
      if (match_status == RouteMatchStatus::Continue &&
          eval_status == RouteEvalStatus::NoMoreRoutes) {
        return nullptr;
      }
      continue;
    }

    return route_entry;
  }

  return nullptr;
}

const VirtualHostImpl* RouteMatcher::findVirtualHost(const Http::RequestHeaderMap& headers) const {
  // Fast path the case where we only have a default virtual host.
  if (virtual_hosts_.empty() && wildcard_virtual_host_suffixes_.empty() &&
      wildcard_virtual_host_prefixes_.empty()) {
    return default_virtual_host_.get();
  }

  // There may be no authority in early reply paths in the HTTP connection manager.
  if (headers.Host() == nullptr) {
    return nullptr;
  }

  // TODO (@rshriram) Match Origin header in WebSocket
  // request with VHost, using wildcard match
  // Lower-case the value of the host header, as hostnames are case insensitive.
  const std::string host = absl::AsciiStrToLower(headers.getHostValue());
  const auto& iter = virtual_hosts_.find(host);
  if (iter != virtual_hosts_.end()) {
    return iter->second.get();
  }
  if (!wildcard_virtual_host_suffixes_.empty()) {
    const VirtualHostImpl* vhost = findWildcardVirtualHost(
        host, wildcard_virtual_host_suffixes_,
        [](const std::string& h, int l) -> std::string { return h.substr(h.size() - l); });
    if (vhost != nullptr) {
      return vhost;
    }
  }
  if (!wildcard_virtual_host_prefixes_.empty()) {
    const VirtualHostImpl* vhost = findWildcardVirtualHost(
        host, wildcard_virtual_host_prefixes_,
        [](const std::string& h, int l) -> std::string { return h.substr(0, l); });
    if (vhost != nullptr) {
      return vhost;
    }
  }
  return default_virtual_host_.get();
}

RouteConstSharedPtr RouteMatcher::route(const RouteCallback& cb,
                                        const Http::RequestHeaderMap& headers,
                                        const StreamInfo::StreamInfo& stream_info,
                                        uint64_t random_value) const {

  const VirtualHostImpl* virtual_host = findVirtualHost(headers);
  if (virtual_host) {
    return virtual_host->getRouteFromEntries(cb, headers, stream_info, random_value);
  } else {
    return nullptr;
  }
}

const SslRedirector SslRedirectRoute::SSL_REDIRECTOR;
const std::shared_ptr<const SslRedirectRoute> VirtualHostImpl::SSL_REDIRECT_ROUTE{
    new SslRedirectRoute()};

const VirtualCluster*
VirtualHostImpl::virtualClusterFromEntries(const Http::HeaderMap& headers) const {
  for (const VirtualClusterEntry& entry : virtual_clusters_) {
    if (Http::HeaderUtility::matchHeaders(headers, entry.headers_)) {
      return &entry;
    }
  }

  if (!virtual_clusters_.empty()) {
    return &virtual_cluster_catch_all_;
  }

  return nullptr;
}

ConfigImpl::ConfigImpl(const envoy::config::route::v3::RouteConfiguration& config,
                       Server::Configuration::ServerFactoryContext& factory_context,
                       ProtobufMessage::ValidationVisitor& validator,
                       bool validate_clusters_default)
    : name_(config.name()), symbol_table_(factory_context.scope().symbolTable()),
      uses_vhds_(config.has_vhds()),
      most_specific_header_mutations_wins_(config.most_specific_header_mutations_wins()) {
  route_matcher_ = std::make_unique<RouteMatcher>(
      config, *this, factory_context, validator,
      PROTOBUF_GET_WRAPPED_OR_DEFAULT(config, validate_clusters, validate_clusters_default));

  for (const std::string& header : config.internal_only_headers()) {
    internal_only_headers_.push_back(Http::LowerCaseString(header));
  }

  request_headers_parser_ =
      HeaderParser::configure(config.request_headers_to_add(), config.request_headers_to_remove());
  response_headers_parser_ = HeaderParser::configure(config.response_headers_to_add(),
                                                     config.response_headers_to_remove());
}

RouteConstSharedPtr ConfigImpl::route(const RouteCallback& cb,
                                      const Http::RequestHeaderMap& headers,
                                      const StreamInfo::StreamInfo& stream_info,
                                      uint64_t random_value) const {
  return route_matcher_->route(cb, headers, stream_info, random_value);
}

namespace {

RouteSpecificFilterConfigConstSharedPtr
createRouteSpecificFilterConfig(const std::string& name, const ProtobufWkt::Any& typed_config,
                                const ProtobufWkt::Struct& config,
                                Server::Configuration::ServerFactoryContext& factory_context,
                                ProtobufMessage::ValidationVisitor& validator) {
  auto& factory = Envoy::Config::Utility::getAndCheckFactoryByName<
      Server::Configuration::NamedHttpFilterConfigFactory>(name);
  ProtobufTypes::MessagePtr proto_config = factory.createEmptyRouteConfigProto();
  Envoy::Config::Utility::translateOpaqueConfig(typed_config, config, validator, *proto_config);
  return factory.createRouteSpecificFilterConfig(*proto_config, factory_context, validator);
}

} // namespace

PerFilterConfigs::PerFilterConfigs(
    const Protobuf::Map<std::string, ProtobufWkt::Any>& typed_configs,
    const Protobuf::Map<std::string, ProtobufWkt::Struct>& configs,
    Server::Configuration::ServerFactoryContext& factory_context,
    ProtobufMessage::ValidationVisitor& validator) {
  if (!typed_configs.empty() && !configs.empty()) {
    throw EnvoyException("Only one of typed_configs or configs can be specified");
  }

  for (const auto& it : typed_configs) {
    // TODO(zuercher): canonicalization may be removed when deprecated filter names are removed
    const auto& name =
        Extensions::HttpFilters::Common::FilterNameUtil::canonicalFilterName(it.first);

    auto object = createRouteSpecificFilterConfig(
        name, it.second, ProtobufWkt::Struct::default_instance(), factory_context, validator);
    if (object != nullptr) {
      configs_[name] = std::move(object);
    }
  }

  for (const auto& it : configs) {
    // TODO(zuercher): canonicalization may be removed when deprecated filter names are removed
    const auto& name =
        Extensions::HttpFilters::Common::FilterNameUtil::canonicalFilterName(it.first);

    auto object = createRouteSpecificFilterConfig(name, ProtobufWkt::Any::default_instance(),
                                                  it.second, factory_context, validator);
    if (object != nullptr) {
      configs_[name] = std::move(object);
    }
  }
}

const RouteSpecificFilterConfig* PerFilterConfigs::get(const std::string& name) const {
  auto it = configs_.find(name);
  return it == configs_.end() ? nullptr : it->second.get();
}

} // namespace Router
} // namespace Envoy
