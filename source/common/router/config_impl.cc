#include "source/common/router/config_impl.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "envoy/config/common/matcher/v3/matcher.pb.h"
#include "envoy/config/common/matcher/v3/matcher.pb.validate.h"
#include "envoy/config/core/v3/base.pb.h"
#include "envoy/config/route/v3/route.pb.h"
#include "envoy/config/route/v3/route_components.pb.h"
#include "envoy/http/header_map.h"
#include "envoy/runtime/runtime.h"
#include "envoy/type/matcher/v3/http_inputs.pb.h"
#include "envoy/type/matcher/v3/http_inputs.pb.validate.h"
#include "envoy/type/matcher/v3/string.pb.h"
#include "envoy/type/v3/percent.pb.h"
#include "envoy/upstream/cluster_manager.h"
#include "envoy/upstream/upstream.h"

#include "source/common/common/assert.h"
#include "source/common/common/empty_string.h"
#include "source/common/common/fmt.h"
#include "source/common/common/hash.h"
#include "source/common/common/logger.h"
#include "source/common/common/regex.h"
#include "source/common/common/utility.h"
#include "source/common/config/metadata.h"
#include "source/common/config/utility.h"
#include "source/common/config/well_known_names.h"
#include "source/common/grpc/common.h"
#include "source/common/http/header_utility.h"
#include "source/common/http/headers.h"
#include "source/common/http/matching/data_impl.h"
#include "source/common/http/path_utility.h"
#include "source/common/http/utility.h"
#include "source/common/matcher/matcher.h"
#include "source/common/protobuf/protobuf.h"
#include "source/common/protobuf/utility.h"
#include "source/common/router/context_impl.h"
#include "source/common/router/reset_header_parser.h"
#include "source/common/router/retry_state_impl.h"
#include "source/common/runtime/runtime_features.h"
#include "source/common/tracing/custom_tag_impl.h"
#include "source/common/tracing/http_tracer_impl.h"
#include "source/common/upstream/retry_factory.h"
#include "source/extensions/early_data/default_early_data_policy.h"
#include "source/extensions/matching/network/common/inputs.h"
#include "source/extensions/path/match/uri_template/uri_template_match.h"
#include "source/extensions/path/rewrite/uri_template/uri_template_rewrite.h"

#include "absl/strings/match.h"

namespace Envoy {
namespace Router {
class RouteCreator {
public:
  static absl::StatusOr<RouteEntryImplBaseConstSharedPtr> createAndValidateRoute(
      const envoy::config::route::v3::Route& route_config, const CommonVirtualHostSharedPtr& vhost,
      Server::Configuration::ServerFactoryContext& factory_context,
      ProtobufMessage::ValidationVisitor& validator,
      const absl::optional<Upstream::ClusterManager::ClusterInfoMaps>& validation_clusters) {

    absl::Status creation_status = absl::OkStatus();
    RouteEntryImplBaseConstSharedPtr route;
    switch (route_config.match().path_specifier_case()) {
    case envoy::config::route::v3::RouteMatch::PathSpecifierCase::kPrefix:
      route.reset(new PrefixRouteEntryImpl(vhost, route_config, factory_context, validator,
                                           creation_status));
      break;
    case envoy::config::route::v3::RouteMatch::PathSpecifierCase::kPath:
      route.reset(
          new PathRouteEntryImpl(vhost, route_config, factory_context, validator, creation_status));
      break;
    case envoy::config::route::v3::RouteMatch::PathSpecifierCase::kSafeRegex:
      route.reset(new RegexRouteEntryImpl(vhost, route_config, factory_context, validator,
                                          creation_status));
      break;
    case envoy::config::route::v3::RouteMatch::PathSpecifierCase::kConnectMatcher:
      route.reset(new ConnectRouteEntryImpl(vhost, route_config, factory_context, validator,
                                            creation_status));
      break;
    case envoy::config::route::v3::RouteMatch::PathSpecifierCase::kPathSeparatedPrefix:
      route.reset(new PathSeparatedPrefixRouteEntryImpl(vhost, route_config, factory_context,
                                                        validator, creation_status));
      break;
    case envoy::config::route::v3::RouteMatch::PathSpecifierCase::kPathMatchPolicy:
      route.reset(new UriTemplateMatcherRouteEntryImpl(vhost, route_config, factory_context,
                                                       validator, creation_status));
      break;
    case envoy::config::route::v3::RouteMatch::PathSpecifierCase::PATH_SPECIFIER_NOT_SET:
      break; // return the error below.
    }
    if (!route) {
      return absl::InvalidArgumentError("Invalid route config");
    }
    RETURN_IF_NOT_OK(creation_status);

    if (validation_clusters.has_value()) {
      THROW_IF_NOT_OK(route->validateClusters(*validation_clusters));
      for (const auto& shadow_policy : route->shadowPolicies()) {
        if (!shadow_policy->cluster().empty()) {
          ASSERT(shadow_policy->clusterHeader().get().empty());
          if (!validation_clusters->hasCluster(shadow_policy->cluster())) {
            return absl::InvalidArgumentError(
                fmt::format("route: unknown shadow cluster '{}'", shadow_policy->cluster()));
          }
        }
      }
    }

    return route;
  }
};

namespace {

constexpr uint32_t DEFAULT_MAX_DIRECT_RESPONSE_BODY_SIZE_BYTES = 4096;

void mergeTransforms(Http::HeaderTransforms& dest, const Http::HeaderTransforms& src) {
  dest.headers_to_append_or_add.insert(dest.headers_to_append_or_add.end(),
                                       src.headers_to_append_or_add.begin(),
                                       src.headers_to_append_or_add.end());
  dest.headers_to_overwrite_or_add.insert(dest.headers_to_overwrite_or_add.end(),
                                          src.headers_to_overwrite_or_add.begin(),
                                          src.headers_to_overwrite_or_add.end());
  dest.headers_to_add_if_absent.insert(dest.headers_to_add_if_absent.end(),
                                       src.headers_to_add_if_absent.begin(),
                                       src.headers_to_add_if_absent.end());
  dest.headers_to_remove.insert(dest.headers_to_remove.end(), src.headers_to_remove.begin(),
                                src.headers_to_remove.end());
}

class RouteActionValidationVisitor
    : public Matcher::MatchTreeValidationVisitor<Http::HttpMatchingData> {
public:
  absl::Status performDataInputValidation(const Matcher::DataInputFactory<Http::HttpMatchingData>&,
                                          absl::string_view type_url) override {
    static std::string request_header_input_name = TypeUtil::descriptorFullNameToTypeUrl(
        createReflectableMessage(
            envoy::type::matcher::v3::HttpRequestHeaderMatchInput::default_instance())
            ->GetDescriptor()
            ->full_name());
    static std::string filter_state_input_name = TypeUtil::descriptorFullNameToTypeUrl(
        createReflectableMessage(envoy::extensions::matching::common_inputs::network::v3::
                                     FilterStateInput::default_instance())
            ->GetDescriptor()
            ->full_name());
    if (type_url == request_header_input_name || type_url == filter_state_input_name) {
      return absl::OkStatus();
    }

    return absl::InvalidArgumentError(
        fmt::format("Route table can only match on request headers, saw {}", type_url));
  }
};

const envoy::config::route::v3::WeightedCluster::ClusterWeight& validateWeightedClusterSpecifier(
    const envoy::config::route::v3::WeightedCluster::ClusterWeight& cluster) {
  if (!cluster.name().empty() && !cluster.cluster_header().empty()) {
    throwEnvoyExceptionOrPanic("Only one of name or cluster_header can be specified");
  } else if (cluster.name().empty() && cluster.cluster_header().empty()) {
    throwEnvoyExceptionOrPanic("At least one of name or cluster_header need to be specified");
  }
  return cluster;
}

// Returns a vector of header parsers, sorted by specificity. The `specificity_ascend` parameter
// specifies whether the returned parsers will be sorted from least specific to most specific
// (global connection manager level header parser, virtual host level header parser and finally
// route-level parser.) or the reverse.
absl::InlinedVector<const HeaderParser*, 3>
getHeaderParsers(const HeaderParser* global_route_config_header_parser,
                 const HeaderParser* vhost_header_parser, const HeaderParser* route_header_parser,
                 bool specificity_ascend) {
  if (specificity_ascend) {
    // Sorted from least to most specific: global connection manager level headers, virtual host
    // level headers and finally route-level headers.
    return {global_route_config_header_parser, vhost_header_parser, route_header_parser};
  } else {
    // Sorted from most to least specific.
    return {route_header_parser, vhost_header_parser, global_route_config_header_parser};
  }
}

// If the implementation of a cluster specifier plugin is not provided in current Envoy and the
// plugin is set to optional, then this null plugin will be used as a placeholder.
class NullClusterSpecifierPlugin : public ClusterSpecifierPlugin {
public:
  RouteConstSharedPtr route(RouteConstSharedPtr, const Http::RequestHeaderMap&) const override {
    return nullptr;
  }
};

absl::StatusOr<ClusterSpecifierPluginSharedPtr>
getClusterSpecifierPluginByTheProto(const envoy::config::route::v3::ClusterSpecifierPlugin& plugin,
                                    ProtobufMessage::ValidationVisitor& validator,
                                    Server::Configuration::ServerFactoryContext& factory_context) {
  auto* factory =
      Envoy::Config::Utility::getFactory<ClusterSpecifierPluginFactoryConfig>(plugin.extension());
  if (factory == nullptr) {
    if (plugin.is_optional()) {
      return std::make_shared<NullClusterSpecifierPlugin>();
    }
    return absl::InvalidArgumentError(
        fmt::format("Didn't find a registered implementation for '{}' with type URL: '{}'",
                    plugin.extension().name(),
                    Envoy::Config::Utility::getFactoryType(plugin.extension().typed_config())));
  }
  ASSERT(factory != nullptr);
  auto config =
      Envoy::Config::Utility::translateToFactoryConfig(plugin.extension(), validator, *factory);
  return factory->createClusterSpecifierPlugin(*config, factory_context);
}

::Envoy::Http::Utility::RedirectConfig
createRedirectConfig(const envoy::config::route::v3::Route& route, Regex::Engine& regex_engine) {
  ::Envoy::Http::Utility::RedirectConfig redirect_config{
      route.redirect().scheme_redirect(),
      route.redirect().host_redirect(),
      route.redirect().port_redirect() ? ":" + std::to_string(route.redirect().port_redirect())
                                       : "",
      route.redirect().path_redirect(),
      route.redirect().prefix_rewrite(),
      route.redirect().has_regex_rewrite() ? route.redirect().regex_rewrite().substitution() : "",
      route.redirect().has_regex_rewrite()
          ? Regex::Utility::parseRegex(route.redirect().regex_rewrite().pattern(), regex_engine)
          : nullptr,
      route.redirect().path_redirect().find('?') != absl::string_view::npos,
      route.redirect().https_redirect(),
      route.redirect().strip_query()};
  if (route.redirect().has_regex_rewrite()) {
    ASSERT(redirect_config.prefix_rewrite_redirect_.empty());
  }
  return redirect_config;
}

} // namespace

const std::string& OriginalConnectPort::key() {
  CONSTRUCT_ON_FIRST_USE(std::string, "envoy.router.original_connect_port");
}

std::string SslRedirector::newUri(const Http::RequestHeaderMap& headers) const {
  return Http::Utility::createSslRedirectPath(headers);
}

HedgePolicyImpl::HedgePolicyImpl(const envoy::config::route::v3::HedgePolicy& hedge_policy)
    : initial_requests_(PROTOBUF_GET_WRAPPED_OR_DEFAULT(hedge_policy, initial_requests, 1)),
      additional_request_chance_(hedge_policy.additional_request_chance()),
      hedge_on_per_try_timeout_(hedge_policy.hedge_on_per_try_timeout()) {}

HedgePolicyImpl::HedgePolicyImpl() : initial_requests_(1), hedge_on_per_try_timeout_(false) {}

absl::StatusOr<std::unique_ptr<RetryPolicyImpl>>
RetryPolicyImpl::create(const envoy::config::route::v3::RetryPolicy& retry_policy,
                        ProtobufMessage::ValidationVisitor& validation_visitor,
                        Upstream::RetryExtensionFactoryContext& factory_context,
                        Server::Configuration::CommonFactoryContext& common_context) {
  return std::unique_ptr<RetryPolicyImpl>(
      new RetryPolicyImpl(retry_policy, validation_visitor, factory_context, common_context));
}
RetryPolicyImpl::RetryPolicyImpl(const envoy::config::route::v3::RetryPolicy& retry_policy,
                                 ProtobufMessage::ValidationVisitor& validation_visitor,
                                 Upstream::RetryExtensionFactoryContext& factory_context,
                                 Server::Configuration::CommonFactoryContext& common_context)
    : retriable_headers_(Http::HeaderUtility::buildHeaderMatcherVector(
          retry_policy.retriable_headers(), common_context)),
      retriable_request_headers_(Http::HeaderUtility::buildHeaderMatcherVector(
          retry_policy.retriable_request_headers(), common_context)),
      validation_visitor_(&validation_visitor) {
  per_try_timeout_ =
      std::chrono::milliseconds(PROTOBUF_GET_MS_OR_DEFAULT(retry_policy, per_try_timeout, 0));
  per_try_idle_timeout_ =
      std::chrono::milliseconds(PROTOBUF_GET_MS_OR_DEFAULT(retry_policy, per_try_idle_timeout, 0));
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

  for (const auto& options_predicate : retry_policy.retry_options_predicates()) {
    auto& factory =
        Envoy::Config::Utility::getAndCheckFactory<Upstream::RetryOptionsPredicateFactory>(
            options_predicate);
    retry_options_predicates_.emplace_back(
        factory.createOptionsPredicate(*Envoy::Config::Utility::translateToFactoryConfig(
                                           options_predicate, validation_visitor, factory),
                                       factory_context));
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
        throwEnvoyExceptionOrPanic(
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
  predicates.reserve(retry_host_predicate_configs_.size());
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
    Envoy::Config::Utility::translateOpaqueConfig(predicate.typed_config(), validator, *config);
    predicate_factories_.emplace_back(&factory, std::move(config));
  }
  for (const auto& header : policy_config.response_headers_to_copy()) {
    if (!Http::HeaderUtility::isModifiableHeader(header)) {
      throwEnvoyExceptionOrPanic(":-prefixed headers or Hosts may not be specified here.");
    }
    response_headers_to_copy_.emplace_back(header);
  }
}

std::vector<InternalRedirectPredicateSharedPtr> InternalRedirectPolicyImpl::predicates() const {
  std::vector<InternalRedirectPredicateSharedPtr> predicates;
  predicates.reserve(predicate_factories_.size());
  for (const auto& predicate_factory : predicate_factories_) {
    predicates.emplace_back(predicate_factory.first->createInternalRedirectPredicate(
        *predicate_factory.second, current_route_name_));
  }
  return predicates;
}

const std::vector<Http::LowerCaseString>&
InternalRedirectPolicyImpl::responseHeadersToCopy() const {
  return response_headers_to_copy_;
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

absl::Status validateMirrorClusterSpecifier(
    const envoy::config::route::v3::RouteAction::RequestMirrorPolicy& config) {
  if (!config.cluster().empty() && !config.cluster_header().empty()) {
    return absl::InvalidArgumentError(fmt::format("Only one of cluster '{}' or cluster_header '{}' "
                                                  "in request mirror policy can be specified",
                                                  config.cluster(), config.cluster_header()));
  } else if (config.cluster().empty() && config.cluster_header().empty()) {
    // For shadow policies with `cluster_header_`, we only verify that this field is not
    // empty because the cluster name is not set yet at config time.
    return absl::InvalidArgumentError(
        "Exactly one of cluster or cluster_header in request mirror policy need to be specified");
  }
  return absl::OkStatus();
}

ShadowPolicyImpl::ShadowPolicyImpl(const RequestMirrorPolicy& config)
    : cluster_(config.cluster()), cluster_header_(config.cluster_header()),
      disable_shadow_host_suffix_append_(config.disable_shadow_host_suffix_append()) {
  THROW_IF_NOT_OK(validateMirrorClusterSpecifier(config));

  if (config.has_runtime_fraction()) {
    runtime_key_ = config.runtime_fraction().runtime_key();
    default_value_ = config.runtime_fraction().default_value();
  } else {
    // If there is no runtime fraction specified, the default is 100% sampled. By leaving
    // runtime_key_ empty and forcing the default to 100% this will yield the expected behavior.
    default_value_.set_numerator(100);
    default_value_.set_denominator(envoy::type::v3::FractionalPercent::HUNDRED);
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
    custom_tags_.emplace(tag.tag(), Tracing::CustomTagUtility::createCustomTag(tag));
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

RouteEntryImplBase::RouteEntryImplBase(const CommonVirtualHostSharedPtr& vhost,
                                       const envoy::config::route::v3::Route& route,
                                       Server::Configuration::ServerFactoryContext& factory_context,
                                       ProtobufMessage::ValidationVisitor& validator,
                                       absl::Status& creation_status)
    : prefix_rewrite_(route.route().prefix_rewrite()),
      path_matcher_(
          THROW_OR_RETURN_VALUE(buildPathMatcher(route, validator), PathMatcherSharedPtr)),
      path_rewriter_(
          THROW_OR_RETURN_VALUE(buildPathRewriter(route, validator), PathRewriterSharedPtr)),
      host_rewrite_(route.route().host_rewrite_literal()), vhost_(vhost),
      auto_host_rewrite_header_(!route.route().host_rewrite_header().empty()
                                    ? absl::optional<Http::LowerCaseString>(Http::LowerCaseString(
                                          route.route().host_rewrite_header()))
                                    : absl::nullopt),
      host_rewrite_path_regex_(
          route.route().has_host_rewrite_path_regex()
              ? Regex::Utility::parseRegex(route.route().host_rewrite_path_regex().pattern(),
                                           factory_context.regexEngine())
              : nullptr),
      host_rewrite_path_regex_substitution_(
          route.route().has_host_rewrite_path_regex()
              ? route.route().host_rewrite_path_regex().substitution()
              : ""),
      cluster_name_(route.route().cluster()), cluster_header_name_(route.route().cluster_header()),
      timeout_(PROTOBUF_GET_MS_OR_DEFAULT(route.route(), timeout, DEFAULT_ROUTE_TIMEOUT_MS)),
      optional_timeouts_(buildOptionalTimeouts(route.route())), loader_(factory_context.runtime()),
      runtime_(loadRuntimeData(route.match())),
      redirect_config_(route.has_redirect()
                           ? std::make_unique<::Envoy::Http::Utility::RedirectConfig>(
                                 createRedirectConfig(route, factory_context.regexEngine()))
                           : nullptr),
      hedge_policy_(buildHedgePolicy(vhost->hedgePolicy(), route.route())),
      retry_policy_(
          buildRetryPolicy(vhost->retryPolicy(), route.route(), validator, factory_context)),
      internal_redirect_policy_(
          buildInternalRedirectPolicy(route.route(), validator, route.name())),
      config_headers_(
          Http::HeaderUtility::buildHeaderDataVector(route.match().headers(), factory_context)),
      dynamic_metadata_([&]() {
        std::vector<Envoy::Matchers::MetadataMatcher> vec;
        for (const auto& elt : route.match().dynamic_metadata()) {
          vec.emplace_back(elt, factory_context);
        }
        return vec;
      }()),
      opaque_config_(parseOpaqueConfig(route)), decorator_(parseDecorator(route)),
      route_tracing_(parseRouteTracing(route)),
      per_filter_configs_(route.typed_per_filter_config(), factory_context, validator),
      route_name_(route.name()), time_source_(factory_context.mainThreadDispatcher().timeSource()),
      retry_shadow_buffer_limit_(PROTOBUF_GET_WRAPPED_OR_DEFAULT(
          route, per_request_buffer_limit_bytes, vhost->retryShadowBufferLimit())),
      direct_response_code_(ConfigUtility::parseDirectResponseCode(route)),
      cluster_not_found_response_code_(ConfigUtility::parseClusterNotFoundResponseCode(
          route.route().cluster_not_found_response_code())),
      priority_(ConfigUtility::parsePriority(route.route().priority())),
      auto_host_rewrite_(PROTOBUF_GET_WRAPPED_OR_DEFAULT(route.route(), auto_host_rewrite, false)),
      append_xfh_(route.route().append_x_forwarded_host()),
      using_new_timeouts_(route.route().has_max_stream_duration()),
      match_grpc_(route.match().has_grpc()),
      case_sensitive_(PROTOBUF_GET_WRAPPED_OR_DEFAULT(route.match(), case_sensitive, true)) {
  auto body_or_error = ConfigUtility::parseDirectResponseBody(
      route, factory_context.api(), vhost_->globalRouteConfig().maxDirectResponseBodySizeBytes());
  SET_AND_RETURN_IF_NOT_OK(body_or_error.status(), creation_status);
  direct_response_body_ = body_or_error.value();
  if (!route.request_headers_to_add().empty() || !route.request_headers_to_remove().empty()) {
    request_headers_parser_ = THROW_OR_RETURN_VALUE(
        HeaderParser::configure(route.request_headers_to_add(), route.request_headers_to_remove()),
        Router::HeaderParserPtr);
  }
  if (!route.response_headers_to_add().empty() || !route.response_headers_to_remove().empty()) {
    response_headers_parser_ =
        THROW_OR_RETURN_VALUE(HeaderParser::configure(route.response_headers_to_add(),
                                                      route.response_headers_to_remove()),
                              Router::HeaderParserPtr);
  }
  if (route.has_metadata()) {
    metadata_ = std::make_unique<RouteMetadataPack>(route.metadata());
  }
  if (route.route().has_metadata_match()) {
    const auto filter_it = route.route().metadata_match().filter_metadata().find(
        Envoy::Config::MetadataFilters::get().ENVOY_LB);
    if (filter_it != route.route().metadata_match().filter_metadata().end()) {
      metadata_match_criteria_ = std::make_unique<MetadataMatchCriteriaImpl>(filter_it->second);
    }
  }

  shadow_policies_.reserve(route.route().request_mirror_policies().size());
  for (const auto& mirror_policy_config : route.route().request_mirror_policies()) {
    shadow_policies_.push_back(std::make_shared<ShadowPolicyImpl>(mirror_policy_config));
  }

  // Inherit policies from the virtual host, which might be from the route config.
  if (shadow_policies_.empty()) {
    shadow_policies_ = vhost->shadowPolicies();
  }

  // If this is a weighted_cluster, we create N internal route entries
  // (called WeightedClusterEntry), such that each object is a simple
  // single cluster, pointing back to the parent. Metadata criteria
  // from the weighted cluster (if any) are merged with and override
  // the criteria from the route.
  if (route.route().cluster_specifier_case() ==
      envoy::config::route::v3::RouteAction::ClusterSpecifierCase::kWeightedClusters) {
    uint64_t total_weight = 0UL;
    const std::string& runtime_key_prefix = route.route().weighted_clusters().runtime_key_prefix();

    std::vector<WeightedClusterEntrySharedPtr> weighted_clusters;
    weighted_clusters.reserve(route.route().weighted_clusters().clusters().size());
    for (const auto& cluster : route.route().weighted_clusters().clusters()) {
      auto cluster_entry = std::make_unique<WeightedClusterEntry>(
          this, runtime_key_prefix + "." + cluster.name(), factory_context, validator, cluster);
      weighted_clusters.emplace_back(std::move(cluster_entry));
      total_weight += weighted_clusters.back()->clusterWeight();
      if (total_weight > std::numeric_limits<uint32_t>::max()) {
        creation_status = absl::InvalidArgumentError(
            fmt::format("The sum of weights of all weighted clusters of route {} exceeds {}",
                        route_name_, std::numeric_limits<uint32_t>::max()));
        return;
      }
    }

    // Reject the config if the total_weight of all clusters is 0.
    if (total_weight == 0) {
      creation_status = absl::InvalidArgumentError(
          "Sum of weights in the weighted_cluster must be greater than 0.");
      return;
    }

    weighted_clusters_config_ =
        std::make_unique<WeightedClustersConfig>(std::move(weighted_clusters), total_weight,
                                                 route.route().weighted_clusters().header_name());

  } else if (route.route().cluster_specifier_case() ==
             envoy::config::route::v3::RouteAction::ClusterSpecifierCase::
                 kInlineClusterSpecifierPlugin) {
    cluster_specifier_plugin_ = THROW_OR_RETURN_VALUE(
        getClusterSpecifierPluginByTheProto(route.route().inline_cluster_specifier_plugin(),
                                            validator, factory_context),
        ClusterSpecifierPluginSharedPtr);
  } else if (route.route().has_cluster_specifier_plugin()) {
    cluster_specifier_plugin_ =
        THROW_OR_RETURN_VALUE(vhost_->globalRouteConfig().clusterSpecifierPlugin(
                                  route.route().cluster_specifier_plugin()),
                              ClusterSpecifierPluginSharedPtr);
  }

  for (const auto& query_parameter : route.match().query_parameters()) {
    config_query_parameters_.push_back(
        std::make_unique<ConfigUtility::QueryParameterMatcher>(query_parameter, factory_context));
  }

  if (!route.route().hash_policy().empty()) {
    hash_policy_ = std::make_unique<Http::HashPolicyImpl>(route.route().hash_policy(),
                                                          factory_context.regexEngine());
  }

  if (route.match().has_tls_context()) {
    tls_context_match_criteria_ =
        std::make_unique<TlsContextMatchCriteriaImpl>(route.match().tls_context());
  }

  if (!route.route().rate_limits().empty()) {
    rate_limit_policy_ = std::make_unique<RateLimitPolicyImpl>(route.route().rate_limits(),
                                                               factory_context, creation_status);
    if (!creation_status.ok()) {
      return;
    }
  }

  // Returns true if include_vh_rate_limits is explicitly set to true otherwise it defaults to false
  // which is similar to VhRateLimitOptions::Override and will only use virtual host rate limits if
  // the route is empty
  include_vh_rate_limits_ =
      PROTOBUF_GET_WRAPPED_OR_DEFAULT(route.route(), include_vh_rate_limits, false);

  if (route.route().has_cors()) {
    cors_policy_ = std::make_unique<CorsPolicyImpl>(route.route().cors(), factory_context);
  }
  for (const auto& upgrade_config : route.route().upgrade_configs()) {
    const bool enabled = upgrade_config.has_enabled() ? upgrade_config.enabled().value() : true;
    const bool success =
        upgrade_map_
            .emplace(std::make_pair(
                Envoy::Http::LowerCaseString(upgrade_config.upgrade_type()).get(), enabled))
            .second;
    if (!success) {
      creation_status = absl::InvalidArgumentError(
          absl::StrCat("Duplicate upgrade ", upgrade_config.upgrade_type()));
      return;
    }
    if (absl::EqualsIgnoreCase(upgrade_config.upgrade_type(),
                               Http::Headers::get().MethodValues.Connect) ||
        (Runtime::runtimeFeatureEnabled("envoy.reloadable_features.enable_connect_udp_support") &&
         absl::EqualsIgnoreCase(upgrade_config.upgrade_type(),
                                Http::Headers::get().UpgradeValues.ConnectUdp))) {
      if (Runtime::runtimeFeatureEnabled(
              "envoy.reloadable_features.http_route_connect_proxy_by_default")) {
        if (upgrade_config.has_connect_config()) {
          connect_config_ = std::make_unique<ConnectConfig>(upgrade_config.connect_config());
        }
      } else {
        connect_config_ = std::make_unique<ConnectConfig>(upgrade_config.connect_config());
      }
    } else if (upgrade_config.has_connect_config()) {
      creation_status = absl::InvalidArgumentError(absl::StrCat(
          "Non-CONNECT upgrade type ", upgrade_config.upgrade_type(), " has ConnectConfig"));
      return;
    }
  }

  int num_rewrite_polices = 0;
  if (path_rewriter_ != nullptr) {
    ++num_rewrite_polices;
  }

  if (!prefix_rewrite_.empty()) {
    ++num_rewrite_polices;
  }

  if (route.route().has_regex_rewrite()) {
    ++num_rewrite_polices;
  }

  if (num_rewrite_polices > 1) {
    creation_status = absl::InvalidArgumentError(
        "Specify only one of prefix_rewrite, regex_rewrite or path_rewrite_policy");
    return;
  }

  if (!prefix_rewrite_.empty() && path_matcher_ != nullptr) {
    creation_status =
        absl::InvalidArgumentError("Cannot use prefix_rewrite with matcher extension");
    return;
  }

  if (route.route().has_regex_rewrite()) {
    auto rewrite_spec = route.route().regex_rewrite();
    regex_rewrite_ =
        Regex::Utility::parseRegex(rewrite_spec.pattern(), factory_context.regexEngine());
    regex_rewrite_substitution_ = rewrite_spec.substitution();
  }

  if (path_rewriter_ != nullptr) {
    SET_AND_RETURN_IF_NOT_OK(path_rewriter_->isCompatiblePathMatcher(path_matcher_),
                             creation_status);
  }

  if (redirect_config_ != nullptr && redirect_config_->path_redirect_has_query_ &&
      redirect_config_->strip_query_) {
    ENVOY_LOG(warn,
              "`strip_query` is set to true, but `path_redirect` contains query string and it will "
              "not be stripped: {}",
              redirect_config_->path_redirect_);
  }
  if (!route.stat_prefix().empty()) {
    route_stats_context_ = std::make_unique<RouteStatsContextImpl>(
        factory_context.scope(), factory_context.routerContext().routeStatNames(),
        vhost->statName(), route.stat_prefix());
  }

  if (route.route().has_early_data_policy()) {
    auto& factory = Envoy::Config::Utility::getAndCheckFactory<EarlyDataPolicyFactory>(
        route.route().early_data_policy());
    auto message = Envoy::Config::Utility::translateToFactoryConfig(
        route.route().early_data_policy(), validator, factory);
    early_data_policy_ = factory.createEarlyDataPolicy(*message);
  } else {
    early_data_policy_ = std::make_unique<DefaultEarlyDataPolicy>(/*allow_safe_request*/ true);
  }
}

bool RouteEntryImplBase::evaluateRuntimeMatch(const uint64_t random_value) const {
  return runtime_ == nullptr
             ? true
             : loader_.snapshot().featureEnabled(runtime_->fractional_runtime_key_,
                                                 runtime_->fractional_runtime_default_,
                                                 random_value);
}

absl::string_view
RouteEntryImplBase::sanitizePathBeforePathMatching(const absl::string_view path) const {
  absl::string_view ret = path;
  if (vhost_->globalRouteConfig().ignorePathParametersInPathMatching()) {
    auto pos = ret.find_first_of(';');
    if (pos != absl::string_view::npos) {
      ret.remove_suffix(ret.length() - pos);
    }
  }
  return ret;
}

bool RouteEntryImplBase::evaluateTlsContextMatch(const StreamInfo::StreamInfo& stream_info) const {
  bool matches = true;

  if (!tlsContextMatchCriteria()) {
    return matches;
  }

  const TlsContextMatchCriteria& criteria = *tlsContextMatchCriteria();

  if (criteria.presented().has_value()) {
    const bool peer_presented =
        stream_info.downstreamAddressProvider().sslConnection() &&
        stream_info.downstreamAddressProvider().sslConnection()->peerCertificatePresented();
    matches &= criteria.presented().value() == peer_presented;
  }

  if (criteria.validated().has_value()) {
    const bool peer_validated =
        stream_info.downstreamAddressProvider().sslConnection() &&
        stream_info.downstreamAddressProvider().sslConnection()->peerCertificateValidated();
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
    if (!matches) {
      return false;
    }
  }

  matches &= Http::HeaderUtility::matchHeaders(headers, config_headers_);
  if (!matches) {
    return false;
  }
  if (!config_query_parameters_.empty()) {
    auto query_parameters =
        Http::Utility::QueryParamsMulti::parseQueryString(headers.getPathValue());
    matches &= ConfigUtility::matchQueryParams(query_parameters, config_query_parameters_);
    if (!matches) {
      return false;
    }
  }

  matches &= evaluateTlsContextMatch(stream_info);

  for (const auto& m : dynamic_metadata_) {
    if (!matches) {
      // No need to check anymore as all dynamic metadata matchers must match for a match to occur.
      break;
    }
    matches &= m.match(stream_info.dynamicMetadata());
  }

  return matches;
}

const std::string& RouteEntryImplBase::clusterName() const { return cluster_name_; }

void RouteEntryImplBase::finalizeRequestHeaders(Http::RequestHeaderMap& headers,
                                                const StreamInfo::StreamInfo& stream_info,
                                                bool insert_envoy_original_path) const {
  for (const HeaderParser* header_parser : getRequestHeaderParsers(
           /*specificity_ascend=*/vhost_->globalRouteConfig().mostSpecificHeaderMutationsWins())) {
    // Later evaluated header parser wins.
    header_parser->evaluateHeaders(headers, stream_info);
  }

  // Restore the port if this was a CONNECT request.
  // Note this will restore the port for HTTP/2 CONNECT-upgrades as well as as HTTP/1.1 style
  // CONNECT requests.
  if (Http::HeaderUtility::getPortStart(headers.getHostValue()) == absl::string_view::npos) {
    if (auto typed_state = stream_info.filterState().getDataReadOnly<OriginalConnectPort>(
            OriginalConnectPort::key());
        typed_state != nullptr) {
      headers.setHost(absl::StrCat(headers.getHostValue(), ":", typed_state->value()));
    }
  }

  if (!host_rewrite_.empty()) {
    Http::Utility::updateAuthority(headers, host_rewrite_, append_xfh_);
  } else if (auto_host_rewrite_header_) {
    const auto header = headers.get(*auto_host_rewrite_header_);
    if (!header.empty()) {
      // This is an implicitly untrusted header, so per the API documentation only the first
      // value is used.
      const absl::string_view header_value = header[0]->value().getStringView();
      if (!header_value.empty()) {
        Http::Utility::updateAuthority(headers, header_value, append_xfh_);
      }
    }
  } else if (host_rewrite_path_regex_ != nullptr) {
    const std::string path(headers.getPathValue());
    absl::string_view just_path(Http::PathUtil::removeQueryAndFragment(path));
    Http::Utility::updateAuthority(
        headers,
        host_rewrite_path_regex_->replaceAll(just_path, host_rewrite_path_regex_substitution_),
        append_xfh_);
  }

  // Handle path rewrite
  absl::optional<std::string> container;
  if (!getPathRewrite(headers, container).empty() || regex_rewrite_ != nullptr ||
      path_rewriter_ != nullptr) {
    rewritePathHeader(headers, insert_envoy_original_path);
  }
}

void RouteEntryImplBase::finalizeResponseHeaders(Http::ResponseHeaderMap& headers,
                                                 const StreamInfo::StreamInfo& stream_info) const {
  for (const HeaderParser* header_parser : getResponseHeaderParsers(
           /*specificity_ascend=*/vhost_->globalRouteConfig().mostSpecificHeaderMutationsWins())) {
    // Later evaluated header parser wins.
    header_parser->evaluateHeaders(headers, {stream_info.getRequestHeaders(), &headers},
                                   stream_info);
  }
}

Http::HeaderTransforms
RouteEntryImplBase::responseHeaderTransforms(const StreamInfo::StreamInfo& stream_info,
                                             bool do_formatting) const {
  Http::HeaderTransforms transforms;
  for (const HeaderParser* header_parser : getResponseHeaderParsers(
           /*specificity_ascend=*/vhost_->globalRouteConfig().mostSpecificHeaderMutationsWins())) {
    // Later evaluated header parser wins.
    mergeTransforms(transforms, header_parser->getHeaderTransforms(stream_info, do_formatting));
  }
  return transforms;
}

Http::HeaderTransforms
RouteEntryImplBase::requestHeaderTransforms(const StreamInfo::StreamInfo& stream_info,
                                            bool do_formatting) const {
  Http::HeaderTransforms transforms;
  for (const HeaderParser* header_parser : getRequestHeaderParsers(
           /*specificity_ascend=*/vhost_->globalRouteConfig().mostSpecificHeaderMutationsWins())) {
    // Later evaluated header parser wins.
    mergeTransforms(transforms, header_parser->getHeaderTransforms(stream_info, do_formatting));
  }
  return transforms;
}

absl::InlinedVector<const HeaderParser*, 3>
RouteEntryImplBase::getRequestHeaderParsers(bool specificity_ascend) const {
  return getHeaderParsers(&vhost_->globalRouteConfig().requestHeaderParser(),
                          &vhost_->requestHeaderParser(), &requestHeaderParser(),
                          specificity_ascend);
}

absl::InlinedVector<const HeaderParser*, 3>
RouteEntryImplBase::getResponseHeaderParsers(bool specificity_ascend) const {
  return getHeaderParsers(&vhost_->globalRouteConfig().responseHeaderParser(),
                          &vhost_->responseHeaderParser(), &responseHeaderParser(),
                          specificity_ascend);
}

std::unique_ptr<const RouteEntryImplBase::RuntimeData>
RouteEntryImplBase::loadRuntimeData(const envoy::config::route::v3::RouteMatch& route_match) {
  if (route_match.has_runtime_fraction()) {
    auto runtime_data = std::make_unique<RouteEntryImplBase::RuntimeData>();
    runtime_data->fractional_runtime_default_ = route_match.runtime_fraction().default_value();
    runtime_data->fractional_runtime_key_ = route_match.runtime_fraction().runtime_key();
    return runtime_data;
  }
  return nullptr;
}

const std::string&
RouteEntryImplBase::getPathRewrite(const Http::RequestHeaderMap& headers,
                                   absl::optional<std::string>& container) const {
  // Just use the prefix rewrite if this isn't a redirect.
  if (!isRedirect()) {
    return prefix_rewrite_;
  }

  // Return the regex rewrite substitution for redirects, if set.
  // redirect_config_ is known to not be nullptr here, because of the isRedirect check above.
  ASSERT(redirect_config_ != nullptr);
  if (redirect_config_->regex_rewrite_redirect_ != nullptr) {
    // Copy just the path and rewrite it using the regex.
    //
    // Store the result in the output container, and return a reference to the underlying string.
    auto just_path(Http::PathUtil::removeQueryAndFragment(headers.getPathValue()));
    container = redirect_config_->regex_rewrite_redirect_->replaceAll(
        just_path, redirect_config_->regex_rewrite_redirect_substitution_);

    return container.value();
  }

  // Otherwise, return the prefix rewrite used for redirects.
  return redirect_config_->prefix_rewrite_redirect_;
}

void RouteEntryImplBase::finalizePathHeader(Http::RequestHeaderMap& headers,
                                            absl::string_view matched_path,
                                            bool insert_envoy_original_path) const {
  absl::optional<std::string> new_path =
      currentUrlPathAfterRewriteWithMatchedPath(headers, matched_path);
  if (!new_path.has_value()) {
    // There are no rewrites configured. Just return.
    return;
  }

  if (insert_envoy_original_path) {
    headers.setEnvoyOriginalPath(headers.getPathValue());
  }

  headers.setPath(new_path.value());
}

// currentUrlPathAfterRewriteWithMatchedPath does the "standard" path rewriting, meaning that it
// handles the "prefix_rewrite" and "regex_rewrite" route actions, only one of
// which can be specified. The "matched_path" argument applies only to the
// prefix rewriting, and describes the portion of the path (excluding query
// parameters) that should be replaced by the rewrite. A "regex_rewrite"
// applies to the entire path (excluding query parameters), regardless of what
// portion was matched.
absl::optional<std::string> RouteEntryImplBase::currentUrlPathAfterRewriteWithMatchedPath(
    const Http::RequestHeaderMap& headers, absl::string_view matched_path) const {
  absl::optional<std::string> container;
  const auto& rewrite = getPathRewrite(headers, container);
  if (rewrite.empty() && regex_rewrite_ == nullptr && path_rewriter_ == nullptr) {
    // There are no rewrites configured.
    return {};
  }

  // TODO(perf): can we avoid the string copy for the common case?
  std::string path(headers.getPathValue());
  if (!rewrite.empty()) {
    if (redirect_config_ != nullptr && redirect_config_->regex_rewrite_redirect_ != nullptr) {
      // As the rewrite constant may contain the result of a regex rewrite for a redirect, we must
      // replace the full path if this is the case. This is because the matched path does not need
      // to correspond to the full path, e.g. in the case of prefix matches.
      auto just_path(Http::PathUtil::removeQueryAndFragment(path));
      return path.replace(0, just_path.size(), rewrite);
    }
    ASSERT(case_sensitive() ? absl::StartsWith(path, matched_path)
                            : absl::StartsWithIgnoreCase(path, matched_path));
    return path.replace(0, matched_path.size(), rewrite);
  }

  if (regex_rewrite_ != nullptr) {
    // Replace the entire path, but preserve the query parameters
    auto just_path(Http::PathUtil::removeQueryAndFragment(path));
    return path.replace(0, just_path.size(),
                        regex_rewrite_->replaceAll(just_path, regex_rewrite_substitution_));
  }

  if (path_rewriter_ != nullptr) {
    absl::string_view just_path(Http::PathUtil::removeQueryAndFragment(headers.getPathValue()));

    absl::StatusOr<std::string> new_path = path_rewriter_->rewritePath(just_path, matched_path);

    // if rewrite fails return old path.
    if (!new_path.ok()) {
      return std::string(headers.getPathValue());
    }
    return path.replace(0, just_path.size(), new_path.value());
  }

  // There are no rewrites configured.
  return {};
}

std::string RouteEntryImplBase::newUri(const Http::RequestHeaderMap& headers) const {
  ASSERT(isDirectResponse());
  return ::Envoy::Http::Utility::newUri(
      ::Envoy::makeOptRefFromPtr(
          const_cast<const ::Envoy::Http::Utility::RedirectConfig*>(redirect_config_.get())),
      headers);
}

std::multimap<std::string, std::string>
RouteEntryImplBase::parseOpaqueConfig(const envoy::config::route::v3::Route& route) {
  std::multimap<std::string, std::string> ret;
  if (route.has_metadata()) {
    auto filter_metadata = route.metadata().filter_metadata().find("envoy.filters.http.router");
    if (filter_metadata == route.metadata().filter_metadata().end()) {
      return ret;
    }
    for (const auto& it : filter_metadata->second.fields()) {
      if (it.second.kind_case() == ProtobufWkt::Value::kStringValue) {
        ret.emplace(it.first, it.second.string_value());
      }
    }
  }
  return ret;
}

std::unique_ptr<HedgePolicyImpl> RouteEntryImplBase::buildHedgePolicy(
    HedgePolicyConstOptRef vhost_hedge_policy,
    const envoy::config::route::v3::RouteAction& route_config) const {
  // Route specific policy wins, if available.
  if (route_config.has_hedge_policy()) {
    return std::make_unique<HedgePolicyImpl>(route_config.hedge_policy());
  }

  // If not, we fall back to the virtual host policy if there is one.
  if (vhost_hedge_policy.has_value()) {
    return std::make_unique<HedgePolicyImpl>(*vhost_hedge_policy);
  }

  // Otherwise, an empty policy will do.
  return nullptr;
}

std::unique_ptr<RetryPolicyImpl> RouteEntryImplBase::buildRetryPolicy(
    RetryPolicyConstOptRef vhost_retry_policy,
    const envoy::config::route::v3::RouteAction& route_config,
    ProtobufMessage::ValidationVisitor& validation_visitor,
    Server::Configuration::ServerFactoryContext& factory_context) const {
  Upstream::RetryExtensionFactoryContextImpl retry_factory_context(
      factory_context.singletonManager());
  // Route specific policy wins, if available.
  if (route_config.has_retry_policy()) {
    return THROW_OR_RETURN_VALUE(RetryPolicyImpl::create(route_config.retry_policy(),
                                                         validation_visitor, retry_factory_context,
                                                         factory_context),
                                 std::unique_ptr<RetryPolicyImpl>);
  }

  // If not, we fallback to the virtual host policy if there is one.
  if (vhost_retry_policy.has_value()) {
    return THROW_OR_RETURN_VALUE(RetryPolicyImpl::create(*vhost_retry_policy, validation_visitor,
                                                         retry_factory_context, factory_context),
                                 std::unique_ptr<RetryPolicyImpl>);
  }

  // Otherwise, an empty policy will do.
  return nullptr;
}

std::unique_ptr<InternalRedirectPolicyImpl> RouteEntryImplBase::buildInternalRedirectPolicy(
    const envoy::config::route::v3::RouteAction& route_config,
    ProtobufMessage::ValidationVisitor& validator, absl::string_view current_route_name) const {
  if (route_config.has_internal_redirect_policy()) {
    return std::make_unique<InternalRedirectPolicyImpl>(route_config.internal_redirect_policy(),
                                                        validator, current_route_name);
  }
  envoy::config::route::v3::InternalRedirectPolicy policy_config;
  switch (route_config.internal_redirect_action()) {
  case envoy::config::route::v3::RouteAction::HANDLE_INTERNAL_REDIRECT:
    break;
  case envoy::config::route::v3::RouteAction::PASS_THROUGH_INTERNAL_REDIRECT:
    FALLTHRU;
  default:
    return nullptr;
  }
  if (route_config.has_max_internal_redirects()) {
    *policy_config.mutable_max_internal_redirects() = route_config.max_internal_redirects();
  }
  return std::make_unique<InternalRedirectPolicyImpl>(policy_config, validator, current_route_name);
}

RouteEntryImplBase::OptionalTimeouts RouteEntryImplBase::buildOptionalTimeouts(
    const envoy::config::route::v3::RouteAction& route) const {
  // Calculate how many values are actually set, to initialize `OptionalTimeouts` packed_struct,
  // avoiding memory re-allocation on each set() call.
  int num_timeouts_set = route.has_idle_timeout() ? 1 : 0;
  num_timeouts_set += route.has_max_grpc_timeout() ? 1 : 0;
  num_timeouts_set += route.has_grpc_timeout_offset() ? 1 : 0;
  if (route.has_max_stream_duration()) {
    num_timeouts_set += route.max_stream_duration().has_max_stream_duration() ? 1 : 0;
    num_timeouts_set += route.max_stream_duration().has_grpc_timeout_header_max() ? 1 : 0;
    num_timeouts_set += route.max_stream_duration().has_grpc_timeout_header_offset() ? 1 : 0;
  }
  OptionalTimeouts timeouts(num_timeouts_set);
  if (route.has_idle_timeout()) {
    timeouts.set<OptionalTimeoutNames::IdleTimeout>(
        std::chrono::milliseconds(PROTOBUF_GET_MS_REQUIRED(route, idle_timeout)));
  }
  if (route.has_max_grpc_timeout()) {
    timeouts.set<OptionalTimeoutNames::MaxGrpcTimeout>(
        std::chrono::milliseconds(PROTOBUF_GET_MS_REQUIRED(route, max_grpc_timeout)));
  }
  if (route.has_grpc_timeout_offset()) {
    timeouts.set<OptionalTimeoutNames::GrpcTimeoutOffset>(
        std::chrono::milliseconds(PROTOBUF_GET_MS_REQUIRED(route, grpc_timeout_offset)));
  }
  if (route.has_max_stream_duration()) {
    if (route.max_stream_duration().has_max_stream_duration()) {
      timeouts.set<OptionalTimeoutNames::MaxStreamDuration>(std::chrono::milliseconds(
          PROTOBUF_GET_MS_REQUIRED(route.max_stream_duration(), max_stream_duration)));
    }
    if (route.max_stream_duration().has_grpc_timeout_header_max()) {
      timeouts.set<OptionalTimeoutNames::GrpcTimeoutHeaderMax>(std::chrono::milliseconds(
          PROTOBUF_GET_MS_REQUIRED(route.max_stream_duration(), grpc_timeout_header_max)));
    }
    if (route.max_stream_duration().has_grpc_timeout_header_offset()) {
      timeouts.set<OptionalTimeoutNames::GrpcTimeoutHeaderOffset>(std::chrono::milliseconds(
          PROTOBUF_GET_MS_REQUIRED(route.max_stream_duration(), grpc_timeout_header_offset)));
    }
  }
  return timeouts;
}

absl::StatusOr<PathRewriterSharedPtr>
RouteEntryImplBase::buildPathRewriter(envoy::config::route::v3::Route route,
                                      ProtobufMessage::ValidationVisitor& validator) const {
  if (!route.route().has_path_rewrite_policy()) {
    return nullptr;
  }

  auto& factory = Envoy::Config::Utility::getAndCheckFactory<PathRewriterFactory>(
      route.route().path_rewrite_policy());

  ProtobufTypes::MessagePtr config = Envoy::Config::Utility::translateAnyToFactoryConfig(
      route.route().path_rewrite_policy().typed_config(), validator, factory);

  absl::StatusOr<PathRewriterSharedPtr> rewriter = factory.createPathRewriter(*config);
  RETURN_IF_STATUS_NOT_OK(rewriter);

  return rewriter.value();
}

absl::StatusOr<PathMatcherSharedPtr>
RouteEntryImplBase::buildPathMatcher(envoy::config::route::v3::Route route,
                                     ProtobufMessage::ValidationVisitor& validator) const {
  if (!route.match().has_path_match_policy()) {
    return nullptr;
  }
  auto& factory = Envoy::Config::Utility::getAndCheckFactory<PathMatcherFactory>(
      route.match().path_match_policy());

  ProtobufTypes::MessagePtr config = Envoy::Config::Utility::translateAnyToFactoryConfig(
      route.match().path_match_policy().typed_config(), validator, factory);

  absl::StatusOr<PathMatcherSharedPtr> matcher = factory.createPathMatcher(*config);
  RETURN_IF_STATUS_NOT_OK(matcher);

  return matcher.value();
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

RouteConstSharedPtr RouteEntryImplBase::pickClusterViaClusterHeader(
    const Http::LowerCaseString& cluster_header_name, const Http::HeaderMap& headers,
    const RouteEntryAndRoute* route_selector_override) const {
  const auto entry = headers.get(cluster_header_name);
  std::string final_cluster_name;
  if (!entry.empty()) {
    // This is an implicitly untrusted header, so per the API documentation only
    // the first value is used.
    final_cluster_name = std::string(entry[0]->value().getStringView());
  }

  return std::make_shared<DynamicRouteEntry>(route_selector_override
                                                 ? route_selector_override
                                                 : static_cast<const RouteEntryAndRoute*>(this),
                                             shared_from_this(), final_cluster_name);
}

RouteConstSharedPtr RouteEntryImplBase::clusterEntry(const Http::RequestHeaderMap& headers,
                                                     uint64_t random_value) const {
  // Gets the route object chosen from the list of weighted clusters
  // (if there is one) or returns self.
  if (weighted_clusters_config_ == nullptr) {
    if (!cluster_name_.empty() || isDirectResponse()) {
      return shared_from_this();
    } else if (!cluster_header_name_.get().empty()) {
      return pickClusterViaClusterHeader(cluster_header_name_, headers,
                                         /*route_selector_override=*/nullptr);
    } else {
      // TODO(wbpcode): make the cluster header or weighted clusters an implementation of the
      // cluster specifier plugin.
      ASSERT(cluster_specifier_plugin_ != nullptr);
      return cluster_specifier_plugin_->route(shared_from_this(), headers);
    }
  }
  return pickWeightedCluster(headers, random_value, true);
}

RouteConstSharedPtr RouteEntryImplBase::pickWeightedCluster(const Http::HeaderMap& headers,
                                                            const uint64_t random_value,
                                                            const bool ignore_overflow) const {
  absl::optional<uint64_t> random_value_from_header;
  // Retrieve the random value from the header if corresponding header name is specified.
  // weighted_clusters_config_ is known not to be nullptr here. If it were, pickWeightedCluster
  // would not be called.
  ASSERT(weighted_clusters_config_ != nullptr);
  if (!weighted_clusters_config_->random_value_header_name_.empty()) {
    const auto header_value = headers.get(
        Envoy::Http::LowerCaseString(weighted_clusters_config_->random_value_header_name_));
    if (!header_value.empty() && header_value.size() == 1) {
      // We expect single-valued header here, otherwise it will potentially cause inconsistent
      // weighted cluster picking throughout the process because different values are used to
      // compute the selected value. So, we treat multi-valued header as invalid input and fall back
      // to use internally generated random number.
      uint64_t random_value = 0;
      if (absl::SimpleAtoi(header_value[0]->value().getStringView(), &random_value)) {
        random_value_from_header = random_value;
      }
    }

    if (!random_value_from_header.has_value()) {
      // Random value should be found here. But if it is not set due to some errors, log the
      // information and fallback to the random value that is set by stream id.
      ENVOY_LOG(debug, "The random value can not be found from the header and it will fall back to "
                       "the value that is set by stream id");
    }
  }

  const uint64_t selected_value =
      (random_value_from_header.has_value() ? random_value_from_header.value() : random_value) %
      weighted_clusters_config_->total_cluster_weight_;
  uint64_t begin = 0;
  uint64_t end = 0;

  // Find the right cluster to route to based on the interval in which
  // the selected value falls. The intervals are determined as
  // [0, cluster1_weight), [cluster1_weight, cluster1_weight+cluster2_weight),..
  for (const WeightedClusterEntrySharedPtr& cluster :
       weighted_clusters_config_->weighted_clusters_) {
    end = begin + cluster->clusterWeight();
    if (!ignore_overflow) {
      // end > total_cluster_weight: This case can only occur with Runtimes,
      // when the user specifies invalid weights such that
      // sum(weights) > total_cluster_weight.
      ASSERT(end <= weighted_clusters_config_->total_cluster_weight_);
    }

    if (selected_value >= begin && selected_value < end) {
      if (!cluster->clusterHeaderName().get().empty() &&
          !headers.get(cluster->clusterHeaderName()).empty()) {
        return pickClusterViaClusterHeader(cluster->clusterHeaderName(), headers,
                                           static_cast<RouteEntryAndRoute*>(cluster.get()));
      }
      // The WeightedClusterEntry does not contain reference to the RouteEntryImplBase to
      // avoid circular reference. To ensure that the RouteEntryImplBase is not destructed
      // before the WeightedClusterEntry, additional wrapper is used to hold the reference
      // to the RouteEntryImplBase.
      return std::make_shared<DynamicRouteEntry>(cluster.get(), shared_from_this(),
                                                 cluster->clusterName());
    }
    begin = end;
  }

  PANIC("unexpected");
}

absl::Status RouteEntryImplBase::validateClusters(
    const Upstream::ClusterManager::ClusterInfoMaps& cluster_info_maps) const {
  if (isDirectResponse()) {
    return absl::OkStatus();
  }

  // Currently, we verify that the cluster exists in the CM if we have an explicit cluster or
  // weighted cluster rule. We obviously do not verify a cluster_header rule. This means that
  // trying to use all CDS clusters with a static route table will not work. In the upcoming RDS
  // change we will make it so that dynamically loaded route tables do *not* perform CM checks.
  // In the future we might decide to also have a config option that turns off checks for static
  // route tables. This would enable the all CDS with static route table case.
  if (!cluster_name_.empty()) {
    if (!cluster_info_maps.hasCluster(cluster_name_)) {
      return absl::InvalidArgumentError(fmt::format("route: unknown cluster '{}'", cluster_name_));
    }
  } else if (weighted_clusters_config_ != nullptr) {
    for (const WeightedClusterEntrySharedPtr& cluster :
         weighted_clusters_config_->weighted_clusters_) {
      if (!cluster->clusterName().empty()) {
        if (!cluster_info_maps.hasCluster(cluster->clusterName())) {
          return absl::InvalidArgumentError(
              fmt::format("route: unknown weighted cluster '{}'", cluster->clusterName()));
        }
      }
      // For weighted clusters with `cluster_header_name`, we only verify that this field is
      // not empty because the cluster name is not set yet at config time (hence the validation
      // here).
      else if (cluster->clusterHeaderName().get().empty()) {
        return absl::InvalidArgumentError(
            "route: unknown weighted cluster with no cluster_header field");
      }
    }
  }
  return absl::OkStatus();
}

absl::optional<bool> RouteEntryImplBase::filterDisabled(absl::string_view config_name) const {
  absl::optional<bool> result = per_filter_configs_.disabled(config_name);
  if (result.has_value()) {
    return result.value();
  }
  return vhost_->filterDisabled(config_name);
}

void RouteEntryImplBase::traversePerFilterConfig(
    const std::string& filter_name,
    std::function<void(const Router::RouteSpecificFilterConfig&)> cb) const {
  vhost_->traversePerFilterConfig(filter_name, cb);

  auto maybe_route_config = per_filter_configs_.get(filter_name);
  if (maybe_route_config != nullptr) {
    cb(*maybe_route_config);
  }
}

const envoy::config::core::v3::Metadata& RouteEntryImplBase::metadata() const {
  return metadata_ != nullptr ? metadata_->proto_metadata_
                              : DefaultRouteMetadataPack::get().proto_metadata_;
}
const Envoy::Config::TypedMetadata& RouteEntryImplBase::typedMetadata() const {
  return metadata_ != nullptr ? metadata_->typed_metadata_
                              : DefaultRouteMetadataPack::get().typed_metadata_;
}

RouteEntryImplBase::WeightedClusterEntry::WeightedClusterEntry(
    const RouteEntryImplBase* parent, const std::string& runtime_key,
    Server::Configuration::ServerFactoryContext& factory_context,
    ProtobufMessage::ValidationVisitor& validator,
    const envoy::config::route::v3::WeightedCluster::ClusterWeight& cluster)
    : DynamicRouteEntry(parent, nullptr, validateWeightedClusterSpecifier(cluster).name()),
      runtime_key_(runtime_key), loader_(factory_context.runtime()),
      cluster_weight_(PROTOBUF_GET_WRAPPED_REQUIRED(cluster, weight)),
      per_filter_configs_(cluster.typed_per_filter_config(), factory_context, validator),
      host_rewrite_(cluster.host_rewrite_literal()),
      cluster_header_name_(cluster.cluster_header()) {
  if (!cluster.request_headers_to_add().empty() || !cluster.request_headers_to_remove().empty()) {
    request_headers_parser_ =
        THROW_OR_RETURN_VALUE(HeaderParser::configure(cluster.request_headers_to_add(),
                                                      cluster.request_headers_to_remove()),
                              Router::HeaderParserPtr);
  }
  if (!cluster.response_headers_to_add().empty() || !cluster.response_headers_to_remove().empty()) {
    response_headers_parser_ =
        THROW_OR_RETURN_VALUE(HeaderParser::configure(cluster.response_headers_to_add(),
                                                      cluster.response_headers_to_remove()),
                              Router::HeaderParserPtr);
  }

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

Http::HeaderTransforms RouteEntryImplBase::WeightedClusterEntry::requestHeaderTransforms(
    const StreamInfo::StreamInfo& stream_info, bool do_formatting) const {
  auto transforms = requestHeaderParser().getHeaderTransforms(stream_info, do_formatting);
  mergeTransforms(transforms,
                  DynamicRouteEntry::requestHeaderTransforms(stream_info, do_formatting));
  return transforms;
}

Http::HeaderTransforms RouteEntryImplBase::WeightedClusterEntry::responseHeaderTransforms(
    const StreamInfo::StreamInfo& stream_info, bool do_formatting) const {
  auto transforms = responseHeaderParser().getHeaderTransforms(stream_info, do_formatting);
  mergeTransforms(transforms,
                  DynamicRouteEntry::responseHeaderTransforms(stream_info, do_formatting));
  return transforms;
}

void RouteEntryImplBase::WeightedClusterEntry::traversePerFilterConfig(
    const std::string& filter_name,
    std::function<void(const Router::RouteSpecificFilterConfig&)> cb) const {
  DynamicRouteEntry::traversePerFilterConfig(filter_name, cb);

  const auto* cfg = per_filter_configs_.get(filter_name);
  if (cfg) {
    cb(*cfg);
  }
}

UriTemplateMatcherRouteEntryImpl::UriTemplateMatcherRouteEntryImpl(
    const CommonVirtualHostSharedPtr& vhost, const envoy::config::route::v3::Route& route,
    Server::Configuration::ServerFactoryContext& factory_context,
    ProtobufMessage::ValidationVisitor& validator, absl::Status& creation_status)
    : RouteEntryImplBase(vhost, route, factory_context, validator, creation_status),
      uri_template_(path_matcher_->uriTemplate()){};

void UriTemplateMatcherRouteEntryImpl::rewritePathHeader(Http::RequestHeaderMap& headers,
                                                         bool insert_envoy_original_path) const {
  finalizePathHeader(headers, path_matcher_->uriTemplate(), insert_envoy_original_path);
}

absl::optional<std::string> UriTemplateMatcherRouteEntryImpl::currentUrlPathAfterRewrite(
    const Http::RequestHeaderMap& headers) const {
  return currentUrlPathAfterRewriteWithMatchedPath(headers, path_matcher_->uriTemplate());
}

RouteConstSharedPtr
UriTemplateMatcherRouteEntryImpl::matches(const Http::RequestHeaderMap& headers,
                                          const StreamInfo::StreamInfo& stream_info,
                                          uint64_t random_value) const {
  if (RouteEntryImplBase::matchRoute(headers, stream_info, random_value) &&
      path_matcher_->match(headers.getPathValue())) {
    return clusterEntry(headers, random_value);
  }
  return nullptr;
}

PrefixRouteEntryImpl::PrefixRouteEntryImpl(
    const CommonVirtualHostSharedPtr& vhost, const envoy::config::route::v3::Route& route,
    Server::Configuration::ServerFactoryContext& factory_context,
    ProtobufMessage::ValidationVisitor& validator, absl::Status& creation_status)
    : RouteEntryImplBase(vhost, route, factory_context, validator, creation_status),
      path_matcher_(Matchers::PathMatcher::createPrefix(route.match().prefix(), !case_sensitive(),
                                                        factory_context)) {}

void PrefixRouteEntryImpl::rewritePathHeader(Http::RequestHeaderMap& headers,
                                             bool insert_envoy_original_path) const {
  finalizePathHeader(headers, matcher(), insert_envoy_original_path);
}

absl::optional<std::string>
PrefixRouteEntryImpl::currentUrlPathAfterRewrite(const Http::RequestHeaderMap& headers) const {
  return currentUrlPathAfterRewriteWithMatchedPath(headers, matcher());
}

RouteConstSharedPtr PrefixRouteEntryImpl::matches(const Http::RequestHeaderMap& headers,
                                                  const StreamInfo::StreamInfo& stream_info,
                                                  uint64_t random_value) const {
  if (RouteEntryImplBase::matchRoute(headers, stream_info, random_value) &&
      path_matcher_->match(sanitizePathBeforePathMatching(headers.getPathValue()))) {
    return clusterEntry(headers, random_value);
  }
  return nullptr;
}

PathRouteEntryImpl::PathRouteEntryImpl(const CommonVirtualHostSharedPtr& vhost,
                                       const envoy::config::route::v3::Route& route,
                                       Server::Configuration::ServerFactoryContext& factory_context,
                                       ProtobufMessage::ValidationVisitor& validator,
                                       absl::Status& creation_status)
    : RouteEntryImplBase(vhost, route, factory_context, validator, creation_status),
      path_matcher_(Matchers::PathMatcher::createExact(route.match().path(), !case_sensitive(),
                                                       factory_context)) {}

void PathRouteEntryImpl::rewritePathHeader(Http::RequestHeaderMap& headers,
                                           bool insert_envoy_original_path) const {
  finalizePathHeader(headers, matcher(), insert_envoy_original_path);
}

absl::optional<std::string>
PathRouteEntryImpl::currentUrlPathAfterRewrite(const Http::RequestHeaderMap& headers) const {
  return currentUrlPathAfterRewriteWithMatchedPath(headers, matcher());
}

RouteConstSharedPtr PathRouteEntryImpl::matches(const Http::RequestHeaderMap& headers,
                                                const StreamInfo::StreamInfo& stream_info,
                                                uint64_t random_value) const {
  if (RouteEntryImplBase::matchRoute(headers, stream_info, random_value) &&
      path_matcher_->match(sanitizePathBeforePathMatching(headers.getPathValue()))) {
    return clusterEntry(headers, random_value);
  }

  return nullptr;
}

RegexRouteEntryImpl::RegexRouteEntryImpl(
    const CommonVirtualHostSharedPtr& vhost, const envoy::config::route::v3::Route& route,
    Server::Configuration::ServerFactoryContext& factory_context,
    ProtobufMessage::ValidationVisitor& validator, absl::Status& creation_status)
    : RouteEntryImplBase(vhost, route, factory_context, validator, creation_status),
      path_matcher_(
          Matchers::PathMatcher::createSafeRegex(route.match().safe_regex(), factory_context)) {
  ASSERT(route.match().path_specifier_case() ==
         envoy::config::route::v3::RouteMatch::PathSpecifierCase::kSafeRegex);
}

void RegexRouteEntryImpl::rewritePathHeader(Http::RequestHeaderMap& headers,
                                            bool insert_envoy_original_path) const {
  absl::string_view path = Http::PathUtil::removeQueryAndFragment(headers.getPathValue());
  // TODO(yuval-k): This ASSERT can happen if the path was changed by a filter without clearing
  // the route cache. We should consider if ASSERT-ing is the desired behavior in this case.
  ASSERT(path_matcher_->match(sanitizePathBeforePathMatching(path)));
  finalizePathHeader(headers, path, insert_envoy_original_path);
}

absl::optional<std::string>
RegexRouteEntryImpl::currentUrlPathAfterRewrite(const Http::RequestHeaderMap& headers) const {
  const absl::string_view path = Http::PathUtil::removeQueryAndFragment(headers.getPathValue());
  return currentUrlPathAfterRewriteWithMatchedPath(headers, path);
}

RouteConstSharedPtr RegexRouteEntryImpl::matches(const Http::RequestHeaderMap& headers,
                                                 const StreamInfo::StreamInfo& stream_info,
                                                 uint64_t random_value) const {
  if (RouteEntryImplBase::matchRoute(headers, stream_info, random_value)) {
    if (path_matcher_->match(sanitizePathBeforePathMatching(headers.getPathValue()))) {
      return clusterEntry(headers, random_value);
    }
  }
  return nullptr;
}

ConnectRouteEntryImpl::ConnectRouteEntryImpl(
    const CommonVirtualHostSharedPtr& vhost, const envoy::config::route::v3::Route& route,
    Server::Configuration::ServerFactoryContext& factory_context,
    ProtobufMessage::ValidationVisitor& validator, absl::Status& creation_status)
    : RouteEntryImplBase(vhost, route, factory_context, validator, creation_status) {}

void ConnectRouteEntryImpl::rewritePathHeader(Http::RequestHeaderMap& headers,
                                              bool insert_envoy_original_path) const {
  const absl::string_view path = Http::PathUtil::removeQueryAndFragment(headers.getPathValue());
  finalizePathHeader(headers, path, insert_envoy_original_path);
}

absl::optional<std::string>
ConnectRouteEntryImpl::currentUrlPathAfterRewrite(const Http::RequestHeaderMap& headers) const {
  const absl::string_view path = Http::PathUtil::removeQueryAndFragment(headers.getPathValue());
  return currentUrlPathAfterRewriteWithMatchedPath(headers, path);
}

RouteConstSharedPtr ConnectRouteEntryImpl::matches(const Http::RequestHeaderMap& headers,
                                                   const StreamInfo::StreamInfo& stream_info,
                                                   uint64_t random_value) const {
  if ((Http::HeaderUtility::isConnect(headers) ||
       (Runtime::runtimeFeatureEnabled("envoy.reloadable_features.enable_connect_udp_support") &&
        Http::HeaderUtility::isConnectUdpRequest(headers))) &&
      RouteEntryImplBase::matchRoute(headers, stream_info, random_value)) {
    return clusterEntry(headers, random_value);
  }
  return nullptr;
}

PathSeparatedPrefixRouteEntryImpl::PathSeparatedPrefixRouteEntryImpl(
    const CommonVirtualHostSharedPtr& vhost, const envoy::config::route::v3::Route& route,
    Server::Configuration::ServerFactoryContext& factory_context,
    ProtobufMessage::ValidationVisitor& validator, absl::Status& creation_status)
    : RouteEntryImplBase(vhost, route, factory_context, validator, creation_status),
      path_matcher_(Matchers::PathMatcher::createPrefix(route.match().path_separated_prefix(),
                                                        !case_sensitive(), factory_context)) {}

void PathSeparatedPrefixRouteEntryImpl::rewritePathHeader(Http::RequestHeaderMap& headers,
                                                          bool insert_envoy_original_path) const {
  finalizePathHeader(headers, matcher(), insert_envoy_original_path);
}

absl::optional<std::string> PathSeparatedPrefixRouteEntryImpl::currentUrlPathAfterRewrite(
    const Http::RequestHeaderMap& headers) const {
  return currentUrlPathAfterRewriteWithMatchedPath(headers, matcher());
}

RouteConstSharedPtr
PathSeparatedPrefixRouteEntryImpl::matches(const Http::RequestHeaderMap& headers,
                                           const StreamInfo::StreamInfo& stream_info,
                                           uint64_t random_value) const {
  if (!RouteEntryImplBase::matchRoute(headers, stream_info, random_value)) {
    return nullptr;
  }
  absl::string_view sanitized_path = sanitizePathBeforePathMatching(
      Http::PathUtil::removeQueryAndFragment(headers.getPathValue()));
  const size_t sanitized_size = sanitized_path.size();
  const size_t matcher_size = matcher().size();
  if (sanitized_size >= matcher_size && path_matcher_->match(sanitized_path) &&
      (sanitized_size == matcher_size || sanitized_path[matcher_size] == '/')) {
    return clusterEntry(headers, random_value);
  }
  return nullptr;
}

CommonVirtualHostImpl::CommonVirtualHostImpl(
    const envoy::config::route::v3::VirtualHost& virtual_host,
    const CommonConfigSharedPtr& global_route_config,
    Server::Configuration::ServerFactoryContext& factory_context, Stats::Scope& scope,
    ProtobufMessage::ValidationVisitor& validator, absl::Status& creation_status)
    : stat_name_storage_(virtual_host.name(), factory_context.scope().symbolTable()),
      global_route_config_(global_route_config),
      per_filter_configs_(virtual_host.typed_per_filter_config(), factory_context, validator),
      retry_shadow_buffer_limit_(PROTOBUF_GET_WRAPPED_OR_DEFAULT(
          virtual_host, per_request_buffer_limit_bytes, std::numeric_limits<uint32_t>::max())),
      include_attempt_count_in_request_(virtual_host.include_request_attempt_count()),
      include_attempt_count_in_response_(virtual_host.include_attempt_count_in_response()),
      include_is_timeout_retry_header_(virtual_host.include_is_timeout_retry_header()) {
  if (!virtual_host.request_headers_to_add().empty() ||
      !virtual_host.request_headers_to_remove().empty()) {
    request_headers_parser_ =
        THROW_OR_RETURN_VALUE(HeaderParser::configure(virtual_host.request_headers_to_add(),
                                                      virtual_host.request_headers_to_remove()),
                              Router::HeaderParserPtr);
  }
  if (!virtual_host.response_headers_to_add().empty() ||
      !virtual_host.response_headers_to_remove().empty()) {
    response_headers_parser_ =
        THROW_OR_RETURN_VALUE(HeaderParser::configure(virtual_host.response_headers_to_add(),
                                                      virtual_host.response_headers_to_remove()),
                              Router::HeaderParserPtr);
  }

  // Retry and Hedge policies must be set before routes, since they may use them.
  if (virtual_host.has_retry_policy()) {
    retry_policy_ = std::make_unique<envoy::config::route::v3::RetryPolicy>();
    retry_policy_->CopyFrom(virtual_host.retry_policy());
  }
  if (virtual_host.has_hedge_policy()) {
    hedge_policy_ = std::make_unique<envoy::config::route::v3::HedgePolicy>();
    hedge_policy_->CopyFrom(virtual_host.hedge_policy());
  }

  if (!virtual_host.rate_limits().empty()) {
    rate_limit_policy_ = std::make_unique<RateLimitPolicyImpl>(virtual_host.rate_limits(),
                                                               factory_context, creation_status);
    SET_AND_RETURN_IF_NOT_OK(creation_status, creation_status);
  }

  shadow_policies_.reserve(virtual_host.request_mirror_policies().size());
  for (const auto& mirror_policy_config : virtual_host.request_mirror_policies()) {
    shadow_policies_.push_back(std::make_shared<ShadowPolicyImpl>(mirror_policy_config));
  }

  // Inherit policies from the global config.
  if (shadow_policies_.empty()) {
    shadow_policies_ = global_route_config_->shadowPolicies();
  }

  if (virtual_host.has_matcher() && !virtual_host.routes().empty()) {
    creation_status =
        absl::InvalidArgumentError("cannot set both matcher and routes on virtual host");
    return;
  }

  if (!virtual_host.virtual_clusters().empty()) {
    vcluster_scope_ = Stats::Utility::scopeFromStatNames(
        scope, {stat_name_storage_.statName(),
                factory_context.routerContext().virtualClusterStatNames().vcluster_});
    virtual_cluster_catch_all_ = std::make_unique<CatchAllVirtualCluster>(
        *vcluster_scope_, factory_context.routerContext().virtualClusterStatNames());
    for (const auto& virtual_cluster : virtual_host.virtual_clusters()) {
      if (virtual_cluster.headers().empty()) {
        creation_status = absl::InvalidArgumentError("virtual clusters must define 'headers'");
        return;
      }

      virtual_clusters_.push_back(
          VirtualClusterEntry(virtual_cluster, *vcluster_scope_, factory_context,
                              factory_context.routerContext().virtualClusterStatNames()));
    }
  }

  if (virtual_host.has_cors()) {
    cors_policy_ = std::make_unique<CorsPolicyImpl>(virtual_host.cors(), factory_context);
  }

  if (virtual_host.has_metadata()) {
    metadata_ = std::make_unique<RouteMetadataPack>(virtual_host.metadata());
  }
}

CommonVirtualHostImpl::VirtualClusterEntry::VirtualClusterEntry(
    const envoy::config::route::v3::VirtualCluster& virtual_cluster, Stats::Scope& scope,
    Server::Configuration::CommonFactoryContext& context, const VirtualClusterStatNames& stat_names)
    : StatNameProvider(virtual_cluster.name(), scope.symbolTable()),
      VirtualClusterBase(virtual_cluster.name(), stat_name_storage_.statName(),
                         scope.scopeFromStatName(stat_name_storage_.statName()), stat_names) {
  ASSERT(!virtual_cluster.headers().empty());
  headers_ = Http::HeaderUtility::buildHeaderDataVector(virtual_cluster.headers(), context);
}

const CommonConfig& CommonVirtualHostImpl::routeConfig() const { return *global_route_config_; }

absl::optional<bool> CommonVirtualHostImpl::filterDisabled(absl::string_view config_name) const {
  absl::optional<bool> result = per_filter_configs_.disabled(config_name);
  if (result.has_value()) {
    return result.value();
  }
  return global_route_config_->filterDisabled(config_name);
}

const RouteSpecificFilterConfig*
CommonVirtualHostImpl::mostSpecificPerFilterConfig(const std::string& name) const {
  auto* per_filter_config = per_filter_configs_.get(name);
  return per_filter_config != nullptr ? per_filter_config
                                      : global_route_config_->perFilterConfig(name);
}
void CommonVirtualHostImpl::traversePerFilterConfig(
    const std::string& filter_name,
    std::function<void(const Router::RouteSpecificFilterConfig&)> cb) const {
  // Parent first.
  if (auto* maybe_rc_config = global_route_config_->perFilterConfig(filter_name);
      maybe_rc_config != nullptr) {
    cb(*maybe_rc_config);
  }
  if (auto* maybe_vhost_config = per_filter_configs_.get(filter_name);
      maybe_vhost_config != nullptr) {
    cb(*maybe_vhost_config);
  }
}

const envoy::config::core::v3::Metadata& CommonVirtualHostImpl::metadata() const {
  return metadata_ != nullptr ? metadata_->proto_metadata_
                              : DefaultRouteMetadataPack::get().proto_metadata_;
}
const Envoy::Config::TypedMetadata& CommonVirtualHostImpl::typedMetadata() const {
  return metadata_ != nullptr ? metadata_->typed_metadata_
                              : DefaultRouteMetadataPack::get().typed_metadata_;
}

VirtualHostImpl::VirtualHostImpl(
    const envoy::config::route::v3::VirtualHost& virtual_host,
    const CommonConfigSharedPtr& global_route_config,
    Server::Configuration::ServerFactoryContext& factory_context, Stats::Scope& scope,
    ProtobufMessage::ValidationVisitor& validator,
    const absl::optional<Upstream::ClusterManager::ClusterInfoMaps>& validation_clusters,
    absl::Status& creation_status) {

  shared_virtual_host_ = std::make_shared<CommonVirtualHostImpl>(
      virtual_host, global_route_config, factory_context, scope, validator, creation_status);
  if (!creation_status.ok()) {
    return;
  }

  switch (virtual_host.require_tls()) {
    PANIC_ON_PROTO_ENUM_SENTINEL_VALUES;
  case envoy::config::route::v3::VirtualHost::NONE:
    ssl_requirements_ = SslRequirements::None;
    break;
  case envoy::config::route::v3::VirtualHost::EXTERNAL_ONLY:
    ssl_requirements_ = SslRequirements::ExternalOnly;
    break;
  case envoy::config::route::v3::VirtualHost::ALL:
    ssl_requirements_ = SslRequirements::All;
    break;
  }

  if (virtual_host.has_matcher()) {
    RouteActionContext context{shared_virtual_host_, factory_context};
    RouteActionValidationVisitor validation_visitor;
    Matcher::MatchTreeFactory<Http::HttpMatchingData, RouteActionContext> factory(
        context, factory_context, validation_visitor);

    matcher_ = factory.create(virtual_host.matcher())();

    if (!validation_visitor.errors().empty()) {
      // TODO(snowp): Output all violations.
      creation_status = absl::InvalidArgumentError(
          fmt::format("requirement violation while creating route match tree: {}",
                      validation_visitor.errors()[0]));
      return;
    }
  } else {
    for (const auto& route : virtual_host.routes()) {
      auto route_or_error = RouteCreator::createAndValidateRoute(
          route, shared_virtual_host_, factory_context, validator, validation_clusters);
      SET_AND_RETURN_IF_NOT_OK(route_or_error.status(), creation_status);
      routes_.emplace_back(route_or_error.value());
    }
  }
}

const std::shared_ptr<const SslRedirectRoute> VirtualHostImpl::SSL_REDIRECT_ROUTE{
    new SslRedirectRoute()};

RouteConstSharedPtr VirtualHostImpl::getRouteFromRoutes(
    const RouteCallback& cb, const Http::RequestHeaderMap& headers,
    const StreamInfo::StreamInfo& stream_info, uint64_t random_value,
    absl::Span<const RouteEntryImplBaseConstSharedPtr> routes) const {
  for (auto route = routes.begin(); route != routes.end(); ++route) {
    if (!headers.Path() && !(*route)->supportsPathlessHeaders()) {
      continue;
    }

    RouteConstSharedPtr route_entry = (*route)->matches(headers, stream_info, random_value);
    if (route_entry == nullptr) {
      continue;
    }

    if (cb == nullptr) {
      return route_entry;
    }

    RouteEvalStatus eval_status = (std::next(route) == routes.end())
                                      ? RouteEvalStatus::NoMoreRoutes
                                      : RouteEvalStatus::HasMoreRoutes;
    RouteMatchStatus match_status = cb(route_entry, eval_status);
    if (match_status == RouteMatchStatus::Accept) {
      return route_entry;
    }
    if (match_status == RouteMatchStatus::Continue &&
        eval_status == RouteEvalStatus::NoMoreRoutes) {
      ENVOY_LOG(debug,
                "return null when route match status is Continue but there is no more routes");
      return nullptr;
    }
  }

  ENVOY_LOG(debug, "route was resolved but final route list did not match incoming request");
  return nullptr;
}

RouteConstSharedPtr VirtualHostImpl::getRouteFromEntries(const RouteCallback& cb,
                                                         const Http::RequestHeaderMap& headers,
                                                         const StreamInfo::StreamInfo& stream_info,
                                                         uint64_t random_value) const {
  // In the rare case that X-Forwarded-Proto and scheme disagree (say http URL over an HTTPS
  // connection), force a redirect based on underlying protocol, rather than URL
  // scheme, so don't force a redirect for a http:// url served over a TLS
  // connection.
  const absl::string_view scheme = headers.getForwardedProtoValue();
  if (scheme.empty()) {
    // No scheme header. This normally only happens when ActiveStream::decodeHeaders
    // bails early (as it rejects a request), or a buggy filter removes the :scheme header.
    return nullptr;
  }

  // First check for ssl redirect.
  if (ssl_requirements_ == SslRequirements::All && scheme != "https") {
    return SSL_REDIRECT_ROUTE;
  } else if (ssl_requirements_ == SslRequirements::ExternalOnly && scheme != "https" &&
             !Http::HeaderUtility::isEnvoyInternalRequest(headers)) {
    return SSL_REDIRECT_ROUTE;
  }

  if (matcher_) {
    Http::Matching::HttpMatchingDataImpl data(stream_info);
    data.onRequestHeaders(headers);

    auto match = Matcher::evaluateMatch<Http::HttpMatchingData>(*matcher_, data);

    if (match.result_) {
      const auto result = match.result_();
      if (result->typeUrl() == RouteMatchAction::staticTypeUrl()) {
        const RouteMatchAction& route_action = result->getTyped<RouteMatchAction>();

        return getRouteFromRoutes(cb, headers, stream_info, random_value, {route_action.route()});
      } else if (result->typeUrl() == RouteListMatchAction::staticTypeUrl()) {
        const RouteListMatchAction& action = result->getTyped<RouteListMatchAction>();

        return getRouteFromRoutes(cb, headers, stream_info, random_value, action.routes());
      }
      PANIC("Action in router matcher should be Route or RouteList");
    }

    ENVOY_LOG(debug, "failed to match incoming request: {}", static_cast<int>(match.match_state_));

    return nullptr;
  }

  // Check for a route that matches the request.
  return getRouteFromRoutes(cb, headers, stream_info, random_value, routes_);
}

const VirtualHostImpl* RouteMatcher::findWildcardVirtualHost(
    absl::string_view host, const RouteMatcher::WildcardVirtualHosts& wildcard_virtual_hosts,
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
    const auto match = wildcard_map.find(substring_function(host, wildcard_length));
    if (match != wildcard_map.end()) {
      return match->second.get();
    }
  }
  return nullptr;
}
absl::StatusOr<std::unique_ptr<RouteMatcher>>
RouteMatcher::create(const envoy::config::route::v3::RouteConfiguration& route_config,
                     const CommonConfigSharedPtr& global_route_config,
                     Server::Configuration::ServerFactoryContext& factory_context,
                     ProtobufMessage::ValidationVisitor& validator, bool validate_clusters) {
  absl::Status creation_status = absl::OkStatus();
  auto ret = std::unique_ptr<RouteMatcher>{new RouteMatcher(route_config, global_route_config,
                                                            factory_context, validator,
                                                            validate_clusters, creation_status)};
  RETURN_IF_NOT_OK(creation_status);
  return ret;
}
RouteMatcher::RouteMatcher(const envoy::config::route::v3::RouteConfiguration& route_config,
                           const CommonConfigSharedPtr& global_route_config,
                           Server::Configuration::ServerFactoryContext& factory_context,
                           ProtobufMessage::ValidationVisitor& validator, bool validate_clusters,
                           absl::Status& creation_status)
    : vhost_scope_(factory_context.scope().scopeFromStatName(
          factory_context.routerContext().virtualClusterStatNames().vhost_)),
      ignore_port_in_host_matching_(route_config.ignore_port_in_host_matching()) {
  absl::optional<Upstream::ClusterManager::ClusterInfoMaps> validation_clusters;
  if (validate_clusters) {
    validation_clusters = factory_context.clusterManager().clusters();
  }
  for (const auto& virtual_host_config : route_config.virtual_hosts()) {
    VirtualHostSharedPtr virtual_host = std::make_shared<VirtualHostImpl>(
        virtual_host_config, global_route_config, factory_context, *vhost_scope_, validator,
        validation_clusters, creation_status);
    SET_AND_RETURN_IF_NOT_OK(creation_status, creation_status);
    for (const std::string& domain_name : virtual_host_config.domains()) {
      const Http::LowerCaseString lower_case_domain_name(domain_name);
      absl::string_view domain = lower_case_domain_name;
      bool duplicate_found = false;
      if ("*" == domain) {
        if (default_virtual_host_) {
          creation_status = absl::InvalidArgumentError(fmt::format(
              "Only a single wildcard domain is permitted in route {}", route_config.name()));
          return;
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
        creation_status = absl::InvalidArgumentError(
            fmt::format("Only unique values for domains are permitted. Duplicate "
                        "entry of domain {} in route {}",
                        domain, route_config.name()));
        return;
      }
    }
  }
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

  // If 'ignore_port_in_host_matching' is set, ignore the port number in the host header(if any).
  absl::string_view host_header_value = headers.getHostValue();
  if (ignorePortInHostMatching()) {
    if (const absl::string_view::size_type port_start =
            Http::HeaderUtility::getPortStart(host_header_value);
        port_start != absl::string_view::npos) {
      host_header_value = host_header_value.substr(0, port_start);
    }
  }
  // TODO (@rshriram) Match Origin header in WebSocket
  // request with VHost, using wildcard match
  // Lower-case the value of the host header, as hostnames are case insensitive.
  const std::string host = absl::AsciiStrToLower(host_header_value);
  const auto iter = virtual_hosts_.find(host);
  if (iter != virtual_hosts_.end()) {
    return iter->second.get();
  }
  if (!wildcard_virtual_host_suffixes_.empty()) {
    const VirtualHostImpl* vhost = findWildcardVirtualHost(
        host, wildcard_virtual_host_suffixes_,
        [](absl::string_view h, int l) -> absl::string_view { return h.substr(h.size() - l); });
    if (vhost != nullptr) {
      return vhost;
    }
  }
  if (!wildcard_virtual_host_prefixes_.empty()) {
    const VirtualHostImpl* vhost = findWildcardVirtualHost(
        host, wildcard_virtual_host_prefixes_,
        [](absl::string_view h, int l) -> absl::string_view { return h.substr(0, l); });
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
const envoy::config::core::v3::Metadata SslRedirectRoute::metadata_;
const Envoy::Config::TypedMetadataImpl<Envoy::Config::TypedMetadataFactory>
    SslRedirectRoute::typed_metadata_({});

const VirtualCluster*
CommonVirtualHostImpl::virtualClusterFromEntries(const Http::HeaderMap& headers) const {
  for (const VirtualClusterEntry& entry : virtual_clusters_) {
    if (Http::HeaderUtility::matchHeaders(headers, entry.headers_)) {
      return &entry;
    }
  }

  if (!virtual_clusters_.empty()) {
    return virtual_cluster_catch_all_.get();
  }

  return nullptr;
}

CommonConfigImpl::CommonConfigImpl(const envoy::config::route::v3::RouteConfiguration& config,
                                   Server::Configuration::ServerFactoryContext& factory_context,
                                   ProtobufMessage::ValidationVisitor& validator)
    : name_(config.name()), symbol_table_(factory_context.scope().symbolTable()),
      per_filter_configs_(config.typed_per_filter_config(), factory_context, validator),
      max_direct_response_body_size_bytes_(
          PROTOBUF_GET_WRAPPED_OR_DEFAULT(config, max_direct_response_body_size_bytes,
                                          DEFAULT_MAX_DIRECT_RESPONSE_BODY_SIZE_BYTES)),
      uses_vhds_(config.has_vhds()),
      most_specific_header_mutations_wins_(config.most_specific_header_mutations_wins()),
      ignore_path_parameters_in_path_matching_(config.ignore_path_parameters_in_path_matching()) {
  if (!config.request_mirror_policies().empty()) {
    shadow_policies_.reserve(config.request_mirror_policies().size());
    for (const auto& mirror_policy_config : config.request_mirror_policies()) {
      shadow_policies_.push_back(std::make_shared<ShadowPolicyImpl>(mirror_policy_config));
    }
  }

  // Initialize all cluster specifier plugins before creating route matcher. Because the route may
  // reference it by name.
  for (const auto& plugin_proto : config.cluster_specifier_plugins()) {
    auto plugin = THROW_OR_RETURN_VALUE(
        getClusterSpecifierPluginByTheProto(plugin_proto, validator, factory_context),
        ClusterSpecifierPluginSharedPtr);
    cluster_specifier_plugins_.emplace(plugin_proto.extension().name(), std::move(plugin));
  }

  for (const std::string& header : config.internal_only_headers()) {
    internal_only_headers_.push_back(Http::LowerCaseString(header));
  }

  if (!config.request_headers_to_add().empty() || !config.request_headers_to_remove().empty()) {
    request_headers_parser_ =
        THROW_OR_RETURN_VALUE(HeaderParser::configure(config.request_headers_to_add(),
                                                      config.request_headers_to_remove()),
                              Router::HeaderParserPtr);
  }
  if (!config.response_headers_to_add().empty() || !config.response_headers_to_remove().empty()) {
    response_headers_parser_ =
        THROW_OR_RETURN_VALUE(HeaderParser::configure(config.response_headers_to_add(),
                                                      config.response_headers_to_remove()),
                              Router::HeaderParserPtr);
  }

  if (config.has_metadata()) {
    metadata_ = std::make_unique<RouteMetadataPack>(config.metadata());
  }
}

absl::StatusOr<ClusterSpecifierPluginSharedPtr>
CommonConfigImpl::clusterSpecifierPlugin(absl::string_view provider) const {
  auto iter = cluster_specifier_plugins_.find(provider);
  if (iter == cluster_specifier_plugins_.end() || iter->second == nullptr) {
    return absl::InvalidArgumentError(
        fmt::format("Unknown cluster specifier plugin name: {} is used in the route", provider));
  }
  return iter->second;
}

const envoy::config::core::v3::Metadata& CommonConfigImpl::metadata() const {
  return metadata_ != nullptr ? metadata_->proto_metadata_
                              : DefaultRouteMetadataPack::get().proto_metadata_;
}
const Envoy::Config::TypedMetadata& CommonConfigImpl::typedMetadata() const {
  return metadata_ != nullptr ? metadata_->typed_metadata_
                              : DefaultRouteMetadataPack::get().typed_metadata_;
}

ConfigImpl::ConfigImpl(const envoy::config::route::v3::RouteConfiguration& config,
                       Server::Configuration::ServerFactoryContext& factory_context,
                       ProtobufMessage::ValidationVisitor& validator,
                       bool validate_clusters_default) {

  shared_config_ = std::make_shared<CommonConfigImpl>(config, factory_context, validator);

  auto matcher_or_error = RouteMatcher::create(
      config, shared_config_, factory_context, validator,
      PROTOBUF_GET_WRAPPED_OR_DEFAULT(config, validate_clusters, validate_clusters_default));
  THROW_IF_STATUS_NOT_OK(matcher_or_error, throw);
  route_matcher_ = std::move(matcher_or_error.value());
}

RouteConstSharedPtr ConfigImpl::route(const RouteCallback& cb,
                                      const Http::RequestHeaderMap& headers,
                                      const StreamInfo::StreamInfo& stream_info,
                                      uint64_t random_value) const {
  return route_matcher_->route(cb, headers, stream_info, random_value);
}

const envoy::config::core::v3::Metadata& NullConfigImpl::metadata() const {
  return DefaultRouteMetadataPack::get().proto_metadata_;
}
const Envoy::Config::TypedMetadata& NullConfigImpl::typedMetadata() const {
  return DefaultRouteMetadataPack::get().typed_metadata_;
}

absl::StatusOr<RouteSpecificFilterConfigConstSharedPtr>
PerFilterConfigs::createRouteSpecificFilterConfig(
    const std::string& name, const ProtobufWkt::Any& typed_config, bool is_optional,
    Server::Configuration::ServerFactoryContext& factory_context,
    ProtobufMessage::ValidationVisitor& validator) {
  Server::Configuration::NamedHttpFilterConfigFactory* factory =
      Envoy::Config::Utility::getFactoryByType<Server::Configuration::NamedHttpFilterConfigFactory>(
          typed_config);
  if (factory == nullptr) {
    if (is_optional) {
      ENVOY_LOG(warn,
                "Can't find a registered implementation for http filter '{}' with type URL: '{}'",
                name, Envoy::Config::Utility::getFactoryType(typed_config));
      return nullptr;
    } else {
      return absl::InvalidArgumentError(
          fmt::format("Didn't find a registered implementation for '{}' with type URL: '{}'", name,
                      Envoy::Config::Utility::getFactoryType(typed_config)));
    }
  }

  ProtobufTypes::MessagePtr proto_config = factory->createEmptyRouteConfigProto();
  Envoy::Config::Utility::translateOpaqueConfig(typed_config, validator, *proto_config);
  auto object = factory->createRouteSpecificFilterConfig(*proto_config, factory_context, validator);
  if (object == nullptr) {
    if (is_optional) {
      ENVOY_LOG(
          debug,
          "The filter {} doesn't support virtual host or route specific configurations, and it is "
          "optional, so ignore it.",
          name);
    } else {
      return absl::InvalidArgumentError(fmt::format(
          "The filter {} doesn't support virtual host or route specific configurations", name));
    }
  }
  return object;
}

PerFilterConfigs::PerFilterConfigs(
    const Protobuf::Map<std::string, ProtobufWkt::Any>& typed_configs,
    Server::Configuration::ServerFactoryContext& factory_context,
    ProtobufMessage::ValidationVisitor& validator) {

  std::string filter_config_type =
      envoy::config::route::v3::FilterConfig::default_instance().GetTypeName();

  for (const auto& per_filter_config : typed_configs) {
    const std::string& name = per_filter_config.first;
    RouteSpecificFilterConfigConstSharedPtr config;

    if (TypeUtil::typeUrlToDescriptorFullName(per_filter_config.second.type_url()) ==
        filter_config_type) {
      envoy::config::route::v3::FilterConfig filter_config;
      Envoy::Config::Utility::translateOpaqueConfig(per_filter_config.second, validator,
                                                    filter_config);

      // The filter is marked as disabled explicitly and the config is ignored directly.
      if (filter_config.disabled()) {
        configs_.emplace(name, FilterConfig{nullptr, true});
        continue;
      }

      // If the field `config` is not configured, we treat it as configuration error.
      if (!filter_config.has_config()) {
        throwEnvoyExceptionOrPanic(
            fmt::format("Empty route/virtual host per filter configuration for {} filter", name));
      }

      // If the field `config` is configured but is empty, we treat the filter is enabled
      // explicitly.
      if (filter_config.config().type_url().empty()) {
        configs_.emplace(name, FilterConfig{nullptr, false});
        continue;
      }

      config = THROW_OR_RETURN_VALUE(createRouteSpecificFilterConfig(name, filter_config.config(),
                                                                     filter_config.is_optional(),
                                                                     factory_context, validator),
                                     RouteSpecificFilterConfigConstSharedPtr);
    } else {
      config =
          THROW_OR_RETURN_VALUE(createRouteSpecificFilterConfig(name, per_filter_config.second,
                                                                false, factory_context, validator),
                                RouteSpecificFilterConfigConstSharedPtr);
    }

    // If a filter is explicitly configured we treat it as enabled.
    // The config may be nullptr because the filter could be optional.
    configs_.emplace(name, FilterConfig{std::move(config), false});
  }
}

const RouteSpecificFilterConfig* PerFilterConfigs::get(const std::string& name) const {
  auto it = configs_.find(name);
  return it == configs_.end() ? nullptr : it->second.config_.get();
}

absl::optional<bool> PerFilterConfigs::disabled(absl::string_view name) const {
  // Quick exit if there are no configs.
  if (configs_.empty()) {
    return absl::nullopt;
  }

  const auto it = configs_.find(name);
  return it != configs_.end() ? absl::optional<bool>{it->second.disabled_} : absl::nullopt;
}

Matcher::ActionFactoryCb RouteMatchActionFactory::createActionFactoryCb(
    const Protobuf::Message& config, RouteActionContext& context,
    ProtobufMessage::ValidationVisitor& validation_visitor) {
  const auto& route_config =
      MessageUtil::downcastAndValidate<const envoy::config::route::v3::Route&>(config,
                                                                               validation_visitor);
  auto route = THROW_OR_RETURN_VALUE(
      RouteCreator::createAndValidateRoute(route_config, context.vhost, context.factory_context,
                                           validation_visitor, absl::nullopt),
      RouteEntryImplBaseConstSharedPtr);

  return [route]() { return std::make_unique<RouteMatchAction>(route); };
}
REGISTER_FACTORY(RouteMatchActionFactory, Matcher::ActionFactory<RouteActionContext>);

Matcher::ActionFactoryCb RouteListMatchActionFactory::createActionFactoryCb(
    const Protobuf::Message& config, RouteActionContext& context,
    ProtobufMessage::ValidationVisitor& validation_visitor) {
  const auto& route_config =
      MessageUtil::downcastAndValidate<const envoy::config::route::v3::RouteList&>(
          config, validation_visitor);

  std::vector<RouteEntryImplBaseConstSharedPtr> routes;
  for (const auto& route : route_config.routes()) {
    routes.emplace_back(THROW_OR_RETURN_VALUE(
        RouteCreator::createAndValidateRoute(route, context.vhost, context.factory_context,
                                             validation_visitor, absl::nullopt),
        RouteEntryImplBaseConstSharedPtr));
  }
  return [routes]() { return std::make_unique<RouteListMatchAction>(routes); };
}
REGISTER_FACTORY(RouteListMatchActionFactory, Matcher::ActionFactory<RouteActionContext>);

} // namespace Router
} // namespace Envoy
