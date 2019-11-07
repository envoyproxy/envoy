#include "common/router/router.h"

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>

#include "envoy/event/dispatcher.h"
#include "envoy/event/timer.h"
#include "envoy/grpc/status.h"
#include "envoy/http/conn_pool.h"
#include "envoy/runtime/runtime.h"
#include "envoy/upstream/cluster_manager.h"
#include "envoy/upstream/upstream.h"

#include "common/common/assert.h"
#include "common/common/empty_string.h"
#include "common/common/enum_to_int.h"
#include "common/common/scope_tracker.h"
#include "common/common/utility.h"
#include "common/grpc/common.h"
#include "common/http/codes.h"
#include "common/http/header_map_impl.h"
#include "common/http/headers.h"
#include "common/http/message_impl.h"
#include "common/http/utility.h"
#include "common/network/application_protocol.h"
#include "common/network/transport_socket_options_impl.h"
#include "common/router/config_impl.h"
#include "common/router/debug_config.h"
#include "common/router/retry_state_impl.h"
#include "common/runtime/runtime_impl.h"
#include "common/tracing/http_tracer_impl.h"

#include "extensions/filters/http/well_known_names.h"

namespace Envoy {
namespace Router {
namespace {
uint32_t getLength(const Buffer::Instance* instance) { return instance ? instance->length() : 0; }

bool schemeIsHttp(const Http::HeaderMap& downstream_headers,
                  const Network::Connection& connection) {
  if (downstream_headers.ForwardedProto() &&
      downstream_headers.ForwardedProto()->value().getStringView() ==
          Http::Headers::get().SchemeValues.Http) {
    return true;
  }
  if (!connection.ssl()) {
    return true;
  }
  return false;
}

bool convertRequestHeadersForInternalRedirect(Http::HeaderMap& downstream_headers,
                                              const Http::HeaderEntry& internal_redirect,
                                              const Network::Connection& connection) {
  // Envoy does not currently support multiple rounds of redirects.
  if (downstream_headers.EnvoyOriginalUrl()) {
    return false;
  }
  // Make sure the redirect response contains a URL to redirect to.
  if (internal_redirect.value().getStringView().length() == 0) {
    return false;
  }

  Http::Utility::Url absolute_url;
  if (!absolute_url.initialize(internal_redirect.value().getStringView())) {
    return false;
  }

  bool scheme_is_http = schemeIsHttp(downstream_headers, connection);
  if (scheme_is_http && absolute_url.scheme() == Http::Headers::get().SchemeValues.Https) {
    // Don't allow serving TLS responses over plaintext.
    return false;
  }

  // Preserve the original request URL for the second pass.
  downstream_headers.insertEnvoyOriginalUrl().value(
      absl::StrCat(scheme_is_http ? Http::Headers::get().SchemeValues.Http
                                  : Http::Headers::get().SchemeValues.Https,
                   "://", downstream_headers.Host()->value().getStringView(),
                   downstream_headers.Path()->value().getStringView()));

  // Replace the original host, scheme and path.
  downstream_headers.insertScheme().value(std::string(absolute_url.scheme()));
  downstream_headers.insertHost().value(std::string(absolute_url.host_and_port()));
  downstream_headers.insertPath().value(std::string(absolute_url.path_and_query_params()));

  return true;
}

} // namespace

void FilterUtility::setUpstreamScheme(Http::HeaderMap& headers, bool use_secure_transport) {
  if (use_secure_transport) {
    headers.insertScheme().value().setReference(Http::Headers::get().SchemeValues.Https);
  } else {
    headers.insertScheme().value().setReference(Http::Headers::get().SchemeValues.Http);
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
FilterUtility::finalTimeout(const RouteEntry& route, Http::HeaderMap& request_headers,
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
    std::chrono::milliseconds grpc_timeout = Grpc::Common::getGrpcTimeout(request_headers);
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
    Http::HeaderEntry* header_expected_timeout_entry =
        request_headers.EnvoyExpectedRequestTimeoutMs();
    if (header_expected_timeout_entry) {
      trySetGlobalTimeout(header_expected_timeout_entry, timeout);
    } else {
      Http::HeaderEntry* header_timeout_entry = request_headers.EnvoyUpstreamRequestTimeoutMs();

      if (trySetGlobalTimeout(header_timeout_entry, timeout)) {
        request_headers.removeEnvoyUpstreamRequestTimeoutMs();
      }
    }
  } else {
    Http::HeaderEntry* header_timeout_entry = request_headers.EnvoyUpstreamRequestTimeoutMs();
    if (trySetGlobalTimeout(header_timeout_entry, timeout)) {
      request_headers.removeEnvoyUpstreamRequestTimeoutMs();
    }
  }

  // See if there is a per try/retry timeout. If it's >= global we just ignore it.
  Http::HeaderEntry* per_try_timeout_entry = request_headers.EnvoyUpstreamRequestPerTryTimeoutMs();
  if (per_try_timeout_entry) {
    if (absl::SimpleAtoi(per_try_timeout_entry->value().getStringView(), &header_timeout)) {
      timeout.per_try_timeout_ = std::chrono::milliseconds(header_timeout);
    }
    request_headers.removeEnvoyUpstreamRequestPerTryTimeoutMs();
  }

  if (timeout.per_try_timeout_ >= timeout.global_timeout_) {
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
    request_headers.insertEnvoyExpectedRequestTimeoutMs().value(expected_timeout);
  }

  // If we've configured max_grpc_timeout, override the grpc-timeout header with
  // the expected timeout. This ensures that the optional per try timeout is reflected
  // in grpc-timeout, ensuring that the upstream gRPC server is aware of the actual timeout.
  // If the expected timeout is 0 set no timeout, as Envoy treats 0 as infinite timeout.
  if (grpc_request && route.maxGrpcTimeout() && expected_timeout != 0) {
    Grpc::Common::toGrpcTimeout(std::chrono::milliseconds(expected_timeout),
                                request_headers.insertGrpcTimeout().value());
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

FilterUtility::HedgingParams FilterUtility::finalHedgingParams(const RouteEntry& route,
                                                               Http::HeaderMap& request_headers) {
  HedgingParams hedging_params;
  hedging_params.hedge_on_per_try_timeout_ = route.hedgePolicy().hedgeOnPerTryTimeout();

  Http::HeaderEntry* hedge_on_per_try_timeout_entry = request_headers.EnvoyHedgeOnPerTryTimeout();
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
FilterUtility::StrictHeaderChecker::checkHeader(Http::HeaderMap& headers,
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
                                const Http::HeaderMap& response_headers,
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
  Http::HeaderMapImpl fake_response_headers{
      {Http::Headers::get().Status, std::to_string(response_status_code)}};
  chargeUpstreamCode(response_status_code, fake_response_headers, upstream_host, dropped);
}

Http::FilterHeadersStatus Filter::decodeHeaders(Http::HeaderMap& headers, bool end_stream) {
  // Do a common header check. We make sure that all outgoing requests have all HTTP/2 headers.
  // These get stripped by HTTP/1 codec where applicable.
  ASSERT(headers.Path());
  ASSERT(headers.Method());
  ASSERT(headers.Host());

  downstream_headers_ = &headers;

  // Extract debug configuration from filter state. This is used further along to determine whether
  // we should append cluster and host headers to the response, and whether to forward the request
  // upstream.
  const StreamInfo::FilterState& filter_state = callbacks_->streamInfo().filterState();
  const DebugConfig* debug_config =
      filter_state.hasData<DebugConfig>(DebugConfig::key())
          ? &(filter_state.getDataReadOnly<DebugConfig>(DebugConfig::key()))
          : nullptr;

  // TODO: Maybe add a filter API for this.
  grpc_request_ = Grpc::Common::hasGrpcContentType(headers);

  // Only increment rq total stat if we actually decode headers here. This does not count requests
  // that get handled by earlier filters.
  config_.stats_.rq_total_.inc();

  // Initialize the `modify_headers` function as a no-op (so we don't have to remember to check it
  // against nullptr before calling it), and feed it behavior later if/when we have cluster info
  // headers to append.
  std::function<void(Http::HeaderMap&)> modify_headers = [](Http::HeaderMap&) {};

  // Determine if there is a route entry or a direct response for the request.
  route_ = callbacks_->route();
  if (!route_) {
    config_.stats_.no_route_.inc();
    ENVOY_STREAM_LOG(debug, "no cluster match for URL '{}'", *callbacks_,
                     headers.Path()->value().getStringView());

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
    callbacks_->sendLocalReply(
        direct_response->responseCode(), direct_response->responseBody(),
        [this, direct_response,
         &request_headers = headers](Http::HeaderMap& response_headers) -> void {
          const auto new_path = direct_response->newPath(request_headers);
          if (!new_path.empty()) {
            response_headers.addReferenceKey(Http::Headers::get().Location, new_path);
          }
          direct_response->finalizeResponseHeaders(response_headers, callbacks_->streamInfo());
        },
        absl::nullopt, StreamInfo::ResponseCodeDetails::get().DirectResponse);
    callbacks_->streamInfo().setRouteName(direct_response->routeName());
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
    modify_headers = [this, debug_config](Http::HeaderMap& headers) {
      headers.addCopy(debug_config->cluster_header_.value_or(Http::Headers::get().EnvoyCluster),
                      route_entry_->clusterName());
    };
  }
  Upstream::ThreadLocalCluster* cluster = config_.cm_.get(route_entry_->clusterName());
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
                   route_entry_->clusterName(), headers.Path()->value().getStringView());

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
    // TODO(#7003): converting this header value into a StatName requires
    // taking a global symbol-table lock. This is not a frequently used feature,
    // but may not be the only occurrence of this pattern, where it's difficult
    // or impossible to pre-compute a StatName for a component of a stat name.
    alt_stat_prefix_ = std::make_unique<Stats::StatNameManagedStorage>(
        request_alt_name->value().getStringView(), config_.scope_.symbolTable());
    headers.removeEnvoyUpstreamAltStatName();
  }

  // See if we are supposed to immediately kill some percentage of this cluster's traffic.
  if (cluster_->maintenanceMode()) {
    callbacks_->streamInfo().setResponseFlag(StreamInfo::ResponseFlag::UpstreamOverflow);
    chargeUpstreamCode(Http::Code::ServiceUnavailable, nullptr, true);
    callbacks_->sendLocalReply(
        Http::Code::ServiceUnavailable, "maintenance mode",
        [modify_headers, this](Http::HeaderMap& headers) {
          if (!config_.suppress_envoy_headers_) {
            headers.insertEnvoyOverloaded().value(Http::Headers::get().EnvoyOverloadedValues.True);
          }
          // Note: append_cluster_info does not respect suppress_envoy_headers.
          modify_headers(headers);
        },
        absl::nullopt, StreamInfo::ResponseCodeDetails::get().MaintenanceMode);
    cluster_->stats().upstream_rq_maintenance_mode_.inc();
    return Http::FilterHeadersStatus::StopIteration;
  }

  // Fetch a connection pool for the upstream cluster.
  Http::ConnectionPool::Instance* conn_pool = getConnPool();
  if (!conn_pool) {
    sendNoHealthyUpstreamResponse();
    return Http::FilterHeadersStatus::StopIteration;
  }
  if (debug_config && debug_config->append_upstream_host_) {
    // The hostname and address will be appended to any local or upstream responses from this point,
    // possibly in addition to the cluster name.
    modify_headers = [modify_headers, debug_config, conn_pool](Http::HeaderMap& headers) {
      modify_headers(headers);
      headers.addCopy(
          debug_config->hostname_header_.value_or(Http::Headers::get().EnvoyUpstreamHostname),
          conn_pool->host()->hostname());
      headers.addCopy(debug_config->host_address_header_.value_or(
                          Http::Headers::get().EnvoyUpstreamHostAddress),
                      conn_pool->host()->address()->asString());
    };
  }

  // If we've been instructed not to forward the request upstream, send an empty local response.
  if (debug_config && debug_config->do_not_forward_) {
    modify_headers = [modify_headers, debug_config](Http::HeaderMap& headers) {
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

  include_attempt_count_ = route_entry_->includeAttemptCount();
  if (include_attempt_count_) {
    headers.insertEnvoyAttemptCount().value(attempt_count_);
  }

  // Inject the active span's tracing context into the request headers.
  callbacks_->activeSpan().injectContext(headers);

  route_entry_->finalizeRequestHeaders(headers, callbacks_->streamInfo(),
                                       !config_.suppress_envoy_headers_);
  FilterUtility::setUpstreamScheme(
      headers, conn_pool->host()->transportSocketFactory().implementsSecureTransport());

  // Ensure an http transport scheme is selected before continuing with decoding.
  ASSERT(headers.Scheme());

  retry_state_ =
      createRetryState(route_entry_->retryPolicy(), headers, *cluster_, config_.runtime_,
                       config_.random_, callbacks_->dispatcher(), route_entry_->priority());
  do_shadowing_ = FilterUtility::shouldShadow(route_entry_->shadowPolicy(), config_.runtime_,
                                              callbacks_->streamId());

  ENVOY_STREAM_LOG(debug, "router decoding headers:\n{}", *callbacks_, headers);

  // Hang onto the modify_headers function for later use in handling upstream responses.
  modify_headers_ = modify_headers;

  UpstreamRequestPtr upstream_request = std::make_unique<UpstreamRequest>(*this, *conn_pool);
  upstream_request->moveIntoList(std::move(upstream_request), upstream_requests_);
  upstream_requests_.front()->encodeHeaders(end_stream);
  if (end_stream) {
    onRequestComplete();
  }

  return Http::FilterHeadersStatus::StopIteration;
}

Http::ConnectionPool::Instance* Filter::getConnPool() {
  // Choose protocol based on cluster configuration and downstream connection
  // Note: Cluster may downgrade HTTP2 to HTTP1 based on runtime configuration.
  Http::Protocol protocol = cluster_->upstreamHttpProtocol(callbacks_->streamInfo().protocol());

  transport_socket_options_ = Network::TransportSocketOptionsUtility::fromFilterState(
      callbacks_->streamInfo().filterState());

  return config_.cm_.httpConnPoolForCluster(route_entry_->clusterName(), route_entry_->priority(),
                                            protocol, this);
}

void Filter::sendNoHealthyUpstreamResponse() {
  callbacks_->streamInfo().setResponseFlag(StreamInfo::ResponseFlag::NoHealthyUpstream);
  chargeUpstreamCode(Http::Code::ServiceUnavailable, nullptr, false);
  callbacks_->sendLocalReply(Http::Code::ServiceUnavailable, "no healthy upstream", modify_headers_,
                             absl::nullopt,
                             StreamInfo::ResponseCodeDetails::get().NoHealthyUpstream);
}

Http::FilterDataStatus Filter::decodeData(Buffer::Instance& data, bool end_stream) {
  // upstream_requests_.size() cannot be 0 because we add to it unconditionally
  // in decodeHeaders(). It cannot be > 1 because that only happens when a per
  // try timeout occurs with hedge_on_per_try_timeout enabled but the per
  // try timeout timer is not started until onUpstreamComplete().
  ASSERT(upstream_requests_.size() == 1);

  bool buffering = (retry_state_ && retry_state_->enabled()) || do_shadowing_;
  if (buffering &&
      getLength(callbacks_->decodingBuffer()) + data.length() > retry_shadow_buffer_limit_) {
    // The request is larger than we should buffer. Give up on the retry/shadow
    cluster_->stats().retry_or_shadow_abandoned_.inc();
    retry_state_.reset();
    buffering = false;
    do_shadowing_ = false;
  }

  if (buffering) {
    // If we are going to buffer for retries or shadowing, we need to make a copy before encoding
    // since it's all moves from here on.
    Buffer::OwnedImpl copy(data);
    upstream_requests_.front()->encodeData(copy, end_stream);

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

Http::FilterTrailersStatus Filter::decodeTrailers(Http::HeaderMap& trailers) {
  ENVOY_STREAM_LOG(debug, "router decoding trailers:\n{}", *callbacks_, trailers);

  // upstream_requests_.size() cannot be 0 because we add to it unconditionally
  // in decodeHeaders(). It cannot be > 1 because that only happens when a per
  // try timeout occurs with hedge_on_per_try_timeout enabled but the per
  // try timeout timer is not started until onUpstreamComplete().
  ASSERT(upstream_requests_.size() == 1);
  downstream_trailers_ = &trailers;
  for (auto& upstream_request : upstream_requests_) {
    upstream_request->encodeTrailers(trailers);
  }
  onRequestComplete();
  return Http::FilterTrailersStatus::StopIteration;
}

Http::FilterMetadataStatus Filter::decodeMetadata(Http::MetadataMap& metadata_map) {
  Http::MetadataMapPtr metadata_map_ptr = std::make_unique<Http::MetadataMap>(metadata_map);
  ASSERT(upstream_requests_.size() == 1);
  upstream_requests_.front()->encodeMetadata(std::move(metadata_map_ptr));
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
  if (!do_shadowing_) {
    return;
  }

  ASSERT(!route_entry_->shadowPolicy().cluster().empty());
  Http::MessagePtr request(new Http::RequestMessageImpl(
      Http::HeaderMapPtr{new Http::HeaderMapImpl(*downstream_headers_)}));
  if (callbacks_->decodingBuffer()) {
    request->body() = std::make_unique<Buffer::OwnedImpl>(*callbacks_->decodingBuffer());
  }
  if (downstream_trailers_) {
    request->trailers(Http::HeaderMapPtr{new Http::HeaderMapImpl(*downstream_trailers_)});
  }

  config_.shadowWriter().shadow(route_entry_->shadowPolicy().cluster(), std::move(request),
                                timeout_.global_timeout_);
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
      if (upstream_request->create_per_try_timeout_on_request_complete_) {
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
    callbacks_->streamInfo().setUpstreamTiming(final_upstream_request_->upstream_timing_);
  }

  // Reset any upstream requests that are still in flight.
  while (!upstream_requests_.empty()) {
    UpstreamRequestPtr upstream_request =
        upstream_requests_.back()->removeFromList(upstream_requests_);

    // Don't record a timeout for upstream requests we've already seen headers
    // for.
    if (upstream_request->awaiting_headers_) {
      cluster_->stats().upstream_rq_timeout_.inc();
      if (upstream_request->upstream_host_) {
        upstream_request->upstream_host_->stats().rq_timeout_.inc();
      }

      // If this upstream request already hit a "soft" timeout, then it
      // already recorded a timeout into outlier detection. Don't do it again.
      if (!upstream_request->outlier_detection_timeout_recorded_) {
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
  upstream_request.outlier_detection_timeout_recorded_ = true;

  if (!downstream_response_started_ && retry_state_) {
    RetryStatus retry_status =
        retry_state_->shouldHedgeRetryPerTryTimeout([this]() -> void { doRetry(); });

    if (retry_status == RetryStatus::Yes && setupRetry()) {
      setupRetry();
      // Don't increment upstream_host->stats().rq_error_ here, we'll do that
      // later if 1) we hit global timeout or 2) we get bad response headers
      // back.
      upstream_request.retried_ = true;

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
  if (upstream_request.upstream_host_) {
    upstream_request.upstream_host_->stats().rq_timeout_.inc();
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

void Filter::updateOutlierDetection(Upstream::Outlier::Result result,
                                    UpstreamRequest& upstream_request,
                                    absl::optional<uint64_t> code) {
  if (upstream_request.upstream_host_) {
    upstream_request.upstream_host_->outlierDetector().putResult(result, code);
  }
}

void Filter::chargeUpstreamAbort(Http::Code code, bool dropped, UpstreamRequest& upstream_request) {
  if (downstream_response_started_) {
    if (upstream_request.grpc_rq_success_deferred_) {
      upstream_request.upstream_host_->stats().rq_error_.inc();
      config_.stats_.rq_reset_after_downstream_response_started_.inc();
    }
  } else {
    Upstream::HostDescriptionConstSharedPtr upstream_host = upstream_request.upstream_host_;

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
  const absl::string_view body =
      timeout_response_code_ == Http::Code::GatewayTimeout ? "upstream request timeout" : "";
  onUpstreamAbort(timeout_response_code_, response_flags, body, false, details);
}

void Filter::onUpstreamAbort(Http::Code code, StreamInfo::ResponseFlag response_flags,
                             absl::string_view body, bool dropped, absl::string_view details) {
  // If we have not yet sent anything downstream, send a response with an appropriate status code.
  // Otherwise just reset the ongoing response.
  if (downstream_response_started_) {
    // This will destroy any created retry timers.
    callbacks_->streamInfo().setResponseCodeDetails(details);
    cleanup();
    callbacks_->resetStream();
  } else {
    // This will destroy any created retry timers.
    cleanup();

    callbacks_->streamInfo().setResponseFlag(response_flags);

    callbacks_->sendLocalReply(
        code, body,
        [dropped, this](Http::HeaderMap& headers) {
          if (dropped && !config_.suppress_envoy_headers_) {
            headers.insertEnvoyOverloaded().value(Http::Headers::get().EnvoyOverloadedValues.True);
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
  if (downstream_response_started_ || !retry_state_ || upstream_request.retried_) {
    return false;
  }

  const RetryStatus retry_status =
      retry_state_->shouldRetryReset(reset_reason, [this]() -> void { doRetry(); });
  if (retry_status == RetryStatus::Yes && setupRetry()) {
    if (upstream_request.upstream_host_) {
      upstream_request.upstream_host_->stats().rq_error_.inc();
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
  ENVOY_STREAM_LOG(debug, "upstream reset: reset reason {}", *callbacks_,
                   Http::Utility::resetReasonToString(reset_reason));

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
                   Http::Utility::resetReasonToString(reset_reason));

  callbacks_->streamInfo().setUpstreamTransportFailureReason(transport_failure_reason);
  const std::string& basic_details =
      downstream_response_started_ ? StreamInfo::ResponseCodeDetails::get().LateUpstreamReset
                                   : StreamInfo::ResponseCodeDetails::get().EarlyUpstreamReset;
  const std::string details = absl::StrCat(
      basic_details, "{", Http::Utility::resetReasonToString(reset_reason),
      transport_failure_reason.empty() ? "" : absl::StrCat(",", transport_failure_reason), "}");
  onUpstreamAbort(Http::Code::ServiceUnavailable, response_flags, body, dropped, details);
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
        upstream_request.upstream_host_->stats().rq_success_.inc();
      } else {
        upstream_request.upstream_host_->stats().rq_error_.inc();
      }
    } else {
      upstream_request.grpc_rq_success_deferred_ = true;
    }
  } else {
    upstream_request.upstream_host_->stats().rq_success_.inc();
  }
}

void Filter::onUpstream100ContinueHeaders(Http::HeaderMapPtr&& headers,
                                          UpstreamRequest& upstream_request) {
  chargeUpstreamCode(100, *headers, upstream_request.upstream_host_, false);
  ENVOY_STREAM_LOG(debug, "upstream 100 continue", *callbacks_);

  downstream_response_started_ = true;
  final_upstream_request_ = &upstream_request;
  resetOtherUpstreams(upstream_request);

  // Don't send retries after 100-Continue has been sent on. Arguably we could attempt to do a
  // retry, assume the next upstream would also send an 100-Continue and swallow the second one
  // but it's sketchy (as the subsequent upstream might not send a 100-Continue) and not worth
  // the complexity until someone asks for it.
  retry_state_.reset();

  callbacks_->encode100ContinueHeaders(std::move(headers));
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
  final_upstream_request->moveIntoList(std::move(final_upstream_request), upstream_requests_);
}

void Filter::onUpstreamHeaders(uint64_t response_code, Http::HeaderMapPtr&& headers,
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

  if (grpc_status.has_value() &&
      Runtime::runtimeFeatureEnabled(
          "envoy.reloadable_features.outlier_detection_support_for_grpc_status")) {
    upstream_request.upstream_host_->outlierDetector().putHttpResponseCode(grpc_to_http_status);
  } else {
    upstream_request.upstream_host_->outlierDetector().putHttpResponseCode(response_code);
  }

  if (headers->EnvoyImmediateHealthCheckFail() != nullptr) {
    upstream_request.upstream_host_->healthChecker().setUnhealthy();
  }

  bool could_not_retry = false;

  // Check if this upstream request was already retried, for instance after
  // hitting a per try timeout. Don't retry it if we already have.
  if (retry_state_) {
    if (upstream_request.retried_) {
      // We already retried this request (presumably for a per try timeout) so
      // we definitely won't retry it again. Check if we would have retried it
      // if we could.
      could_not_retry = retry_state_->wouldRetryFromHeaders(*headers);
    } else {
      const RetryStatus retry_status =
          retry_state_->shouldRetryHeaders(*headers, [this]() -> void { doRetry(); });
      // Capture upstream_host since setupRetry() in the following line will clear
      // upstream_request.
      const auto upstream_host = upstream_request.upstream_host_;
      if (retry_status == RetryStatus::Yes && setupRetry()) {
        if (!end_stream) {
          upstream_request.resetStream();
        }
        upstream_request.removeFromList(upstream_requests_);

        Http::CodeStats& code_stats = httpContext().codeStats();
        code_stats.chargeBasicResponseStat(cluster_->statsScope(), config_.retry_,
                                           static_cast<Http::Code>(response_code));
        upstream_host->stats().rq_error_.inc();
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

  if (static_cast<Http::Code>(response_code) == Http::Code::Found &&
      route_entry_->internalRedirectAction() == InternalRedirectAction::Handle &&
      setupRedirect(*headers, upstream_request)) {
    return;
    // If the redirect could not be handled, fail open and let it pass to the
    // next downstream.
  }

  // Check if we got a "bad" response, but there are still upstream requests in
  // flight awaiting headers or scheduled retries. If so, exit to give them a
  // chance to return before returning a response downstream.
  if (could_not_retry && (numRequestsAwaitingHeaders() > 0 || pending_retries_ > 0)) {
    upstream_request.upstream_host_->stats().rq_error_.inc();

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
      headers->insertEnvoyUpstreamServiceTime().value(ms.count());
    }
  }

  upstream_request.upstream_canary_ =
      (headers->EnvoyUpstreamCanary() && headers->EnvoyUpstreamCanary()->value() == "true") ||
      upstream_request.upstream_host_->canary();
  chargeUpstreamCode(response_code, *headers, upstream_request.upstream_host_, false);
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

  callbacks_->streamInfo().setResponseCodeDetails(
      StreamInfo::ResponseCodeDetails::get().ViaUpstream);
  callbacks_->encodeHeaders(std::move(headers), end_stream);
}

void Filter::onUpstreamData(Buffer::Instance& data, UpstreamRequest& upstream_request,
                            bool end_stream) {
  // This should be true because when we saw headers we either reset the stream
  // (hence wouldn't have made it to onUpstreamData) or all other in-flight
  // streams.
  ASSERT(upstream_requests_.size() == 1);
  if (end_stream) {
    // gRPC request termination without trailers is an error.
    if (upstream_request.grpc_rq_success_deferred_) {
      upstream_request.upstream_host_->stats().rq_error_.inc();
    }
    onUpstreamComplete(upstream_request);
  }

  callbacks_->encodeData(data, end_stream);
}

void Filter::onUpstreamTrailers(Http::HeaderMapPtr&& trailers, UpstreamRequest& upstream_request) {
  // This should be true because when we saw headers we either reset the stream
  // (hence wouldn't have made it to onUpstreamTrailers) or all other in-flight
  // streams.
  ASSERT(upstream_requests_.size() == 1);

  if (upstream_request.grpc_rq_success_deferred_) {
    absl::optional<Grpc::Status::GrpcStatus> grpc_status = Grpc::Common::getGrpcStatus(*trailers);
    if (grpc_status &&
        !Http::CodeUtility::is5xx(Grpc::Utility::grpcToHttpStatus(grpc_status.value()))) {
      upstream_request.upstream_host_->stats().rq_success_.inc();
    } else {
      upstream_request.upstream_host_->stats().rq_error_.inc();
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
  callbacks_->streamInfo().setUpstreamTiming(final_upstream_request_->upstream_timing_);

  if (config_.emit_dynamic_stats_ && !callbacks_->streamInfo().healthCheck() &&
      DateUtil::timePointValid(downstream_request_complete_time_)) {
    Event::Dispatcher& dispatcher = callbacks_->dispatcher();
    std::chrono::milliseconds response_time = std::chrono::duration_cast<std::chrono::milliseconds>(
        dispatcher.timeSource().monotonicTime() - downstream_request_complete_time_);
    upstream_request.upstream_host_->outlierDetector().putResponseTime(response_time);
    const bool internal_request = Http::HeaderUtility::isEnvoyInternalRequest(*downstream_headers_);

    Http::CodeStats& code_stats = httpContext().codeStats();
    Http::CodeStats::ResponseTimingInfo info{config_.scope_,
                                             cluster_->statsScope(),
                                             config_.empty_stat_name_,
                                             response_time,
                                             upstream_request.upstream_canary_,
                                             internal_request,
                                             route_entry_->virtualHost().statName(),
                                             request_vcluster_ ? request_vcluster_->statName()
                                                               : config_.empty_stat_name_,
                                             config_.zone_name_,
                                             upstreamZone(upstream_request.upstream_host_)};

    code_stats.chargeResponseTiming(info);

    if (alt_stat_prefix_ != nullptr) {
      Http::CodeStats::ResponseTimingInfo info{config_.scope_,
                                               cluster_->statsScope(),
                                               alt_stat_prefix_->statName(),
                                               response_time,
                                               upstream_request.upstream_canary_,
                                               internal_request,
                                               config_.empty_stat_name_,
                                               config_.empty_stat_name_,
                                               config_.zone_name_,
                                               upstreamZone(upstream_request.upstream_host_)};

      code_stats.chargeResponseTiming(info);
    }
  }

  upstream_request.removeFromList(upstream_requests_);
  cleanup();
}

bool Filter::setupRetry() {
  // If we responded before the request was complete we don't bother doing a retry. This may not
  // catch certain cases where we are in full streaming mode and we have a connect timeout or an
  // overflow of some kind. However, in many cases deployments will use the buffer filter before
  // this filter which will make this a non-issue. The implementation of supporting retry in cases
  // where the request is not complete is more complicated so we will start with this for now.
  if (!downstream_end_stream_) {
    config_.stats_.rq_retry_skipped_request_not_complete_.inc();
    return false;
  }
  pending_retries_++;

  ENVOY_STREAM_LOG(debug, "performing retry", *callbacks_);

  return true;
}

bool Filter::setupRedirect(const Http::HeaderMap& headers, UpstreamRequest& upstream_request) {
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
      upstream_request.upstream_timing_.last_upstream_rx_byte_received_ && downstream_end_stream_;

  // As with setupRetry, redirects are not supported for streaming requests yet.
  if (downstream_end_stream_ &&
      !callbacks_->decodingBuffer() && // Redirects with body not yet supported.
      location != nullptr &&
      convertRequestHeadersForInternalRedirect(*downstream_headers_, *location,
                                               *callbacks_->connection()) &&
      callbacks_->recreateStream()) {
    cluster_->stats().upstream_internal_redirect_succeeded_total_.inc();
    return true;
  }

  attempting_internal_redirect_with_complete_stream_ = false;

  ENVOY_STREAM_LOG(debug, "Internal redirect failed", *callbacks_);
  cluster_->stats().upstream_internal_redirect_failed_total_.inc();
  return false;
}

void Filter::doRetry() {
  is_retry_ = true;
  attempt_count_++;
  ASSERT(pending_retries_ > 0);
  pending_retries_--;
  Http::ConnectionPool::Instance* conn_pool = getConnPool();
  if (!conn_pool) {
    sendNoHealthyUpstreamResponse();
    cleanup();
    return;
  }

  if (include_attempt_count_) {
    downstream_headers_->insertEnvoyAttemptCount().value(attempt_count_);
  }

  ASSERT(response_timeout_ || timeout_.global_timeout_.count() == 0);
  UpstreamRequestPtr upstream_request = std::make_unique<UpstreamRequest>(*this, *conn_pool);
  UpstreamRequest* upstream_request_tmp = upstream_request.get();
  upstream_request->moveIntoList(std::move(upstream_request), upstream_requests_);
  upstream_requests_.front()->encodeHeaders(!callbacks_->decodingBuffer() && !downstream_trailers_);
  // It's possible we got immediately reset which means the upstream request we just
  // added to the front of the list might have been removed, so we need to check to make
  // sure we don't encodeData on the wrong request.
  if (!upstream_requests_.empty() && (upstream_requests_.front().get() == upstream_request_tmp)) {
    if (callbacks_->decodingBuffer()) {
      // If we are doing a retry we need to make a copy.
      Buffer::OwnedImpl copy(*callbacks_->decodingBuffer());
      upstream_requests_.front()->encodeData(copy, !downstream_trailers_);
    }

    if (downstream_trailers_) {
      upstream_requests_.front()->encodeTrailers(*downstream_trailers_);
    }
  }
}

uint32_t Filter::numRequestsAwaitingHeaders() {
  return std::count_if(upstream_requests_.begin(), upstream_requests_.end(),
                       [](const auto& req) -> bool { return req->awaiting_headers_; });
}

Filter::UpstreamRequest::UpstreamRequest(Filter& parent, Http::ConnectionPool::Instance& pool)
    : parent_(parent), conn_pool_(pool), grpc_rq_success_deferred_(false),
      stream_info_(pool.protocol(), parent_.callbacks_->dispatcher().timeSource()),
      calling_encode_headers_(false), upstream_canary_(false), decode_complete_(false),
      encode_complete_(false), encode_trailers_(false), retried_(false), awaiting_headers_(true),
      outlier_detection_timeout_recorded_(false),
      create_per_try_timeout_on_request_complete_(false) {
  if (parent_.config_.start_child_span_) {
    span_ = parent_.callbacks_->activeSpan().spawnChild(
        parent_.callbacks_->tracingConfig(), "router " + parent.cluster_->name() + " egress",
        parent.timeSource().systemTime());
    if (parent.attempt_count_ != 1) {
      // This is a retry request, add this metadata to span.
      span_->setTag(Tracing::Tags::get().RetryCount, std::to_string(parent.attempt_count_ - 1));
    }
  }

  stream_info_.healthCheck(parent_.callbacks_->streamInfo().healthCheck());
}

Filter::UpstreamRequest::~UpstreamRequest() {
  if (span_ != nullptr) {
    Tracing::HttpTracerUtility::finalizeUpstreamSpan(*span_, upstream_headers_.get(),
                                                     upstream_trailers_.get(), stream_info_,
                                                     Tracing::EgressConfig::get());
  }

  if (per_try_timeout_ != nullptr) {
    // Allows for testing.
    per_try_timeout_->disableTimer();
  }
  clearRequestEncoder();

  stream_info_.setUpstreamTiming(upstream_timing_);
  stream_info_.onRequestComplete();
  // Prior to logging, refresh the byte size of the HeaderMaps.
  // TODO(asraa): Remove this when entries in HeaderMap can no longer be modified by reference and
  // HeaderMap holds an accurate internal byte size count.
  if (upstream_headers_ != nullptr) {
    upstream_headers_->refreshByteSize();
  }
  if (upstream_trailers_ != nullptr) {
    upstream_trailers_->refreshByteSize();
  }
  for (const auto& upstream_log : parent_.config_.upstream_logs_) {
    upstream_log->log(parent_.downstream_headers_, upstream_headers_.get(),
                      upstream_trailers_.get(), stream_info_);
  }
}

void Filter::UpstreamRequest::decode100ContinueHeaders(Http::HeaderMapPtr&& headers) {
  ScopeTrackerScopeState scope(&parent_.callbacks_->scope(), parent_.callbacks_->dispatcher());

  ASSERT(100 == Http::Utility::getResponseStatus(*headers));
  parent_.onUpstream100ContinueHeaders(std::move(headers), *this);
}

void Filter::UpstreamRequest::decodeHeaders(Http::HeaderMapPtr&& headers, bool end_stream) {
  ScopeTrackerScopeState scope(&parent_.callbacks_->scope(), parent_.callbacks_->dispatcher());

  // TODO(rodaine): This is actually measuring after the headers are parsed and not the first
  // byte.
  upstream_timing_.onFirstUpstreamRxByteReceived(parent_.callbacks_->dispatcher().timeSource());
  maybeEndDecode(end_stream);

  awaiting_headers_ = false;
  if (!parent_.config_.upstream_logs_.empty()) {
    upstream_headers_ = std::make_unique<Http::HeaderMapImpl>(*headers);
  }
  const uint64_t response_code = Http::Utility::getResponseStatus(*headers);
  stream_info_.response_code_ = static_cast<uint32_t>(response_code);
  parent_.onUpstreamHeaders(response_code, std::move(headers), *this, end_stream);
}

void Filter::UpstreamRequest::decodeData(Buffer::Instance& data, bool end_stream) {
  ScopeTrackerScopeState scope(&parent_.callbacks_->scope(), parent_.callbacks_->dispatcher());

  maybeEndDecode(end_stream);
  stream_info_.addBytesReceived(data.length());
  parent_.onUpstreamData(data, *this, end_stream);
}

void Filter::UpstreamRequest::decodeTrailers(Http::HeaderMapPtr&& trailers) {
  ScopeTrackerScopeState scope(&parent_.callbacks_->scope(), parent_.callbacks_->dispatcher());

  maybeEndDecode(true);
  if (!parent_.config_.upstream_logs_.empty()) {
    upstream_trailers_ = std::make_unique<Http::HeaderMapImpl>(*trailers);
  }
  parent_.onUpstreamTrailers(std::move(trailers), *this);
}

void Filter::UpstreamRequest::decodeMetadata(Http::MetadataMapPtr&& metadata_map) {
  parent_.onUpstreamMetadata(std::move(metadata_map));
}

void Filter::UpstreamRequest::maybeEndDecode(bool end_stream) {
  if (end_stream) {
    upstream_timing_.onLastUpstreamRxByteReceived(parent_.callbacks_->dispatcher().timeSource());
    decode_complete_ = true;
  }
}

void Filter::UpstreamRequest::encodeHeaders(bool end_stream) {
  ASSERT(!encode_complete_);
  encode_complete_ = end_stream;

  // It's possible for a reset to happen inline within the newStream() call. In this case, we
  // might get deleted inline as well. Only write the returned handle out if it is not nullptr to
  // deal with this case.
  Http::ConnectionPool::Cancellable* handle = conn_pool_.newStream(*this, *this);
  if (handle) {
    conn_pool_stream_handle_ = handle;
  }
}

void Filter::UpstreamRequest::encodeData(Buffer::Instance& data, bool end_stream) {
  ASSERT(!encode_complete_);
  encode_complete_ = end_stream;

  if (!request_encoder_) {
    ENVOY_STREAM_LOG(trace, "buffering {} bytes", *parent_.callbacks_, data.length());
    if (!buffered_request_body_) {
      buffered_request_body_ = std::make_unique<Buffer::WatermarkBuffer>(
          [this]() -> void { this->enableDataFromDownstream(); },
          [this]() -> void { this->disableDataFromDownstream(); });
      buffered_request_body_->setWatermarks(parent_.callbacks_->decoderBufferLimit());
    }

    buffered_request_body_->move(data);
  } else {
    ASSERT(downstream_metadata_map_vector_.empty());

    ENVOY_STREAM_LOG(trace, "proxying {} bytes", *parent_.callbacks_, data.length());
    stream_info_.addBytesSent(data.length());
    request_encoder_->encodeData(data, end_stream);
    if (end_stream) {
      upstream_timing_.onLastUpstreamTxByteSent(parent_.callbacks_->dispatcher().timeSource());
    }
  }
}

void Filter::UpstreamRequest::encodeTrailers(const Http::HeaderMap& trailers) {
  ASSERT(!encode_complete_);
  encode_complete_ = true;
  encode_trailers_ = true;

  if (!request_encoder_) {
    ENVOY_STREAM_LOG(trace, "buffering trailers", *parent_.callbacks_);
  } else {
    ASSERT(downstream_metadata_map_vector_.empty());

    ENVOY_STREAM_LOG(trace, "proxying trailers", *parent_.callbacks_);
    request_encoder_->encodeTrailers(trailers);
    upstream_timing_.onLastUpstreamTxByteSent(parent_.callbacks_->dispatcher().timeSource());
  }
}

void Filter::UpstreamRequest::encodeMetadata(Http::MetadataMapPtr&& metadata_map_ptr) {
  if (!request_encoder_) {
    ENVOY_STREAM_LOG(trace, "request_encoder_ not ready. Store metadata_map to encode later: {}",
                     *parent_.callbacks_, *metadata_map_ptr);
    downstream_metadata_map_vector_.emplace_back(std::move(metadata_map_ptr));
  } else {
    ENVOY_STREAM_LOG(trace, "Encode metadata: {}", *parent_.callbacks_, *metadata_map_ptr);
    Http::MetadataMapVector metadata_map_vector;
    metadata_map_vector.emplace_back(std::move(metadata_map_ptr));
    request_encoder_->encodeMetadata(metadata_map_vector);
  }
}

void Filter::UpstreamRequest::onResetStream(Http::StreamResetReason reason,
                                            absl::string_view transport_failure_reason) {
  ScopeTrackerScopeState scope(&parent_.callbacks_->scope(), parent_.callbacks_->dispatcher());

  if (span_ != nullptr) {
    // Add tags about reset.
    span_->setTag(Tracing::Tags::get().Error, Tracing::Tags::get().True);
    span_->setTag(Tracing::Tags::get().ErrorReason, Http::Utility::resetReasonToString(reason));
  }

  clearRequestEncoder();
  awaiting_headers_ = false;
  if (!calling_encode_headers_) {
    stream_info_.setResponseFlag(parent_.streamResetReasonToResponseFlag(reason));
    parent_.onUpstreamReset(reason, transport_failure_reason, *this);
  } else {
    deferred_reset_reason_ = reason;
  }
}

void Filter::UpstreamRequest::resetStream() {
  // Don't reset the stream if we're already done with it.
  if (encode_complete_ && decode_complete_) {
    return;
  }

  if (span_ != nullptr) {
    // Add tags about the cancellation.
    span_->setTag(Tracing::Tags::get().Canceled, Tracing::Tags::get().True);
  }

  if (conn_pool_stream_handle_) {
    ENVOY_STREAM_LOG(debug, "cancelling pool request", *parent_.callbacks_);
    ASSERT(!request_encoder_);
    conn_pool_stream_handle_->cancel();
    conn_pool_stream_handle_ = nullptr;
  }

  if (request_encoder_) {
    ENVOY_STREAM_LOG(debug, "resetting pool request", *parent_.callbacks_);
    request_encoder_->getStream().removeCallbacks(*this);
    request_encoder_->getStream().resetStream(Http::StreamResetReason::LocalReset);
    clearRequestEncoder();
  }
}

void Filter::UpstreamRequest::setupPerTryTimeout() {
  ASSERT(!per_try_timeout_);
  if (parent_.timeout_.per_try_timeout_.count() > 0) {
    per_try_timeout_ =
        parent_.callbacks_->dispatcher().createTimer([this]() -> void { onPerTryTimeout(); });
    per_try_timeout_->enableTimer(parent_.timeout_.per_try_timeout_);
  }
}

void Filter::UpstreamRequest::onPerTryTimeout() {
  // If we've sent anything downstream, ignore the per try timeout and let the response continue
  // up to the global timeout
  if (!parent_.downstream_response_started_) {
    ENVOY_STREAM_LOG(debug, "upstream per try timeout", *parent_.callbacks_);

    stream_info_.setResponseFlag(StreamInfo::ResponseFlag::UpstreamRequestTimeout);
    parent_.onPerTryTimeout(*this);
  } else {
    ENVOY_STREAM_LOG(debug,
                     "ignored upstream per try timeout due to already started downstream response",
                     *parent_.callbacks_);
  }
}

void Filter::UpstreamRequest::onPoolFailure(Http::ConnectionPool::PoolFailureReason reason,
                                            absl::string_view transport_failure_reason,
                                            Upstream::HostDescriptionConstSharedPtr host) {
  Http::StreamResetReason reset_reason = Http::StreamResetReason::ConnectionFailure;
  switch (reason) {
  case Http::ConnectionPool::PoolFailureReason::Overflow:
    reset_reason = Http::StreamResetReason::Overflow;
    break;
  case Http::ConnectionPool::PoolFailureReason::ConnectionFailure:
    reset_reason = Http::StreamResetReason::ConnectionFailure;
    break;
  }

  // Mimic an upstream reset.
  onUpstreamHostSelected(host);
  onResetStream(reset_reason, transport_failure_reason);
}

void Filter::UpstreamRequest::onPoolReady(Http::StreamEncoder& request_encoder,
                                          Upstream::HostDescriptionConstSharedPtr host,
                                          const StreamInfo::StreamInfo& info) {
  // This may be called under an existing ScopeTrackerScopeState but it will unwind correctly.
  ScopeTrackerScopeState scope(&parent_.callbacks_->scope(), parent_.callbacks_->dispatcher());
  ENVOY_STREAM_LOG(debug, "pool ready", *parent_.callbacks_);

  host->outlierDetector().putResult(Upstream::Outlier::Result::LocalOriginConnectSuccess);

  // TODO(ggreenway): set upstream local address in the StreamInfo.
  onUpstreamHostSelected(host);
  request_encoder.getStream().addCallbacks(*this);

  stream_info_.setUpstreamSslConnection(info.downstreamSslConnection());
  parent_.callbacks_->streamInfo().setUpstreamSslConnection(info.downstreamSslConnection());

  if (parent_.downstream_end_stream_) {
    setupPerTryTimeout();
  } else {
    create_per_try_timeout_on_request_complete_ = true;
  }

  conn_pool_stream_handle_ = nullptr;
  setRequestEncoder(request_encoder);
  calling_encode_headers_ = true;
  if (parent_.route_entry_->autoHostRewrite() && !host->hostname().empty()) {
    parent_.downstream_headers_->Host()->value(host->hostname());
  }

  if (span_ != nullptr) {
    span_->injectContext(*parent_.downstream_headers_);
  }

  upstream_timing_.onFirstUpstreamTxByteSent(parent_.callbacks_->dispatcher().timeSource());

  const bool end_stream = !buffered_request_body_ && encode_complete_ && !encode_trailers_;
  // If end_stream is set in headers, and there are metadata to send, delays end_stream. The case
  // only happens when decoding headers filters return ContinueAndEndStream.
  const bool delay_headers_end_stream = end_stream && !downstream_metadata_map_vector_.empty();
  request_encoder.encodeHeaders(*parent_.downstream_headers_,
                                end_stream && !delay_headers_end_stream);
  calling_encode_headers_ = false;

  // It is possible to get reset in the middle of an encodeHeaders() call. This happens for
  // example in the HTTP/2 codec if the frame cannot be encoded for some reason. This should never
  // happen but it's unclear if we have covered all cases so protect against it and test for it.
  // One specific example of a case where this happens is if we try to encode a total header size
  // that is too big in HTTP/2 (64K currently).
  if (deferred_reset_reason_) {
    onResetStream(deferred_reset_reason_.value(), absl::string_view());
  } else {
    // Encode metadata after headers and before any other frame type.
    if (!downstream_metadata_map_vector_.empty()) {
      ENVOY_STREAM_LOG(debug, "Send metadata onPoolReady. {}", *parent_.callbacks_,
                       downstream_metadata_map_vector_);
      request_encoder.encodeMetadata(downstream_metadata_map_vector_);
      downstream_metadata_map_vector_.clear();
      if (delay_headers_end_stream) {
        Buffer::OwnedImpl empty_data("");
        request_encoder.encodeData(empty_data, true);
      }
    }

    if (buffered_request_body_) {
      stream_info_.addBytesSent(buffered_request_body_->length());
      request_encoder.encodeData(*buffered_request_body_, encode_complete_ && !encode_trailers_);
    }

    if (encode_trailers_) {
      request_encoder.encodeTrailers(*parent_.downstream_trailers_);
    }

    if (encode_complete_) {
      upstream_timing_.onLastUpstreamTxByteSent(parent_.callbacks_->dispatcher().timeSource());
    }
  }
}

RetryStatePtr
ProdFilter::createRetryState(const RetryPolicy& policy, Http::HeaderMap& request_headers,
                             const Upstream::ClusterInfo& cluster, Runtime::Loader& runtime,
                             Runtime::RandomGenerator& random, Event::Dispatcher& dispatcher,
                             Upstream::ResourcePriority priority) {
  return RetryStateImpl::create(policy, request_headers, cluster, runtime, random, dispatcher,
                                priority);
}

void Filter::UpstreamRequest::setRequestEncoder(Http::StreamEncoder& request_encoder) {
  request_encoder_ = &request_encoder;
  // Now that there is an encoder, have the connection manager inform the manager when the
  // downstream buffers are overrun. This may result in immediate watermark callbacks referencing
  // the encoder.
  parent_.callbacks_->addDownstreamWatermarkCallbacks(downstream_watermark_manager_);
}

void Filter::UpstreamRequest::clearRequestEncoder() {
  // Before clearing the encoder, unsubscribe from callbacks.
  if (request_encoder_) {
    parent_.callbacks_->removeDownstreamWatermarkCallbacks(downstream_watermark_manager_);
  }
  request_encoder_ = nullptr;
}

void Filter::UpstreamRequest::DownstreamWatermarkManager::onAboveWriteBufferHighWatermark() {
  ASSERT(parent_.request_encoder_);

  // There are two states we should get this callback in: 1) the watermark was
  // hit due to writes from a different filter instance over a shared
  // downstream connection, or 2) the watermark was hit due to THIS filter
  // instance writing back the "winning" upstream request. In either case we
  // can disable reads from upstream.
  ASSERT(!parent_.parent_.final_upstream_request_ ||
         &parent_ == parent_.parent_.final_upstream_request_);

  // The downstream connection is overrun. Pause reads from upstream.
  // If there are multiple calls to readDisable either the codec (H2) or the underlying
  // Network::Connection (H1) will handle reference counting.
  parent_.parent_.cluster_->stats().upstream_flow_control_paused_reading_total_.inc();
  parent_.request_encoder_->getStream().readDisable(true);
}

void Filter::UpstreamRequest::DownstreamWatermarkManager::onBelowWriteBufferLowWatermark() {
  ASSERT(parent_.request_encoder_);

  // One source of connection blockage has buffer available. Pass this on to the stream, which
  // will resume reads if this was the last remaining high watermark.
  parent_.parent_.cluster_->stats().upstream_flow_control_resumed_reading_total_.inc();
  parent_.request_encoder_->getStream().readDisable(false);
}

} // namespace Router
} // namespace Envoy
