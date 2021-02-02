#include "common/router/router.h"

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#include "envoy/event/dispatcher.h"
#include "envoy/event/timer.h"
#include "envoy/grpc/status.h"
#include "envoy/http/conn_pool.h"
#include "envoy/runtime/runtime.h"
#include "envoy/upstream/cluster_manager.h"
#include "envoy/upstream/health_check_host_monitor.h"
#include "envoy/upstream/upstream.h"

#include "common/common/assert.h"
#include "common/common/cleanup.h"
#include "common/common/empty_string.h"
#include "common/common/enum_to_int.h"
#include "common/common/scope_tracker.h"
#include "common/common/utility.h"
#include "common/config/utility.h"
#include "common/grpc/common.h"
#include "common/http/codes.h"
#include "common/http/header_map_impl.h"
#include "common/http/headers.h"
#include "common/http/message_impl.h"
#include "common/http/utility.h"
#include "common/network/application_protocol.h"
#include "common/network/transport_socket_options_impl.h"
#include "common/network/upstream_server_name.h"
#include "common/network/upstream_subject_alt_names.h"
#include "common/router/config_impl.h"
#include "common/router/debug_config.h"
#include "common/router/retry_state_impl.h"
#include "common/router/upstream_request.h"
#include "common/runtime/runtime_features.h"
#include "common/runtime/runtime_impl.h"
#include "common/stream_info/uint32_accessor_impl.h"
#include "common/tracing/http_tracer_impl.h"

#include "extensions/filters/http/well_known_names.h"

namespace Envoy {
namespace Router {
namespace {
constexpr char NumInternalRedirectsFilterStateName[] = "num_internal_redirects";

uint32_t getLength(const Buffer::Instance* instance) { return instance ? instance->length() : 0; }

bool schemeIsHttp(const Http::RequestHeaderMap& downstream_headers,
                  const Network::Connection& connection) {
  if (downstream_headers.getForwardedProtoValue() == Http::Headers::get().SchemeValues.Http) {
    return true;
  }
  if (!connection.ssl()) {
    return true;
  }
  return false;
}

constexpr uint64_t TimeoutPrecisionFactor = 100;

} // namespace

// Express percentage as [0, TimeoutPrecisionFactor] because stats do not accept floating point
// values, and getting multiple significant figures on the histogram would be nice.
uint64_t FilterUtility::percentageOfTimeout(const std::chrono::milliseconds response_time,
                                            const std::chrono::milliseconds timeout) {
  // Timeouts of 0 are considered infinite. Any portion of an infinite timeout used is still
  // none of it.
  if (timeout.count() == 0) {
    return 0;
  }

  return static_cast<uint64_t>(response_time.count() * TimeoutPrecisionFactor / timeout.count());
}

void FilterUtility::setUpstreamScheme(Http::RequestHeaderMap& headers, bool use_secure_transport) {
  if (use_secure_transport) {
    headers.setReferenceScheme(Http::Headers::get().SchemeValues.Https);
  } else {
    headers.setReferenceScheme(Http::Headers::get().SchemeValues.Http);
  }
}

bool FilterUtility::shouldShadow(const ShadowPolicy& policy, Runtime::Loader& runtime,
                                 uint64_t stable_random) {
  if (policy.cluster().empty()) {
    return false;
  }

  if (policy.defaultValue().numerator() > 0) {
    return runtime.snapshot().featureEnabled(policy.runtimeKey(), policy.defaultValue(),
                                             stable_random);
  }

  if (!policy.runtimeKey().empty() &&
      !runtime.snapshot().featureEnabled(policy.runtimeKey(), 0, stable_random, 10000UL)) {
    return false;
  }

  return true;
}

FilterUtility::TimeoutData
FilterUtility::finalTimeout(const RouteEntry& route, Http::RequestHeaderMap& request_headers,
                            bool insert_envoy_expected_request_timeout_ms, bool grpc_request,
                            bool per_try_timeout_hedging_enabled,
                            bool respect_expected_rq_timeout) {
  // See if there is a user supplied timeout in a request header. If there is we take that.
  // Otherwise if the request is gRPC and a maximum gRPC timeout is configured we use the timeout
  // in the gRPC headers (or infinity when gRPC headers have no timeout), but cap that timeout to
  // the configured maximum gRPC timeout (which may also be infinity, represented by a 0 value),
  // or the default from the route config otherwise.
  TimeoutData timeout;
  if (grpc_request && route.maxGrpcTimeout()) {
    const std::chrono::milliseconds max_grpc_timeout = route.maxGrpcTimeout().value();
    auto header_timeout = Grpc::Common::getGrpcTimeout(request_headers);
    std::chrono::milliseconds grpc_timeout =
        header_timeout ? header_timeout.value() : std::chrono::milliseconds(0);
    if (route.grpcTimeoutOffset()) {
      // We only apply the offset if it won't result in grpc_timeout hitting 0 or below, as
      // setting it to 0 means infinity and a negative timeout makes no sense.
      const auto offset = *route.grpcTimeoutOffset();
      if (offset < grpc_timeout) {
        grpc_timeout -= offset;
      }
    }

    // Cap gRPC timeout to the configured maximum considering that 0 means infinity.
    if (max_grpc_timeout != std::chrono::milliseconds(0) &&
        (grpc_timeout == std::chrono::milliseconds(0) || grpc_timeout > max_grpc_timeout)) {
      grpc_timeout = max_grpc_timeout;
    }
    timeout.global_timeout_ = grpc_timeout;
  } else {
    timeout.global_timeout_ = route.timeout();
  }
  timeout.per_try_timeout_ = route.retryPolicy().perTryTimeout();

  uint64_t header_timeout;

  if (respect_expected_rq_timeout) {
    // Check if there is timeout set by egress Envoy.
    // If present, use that value as route timeout and don't override
    // *x-envoy-expected-rq-timeout-ms* header. At this point *x-envoy-upstream-rq-timeout-ms*
    // header should have been sanitized by egress Envoy.
    const Http::HeaderEntry* header_expected_timeout_entry =
        request_headers.EnvoyExpectedRequestTimeoutMs();
    if (header_expected_timeout_entry) {
      trySetGlobalTimeout(header_expected_timeout_entry, timeout);
    } else {
      const Http::HeaderEntry* header_timeout_entry =
          request_headers.EnvoyUpstreamRequestTimeoutMs();

      if (trySetGlobalTimeout(header_timeout_entry, timeout)) {
        request_headers.removeEnvoyUpstreamRequestTimeoutMs();
      }
    }
  } else {
    const Http::HeaderEntry* header_timeout_entry = request_headers.EnvoyUpstreamRequestTimeoutMs();
    if (trySetGlobalTimeout(header_timeout_entry, timeout)) {
      request_headers.removeEnvoyUpstreamRequestTimeoutMs();
    }
  }

  // See if there is a per try/retry timeout. If it's >= global we just ignore it.
  const absl::string_view per_try_timeout_entry =
      request_headers.getEnvoyUpstreamRequestPerTryTimeoutMsValue();
  if (!per_try_timeout_entry.empty()) {
    if (absl::SimpleAtoi(per_try_timeout_entry, &header_timeout)) {
      timeout.per_try_timeout_ = std::chrono::milliseconds(header_timeout);
    }
    request_headers.removeEnvoyUpstreamRequestPerTryTimeoutMs();
  }

  if (timeout.per_try_timeout_ >= timeout.global_timeout_ && timeout.global_timeout_.count() != 0) {
    timeout.per_try_timeout_ = std::chrono::milliseconds(0);
  }

  // See if there is any timeout to write in the expected timeout header.
  uint64_t expected_timeout = timeout.per_try_timeout_.count();
  // Use the global timeout if no per try timeout was specified or if we're
  // doing hedging when there are per try timeouts. Either of these scenarios
  // mean that the upstream server can use the full global timeout.
  if (per_try_timeout_hedging_enabled || expected_timeout == 0) {
    expected_timeout = timeout.global_timeout_.count();
  }

  if (insert_envoy_expected_request_timeout_ms && expected_timeout > 0) {
    request_headers.setEnvoyExpectedRequestTimeoutMs(expected_timeout);
  }

  // If we've configured max_grpc_timeout, override the grpc-timeout header with
  // the expected timeout. This ensures that the optional per try timeout is reflected
  // in grpc-timeout, ensuring that the upstream gRPC server is aware of the actual timeout.
  // If the expected timeout is 0 set no timeout, as Envoy treats 0 as infinite timeout.
  if (grpc_request && route.maxGrpcTimeout() && expected_timeout != 0) {
    Grpc::Common::toGrpcTimeout(std::chrono::milliseconds(expected_timeout), request_headers);
  }

  return timeout;
}

bool FilterUtility::trySetGlobalTimeout(const Http::HeaderEntry* header_timeout_entry,
                                        TimeoutData& timeout) {
  if (header_timeout_entry) {
    uint64_t header_timeout;
    if (absl::SimpleAtoi(header_timeout_entry->value().getStringView(), &header_timeout)) {
      timeout.global_timeout_ = std::chrono::milliseconds(header_timeout);
    }
    return true;
  }
  return false;
}

FilterUtility::HedgingParams
FilterUtility::finalHedgingParams(const RouteEntry& route,
                                  Http::RequestHeaderMap& request_headers) {
  HedgingParams hedging_params;
  hedging_params.hedge_on_per_try_timeout_ = route.hedgePolicy().hedgeOnPerTryTimeout();

  const Http::HeaderEntry* hedge_on_per_try_timeout_entry =
      request_headers.EnvoyHedgeOnPerTryTimeout();
  if (hedge_on_per_try_timeout_entry) {
    if (hedge_on_per_try_timeout_entry->value() == "true") {
      hedging_params.hedge_on_per_try_timeout_ = true;
    }
    if (hedge_on_per_try_timeout_entry->value() == "false") {
      hedging_params.hedge_on_per_try_timeout_ = false;
    }

    request_headers.removeEnvoyHedgeOnPerTryTimeout();
  }

  return hedging_params;
}

Filter::~Filter() {
  // Upstream resources should already have been cleaned.
  ASSERT(upstream_requests_.empty());
  ASSERT(!retry_state_);
}

const FilterUtility::StrictHeaderChecker::HeaderCheckResult
FilterUtility::StrictHeaderChecker::checkHeader(Http::RequestHeaderMap& headers,
                                                const Http::LowerCaseString& target_header) {
  if (target_header == Http::Headers::get().EnvoyUpstreamRequestTimeoutMs) {
    return isInteger(headers.EnvoyUpstreamRequestTimeoutMs());
  } else if (target_header == Http::Headers::get().EnvoyUpstreamRequestPerTryTimeoutMs) {
    return isInteger(headers.EnvoyUpstreamRequestPerTryTimeoutMs());
  } else if (target_header == Http::Headers::get().EnvoyMaxRetries) {
    return isInteger(headers.EnvoyMaxRetries());
  } else if (target_header == Http::Headers::get().EnvoyRetryOn) {
    return hasValidRetryFields(headers.EnvoyRetryOn(), &Router::RetryStateImpl::parseRetryOn);
  } else if (target_header == Http::Headers::get().EnvoyRetryGrpcOn) {
    return hasValidRetryFields(headers.EnvoyRetryGrpcOn(),
                               &Router::RetryStateImpl::parseRetryGrpcOn);
  }
  // Should only validate headers for which we have implemented a validator.
  NOT_REACHED_GCOVR_EXCL_LINE;
}

Stats::StatName Filter::upstreamZone(Upstream::HostDescriptionConstSharedPtr upstream_host) {
  return upstream_host ? upstream_host->localityZoneStatName() : config_.empty_stat_name_;
}

void Filter::chargeUpstreamCode(uint64_t response_status_code,
                                const Http::ResponseHeaderMap& response_headers,
                                Upstream::HostDescriptionConstSharedPtr upstream_host,
                                bool dropped) {
  // Passing the response_status_code explicitly is an optimization to avoid
  // multiple calls to slow Http::Utility::getResponseStatus.
  ASSERT(response_status_code == Http::Utility::getResponseStatus(response_headers));
  if (config_.emit_dynamic_stats_ && !callbacks_->streamInfo().healthCheck()) {
    const Http::HeaderEntry* upstream_canary_header = response_headers.EnvoyUpstreamCanary();
    const bool is_canary = (upstream_canary_header && upstream_canary_header->value() == "true") ||
                           (upstream_host ? upstream_host->canary() : false);
    const bool internal_request = Http::HeaderUtility::isEnvoyInternalRequest(*downstream_headers_);

    Stats::StatName upstream_zone = upstreamZone(upstream_host);
    Http::CodeStats::ResponseStatInfo info{config_.scope_,
                                           cluster_->statsScope(),
                                           config_.empty_stat_name_,
                                           response_status_code,
                                           internal_request,
                                           route_entry_->virtualHost().statName(),
                                           request_vcluster_ ? request_vcluster_->statName()
                                                             : config_.empty_stat_name_,
                                           config_.zone_name_,
                                           upstream_zone,
                                           is_canary};

    Http::CodeStats& code_stats = httpContext().codeStats();
    code_stats.chargeResponseStat(info);

    if (alt_stat_prefix_ != nullptr) {
      Http::CodeStats::ResponseStatInfo alt_info{config_.scope_,
                                                 cluster_->statsScope(),
                                                 alt_stat_prefix_->statName(),
                                                 response_status_code,
                                                 internal_request,
                                                 config_.empty_stat_name_,
                                                 config_.empty_stat_name_,
                                                 config_.zone_name_,
                                                 upstream_zone,
                                                 is_canary};
      code_stats.chargeResponseStat(alt_info);
    }

    if (dropped) {
      cluster_->loadReportStats().upstream_rq_dropped_.inc();
    }
    if (upstream_host && Http::CodeUtility::is5xx(response_status_code)) {
      upstream_host->stats().rq_error_.inc();
    }
  }
}

void Filter::chargeUpstreamCode(Http::Code code,
                                Upstream::HostDescriptionConstSharedPtr upstream_host,
                                bool dropped) {
  const uint64_t response_status_code = enumToInt(code);
  const auto fake_response_headers = Http::createHeaderMap<Http::ResponseHeaderMapImpl>(
      {{Http::Headers::get().Status, std::to_string(response_status_code)}});
  chargeUpstreamCode(response_status_code, *fake_response_headers, upstream_host, dropped);
}

Http::FilterHeadersStatus Filter::decodeHeaders(Http::RequestHeaderMap& headers, bool end_stream) {
  downstream_headers_ = &headers;

  // Extract debug configuration from filter state. This is used further along to determine whether
  // we should append cluster and host headers to the response, and whether to forward the request
  // upstream.
  const StreamInfo::FilterStateSharedPtr& filter_state = callbacks_->streamInfo().filterState();
  const DebugConfig* debug_config =
      filter_state->hasData<DebugConfig>(DebugConfig::key())
          ? &(filter_state->getDataReadOnly<DebugConfig>(DebugConfig::key()))
          : nullptr;

  // TODO: Maybe add a filter API for this.
  grpc_request_ = Grpc::Common::isGrpcRequestHeaders(headers);

  // Only increment rq total stat if we actually decode headers here. This does not count requests
  // that get handled by earlier filters.
  config_.stats_.rq_total_.inc();

  // Initialize the `modify_headers` function as a no-op (so we don't have to remember to check it
  // against nullptr before calling it), and feed it behavior later if/when we have cluster info
  // headers to append.
  std::function<void(Http::ResponseHeaderMap&)> modify_headers = [](Http::ResponseHeaderMap&) {};

  // Determine if there is a route entry or a direct response for the request.
  route_ = callbacks_->route();
  if (!route_) {
    config_.stats_.no_route_.inc();
    ENVOY_STREAM_LOG(debug, "no cluster match for URL '{}'", *callbacks_, headers.getPathValue());

    callbacks_->streamInfo().setResponseFlag(StreamInfo::ResponseFlag::NoRouteFound);
    callbacks_->sendLocalReply(Http::Code::NotFound, "", modify_headers, absl::nullopt,
                               StreamInfo::ResponseCodeDetails::get().RouteNotFound);
    return Http::FilterHeadersStatus::StopIteration;
  }

  // Determine if there is a direct response for the request.
  const auto* direct_response = route_->directResponseEntry();
  if (direct_response != nullptr) {
    config_.stats_.rq_direct_response_.inc();
    direct_response->rewritePathHeader(headers, !config_.suppress_envoy_headers_);
    callbacks_->streamInfo().setRouteName(direct_response->routeName());
    callbacks_->sendLocalReply(
        direct_response->responseCode(), direct_response->responseBody(),
        [this, direct_response,
         &request_headers = headers](Http::ResponseHeaderMap& response_headers) -> void {
          std::string new_path;
          if (request_headers.Path()) {
            new_path = direct_response->newPath(request_headers);
          }
          // See https://tools.ietf.org/html/rfc7231#section-7.1.2.
          const auto add_location =
              direct_response->responseCode() == Http::Code::Created ||
              Http::CodeUtility::is3xx(enumToInt(direct_response->responseCode()));
          if (!new_path.empty() && add_location) {
            response_headers.addReferenceKey(Http::Headers::get().Location, new_path);
          }
          direct_response->finalizeResponseHeaders(response_headers, callbacks_->streamInfo());
        },
        absl::nullopt, StreamInfo::ResponseCodeDetails::get().DirectResponse);
    return Http::FilterHeadersStatus::StopIteration;
  }

  // A route entry matches for the request.
  route_entry_ = route_->routeEntry();
  // If there's a route specific limit and it's smaller than general downstream
  // limits, apply the new cap.
  retry_shadow_buffer_limit_ =
      std::min(retry_shadow_buffer_limit_, route_entry_->retryShadowBufferLimit());
  callbacks_->streamInfo().setRouteName(route_entry_->routeName());
  if (debug_config && debug_config->append_cluster_) {
    // The cluster name will be appended to any local or upstream responses from this point.
    modify_headers = [this, debug_config](Http::ResponseHeaderMap& headers) {
      headers.addCopy(debug_config->cluster_header_.value_or(Http::Headers::get().EnvoyCluster),
                      route_entry_->clusterName());
    };
  }
  Upstream::ThreadLocalCluster* cluster =
      config_.cm_.getThreadLocalCluster(route_entry_->clusterName());
  if (!cluster) {
    config_.stats_.no_cluster_.inc();
    ENVOY_STREAM_LOG(debug, "unknown cluster '{}'", *callbacks_, route_entry_->clusterName());

    callbacks_->streamInfo().setResponseFlag(StreamInfo::ResponseFlag::NoRouteFound);
    callbacks_->sendLocalReply(route_entry_->clusterNotFoundResponseCode(), "", modify_headers,
                               absl::nullopt,
                               StreamInfo::ResponseCodeDetails::get().ClusterNotFound);
    return Http::FilterHeadersStatus::StopIteration;
  }
  cluster_ = cluster->info();

  // Set up stat prefixes, etc.
  request_vcluster_ = route_entry_->virtualCluster(headers);
  ENVOY_STREAM_LOG(debug, "cluster '{}' match for URL '{}'", *callbacks_,
                   route_entry_->clusterName(), headers.getPathValue());

  if (config_.strict_check_headers_ != nullptr) {
    for (const auto& header : *config_.strict_check_headers_) {
      const auto res = FilterUtility::StrictHeaderChecker::checkHeader(headers, header);
      if (!res.valid_) {
        callbacks_->streamInfo().setResponseFlag(
            StreamInfo::ResponseFlag::InvalidEnvoyRequestHeaders);
        const std::string body = fmt::format("invalid header '{}' with value '{}'",
                                             std::string(res.entry_->key().getStringView()),
                                             std::string(res.entry_->value().getStringView()));
        const std::string details =
            absl::StrCat(StreamInfo::ResponseCodeDetails::get().InvalidEnvoyRequestHeaders, "{",
                         res.entry_->key().getStringView(), "}");
        callbacks_->sendLocalReply(Http::Code::BadRequest, body, nullptr, absl::nullopt, details);
        return Http::FilterHeadersStatus::StopIteration;
      }
    }
  }

  const Http::HeaderEntry* request_alt_name = headers.EnvoyUpstreamAltStatName();
  if (request_alt_name) {
    alt_stat_prefix_ = std::make_unique<Stats::StatNameDynamicStorage>(
        request_alt_name->value().getStringView(), config_.scope_.symbolTable());
    headers.removeEnvoyUpstreamAltStatName();
  }

  // See if we are supposed to immediately kill some percentage of this cluster's traffic.
  if (cluster_->maintenanceMode()) {
    callbacks_->streamInfo().setResponseFlag(StreamInfo::ResponseFlag::UpstreamOverflow);
    chargeUpstreamCode(Http::Code::ServiceUnavailable, nullptr, true);
    callbacks_->sendLocalReply(
        Http::Code::ServiceUnavailable, "maintenance mode",
        [modify_headers, this](Http::ResponseHeaderMap& headers) {
          if (!config_.suppress_envoy_headers_) {
            headers.addReference(Http::Headers::get().EnvoyOverloaded,
                                 Http::Headers::get().EnvoyOverloadedValues.True);
          }
          // Note: append_cluster_info does not respect suppress_envoy_headers.
          modify_headers(headers);
        },
        absl::nullopt, StreamInfo::ResponseCodeDetails::get().MaintenanceMode);
    cluster_->stats().upstream_rq_maintenance_mode_.inc();
    return Http::FilterHeadersStatus::StopIteration;
  }

  // Fetch a connection pool for the upstream cluster.
  const auto& upstream_http_protocol_options = cluster_->upstreamHttpProtocolOptions();

  if (upstream_http_protocol_options.has_value()) {
    const auto parsed_authority = Http::Utility::parseAuthority(headers.getHostValue());
    if (!parsed_authority.is_ip_address_ && upstream_http_protocol_options.value().auto_sni()) {
      callbacks_->streamInfo().filterState()->setData(
          Network::UpstreamServerName::key(),
          std::make_unique<Network::UpstreamServerName>(parsed_authority.host_),
          StreamInfo::FilterState::StateType::Mutable);
    }

    if (upstream_http_protocol_options.value().auto_san_validation()) {
      callbacks_->streamInfo().filterState()->setData(
          Network::UpstreamSubjectAltNames::key(),
          std::make_unique<Network::UpstreamSubjectAltNames>(
              std::vector<std::string>{std::string(parsed_authority.host_)}),
          StreamInfo::FilterState::StateType::Mutable);
    }
  }

  transport_socket_options_ = Network::TransportSocketOptionsUtility::fromFilterState(
      *callbacks_->streamInfo().filterState());
  std::unique_ptr<GenericConnPool> generic_conn_pool = createConnPool(*cluster);

  if (!generic_conn_pool) {
    sendNoHealthyUpstreamResponse();
    return Http::FilterHeadersStatus::StopIteration;
  }
  Upstream::HostDescriptionConstSharedPtr host = generic_conn_pool->host();

  if (debug_config && debug_config->append_upstream_host_) {
    // The hostname and address will be appended to any local or upstream responses from this point,
    // possibly in addition to the cluster name.
    modify_headers = [modify_headers, debug_config, host](Http::ResponseHeaderMap& headers) {
      modify_headers(headers);
      headers.addCopy(
          debug_config->hostname_header_.value_or(Http::Headers::get().EnvoyUpstreamHostname),
          host->hostname());
      headers.addCopy(debug_config->host_address_header_.value_or(
                          Http::Headers::get().EnvoyUpstreamHostAddress),
                      host->address()->asString());
    };
  }

  // If we've been instructed not to forward the request upstream, send an empty local response.
  if (debug_config && debug_config->do_not_forward_) {
    modify_headers = [modify_headers, debug_config](Http::ResponseHeaderMap& headers) {
      modify_headers(headers);
      headers.addCopy(
          debug_config->not_forwarded_header_.value_or(Http::Headers::get().EnvoyNotForwarded),
          "true");
    };
    callbacks_->sendLocalReply(Http::Code::NoContent, "", modify_headers, absl::nullopt, "");
    return Http::FilterHeadersStatus::StopIteration;
  }

  hedging_params_ = FilterUtility::finalHedgingParams(*route_entry_, headers);

  timeout_ = FilterUtility::finalTimeout(*route_entry_, headers, !config_.suppress_envoy_headers_,
                                         grpc_request_, hedging_params_.hedge_on_per_try_timeout_,
                                         config_.respect_expected_rq_timeout_);

  // If this header is set with any value, use an alternate response code on timeout
  if (headers.EnvoyUpstreamRequestTimeoutAltResponse()) {
    timeout_response_code_ = Http::Code::NoContent;
    headers.removeEnvoyUpstreamRequestTimeoutAltResponse();
  }

  include_attempt_count_in_request_ = route_entry_->includeAttemptCountInRequest();
  if (include_attempt_count_in_request_) {
    headers.setEnvoyAttemptCount(attempt_count_);
  }

  // The router has reached a point where it is going to try to send a request upstream,
  // so now modify_headers should attach x-envoy-attempt-count to the downstream response if the
  // config flag is true.
  if (route_entry_->includeAttemptCountInResponse()) {
    modify_headers = [modify_headers, this](Http::ResponseHeaderMap& headers) {
      modify_headers(headers);

      // This header is added without checking for config_.suppress_envoy_headers_ to mirror what is
      // done for upstream requests.
      headers.setEnvoyAttemptCount(attempt_count_);
    };
  }

  // Inject the active span's tracing context into the request headers.
  callbacks_->activeSpan().injectContext(headers);

  route_entry_->finalizeRequestHeaders(headers, callbacks_->streamInfo(),
                                       !config_.suppress_envoy_headers_);
  FilterUtility::setUpstreamScheme(headers,
                                   host->transportSocketFactory().implementsSecureTransport());

  // Ensure an http transport scheme is selected before continuing with decoding.
  ASSERT(headers.Scheme());

  retry_state_ = createRetryState(
      route_entry_->retryPolicy(), headers, *cluster_, request_vcluster_, config_.runtime_,
      config_.random_, callbacks_->dispatcher(), config_.timeSource(), route_entry_->priority());

  // Determine which shadow policies to use. It's possible that we don't do any shadowing due to
  // runtime keys.
  for (const auto& shadow_policy : route_entry_->shadowPolicies()) {
    const auto& policy_ref = *shadow_policy;
    if (FilterUtility::shouldShadow(policy_ref, config_.runtime_, callbacks_->streamId())) {
      active_shadow_policies_.push_back(std::cref(policy_ref));
    }
  }

  ENVOY_STREAM_LOG(debug, "router decoding headers:\n{}", *callbacks_, headers);

  // Hang onto the modify_headers function for later use in handling upstream responses.
  modify_headers_ = modify_headers;

  UpstreamRequestPtr upstream_request =
      std::make_unique<UpstreamRequest>(*this, std::move(generic_conn_pool));
  LinkedList::moveIntoList(std::move(upstream_request), upstream_requests_);
  upstream_requests_.front()->encodeHeaders(end_stream);
  if (end_stream) {
    onRequestComplete();
  }

  return Http::FilterHeadersStatus::StopIteration;
}

std::unique_ptr<GenericConnPool>
Filter::createConnPool(Upstream::ThreadLocalCluster& thread_local_cluster) {
  GenericConnPoolFactory* factory = nullptr;
  if (cluster_->upstreamConfig().has_value()) {
    factory = Envoy::Config::Utility::getFactory<GenericConnPoolFactory>(
        cluster_->upstreamConfig().value());
    ENVOY_BUG(factory != nullptr,
              fmt::format("invalid factory type '{}', failing over to default upstream",
                          cluster_->upstreamConfig().value().DebugString()));
  }
  if (!factory) {
    factory = &Envoy::Config::Utility::getAndCheckFactoryByName<GenericConnPoolFactory>(
        "envoy.filters.connection_pools.http.generic");
  }

  bool should_tcp_proxy = false;

  if (route_entry_->connectConfig().has_value()) {
    auto method = downstream_headers_->getMethodValue();
    should_tcp_proxy = (method == Http::Headers::get().MethodValues.Connect);

    // Allow POST for proxying raw TCP if it is configured.
    if (!should_tcp_proxy && route_entry_->connectConfig().value().allow_post()) {
      should_tcp_proxy = (method == Http::Headers::get().MethodValues.Post);
    }
  }
  return factory->createGenericConnPool(thread_local_cluster, should_tcp_proxy, *route_entry_,
                                        callbacks_->streamInfo().protocol(), this);
}

void Filter::sendNoHealthyUpstreamResponse() {
  callbacks_->streamInfo().setResponseFlag(StreamInfo::ResponseFlag::NoHealthyUpstream);
  chargeUpstreamCode(Http::Code::ServiceUnavailable, nullptr, false);
  callbacks_->sendLocalReply(Http::Code::ServiceUnavailable, "no healthy upstream", modify_headers_,
                             absl::nullopt,
                             StreamInfo::ResponseCodeDetails::get().NoHealthyUpstream);
}

Http::FilterDataStatus Filter::decodeData(Buffer::Instance& data, bool end_stream) {
  // upstream_requests_.size() cannot be > 1 because that only happens when a per
  // try timeout occurs with hedge_on_per_try_timeout enabled but the per
  // try timeout timer is not started until onRequestComplete(). It could be zero
  // if the first request attempt has already failed and a retry is waiting for
  // a backoff timer.
  ASSERT(upstream_requests_.size() <= 1);

  bool buffering = (retry_state_ && retry_state_->enabled()) || !active_shadow_policies_.empty();
  if (buffering &&
      getLength(callbacks_->decodingBuffer()) + data.length() > retry_shadow_buffer_limit_) {
    // The request is larger than we should buffer. Give up on the retry/shadow
    cluster_->stats().retry_or_shadow_abandoned_.inc();
    retry_state_.reset();
    buffering = false;
    active_shadow_policies_.clear();

    // If we had to abandon buffering and there's no request in progress, abort the request and
    // clean up. This happens if the initial upstream request failed, and we are currently waiting
    // for a backoff timer before starting the next upstream attempt.
    if (upstream_requests_.empty()) {
      cleanup();
      callbacks_->sendLocalReply(
          Http::Code::InsufficientStorage, "exceeded request buffer limit while retrying upstream",
          modify_headers_, absl::nullopt,
          StreamInfo::ResponseCodeDetails::get().RequestPayloadExceededRetryBufferLimit);
      return Http::FilterDataStatus::StopIterationNoBuffer;
    }
  }

  // If we aren't buffering and there is no active request, an abort should have occurred
  // already.
  ASSERT(buffering || !upstream_requests_.empty());

  if (buffering) {
    // If we are going to buffer for retries or shadowing, we need to make a copy before encoding
    // since it's all moves from here on.
    if (!upstream_requests_.empty()) {
      Buffer::OwnedImpl copy(data);
      upstream_requests_.front()->encodeData(copy, end_stream);
    }

    // If we are potentially going to retry or shadow this request we need to buffer.
    // This will not cause the connection manager to 413 because before we hit the
    // buffer limit we give up on retries and buffering. We must buffer using addDecodedData()
    // so that all buffered data is available by the time we do request complete processing and
    // potentially shadow.
    callbacks_->addDecodedData(data, true);
  } else {
    upstream_requests_.front()->encodeData(data, end_stream);
  }

  if (end_stream) {
    onRequestComplete();
  }

  return Http::FilterDataStatus::StopIterationNoBuffer;
}

Http::FilterTrailersStatus Filter::decodeTrailers(Http::RequestTrailerMap& trailers) {
  ENVOY_STREAM_LOG(debug, "router decoding trailers:\n{}", *callbacks_, trailers);

  // upstream_requests_.size() cannot be > 1 because that only happens when a per
  // try timeout occurs with hedge_on_per_try_timeout enabled but the per
  // try timeout timer is not started until onRequestComplete(). It could be zero
  // if the first request attempt has already failed and a retry is waiting for
  // a backoff timer.
  ASSERT(upstream_requests_.size() <= 1);
  downstream_trailers_ = &trailers;
  for (auto& upstream_request : upstream_requests_) {
    upstream_request->encodeTrailers(trailers);
  }
  onRequestComplete();
  return Http::FilterTrailersStatus::StopIteration;
}

Http::FilterMetadataStatus Filter::decodeMetadata(Http::MetadataMap& metadata_map) {
  Http::MetadataMapPtr metadata_map_ptr = std::make_unique<Http::MetadataMap>(metadata_map);
  if (!upstream_requests_.empty()) {
    // TODO(soya3129): Save metadata for retry, redirect and shadowing case.
    upstream_requests_.front()->encodeMetadata(std::move(metadata_map_ptr));
  }
  return Http::FilterMetadataStatus::Continue;
}

void Filter::setDecoderFilterCallbacks(Http::StreamDecoderFilterCallbacks& callbacks) {
  callbacks_ = &callbacks;
  // As the decoder filter only pushes back via watermarks once data has reached
  // it, it can latch the current buffer limit and does not need to update the
  // limit if another filter increases it.
  //
  // The default is "do not limit". If there are configured (non-zero) buffer
  // limits, apply them here.
  if (callbacks_->decoderBufferLimit() != 0) {
    retry_shadow_buffer_limit_ = callbacks_->decoderBufferLimit();
  }
}

void Filter::cleanup() {
  // All callers of cleanup() should have cleaned out the upstream_requests_
  // list as appropriate.
  ASSERT(upstream_requests_.empty());

  retry_state_.reset();
  if (response_timeout_) {
    response_timeout_->disableTimer();
    response_timeout_.reset();
  }
}

void Filter::maybeDoShadowing() {
  for (const auto& shadow_policy_wrapper : active_shadow_policies_) {
    const auto& shadow_policy = shadow_policy_wrapper.get();

    ASSERT(!shadow_policy.cluster().empty());
    Http::RequestMessagePtr request(new Http::RequestMessageImpl(
        Http::createHeaderMap<Http::RequestHeaderMapImpl>(*downstream_headers_)));
    if (callbacks_->decodingBuffer()) {
      request->body().add(*callbacks_->decodingBuffer());
    }
    if (downstream_trailers_) {
      request->trailers(Http::createHeaderMap<Http::RequestTrailerMapImpl>(*downstream_trailers_));
    }

    auto options = Http::AsyncClient::RequestOptions()
                       .setTimeout(timeout_.global_timeout_)
                       .setParentSpan(callbacks_->activeSpan())
                       .setChildSpanName("mirror")
                       .setSampled(shadow_policy.traceSampled());
    config_.shadowWriter().shadow(shadow_policy.cluster(), std::move(request), options);
  }
}

void Filter::onRequestComplete() {
  // This should be called exactly once, when the downstream request has been received in full.
  ASSERT(!downstream_end_stream_);
  downstream_end_stream_ = true;
  Event::Dispatcher& dispatcher = callbacks_->dispatcher();
  downstream_request_complete_time_ = dispatcher.timeSource().monotonicTime();

  // Possible that we got an immediate reset.
  if (!upstream_requests_.empty()) {
    // Even if we got an immediate reset, we could still shadow, but that is a riskier change and
    // seems unnecessary right now.
    maybeDoShadowing();

    if (timeout_.global_timeout_.count() > 0) {
      response_timeout_ = dispatcher.createTimer([this]() -> void { onResponseTimeout(); });
      response_timeout_->enableTimer(timeout_.global_timeout_);
    }

    for (auto& upstream_request : upstream_requests_) {
      if (upstream_request->createPerTryTimeoutOnRequestComplete()) {
        upstream_request->setupPerTryTimeout();
      }
    }
  }
}

void Filter::onDestroy() {
  // Reset any in-flight upstream requests.
  resetAll();
  cleanup();
}

void Filter::onResponseTimeout() {
  ENVOY_STREAM_LOG(debug, "upstream timeout", *callbacks_);

  // If we had an upstream request that got a "good" response, save its
  // upstream timing information into the downstream stream info.
  if (final_upstream_request_) {
    callbacks_->streamInfo().setUpstreamTiming(final_upstream_request_->upstreamTiming());
  }

  // Reset any upstream requests that are still in flight.
  while (!upstream_requests_.empty()) {
    UpstreamRequestPtr upstream_request =
        upstream_requests_.back()->removeFromList(upstream_requests_);

    // Don't do work for upstream requests we've already seen headers for.
    if (upstream_request->awaitingHeaders()) {
      cluster_->stats().upstream_rq_timeout_.inc();
      if (request_vcluster_) {
        request_vcluster_->stats().upstream_rq_timeout_.inc();
      }

      if (cluster_->timeoutBudgetStats().has_value()) {
        // Cancel firing per-try timeout information, because the per-try timeout did not come into
        // play when the global timeout was hit.
        upstream_request->recordTimeoutBudget(false);
      }

      if (upstream_request->upstreamHost()) {
        upstream_request->upstreamHost()->stats().rq_timeout_.inc();
      }

      // If this upstream request already hit a "soft" timeout, then it
      // already recorded a timeout into outlier detection. Don't do it again.
      if (!upstream_request->outlierDetectionTimeoutRecorded()) {
        updateOutlierDetection(Upstream::Outlier::Result::LocalOriginTimeout, *upstream_request,
                               absl::optional<uint64_t>(enumToInt(timeout_response_code_)));
      }

      chargeUpstreamAbort(timeout_response_code_, false, *upstream_request);
    }
    upstream_request->resetStream();
  }

  onUpstreamTimeoutAbort(StreamInfo::ResponseFlag::UpstreamRequestTimeout,
                         StreamInfo::ResponseCodeDetails::get().UpstreamTimeout);
}

// Called when the per try timeout is hit but we didn't reset the request
// (hedge_on_per_try_timeout enabled).
void Filter::onSoftPerTryTimeout(UpstreamRequest& upstream_request) {
  // Track this as a timeout for outlier detection purposes even though we didn't
  // cancel the request yet and might get a 2xx later.
  updateOutlierDetection(Upstream::Outlier::Result::LocalOriginTimeout, upstream_request,
                         absl::optional<uint64_t>(enumToInt(timeout_response_code_)));
  upstream_request.outlierDetectionTimeoutRecorded(true);

  if (!downstream_response_started_ && retry_state_) {
    RetryStatus retry_status =
        retry_state_->shouldHedgeRetryPerTryTimeout([this]() -> void { doRetry(); });

    if (retry_status == RetryStatus::Yes) {
      pending_retries_++;

      // Don't increment upstream_host->stats().rq_error_ here, we'll do that
      // later if 1) we hit global timeout or 2) we get bad response headers
      // back.
      upstream_request.retried(true);

      // TODO: cluster stat for hedge attempted.
    } else if (retry_status == RetryStatus::NoOverflow) {
      callbacks_->streamInfo().setResponseFlag(StreamInfo::ResponseFlag::UpstreamOverflow);
    } else if (retry_status == RetryStatus::NoRetryLimitExceeded) {
      callbacks_->streamInfo().setResponseFlag(
          StreamInfo::ResponseFlag::UpstreamRetryLimitExceeded);
    }
  }
}

void Filter::onPerTryTimeout(UpstreamRequest& upstream_request) {
  if (hedging_params_.hedge_on_per_try_timeout_) {
    onSoftPerTryTimeout(upstream_request);
    return;
  }

  cluster_->stats().upstream_rq_per_try_timeout_.inc();
  if (upstream_request.upstreamHost()) {
    upstream_request.upstreamHost()->stats().rq_timeout_.inc();
  }

  upstream_request.resetStream();

  updateOutlierDetection(Upstream::Outlier::Result::LocalOriginTimeout, upstream_request,
                         absl::optional<uint64_t>(enumToInt(timeout_response_code_)));

  if (maybeRetryReset(Http::StreamResetReason::LocalReset, upstream_request)) {
    return;
  }

  chargeUpstreamAbort(timeout_response_code_, false, upstream_request);

  // Remove this upstream request from the list now that we're done with it.
  upstream_request.removeFromList(upstream_requests_);
  onUpstreamTimeoutAbort(StreamInfo::ResponseFlag::UpstreamRequestTimeout,
                         StreamInfo::ResponseCodeDetails::get().UpstreamPerTryTimeout);
}

void Filter::onStreamMaxDurationReached(UpstreamRequest& upstream_request) {
  upstream_request.resetStream();

  if (maybeRetryReset(Http::StreamResetReason::LocalReset, upstream_request)) {
    return;
  }

  upstream_request.removeFromList(upstream_requests_);
  cleanup();

  if (downstream_response_started_ &&
      !Runtime::runtimeFeatureEnabled("envoy.reloadable_features.allow_500_after_100")) {
    callbacks_->streamInfo().setResponseCodeDetails(
        StreamInfo::ResponseCodeDetails::get().UpstreamMaxStreamDurationReached);
    callbacks_->resetStream();
  } else {
    callbacks_->streamInfo().setResponseFlag(
        StreamInfo::ResponseFlag::UpstreamMaxStreamDurationReached);
    // sendLocalReply may instead reset the stream if downstream_response_started_ is true.
    callbacks_->sendLocalReply(
        Http::Code::RequestTimeout, "upstream max stream duration reached", modify_headers_,
        absl::nullopt, StreamInfo::ResponseCodeDetails::get().UpstreamMaxStreamDurationReached);
  }
}

void Filter::updateOutlierDetection(Upstream::Outlier::Result result,
                                    UpstreamRequest& upstream_request,
                                    absl::optional<uint64_t> code) {
  if (upstream_request.upstreamHost()) {
    upstream_request.upstreamHost()->outlierDetector().putResult(result, code);
  }
}

void Filter::chargeUpstreamAbort(Http::Code code, bool dropped, UpstreamRequest& upstream_request) {
  if (downstream_response_started_) {
    if (upstream_request.grpcRqSuccessDeferred()) {
      upstream_request.upstreamHost()->stats().rq_error_.inc();
      config_.stats_.rq_reset_after_downstream_response_started_.inc();
    }
  } else {
    Upstream::HostDescriptionConstSharedPtr upstream_host = upstream_request.upstreamHost();

    chargeUpstreamCode(code, upstream_host, dropped);
    // If we had non-5xx but still have been reset by backend or timeout before
    // starting response, we treat this as an error. We only get non-5xx when
    // timeout_response_code_ is used for code above, where this member can
    // assume values such as 204 (NoContent).
    if (upstream_host != nullptr && !Http::CodeUtility::is5xx(enumToInt(code))) {
      upstream_host->stats().rq_error_.inc();
    }
  }
}

void Filter::onUpstreamTimeoutAbort(StreamInfo::ResponseFlag response_flags,
                                    absl::string_view details) {
  Upstream::ClusterTimeoutBudgetStatsOptRef tb_stats = cluster()->timeoutBudgetStats();
  if (tb_stats.has_value()) {
    Event::Dispatcher& dispatcher = callbacks_->dispatcher();
    std::chrono::milliseconds response_time = std::chrono::duration_cast<std::chrono::milliseconds>(
        dispatcher.timeSource().monotonicTime() - downstream_request_complete_time_);

    tb_stats->get().upstream_rq_timeout_budget_percent_used_.recordValue(
        FilterUtility::percentageOfTimeout(response_time, timeout_.global_timeout_));
  }

  const absl::string_view body =
      timeout_response_code_ == Http::Code::GatewayTimeout ? "upstream request timeout" : "";
  onUpstreamAbort(timeout_response_code_, response_flags, body, false, details);
}

void Filter::onUpstreamAbort(Http::Code code, StreamInfo::ResponseFlag response_flags,
                             absl::string_view body, bool dropped, absl::string_view details) {
  // If we have not yet sent anything downstream, send a response with an appropriate status code.
  // Otherwise just reset the ongoing response.
  callbacks_->streamInfo().setResponseFlag(response_flags);
  if (downstream_response_started_ &&
      !Runtime::runtimeFeatureEnabled("envoy.reloadable_features.allow_500_after_100")) {
    // This will destroy any created retry timers.
    callbacks_->streamInfo().setResponseCodeDetails(details);
    cleanup();
    callbacks_->resetStream();
  } else {
    // This will destroy any created retry timers.
    cleanup();
    // sendLocalReply may instead reset the stream if downstream_response_started_ is true.
    callbacks_->sendLocalReply(
        code, body,
        [dropped, this](Http::ResponseHeaderMap& headers) {
          if (dropped && !config_.suppress_envoy_headers_) {
            headers.addReference(Http::Headers::get().EnvoyOverloaded,
                                 Http::Headers::get().EnvoyOverloadedValues.True);
          }
          modify_headers_(headers);
        },
        absl::nullopt, details);
  }
}

bool Filter::maybeRetryReset(Http::StreamResetReason reset_reason,
                             UpstreamRequest& upstream_request) {
  // We don't retry if we already started the response, don't have a retry policy defined,
  // or if we've already retried this upstream request (currently only possible if a per
  // try timeout occurred and hedge_on_per_try_timeout is enabled).
  if (downstream_response_started_ || !retry_state_ || upstream_request.retried()) {
    return false;
  }

  const RetryStatus retry_status =
      retry_state_->shouldRetryReset(reset_reason, [this]() -> void { doRetry(); });
  if (retry_status == RetryStatus::Yes) {
    pending_retries_++;

    if (upstream_request.upstreamHost()) {
      upstream_request.upstreamHost()->stats().rq_error_.inc();
    }

    upstream_request.removeFromList(upstream_requests_);
    return true;
  } else if (retry_status == RetryStatus::NoOverflow) {
    callbacks_->streamInfo().setResponseFlag(StreamInfo::ResponseFlag::UpstreamOverflow);
  } else if (retry_status == RetryStatus::NoRetryLimitExceeded) {
    callbacks_->streamInfo().setResponseFlag(StreamInfo::ResponseFlag::UpstreamRetryLimitExceeded);
  }

  return false;
}

void Filter::onUpstreamReset(Http::StreamResetReason reset_reason,
                             absl::string_view transport_failure_reason,
                             UpstreamRequest& upstream_request) {
  ENVOY_STREAM_LOG(debug, "upstream reset: reset reason: {}, transport failure reason: {}",
                   *callbacks_, Http::Utility::resetReasonToString(reset_reason),
                   transport_failure_reason);

  // TODO: The reset may also come from upstream over the wire. In this case it should be
  // treated as external origin error and distinguished from local origin error.
  // This matters only when running OutlierDetection with split_external_local_origin_errors
  // config param set to true.
  updateOutlierDetection(Upstream::Outlier::Result::LocalOriginConnectFailed, upstream_request,
                         absl::nullopt);

  if (maybeRetryReset(reset_reason, upstream_request)) {
    return;
  }

  const bool dropped = reset_reason == Http::StreamResetReason::Overflow;
  chargeUpstreamAbort(Http::Code::ServiceUnavailable, dropped, upstream_request);
  upstream_request.removeFromList(upstream_requests_);

  // If there are other in-flight requests that might see an upstream response,
  // don't return anything downstream.
  if (numRequestsAwaitingHeaders() > 0 || pending_retries_ > 0) {
    return;
  }

  const StreamInfo::ResponseFlag response_flags = streamResetReasonToResponseFlag(reset_reason);

  const std::string body =
      absl::StrCat("upstream connect error or disconnect/reset before headers. reset reason: ",
                   Http::Utility::resetReasonToString(reset_reason),
                   Runtime::runtimeFeatureEnabled(
                       "envoy.reloadable_features.http_transport_failure_reason_in_body") &&
                           !transport_failure_reason.empty()
                       ? ", transport failure reason: "
                       : "",
                   transport_failure_reason);
  callbacks_->streamInfo().setUpstreamTransportFailureReason(transport_failure_reason);
  const std::string& basic_details =
      downstream_response_started_ ? StreamInfo::ResponseCodeDetails::get().LateUpstreamReset
                                   : StreamInfo::ResponseCodeDetails::get().EarlyUpstreamReset;
  const std::string details = absl::StrCat(
      basic_details, "{", Http::Utility::resetReasonToString(reset_reason),
      transport_failure_reason.empty() ? "" : absl::StrCat(",", transport_failure_reason), "}");
  onUpstreamAbort(Http::Code::ServiceUnavailable, response_flags, body, dropped, details);
}

void Filter::onUpstreamHostSelected(Upstream::HostDescriptionConstSharedPtr host) {
  if (retry_state_ && host) {
    retry_state_->onHostAttempted(host);
  }
}

StreamInfo::ResponseFlag
Filter::streamResetReasonToResponseFlag(Http::StreamResetReason reset_reason) {
  switch (reset_reason) {
  case Http::StreamResetReason::ConnectionFailure:
    return StreamInfo::ResponseFlag::UpstreamConnectionFailure;
  case Http::StreamResetReason::ConnectionTermination:
    return StreamInfo::ResponseFlag::UpstreamConnectionTermination;
  case Http::StreamResetReason::LocalReset:
  case Http::StreamResetReason::LocalRefusedStreamReset:
    return StreamInfo::ResponseFlag::LocalReset;
  case Http::StreamResetReason::Overflow:
    return StreamInfo::ResponseFlag::UpstreamOverflow;
  case Http::StreamResetReason::RemoteReset:
  case Http::StreamResetReason::RemoteRefusedStreamReset:
  case Http::StreamResetReason::ConnectError:
    return StreamInfo::ResponseFlag::UpstreamRemoteReset;
  }

  NOT_REACHED_GCOVR_EXCL_LINE;
}

void Filter::handleNon5xxResponseHeaders(absl::optional<Grpc::Status::GrpcStatus> grpc_status,
                                         UpstreamRequest& upstream_request, bool end_stream,
                                         uint64_t grpc_to_http_status) {
  // We need to defer gRPC success until after we have processed grpc-status in
  // the trailers.
  if (grpc_request_) {
    if (end_stream) {
      if (grpc_status && !Http::CodeUtility::is5xx(grpc_to_http_status)) {
        upstream_request.upstreamHost()->stats().rq_success_.inc();
      } else {
        upstream_request.upstreamHost()->stats().rq_error_.inc();
      }
    } else {
      upstream_request.grpcRqSuccessDeferred(true);
    }
  } else {
    upstream_request.upstreamHost()->stats().rq_success_.inc();
  }
}

void Filter::onUpstream100ContinueHeaders(Http::ResponseHeaderMapPtr&& headers,
                                          UpstreamRequest& upstream_request) {
  chargeUpstreamCode(100, *headers, upstream_request.upstreamHost(), false);
  ENVOY_STREAM_LOG(debug, "upstream 100 continue", *callbacks_);

  downstream_response_started_ = true;
  final_upstream_request_ = &upstream_request;
  resetOtherUpstreams(upstream_request);

  // Don't send retries after 100-Continue has been sent on. Arguably we could attempt to do a
  // retry, assume the next upstream would also send an 100-Continue and swallow the second one
  // but it's sketchy (as the subsequent upstream might not send a 100-Continue) and not worth
  // the complexity until someone asks for it.
  retry_state_.reset();

  // We coalesce 100-continue headers here, to prevent encoder filters and HCM from having to worry
  // about this. This is done in the router filter, rather than UpstreamRequest, since we want to
  // potentially coalesce across retries and multiple upstream requests in the future, even though
  // we currently don't support retry after 100.
  // It's plausible that this functionality might need to move to HCM in the future for internal
  // redirects, but we would need to maintain the "only call encode100ContinueHeaders() once"
  // invariant.
  if (!downstream_100_continue_headers_encoded_) {
    downstream_100_continue_headers_encoded_ = true;
    callbacks_->encode100ContinueHeaders(std::move(headers));
  }
}

void Filter::resetAll() {
  while (!upstream_requests_.empty()) {
    upstream_requests_.back()->removeFromList(upstream_requests_)->resetStream();
  }
}

void Filter::resetOtherUpstreams(UpstreamRequest& upstream_request) {
  // Pop each upstream request on the list and reset it if it's not the one
  // provided. At the end we'll move it back into the list.
  UpstreamRequestPtr final_upstream_request;
  while (!upstream_requests_.empty()) {
    UpstreamRequestPtr upstream_request_tmp =
        upstream_requests_.back()->removeFromList(upstream_requests_);
    if (upstream_request_tmp.get() != &upstream_request) {
      upstream_request_tmp->resetStream();
      // TODO: per-host stat for hedge abandoned.
      // TODO: cluster stat for hedge abandoned.
    } else {
      final_upstream_request = std::move(upstream_request_tmp);
    }
  }

  ASSERT(final_upstream_request);
  // Now put the final request back on this list.
  LinkedList::moveIntoList(std::move(final_upstream_request), upstream_requests_);
}

void Filter::onUpstreamHeaders(uint64_t response_code, Http::ResponseHeaderMapPtr&& headers,
                               UpstreamRequest& upstream_request, bool end_stream) {
  ENVOY_STREAM_LOG(debug, "upstream headers complete: end_stream={}", *callbacks_, end_stream);

  modify_headers_(*headers);
  // When grpc-status appears in response headers, convert grpc-status to HTTP status code
  // for outlier detection. This does not currently change any stats or logging and does not
  // handle the case when an error grpc-status is sent as a trailer.
  absl::optional<Grpc::Status::GrpcStatus> grpc_status;
  uint64_t grpc_to_http_status = 0;
  if (grpc_request_) {
    grpc_status = Grpc::Common::getGrpcStatus(*headers);
    if (grpc_status.has_value()) {
      grpc_to_http_status = Grpc::Utility::grpcToHttpStatus(grpc_status.value());
    }
  }

  if (grpc_status.has_value()) {
    upstream_request.upstreamHost()->outlierDetector().putHttpResponseCode(grpc_to_http_status);
  } else {
    upstream_request.upstreamHost()->outlierDetector().putHttpResponseCode(response_code);
  }

  if (headers->EnvoyImmediateHealthCheckFail() != nullptr) {
    upstream_request.upstreamHost()->healthChecker().setUnhealthy(
        Upstream::HealthCheckHostMonitor::UnhealthyType::ImmediateHealthCheckFail);
  }

  bool could_not_retry = false;

  // Check if this upstream request was already retried, for instance after
  // hitting a per try timeout. Don't retry it if we already have.
  if (retry_state_) {
    if (upstream_request.retried()) {
      // We already retried this request (presumably for a per try timeout) so
      // we definitely won't retry it again. Check if we would have retried it
      // if we could.
      could_not_retry = retry_state_->wouldRetryFromHeaders(*headers);
    } else {
      const RetryStatus retry_status =
          retry_state_->shouldRetryHeaders(*headers, [this]() -> void { doRetry(); });
      if (retry_status == RetryStatus::Yes) {
        pending_retries_++;
        upstream_request.upstreamHost()->stats().rq_error_.inc();
        Http::CodeStats& code_stats = httpContext().codeStats();
        code_stats.chargeBasicResponseStat(cluster_->statsScope(),
                                           config_.stats_.stat_names_.retry_,
                                           static_cast<Http::Code>(response_code));

        if (!end_stream || !upstream_request.encodeComplete()) {
          upstream_request.resetStream();
        }
        upstream_request.removeFromList(upstream_requests_);

        return;
      } else if (retry_status == RetryStatus::NoOverflow) {
        callbacks_->streamInfo().setResponseFlag(StreamInfo::ResponseFlag::UpstreamOverflow);
        could_not_retry = true;
      } else if (retry_status == RetryStatus::NoRetryLimitExceeded) {
        callbacks_->streamInfo().setResponseFlag(
            StreamInfo::ResponseFlag::UpstreamRetryLimitExceeded);
        could_not_retry = true;
      }
    }
  }

  if (route_entry_->internalRedirectPolicy().enabled() &&
      route_entry_->internalRedirectPolicy().shouldRedirectForResponseCode(
          static_cast<Http::Code>(response_code)) &&
      setupRedirect(*headers, upstream_request)) {
    return;
    // If the redirect could not be handled, fail open and let it pass to the
    // next downstream.
  }

  // Check if we got a "bad" response, but there are still upstream requests in
  // flight awaiting headers or scheduled retries. If so, exit to give them a
  // chance to return before returning a response downstream.
  if (could_not_retry && (numRequestsAwaitingHeaders() > 0 || pending_retries_ > 0)) {
    upstream_request.upstreamHost()->stats().rq_error_.inc();

    // Reset the stream because there are other in-flight requests that we'll
    // wait around for and we're not interested in consuming any body/trailers.
    upstream_request.removeFromList(upstream_requests_)->resetStream();
    return;
  }

  // Make sure any retry timers are destroyed since we may not call cleanup() if end_stream is
  // false.
  if (retry_state_) {
    retry_state_.reset();
  }

  // Only send upstream service time if we received the complete request and this is not a
  // premature response.
  if (DateUtil::timePointValid(downstream_request_complete_time_)) {
    Event::Dispatcher& dispatcher = callbacks_->dispatcher();
    MonotonicTime response_received_time = dispatcher.timeSource().monotonicTime();
    std::chrono::milliseconds ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        response_received_time - downstream_request_complete_time_);
    if (!config_.suppress_envoy_headers_) {
      headers->setEnvoyUpstreamServiceTime(ms.count());
    }
  }

  upstream_request.upstreamCanary(
      (headers->EnvoyUpstreamCanary() && headers->EnvoyUpstreamCanary()->value() == "true") ||
      upstream_request.upstreamHost()->canary());
  chargeUpstreamCode(response_code, *headers, upstream_request.upstreamHost(), false);
  if (!Http::CodeUtility::is5xx(response_code)) {
    handleNon5xxResponseHeaders(grpc_status, upstream_request, end_stream, grpc_to_http_status);
  }

  // Append routing cookies
  for (const auto& header_value : downstream_set_cookies_) {
    headers->addReferenceKey(Http::Headers::get().SetCookie, header_value);
  }

  // TODO(zuercher): If access to response_headers_to_add (at any level) is ever needed outside
  // Router::Filter we'll need to find a better location for this work. One possibility is to
  // provide finalizeResponseHeaders functions on the Router::Config and VirtualHost interfaces.
  route_entry_->finalizeResponseHeaders(*headers, callbacks_->streamInfo());

  downstream_response_started_ = true;
  final_upstream_request_ = &upstream_request;
  resetOtherUpstreams(upstream_request);
  if (end_stream) {
    onUpstreamComplete(upstream_request);
  }

  callbacks_->encodeHeaders(std::move(headers), end_stream,
                            StreamInfo::ResponseCodeDetails::get().ViaUpstream);
}

void Filter::onUpstreamData(Buffer::Instance& data, UpstreamRequest& upstream_request,
                            bool end_stream) {
  // This should be true because when we saw headers we either reset the stream
  // (hence wouldn't have made it to onUpstreamData) or all other in-flight
  // streams.
  ASSERT(upstream_requests_.size() == 1);
  if (end_stream) {
    // gRPC request termination without trailers is an error.
    if (upstream_request.grpcRqSuccessDeferred()) {
      upstream_request.upstreamHost()->stats().rq_error_.inc();
    }
    onUpstreamComplete(upstream_request);
  }

  callbacks_->encodeData(data, end_stream);
}

void Filter::onUpstreamTrailers(Http::ResponseTrailerMapPtr&& trailers,
                                UpstreamRequest& upstream_request) {
  // This should be true because when we saw headers we either reset the stream
  // (hence wouldn't have made it to onUpstreamTrailers) or all other in-flight
  // streams.
  ASSERT(upstream_requests_.size() == 1);

  if (upstream_request.grpcRqSuccessDeferred()) {
    absl::optional<Grpc::Status::GrpcStatus> grpc_status = Grpc::Common::getGrpcStatus(*trailers);
    if (grpc_status &&
        !Http::CodeUtility::is5xx(Grpc::Utility::grpcToHttpStatus(grpc_status.value()))) {
      upstream_request.upstreamHost()->stats().rq_success_.inc();
    } else {
      upstream_request.upstreamHost()->stats().rq_error_.inc();
    }
  }

  onUpstreamComplete(upstream_request);

  callbacks_->encodeTrailers(std::move(trailers));
}

void Filter::onUpstreamMetadata(Http::MetadataMapPtr&& metadata_map) {
  callbacks_->encodeMetadata(std::move(metadata_map));
}

void Filter::onUpstreamComplete(UpstreamRequest& upstream_request) {
  if (!downstream_end_stream_) {
    upstream_request.resetStream();
  }
  callbacks_->streamInfo().setUpstreamTiming(final_upstream_request_->upstreamTiming());

  Event::Dispatcher& dispatcher = callbacks_->dispatcher();
  std::chrono::milliseconds response_time = std::chrono::duration_cast<std::chrono::milliseconds>(
      dispatcher.timeSource().monotonicTime() - downstream_request_complete_time_);

  Upstream::ClusterTimeoutBudgetStatsOptRef tb_stats = cluster()->timeoutBudgetStats();
  if (tb_stats.has_value()) {
    tb_stats->get().upstream_rq_timeout_budget_percent_used_.recordValue(
        FilterUtility::percentageOfTimeout(response_time, timeout_.global_timeout_));
  }

  if (config_.emit_dynamic_stats_ && !callbacks_->streamInfo().healthCheck() &&
      DateUtil::timePointValid(downstream_request_complete_time_)) {
    upstream_request.upstreamHost()->outlierDetector().putResponseTime(response_time);
    const bool internal_request = Http::HeaderUtility::isEnvoyInternalRequest(*downstream_headers_);

    Http::CodeStats& code_stats = httpContext().codeStats();
    Http::CodeStats::ResponseTimingInfo info{config_.scope_,
                                             cluster_->statsScope(),
                                             config_.empty_stat_name_,
                                             response_time,
                                             upstream_request.upstreamCanary(),
                                             internal_request,
                                             route_entry_->virtualHost().statName(),
                                             request_vcluster_ ? request_vcluster_->statName()
                                                               : config_.empty_stat_name_,
                                             config_.zone_name_,
                                             upstreamZone(upstream_request.upstreamHost())};

    code_stats.chargeResponseTiming(info);

    if (alt_stat_prefix_ != nullptr) {
      Http::CodeStats::ResponseTimingInfo info{config_.scope_,
                                               cluster_->statsScope(),
                                               alt_stat_prefix_->statName(),
                                               response_time,
                                               upstream_request.upstreamCanary(),
                                               internal_request,
                                               config_.empty_stat_name_,
                                               config_.empty_stat_name_,
                                               config_.zone_name_,
                                               upstreamZone(upstream_request.upstreamHost())};

      code_stats.chargeResponseTiming(info);
    }
  }

  upstream_request.removeFromList(upstream_requests_);
  cleanup();
}

bool Filter::setupRedirect(const Http::ResponseHeaderMap& headers,
                           UpstreamRequest& upstream_request) {
  ENVOY_STREAM_LOG(debug, "attempting internal redirect", *callbacks_);
  const Http::HeaderEntry* location = headers.Location();

  // If the internal redirect succeeds, callbacks_->recreateStream() will result in the
  // destruction of this filter before the stream is marked as complete, and onDestroy will reset
  // the stream.
  //
  // Normally when a stream is complete we signal this by resetting the upstream but this cam not
  // be done in this case because if recreateStream fails, the "failure" path continues to call
  // code in onUpstreamHeaders which requires the upstream *not* be reset. To avoid onDestroy
  // performing a spurious stream reset in the case recreateStream() succeeds, we explicitly track
  // stream completion here and check it in onDestroy. This is annoyingly complicated but is
  // better than needlessly resetting streams.
  attempting_internal_redirect_with_complete_stream_ =
      upstream_request.upstreamTiming().last_upstream_rx_byte_received_ && downstream_end_stream_;

  // Redirects are not supported for streaming requests yet.
  if (downstream_end_stream_ &&
      !callbacks_->decodingBuffer() && // Redirects with body not yet supported.
      location != nullptr &&
      convertRequestHeadersForInternalRedirect(*downstream_headers_, *location) &&
      callbacks_->recreateStream(&headers)) {
    cluster_->stats().upstream_internal_redirect_succeeded_total_.inc();
    return true;
  }

  attempting_internal_redirect_with_complete_stream_ = false;

  ENVOY_STREAM_LOG(debug, "Internal redirect failed", *callbacks_);
  cluster_->stats().upstream_internal_redirect_failed_total_.inc();
  return false;
}

bool Filter::convertRequestHeadersForInternalRedirect(Http::RequestHeaderMap& downstream_headers,
                                                      const Http::HeaderEntry& internal_redirect) {
  if (!downstream_headers.Path()) {
    ENVOY_STREAM_LOG(trace, "no path in downstream_headers", *callbacks_);
    return false;
  }

  // Make sure the redirect response contains a URL to redirect to.
  if (internal_redirect.value().getStringView().empty()) {
    config_.stats_.passthrough_internal_redirect_bad_location_.inc();
    return false;
  }
  Http::Utility::Url absolute_url;
  if (!absolute_url.initialize(internal_redirect.value().getStringView(), false)) {
    config_.stats_.passthrough_internal_redirect_bad_location_.inc();
    return false;
  }

  const auto& policy = route_entry_->internalRedirectPolicy();
  // Don't allow serving TLS responses over plaintext unless allowed by policy.
  const bool scheme_is_http = schemeIsHttp(downstream_headers, *callbacks_->connection());
  const bool target_is_http = absolute_url.scheme() == Http::Headers::get().SchemeValues.Http;
  if (!policy.isCrossSchemeRedirectAllowed() && scheme_is_http != target_is_http) {
    config_.stats_.passthrough_internal_redirect_unsafe_scheme_.inc();
    return false;
  }

  const StreamInfo::FilterStateSharedPtr& filter_state = callbacks_->streamInfo().filterState();
  // Make sure that performing the redirect won't result in exceeding the configured number of
  // redirects allowed for this route.
  if (!filter_state->hasData<StreamInfo::UInt32Accessor>(NumInternalRedirectsFilterStateName)) {
    filter_state->setData(
        NumInternalRedirectsFilterStateName, std::make_shared<StreamInfo::UInt32AccessorImpl>(0),
        StreamInfo::FilterState::StateType::Mutable, StreamInfo::FilterState::LifeSpan::Request);
  }
  StreamInfo::UInt32Accessor& num_internal_redirect =
      filter_state->getDataMutable<StreamInfo::UInt32Accessor>(NumInternalRedirectsFilterStateName);

  if (num_internal_redirect.value() >= policy.maxInternalRedirects()) {
    config_.stats_.passthrough_internal_redirect_too_many_redirects_.inc();
    return false;
  }
  // Copy the old values, so they can be restored if the redirect fails.
  const std::string original_host(downstream_headers.getHostValue());
  const std::string original_path(downstream_headers.getPathValue());
  const bool scheme_is_set = (downstream_headers.Scheme() != nullptr);
  Cleanup restore_original_headers(
      [&downstream_headers, original_host, original_path, scheme_is_set, scheme_is_http]() {
        downstream_headers.setHost(original_host);
        downstream_headers.setPath(original_path);
        if (scheme_is_set) {
          downstream_headers.setScheme(scheme_is_http ? Http::Headers::get().SchemeValues.Http
                                                      : Http::Headers::get().SchemeValues.Https);
        }
      });

  // Replace the original host, scheme and path.
  downstream_headers.setScheme(absolute_url.scheme());
  downstream_headers.setHost(absolute_url.hostAndPort());
  downstream_headers.setPath(absolute_url.pathAndQueryParams());

  callbacks_->clearRouteCache();
  const auto route = callbacks_->route();
  // Don't allow a redirect to a non existing route.
  if (!route) {
    config_.stats_.passthrough_internal_redirect_no_route_.inc();
    return false;
  }

  const auto& route_name = route->routeEntry()->routeName();
  for (const auto& predicate : policy.predicates()) {
    if (!predicate->acceptTargetRoute(*filter_state, route_name, !scheme_is_http,
                                      !target_is_http)) {
      config_.stats_.passthrough_internal_redirect_predicate_.inc();
      ENVOY_STREAM_LOG(trace, "rejecting redirect targeting {}, by {} predicate", *callbacks_,
                       route_name, predicate->name());
      return false;
    }
  }

  num_internal_redirect.increment();
  restore_original_headers.cancel();
  // Preserve the original request URL for the second pass.
  downstream_headers.setEnvoyOriginalUrl(absl::StrCat(scheme_is_http
                                                          ? Http::Headers::get().SchemeValues.Http
                                                          : Http::Headers::get().SchemeValues.Https,
                                                      "://", original_host, original_path));
  return true;
}

void Filter::doRetry() {
  ENVOY_STREAM_LOG(debug, "performing retry", *callbacks_);

  is_retry_ = true;
  attempt_count_++;
  ASSERT(pending_retries_ > 0);
  pending_retries_--;

  // Clusters can technically get removed by CDS during a retry. Make sure it still exists.
  const auto cluster = config_.cm_.getThreadLocalCluster(route_entry_->clusterName());
  std::unique_ptr<GenericConnPool> generic_conn_pool;
  if (cluster != nullptr) {
    cluster_ = cluster->info();
    generic_conn_pool = createConnPool(*cluster);
  }

  if (!generic_conn_pool) {
    sendNoHealthyUpstreamResponse();
    cleanup();
    return;
  }
  UpstreamRequestPtr upstream_request =
      std::make_unique<UpstreamRequest>(*this, std::move(generic_conn_pool));

  if (include_attempt_count_in_request_) {
    downstream_headers_->setEnvoyAttemptCount(attempt_count_);
  }

  UpstreamRequest* upstream_request_tmp = upstream_request.get();
  LinkedList::moveIntoList(std::move(upstream_request), upstream_requests_);
  upstream_requests_.front()->encodeHeaders(!callbacks_->decodingBuffer() &&
                                            !downstream_trailers_ && downstream_end_stream_);
  // It's possible we got immediately reset which means the upstream request we just
  // added to the front of the list might have been removed, so we need to check to make
  // sure we don't encodeData on the wrong request.
  if (!upstream_requests_.empty() && (upstream_requests_.front().get() == upstream_request_tmp)) {
    if (callbacks_->decodingBuffer()) {
      // If we are doing a retry we need to make a copy.
      Buffer::OwnedImpl copy(*callbacks_->decodingBuffer());
      upstream_requests_.front()->encodeData(copy, !downstream_trailers_ && downstream_end_stream_);
    }

    if (downstream_trailers_) {
      upstream_requests_.front()->encodeTrailers(*downstream_trailers_);
    }
  }
}

uint32_t Filter::numRequestsAwaitingHeaders() {
  return std::count_if(upstream_requests_.begin(), upstream_requests_.end(),
                       [](const auto& req) -> bool { return req->awaitingHeaders(); });
}

RetryStatePtr ProdFilter::createRetryState(const RetryPolicy& policy,
                                           Http::RequestHeaderMap& request_headers,
                                           const Upstream::ClusterInfo& cluster,
                                           const VirtualCluster* vcluster, Runtime::Loader& runtime,
                                           Random::RandomGenerator& random,
                                           Event::Dispatcher& dispatcher, TimeSource& time_source,
                                           Upstream::ResourcePriority priority) {
  return RetryStateImpl::create(policy, request_headers, cluster, vcluster, runtime, random,
                                dispatcher, time_source, priority);
}

} // namespace Router
} // namespace Envoy
