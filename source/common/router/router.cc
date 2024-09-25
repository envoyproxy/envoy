#include "source/common/router/router.h"

#include <algorithm>
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

#include "source/common/common/assert.h"
#include "source/common/common/cleanup.h"
#include "source/common/common/empty_string.h"
#include "source/common/common/enum_to_int.h"
#include "source/common/common/scope_tracker.h"
#include "source/common/common/utility.h"
#include "source/common/config/utility.h"
#include "source/common/grpc/common.h"
#include "source/common/http/codes.h"
#include "source/common/http/header_map_impl.h"
#include "source/common/http/headers.h"
#include "source/common/http/message_impl.h"
#include "source/common/http/utility.h"
#include "source/common/network/application_protocol.h"
#include "source/common/network/socket_option_factory.h"
#include "source/common/network/transport_socket_options_impl.h"
#include "source/common/network/upstream_server_name.h"
#include "source/common/network/upstream_socket_options_filter_state.h"
#include "source/common/network/upstream_subject_alt_names.h"
#include "source/common/orca/orca_load_metrics.h"
#include "source/common/orca/orca_parser.h"
#include "source/common/router/config_impl.h"
#include "source/common/router/debug_config.h"
#include "source/common/router/retry_state_impl.h"
#include "source/common/runtime/runtime_features.h"
#include "source/common/stream_info/uint32_accessor_impl.h"
#include "source/common/tracing/http_tracer_impl.h"

namespace Envoy {
namespace Router {
namespace {
constexpr char NumInternalRedirectsFilterStateName[] = "num_internal_redirects";

uint32_t getLength(const Buffer::Instance* instance) { return instance ? instance->length() : 0; }

bool schemeIsHttp(const Http::RequestHeaderMap& downstream_headers,
                  OptRef<const Network::Connection> connection) {
  if (Http::Utility::schemeIsHttp(downstream_headers.getSchemeValue())) {
    return true;
  }
  if (connection.has_value() && !connection->ssl()) {
    return true;
  }
  return false;
}

constexpr uint64_t TimeoutPrecisionFactor = 100;

} // namespace

FilterConfig::FilterConfig(Stats::StatName stat_prefix,
                           Server::Configuration::FactoryContext& context,
                           ShadowWriterPtr&& shadow_writer,
                           const envoy::extensions::filters::http::router::v3::Router& config)
    : FilterConfig(
          context.serverFactoryContext(), stat_prefix, context.serverFactoryContext().localInfo(),
          context.scope(), context.serverFactoryContext().clusterManager(),
          context.serverFactoryContext().runtime(),
          context.serverFactoryContext().api().randomGenerator(), std::move(shadow_writer),
          PROTOBUF_GET_WRAPPED_OR_DEFAULT(config, dynamic_stats, true), config.start_child_span(),
          config.suppress_envoy_headers(), config.respect_expected_rq_timeout(),
          config.suppress_grpc_request_failure_code_stats(),
          config.has_upstream_log_options()
              ? config.upstream_log_options().flush_upstream_log_on_upstream_stream()
              : false,
          config.strict_check_headers(), context.serverFactoryContext().api().timeSource(),
          context.serverFactoryContext().httpContext(),
          context.serverFactoryContext().routerContext()) {
  for (const auto& upstream_log : config.upstream_log()) {
    upstream_logs_.push_back(AccessLog::AccessLogFactory::fromProto(upstream_log, context));
  }

  if (config.has_upstream_log_options() &&
      config.upstream_log_options().has_upstream_log_flush_interval()) {
    upstream_log_flush_interval_ = std::chrono::milliseconds(DurationUtil::durationToMilliseconds(
        config.upstream_log_options().upstream_log_flush_interval()));
  }

  if (config.upstream_http_filters_size() > 0) {
    auto& server_factory_ctx = context.serverFactoryContext();
    const Http::FilterChainUtility::FiltersList& upstream_http_filters =
        config.upstream_http_filters();
    std::shared_ptr<Http::UpstreamFilterConfigProviderManager> filter_config_provider_manager =
        Http::FilterChainUtility::createSingletonUpstreamFilterConfigProviderManager(
            server_factory_ctx);
    std::string prefix = context.scope().symbolTable().toString(context.scope().prefix());
    upstream_ctx_ = std::make_unique<Upstream::UpstreamFactoryContextImpl>(
        server_factory_ctx, context.initManager(), context.scope());
    Http::FilterChainHelper<Server::Configuration::UpstreamFactoryContext,
                            Server::Configuration::UpstreamHttpFilterConfigFactory>
        helper(*filter_config_provider_manager, server_factory_ctx,
               context.serverFactoryContext().clusterManager(), *upstream_ctx_, prefix);
    THROW_IF_NOT_OK(helper.processFilters(upstream_http_filters, "router upstream http",
                                          "router upstream http", upstream_http_filter_factories_));
  }
}

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

void FilterUtility::setUpstreamScheme(Http::RequestHeaderMap& headers, bool downstream_ssl,
                                      bool upstream_ssl, bool use_upstream) {
  if (use_upstream) {
    if (upstream_ssl) {
      headers.setReferenceScheme(Http::Headers::get().SchemeValues.Https);
    } else {
      headers.setReferenceScheme(Http::Headers::get().SchemeValues.Http);
    }
    return;
  }

  if (Http::Utility::schemeIsValid(headers.getSchemeValue())) {
    return;
  }
  // After all the changes in https://github.com/envoyproxy/envoy/issues/14587
  // this path should only occur if a buggy filter has removed the :scheme
  // header. In that case best-effort set from X-Forwarded-Proto.
  absl::string_view xfp = headers.getForwardedProtoValue();
  if (Http::Utility::schemeIsValid(xfp)) {
    headers.setScheme(xfp);
    return;
  }

  if (downstream_ssl) {
    headers.setReferenceScheme(Http::Headers::get().SchemeValues.Https);
  } else {
    headers.setReferenceScheme(Http::Headers::get().SchemeValues.Http);
  }
}

bool FilterUtility::shouldShadow(const ShadowPolicy& policy, Runtime::Loader& runtime,
                                 uint64_t stable_random) {

  // The policy's default value is set correctly regardless of whether there is a runtime key
  // or not, thus this call is sufficient for all cases (100% if no runtime set, otherwise
  // using the default value within the runtime fractional percent setting).
  return runtime.snapshot().featureEnabled(policy.runtimeKey(), policy.defaultValue(),
                                           stable_random);
}

TimeoutData FilterUtility::finalTimeout(const RouteEntry& route,
                                        Http::RequestHeaderMap& request_headers,
                                        bool insert_envoy_expected_request_timeout_ms,
                                        bool grpc_request, bool per_try_timeout_hedging_enabled,
                                        bool respect_expected_rq_timeout) {
  // See if there is a user supplied timeout in a request header. If there is we take that.
  // Otherwise if the request is gRPC and a maximum gRPC timeout is configured we use the timeout
  // in the gRPC headers (or infinity when gRPC headers have no timeout), but cap that timeout to
  // the configured maximum gRPC timeout (which may also be infinity, represented by a 0 value),
  // or the default from the route config otherwise.
  TimeoutData timeout;
  if (!route.usingNewTimeouts()) {
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
  }
  timeout.per_try_timeout_ = route.retryPolicy().perTryTimeout();
  timeout.per_try_idle_timeout_ = route.retryPolicy().perTryIdleTimeout();

  uint64_t header_timeout;

  if (respect_expected_rq_timeout) {
    // Check if there is timeout set by egress Envoy.
    // If present, use that value as route timeout and don't override
    // *x-envoy-expected-rq-timeout-ms* header. At this point *x-envoy-upstream-rq-timeout-ms*
    // header should have been sanitized by egress Envoy.
    const Http::HeaderEntry* header_expected_timeout_entry =
        request_headers.EnvoyExpectedRequestTimeoutMs();
    if (header_expected_timeout_entry) {
      trySetGlobalTimeout(*header_expected_timeout_entry, timeout);
    } else {
      const Http::HeaderEntry* header_timeout_entry =
          request_headers.EnvoyUpstreamRequestTimeoutMs();

      if (header_timeout_entry) {
        trySetGlobalTimeout(*header_timeout_entry, timeout);
        request_headers.removeEnvoyUpstreamRequestTimeoutMs();
      }
    }
  } else {
    const Http::HeaderEntry* header_timeout_entry = request_headers.EnvoyUpstreamRequestTimeoutMs();

    if (header_timeout_entry) {
      trySetGlobalTimeout(*header_timeout_entry, timeout);
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

  setTimeoutHeaders(0, timeout, route, request_headers, insert_envoy_expected_request_timeout_ms,
                    grpc_request, per_try_timeout_hedging_enabled);

  return timeout;
}

void FilterUtility::setTimeoutHeaders(uint64_t elapsed_time, const TimeoutData& timeout,
                                      const RouteEntry& route,
                                      Http::RequestHeaderMap& request_headers,
                                      bool insert_envoy_expected_request_timeout_ms,
                                      bool grpc_request, bool per_try_timeout_hedging_enabled) {

  const uint64_t global_timeout = timeout.global_timeout_.count();

  // See if there is any timeout to write in the expected timeout header.
  uint64_t expected_timeout = timeout.per_try_timeout_.count();

  // Use the global timeout if no per try timeout was specified or if we're
  // doing hedging when there are per try timeouts. Either of these scenarios
  // mean that the upstream server can use the full global timeout.
  if (per_try_timeout_hedging_enabled || expected_timeout == 0) {
    expected_timeout = global_timeout;
  }

  // If the expected timeout is 0 set no timeout, as Envoy treats 0 as infinite timeout.
  if (expected_timeout > 0) {

    if (global_timeout > 0) {
      if (elapsed_time >= global_timeout) {
        // We are out of time, but 0 would be an infinite timeout. So instead we send a 1ms timeout
        // and assume the timers armed by onRequestComplete() will fire very soon.
        expected_timeout = 1;
      } else {
        expected_timeout = std::min(expected_timeout, global_timeout - elapsed_time);
      }
    }

    if (insert_envoy_expected_request_timeout_ms) {
      request_headers.setEnvoyExpectedRequestTimeoutMs(expected_timeout);
    }

    // If we've configured max_grpc_timeout, override the grpc-timeout header with
    // the expected timeout. This ensures that the optional per try timeout is reflected
    // in grpc-timeout, ensuring that the upstream gRPC server is aware of the actual timeout.
    if (grpc_request && !route.usingNewTimeouts() && route.maxGrpcTimeout()) {
      Grpc::Common::toGrpcTimeout(std::chrono::milliseconds(expected_timeout), request_headers);
    }
  }
}

absl::optional<std::chrono::milliseconds>
FilterUtility::tryParseHeaderTimeout(const Http::HeaderEntry& header_timeout_entry) {
  uint64_t header_timeout;
  if (absl::SimpleAtoi(header_timeout_entry.value().getStringView(), &header_timeout)) {
    return std::chrono::milliseconds(header_timeout);
  }
  return absl::nullopt;
}

void FilterUtility::trySetGlobalTimeout(const Http::HeaderEntry& header_timeout_entry,
                                        TimeoutData& timeout) {
  const auto timeout_ms = tryParseHeaderTimeout(header_timeout_entry);
  if (timeout_ms.has_value()) {
    timeout.global_timeout_ = timeout_ms.value();
  }
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
  PANIC("unexpectedly reached");
}

Stats::StatName Filter::upstreamZone(Upstream::HostDescriptionConstSharedPtr upstream_host) {
  return upstream_host ? upstream_host->localityZoneStatName() : config_->empty_stat_name_;
}

void Filter::chargeUpstreamCode(uint64_t response_status_code,
                                const Http::ResponseHeaderMap& response_headers,
                                Upstream::HostDescriptionConstSharedPtr upstream_host,
                                bool dropped) {
  // Passing the response_status_code explicitly is an optimization to avoid
  // multiple calls to slow Http::Utility::getResponseStatus.
  ASSERT(response_status_code == Http::Utility::getResponseStatus(response_headers));
  if (config_->emit_dynamic_stats_ && !callbacks_->streamInfo().healthCheck()) {
    const Http::HeaderEntry* upstream_canary_header = response_headers.EnvoyUpstreamCanary();
    const bool is_canary = (upstream_canary_header && upstream_canary_header->value() == "true") ||
                           (upstream_host ? upstream_host->canary() : false);
    const bool internal_request = Http::HeaderUtility::isEnvoyInternalRequest(*downstream_headers_);

    Stats::StatName upstream_zone = upstreamZone(upstream_host);
    Http::CodeStats::ResponseStatInfo info{
        config_->scope_,
        cluster_->statsScope(),
        config_->empty_stat_name_,
        response_status_code,
        internal_request,
        route_->virtualHost().statName(),
        request_vcluster_ ? request_vcluster_->statName() : config_->empty_stat_name_,
        route_stats_context_.has_value() ? route_stats_context_->statName()
                                         : config_->empty_stat_name_,
        config_->zone_name_,
        upstream_zone,
        is_canary};

    Http::CodeStats& code_stats = httpContext().codeStats();
    code_stats.chargeResponseStat(info, exclude_http_code_stats_);

    if (alt_stat_prefix_ != nullptr) {
      Http::CodeStats::ResponseStatInfo alt_info{config_->scope_,
                                                 cluster_->statsScope(),
                                                 alt_stat_prefix_->statName(),
                                                 response_status_code,
                                                 internal_request,
                                                 config_->empty_stat_name_,
                                                 config_->empty_stat_name_,
                                                 config_->empty_stat_name_,
                                                 config_->zone_name_,
                                                 upstream_zone,
                                                 is_canary};
      code_stats.chargeResponseStat(alt_info, exclude_http_code_stats_);
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
  const DebugConfig* debug_config = filter_state->getDataReadOnly<DebugConfig>(DebugConfig::key());

  // TODO: Maybe add a filter API for this.
  grpc_request_ = Grpc::Common::isGrpcRequestHeaders(headers);
  exclude_http_code_stats_ = grpc_request_ && config_->suppress_grpc_request_failure_code_stats_;

  // Only increment rq total stat if we actually decode headers here. This does not count requests
  // that get handled by earlier filters.
  stats_.rq_total_.inc();

  // Initialize the `modify_headers` function as a no-op (so we don't have to remember to check it
  // against nullptr before calling it), and feed it behavior later if/when we have cluster info
  // headers to append.
  std::function<void(Http::ResponseHeaderMap&)> modify_headers = [](Http::ResponseHeaderMap&) {};

  // Determine if there is a route entry or a direct response for the request.
  route_ = callbacks_->route();
  if (!route_) {
    stats_.no_route_.inc();
    ENVOY_STREAM_LOG(debug, "no route match for URL '{}'", *callbacks_, headers.getPathValue());

    callbacks_->streamInfo().setResponseFlag(StreamInfo::CoreResponseFlag::NoRouteFound);
    callbacks_->sendLocalReply(Http::Code::NotFound, "", modify_headers, absl::nullopt,
                               StreamInfo::ResponseCodeDetails::get().RouteNotFound);
    return Http::FilterHeadersStatus::StopIteration;
  }

  // Determine if there is a direct response for the request.
  const auto* direct_response = route_->directResponseEntry();
  if (direct_response != nullptr) {
    stats_.rq_direct_response_.inc();
    direct_response->rewritePathHeader(headers, !config_->suppress_envoy_headers_);
    callbacks_->sendLocalReply(
        direct_response->responseCode(), direct_response->responseBody(),
        [this, direct_response,
         &request_headers = headers](Http::ResponseHeaderMap& response_headers) -> void {
          std::string new_uri;
          if (request_headers.Path()) {
            new_uri = direct_response->newUri(request_headers);
          }
          // See https://tools.ietf.org/html/rfc7231#section-7.1.2.
          const auto add_location =
              direct_response->responseCode() == Http::Code::Created ||
              Http::CodeUtility::is3xx(enumToInt(direct_response->responseCode()));
          if (!new_uri.empty() && add_location) {
            response_headers.addReferenceKey(Http::Headers::get().Location, new_uri);
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
  if (debug_config && debug_config->append_cluster_) {
    // The cluster name will be appended to any local or upstream responses from this point.
    modify_headers = [this, debug_config](Http::ResponseHeaderMap& headers) {
      headers.addCopy(debug_config->cluster_header_.value_or(Http::Headers::get().EnvoyCluster),
                      route_entry_->clusterName());
    };
  }
  Upstream::ThreadLocalCluster* cluster =
      config_->cm_.getThreadLocalCluster(route_entry_->clusterName());
  if (!cluster) {
    stats_.no_cluster_.inc();
    ENVOY_STREAM_LOG(debug, "unknown cluster '{}'", *callbacks_, route_entry_->clusterName());

    callbacks_->streamInfo().setResponseFlag(StreamInfo::CoreResponseFlag::NoClusterFound);
    callbacks_->sendLocalReply(route_entry_->clusterNotFoundResponseCode(), "", modify_headers,
                               absl::nullopt,
                               StreamInfo::ResponseCodeDetails::get().ClusterNotFound);
    return Http::FilterHeadersStatus::StopIteration;
  }
  cluster_ = cluster->info();

  // Set up stat prefixes, etc.
  request_vcluster_ = route_->virtualHost().virtualCluster(headers);
  if (request_vcluster_ != nullptr) {
    callbacks_->streamInfo().setVirtualClusterName(request_vcluster_->name());
  }
  route_stats_context_ = route_entry_->routeStatsContext();
  ENVOY_STREAM_LOG(debug, "cluster '{}' match for URL '{}'", *callbacks_,
                   route_entry_->clusterName(), headers.getPathValue());

  if (config_->strict_check_headers_ != nullptr) {
    for (const auto& header : *config_->strict_check_headers_) {
      const auto res = FilterUtility::StrictHeaderChecker::checkHeader(headers, header);
      if (!res.valid_) {
        callbacks_->streamInfo().setResponseFlag(
            StreamInfo::CoreResponseFlag::InvalidEnvoyRequestHeaders);
        const std::string body = fmt::format("invalid header '{}' with value '{}'",
                                             std::string(res.entry_->key().getStringView()),
                                             std::string(res.entry_->value().getStringView()));
        const std::string details =
            absl::StrCat(StreamInfo::ResponseCodeDetails::get().InvalidEnvoyRequestHeaders, "{",
                         StringUtil::replaceAllEmptySpace(res.entry_->key().getStringView()), "}");
        callbacks_->sendLocalReply(Http::Code::BadRequest, body, nullptr, absl::nullopt, details);
        return Http::FilterHeadersStatus::StopIteration;
      }
    }
  }

  const Http::HeaderEntry* request_alt_name = headers.EnvoyUpstreamAltStatName();
  if (request_alt_name) {
    alt_stat_prefix_ = std::make_unique<Stats::StatNameDynamicStorage>(
        request_alt_name->value().getStringView(), config_->scope_.symbolTable());
    headers.removeEnvoyUpstreamAltStatName();
  }

  // See if we are supposed to immediately kill some percentage of this cluster's traffic.
  if (cluster_->maintenanceMode()) {
    callbacks_->streamInfo().setResponseFlag(StreamInfo::CoreResponseFlag::UpstreamOverflow);
    chargeUpstreamCode(Http::Code::ServiceUnavailable, nullptr, true);
    callbacks_->sendLocalReply(
        Http::Code::ServiceUnavailable, "maintenance mode",
        [modify_headers, this](Http::ResponseHeaderMap& headers) {
          if (!config_->suppress_envoy_headers_) {
            headers.addReference(Http::Headers::get().EnvoyOverloaded,
                                 Http::Headers::get().EnvoyOverloadedValues.True);
          }
          // Note: append_cluster_info does not respect suppress_envoy_headers.
          modify_headers(headers);
        },
        absl::nullopt, StreamInfo::ResponseCodeDetails::get().MaintenanceMode);
    cluster_->trafficStats()->upstream_rq_maintenance_mode_.inc();
    return Http::FilterHeadersStatus::StopIteration;
  }

  // Support DROP_OVERLOAD config from control plane to drop certain percentage of traffic.
  if (checkDropOverload(*cluster, modify_headers)) {
    return Http::FilterHeadersStatus::StopIteration;
  }

  // Fetch a connection pool for the upstream cluster.
  const auto& upstream_http_protocol_options = cluster_->upstreamHttpProtocolOptions();

  if (upstream_http_protocol_options.has_value() &&
      (upstream_http_protocol_options.value().auto_sni() ||
       upstream_http_protocol_options.value().auto_san_validation())) {
    // Default the header to Host/Authority header.
    std::string header_value = route_entry_->getRequestHostValue(headers);
    if (!Runtime::runtimeFeatureEnabled(
            "envoy.reloadable_features.use_route_host_mutation_for_auto_sni_san")) {
      header_value = std::string(headers.getHostValue());
    }

    // Check whether `override_auto_sni_header` is specified.
    const auto override_auto_sni_header =
        upstream_http_protocol_options.value().override_auto_sni_header();
    if (!override_auto_sni_header.empty()) {
      // Use the header value from `override_auto_sni_header` to set the SNI value.
      const auto overridden_header_value = Http::HeaderUtility::getAllOfHeaderAsString(
          headers, Http::LowerCaseString(override_auto_sni_header));
      if (overridden_header_value.result().has_value() &&
          !overridden_header_value.result().value().empty()) {
        header_value = overridden_header_value.result().value();
      }
    }
    const auto parsed_authority = Http::Utility::parseAuthority(header_value);
    bool should_set_sni = !parsed_authority.is_ip_address_;
    // `host_` returns a string_view so doing this should be safe.
    absl::string_view sni_value = parsed_authority.host_;

    if (should_set_sni && upstream_http_protocol_options.value().auto_sni() &&
        !callbacks_->streamInfo().filterState()->hasDataWithName(
            Network::UpstreamServerName::key())) {
      callbacks_->streamInfo().filterState()->setData(
          Network::UpstreamServerName::key(),
          std::make_unique<Network::UpstreamServerName>(sni_value),
          StreamInfo::FilterState::StateType::Mutable);
    }

    if (upstream_http_protocol_options.value().auto_san_validation() &&
        !callbacks_->streamInfo().filterState()->hasDataWithName(
            Network::UpstreamSubjectAltNames::key())) {
      callbacks_->streamInfo().filterState()->setData(
          Network::UpstreamSubjectAltNames::key(),
          std::make_unique<Network::UpstreamSubjectAltNames>(
              std::vector<std::string>{std::string(sni_value)}),
          StreamInfo::FilterState::StateType::Mutable);
    }
  }

  transport_socket_options_ = Network::TransportSocketOptionsUtility::fromFilterState(
      *callbacks_->streamInfo().filterState());

  if (auto downstream_connection = downstreamConnection(); downstream_connection != nullptr) {
    if (auto typed_state = downstream_connection->streamInfo()
                               .filterState()
                               .getDataReadOnly<Network::UpstreamSocketOptionsFilterState>(
                                   Network::UpstreamSocketOptionsFilterState::key());
        typed_state != nullptr) {
      auto downstream_options = typed_state->value();
      if (!upstream_options_) {
        upstream_options_ = std::make_shared<Network::Socket::Options>();
      }
      Network::Socket::appendOptions(upstream_options_, downstream_options);
    }
  }

  if (upstream_options_ && callbacks_->getUpstreamSocketOptions()) {
    Network::Socket::appendOptions(upstream_options_, callbacks_->getUpstreamSocketOptions());
  }

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

  if (callbacks_->shouldLoadShed()) {
    callbacks_->sendLocalReply(Http::Code::ServiceUnavailable, "envoy overloaded", nullptr,
                               absl::nullopt, StreamInfo::ResponseCodeDetails::get().Overload);
    stats_.rq_overload_local_reply_.inc();
    return Http::FilterHeadersStatus::StopIteration;
  }

  hedging_params_ = FilterUtility::finalHedgingParams(*route_entry_, headers);

  timeout_ = FilterUtility::finalTimeout(*route_entry_, headers, !config_->suppress_envoy_headers_,
                                         grpc_request_, hedging_params_.hedge_on_per_try_timeout_,
                                         config_->respect_expected_rq_timeout_);

  const Http::HeaderEntry* header_max_stream_duration_entry =
      headers.EnvoyUpstreamStreamDurationMs();
  if (header_max_stream_duration_entry) {
    dynamic_max_stream_duration_ =
        FilterUtility::tryParseHeaderTimeout(*header_max_stream_duration_entry);
    headers.removeEnvoyUpstreamStreamDurationMs();
  }

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

      // This header is added without checking for config_->suppress_envoy_headers_ to mirror what
      // is done for upstream requests.
      headers.setEnvoyAttemptCount(attempt_count_);
    };
  }
  callbacks_->streamInfo().setAttemptCount(attempt_count_);

  route_entry_->finalizeRequestHeaders(headers, callbacks_->streamInfo(),
                                       !config_->suppress_envoy_headers_);
  FilterUtility::setUpstreamScheme(
      headers, callbacks_->streamInfo().downstreamAddressProvider().sslConnection() != nullptr,
      host->transportSocketFactory().sslCtx() != nullptr,
      callbacks_->streamInfo().shouldSchemeMatchUpstream());

  // Ensure an http transport scheme is selected before continuing with decoding.
  ASSERT(headers.Scheme());

  retry_state_ = createRetryState(
      route_entry_->retryPolicy(), headers, *cluster_, request_vcluster_, route_stats_context_,
      config_->factory_context_, callbacks_->dispatcher(), route_entry_->priority());

  // Determine which shadow policies to use. It's possible that we don't do any shadowing due to
  // runtime keys. Also the method CONNECT doesn't support shadowing.
  auto method = headers.getMethodValue();
  if (method != Http::Headers::get().MethodValues.Connect) {
    for (const auto& shadow_policy : route_entry_->shadowPolicies()) {
      const auto& policy_ref = *shadow_policy;
      if (FilterUtility::shouldShadow(policy_ref, config_->runtime_, callbacks_->streamId())) {
        active_shadow_policies_.push_back(std::cref(policy_ref));
        shadow_headers_ = Http::createHeaderMap<Http::RequestHeaderMapImpl>(*downstream_headers_);
      }
    }
  }

  ENVOY_STREAM_LOG(debug, "router decoding headers:\n{}", *callbacks_, headers);

  // Hang onto the modify_headers function for later use in handling upstream responses.
  modify_headers_ = modify_headers;

  const bool can_send_early_data =
      route_entry_->earlyDataPolicy().allowsEarlyDataForRequest(*downstream_headers_);

  include_timeout_retry_header_in_request_ = route_->virtualHost().includeIsTimeoutRetryHeader();

  // Set initial HTTP/3 use based on the presence of HTTP/1.1 proxy config.
  // For retries etc, HTTP/3 usability may transition from true to false, but
  // will never transition from false to true.
  bool can_use_http3 =
      !transport_socket_options_ || !transport_socket_options_->http11ProxyInfo().has_value();
  UpstreamRequestPtr upstream_request = std::make_unique<UpstreamRequest>(
      *this, std::move(generic_conn_pool), can_send_early_data, can_use_http3,
      allow_multiplexed_upstream_half_close_ /*enable_half_close*/);
  LinkedList::moveIntoList(std::move(upstream_request), upstream_requests_);
  upstream_requests_.front()->acceptHeadersFromRouter(end_stream);
  if (streaming_shadows_) {
    // start the shadow streams.
    for (const auto& shadow_policy_wrapper : active_shadow_policies_) {
      const auto& shadow_policy = shadow_policy_wrapper.get();
      const absl::optional<absl::string_view> shadow_cluster_name =
          getShadowCluster(shadow_policy, *downstream_headers_);
      if (!shadow_cluster_name.has_value()) {
        continue;
      }
      auto shadow_headers = Http::createHeaderMap<Http::RequestHeaderMapImpl>(*shadow_headers_);
      auto options =
          Http::AsyncClient::RequestOptions()
              .setTimeout(timeout_.global_timeout_)
              .setParentSpan(callbacks_->activeSpan())
              .setChildSpanName("mirror")
              .setSampled(shadow_policy.traceSampled())
              .setIsShadow(true)
              .setIsShadowSuffixDisabled(shadow_policy.disableShadowHostSuffixAppend())
              .setBufferAccount(callbacks_->account())
              // A buffer limit of 1 is set in the case that retry_shadow_buffer_limit_ == 0,
              // because a buffer limit of zero on async clients is interpreted as no buffer limit.
              .setBufferLimit(1 > retry_shadow_buffer_limit_ ? 1 : retry_shadow_buffer_limit_)
              .setDiscardResponseBody(true);
      options.setFilterConfig(config_);
      if (end_stream) {
        // This is a header-only request, and can be dispatched immediately to the shadow
        // without waiting.
        Http::RequestMessagePtr request(new Http::RequestMessageImpl(
            Http::createHeaderMap<Http::RequestHeaderMapImpl>(*shadow_headers_)));
        config_->shadowWriter().shadow(std::string(shadow_cluster_name.value()), std::move(request),
                                       options);
      } else {
        Http::AsyncClient::OngoingRequest* shadow_stream = config_->shadowWriter().streamingShadow(
            std::string(shadow_cluster_name.value()), std::move(shadow_headers), options);
        if (shadow_stream != nullptr) {
          shadow_streams_.insert(shadow_stream);
          shadow_stream->setDestructorCallback(
              [this, shadow_stream]() { shadow_streams_.erase(shadow_stream); });
          shadow_stream->setWatermarkCallbacks(watermark_callbacks_);
        }
      }
    }
  }
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
        cluster_->upstreamConfig().ref());
    ENVOY_BUG(factory != nullptr,
              fmt::format("invalid factory type '{}', failing over to default upstream",
                          cluster_->upstreamConfig().ref().DebugString()));
  }
  if (!factory) {
    factory = &config_->router_context_.genericConnPoolFactory();
  }

  using UpstreamProtocol = Envoy::Router::GenericConnPoolFactory::UpstreamProtocol;
  UpstreamProtocol upstream_protocol = UpstreamProtocol::HTTP;
  if (route_entry_->connectConfig().has_value()) {
    auto method = downstream_headers_->getMethodValue();
    if (Http::HeaderUtility::isConnectUdpRequest(*downstream_headers_)) {
      upstream_protocol = UpstreamProtocol::UDP;
    } else if (method == Http::Headers::get().MethodValues.Connect ||
               (route_entry_->connectConfig()->allow_post() &&
                method == Http::Headers::get().MethodValues.Post)) {
      // Allow POST for proxying raw TCP if it is configured.
      upstream_protocol = UpstreamProtocol::TCP;
    }
  }
  return factory->createGenericConnPool(thread_local_cluster, upstream_protocol,
                                        route_entry_->priority(),
                                        callbacks_->streamInfo().protocol(), this);
}

void Filter::sendNoHealthyUpstreamResponse() {
  callbacks_->streamInfo().setResponseFlag(StreamInfo::CoreResponseFlag::NoHealthyUpstream);
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

  bool buffering = (retry_state_ && retry_state_->enabled()) ||
                   (!active_shadow_policies_.empty() && !streaming_shadows_) ||
                   (route_entry_ && route_entry_->internalRedirectPolicy().enabled());
  if (buffering &&
      getLength(callbacks_->decodingBuffer()) + data.length() > retry_shadow_buffer_limit_) {
    ENVOY_LOG(debug,
              "The request payload has at least {} bytes data which exceeds buffer limit {}. Give "
              "up on the retry/shadow.",
              getLength(callbacks_->decodingBuffer()) + data.length(), retry_shadow_buffer_limit_);
    cluster_->trafficStats()->retry_or_shadow_abandoned_.inc();
    retry_state_.reset();
    buffering = false;
    active_shadow_policies_.clear();
    request_buffer_overflowed_ = true;

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

  for (auto* shadow_stream : shadow_streams_) {
    if (end_stream) {
      shadow_stream->removeDestructorCallback();
      shadow_stream->removeWatermarkCallbacks();
    }
    Buffer::OwnedImpl copy(data);
    shadow_stream->sendData(copy, end_stream);
  }
  if (end_stream) {
    shadow_streams_.clear();
  }
  if (buffering) {
    if (!upstream_requests_.empty()) {
      Buffer::OwnedImpl copy(data);
      upstream_requests_.front()->acceptDataFromRouter(copy, end_stream);
    }

    // If we are potentially going to retry or buffer shadow this request we need to buffer.
    // This will not cause the connection manager to 413 because before we hit the
    // buffer limit we give up on retries and buffering. We must buffer using addDecodedData()
    // so that all buffered data is available by the time we do request complete processing and
    // potentially shadow. Additionally, we can't do a copy here because there's a check down
    // this stack for whether `data` is the same buffer as already buffered data.
    callbacks_->addDecodedData(data, true);
  } else {
    if (!Runtime::runtimeFeatureEnabled(
            "envoy.reloadable_features.send_local_reply_when_no_buffer_and_upstream_request")) {
      upstream_requests_.front()->acceptDataFromRouter(data, end_stream);
    } else {
      if (!upstream_requests_.empty()) {
        upstream_requests_.front()->acceptDataFromRouter(data, end_stream);
      } else {
        // not buffering any data for retry, shadow, and internal redirect, and there will be
        // no more upstream request, abort the request and clean up.
        cleanup();
        callbacks_->sendLocalReply(
            Http::Code::ServiceUnavailable,
            "upstream is closed prematurely during decoding data from downstream", modify_headers_,
            absl::nullopt, StreamInfo::ResponseCodeDetails::get().EarlyUpstreamReset);
        return Http::FilterDataStatus::StopIterationNoBuffer;
      }
    }
  }

  if (end_stream) {
    onRequestComplete();
  }

  return Http::FilterDataStatus::StopIterationNoBuffer;
}

Http::FilterTrailersStatus Filter::decodeTrailers(Http::RequestTrailerMap& trailers) {
  ENVOY_STREAM_LOG(debug, "router decoding trailers:\n{}", *callbacks_, trailers);

  if (shadow_headers_) {
    shadow_trailers_ = Http::createHeaderMap<Http::RequestTrailerMapImpl>(trailers);
  }

  // upstream_requests_.size() cannot be > 1 because that only happens when a per
  // try timeout occurs with hedge_on_per_try_timeout enabled but the per
  // try timeout timer is not started until onRequestComplete(). It could be zero
  // if the first request attempt has already failed and a retry is waiting for
  // a backoff timer.
  ASSERT(upstream_requests_.size() <= 1);
  downstream_trailers_ = &trailers;
  if (!upstream_requests_.empty()) {
    upstream_requests_.front()->acceptTrailersFromRouter(trailers);
  }
  for (auto* shadow_stream : shadow_streams_) {
    shadow_stream->removeDestructorCallback();
    shadow_stream->removeWatermarkCallbacks();
    shadow_stream->captureAndSendTrailers(
        Http::createHeaderMap<Http::RequestTrailerMapImpl>(*shadow_trailers_));
  }
  shadow_streams_.clear();

  onRequestComplete();
  return Http::FilterTrailersStatus::StopIteration;
}

Http::FilterMetadataStatus Filter::decodeMetadata(Http::MetadataMap& metadata_map) {
  Http::MetadataMapPtr metadata_map_ptr = std::make_unique<Http::MetadataMap>(metadata_map);
  if (!upstream_requests_.empty()) {
    // TODO(soya3129): Save metadata for retry, redirect and shadowing case.
    upstream_requests_.front()->acceptMetadataFromRouter(std::move(metadata_map_ptr));
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

  watermark_callbacks_.setDecoderFilterCallbacks(callbacks_);
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

absl::optional<absl::string_view> Filter::getShadowCluster(const ShadowPolicy& policy,
                                                           const Http::HeaderMap& headers) const {
  if (!policy.cluster().empty()) {
    return policy.cluster();
  } else {
    ASSERT(!policy.clusterHeader().get().empty());
    const auto entry = headers.get(policy.clusterHeader());
    if (!entry.empty() && !entry[0]->value().empty()) {
      return entry[0]->value().getStringView();
    }
    ENVOY_STREAM_LOG(debug, "There is no cluster name in header: {}", *callbacks_,
                     policy.clusterHeader());
    return absl::nullopt;
  }
}

void Filter::maybeDoShadowing() {
  for (const auto& shadow_policy_wrapper : active_shadow_policies_) {
    const auto& shadow_policy = shadow_policy_wrapper.get();

    const absl::optional<absl::string_view> shadow_cluster_name =
        getShadowCluster(shadow_policy, *downstream_headers_);

    // The cluster name got from headers is empty.
    if (!shadow_cluster_name.has_value()) {
      continue;
    }

    Http::RequestMessagePtr request(new Http::RequestMessageImpl(
        Http::createHeaderMap<Http::RequestHeaderMapImpl>(*shadow_headers_)));
    if (callbacks_->decodingBuffer()) {
      request->body().add(*callbacks_->decodingBuffer());
    }
    if (shadow_trailers_) {
      request->trailers(Http::createHeaderMap<Http::RequestTrailerMapImpl>(*shadow_trailers_));
    }

    auto options = Http::AsyncClient::RequestOptions()
                       .setTimeout(timeout_.global_timeout_)
                       .setParentSpan(callbacks_->activeSpan())
                       .setChildSpanName("mirror")
                       .setSampled(shadow_policy.traceSampled())
                       .setIsShadow(true)
                       .setIsShadowSuffixDisabled(shadow_policy.disableShadowHostSuffixAppend());
    options.setFilterConfig(config_);
    config_->shadowWriter().shadow(std::string(shadow_cluster_name.value()), std::move(request),
                                   options);
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
    if (!streaming_shadows_) {
      maybeDoShadowing();
    }

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

  // Unregister from shadow stream notifications and cancel active streams.
  for (auto* shadow_stream : shadow_streams_) {
    shadow_stream->removeDestructorCallback();
    shadow_stream->removeWatermarkCallbacks();
    shadow_stream->cancel();
  }

  cleanup();
}

void Filter::onResponseTimeout() {
  ENVOY_STREAM_LOG(debug, "upstream timeout", *callbacks_);

  // Reset any upstream requests that are still in flight.
  while (!upstream_requests_.empty()) {
    UpstreamRequestPtr upstream_request =
        upstream_requests_.back()->removeFromList(upstream_requests_);

    // We want to record the upstream timeouts and increase the stats counters in all the cases.
    // For example, we also want to record the stats in the case of BiDi streaming APIs where we
    // might have already seen the headers.
    cluster_->trafficStats()->upstream_rq_timeout_.inc();
    if (request_vcluster_) {
      request_vcluster_->stats().upstream_rq_timeout_.inc();
    }
    if (route_stats_context_.has_value()) {
      route_stats_context_->stats().upstream_rq_timeout_.inc();
    }

    if (upstream_request->upstreamHost()) {
      upstream_request->upstreamHost()->stats().rq_timeout_.inc();
    }

    if (upstream_request->awaitingHeaders()) {
      if (cluster_->timeoutBudgetStats().has_value()) {
        // Cancel firing per-try timeout information, because the per-try timeout did not come into
        // play when the global timeout was hit.
        upstream_request->recordTimeoutBudget(false);
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

  onUpstreamTimeoutAbort(StreamInfo::CoreResponseFlag::UpstreamRequestTimeout,
                         StreamInfo::ResponseCodeDetails::get().ResponseTimeout);
}

// Called when the per try timeout is hit but we didn't reset the request
// (hedge_on_per_try_timeout enabled).
void Filter::onSoftPerTryTimeout(UpstreamRequest& upstream_request) {
  ASSERT(!upstream_request.retried());
  // Track this as a timeout for outlier detection purposes even though we didn't
  // cancel the request yet and might get a 2xx later.
  updateOutlierDetection(Upstream::Outlier::Result::LocalOriginTimeout, upstream_request,
                         absl::optional<uint64_t>(enumToInt(timeout_response_code_)));
  upstream_request.outlierDetectionTimeoutRecorded(true);

  if (!downstream_response_started_ && retry_state_) {
    RetryStatus retry_status = retry_state_->shouldHedgeRetryPerTryTimeout(
        [this, can_use_http3 = upstream_request.upstreamStreamOptions().can_use_http3_]() -> void {
          // Without any knowledge about what's going on in the connection pool, retry the request
          // with the safest settings which is no early data but keep using or not using alt-svc as
          // before. In this way, QUIC won't be falsely marked as broken.
          doRetry(/*can_send_early_data*/ false, can_use_http3, TimeoutRetry::Yes);
        });

    if (retry_status == RetryStatus::Yes) {
      runRetryOptionsPredicates(upstream_request);
      pending_retries_++;

      // Don't increment upstream_host->stats().rq_error_ here, we'll do that
      // later if 1) we hit global timeout or 2) we get bad response headers
      // back.
      upstream_request.retried(true);

      // TODO: cluster stat for hedge attempted.
    } else if (retry_status == RetryStatus::NoOverflow) {
      callbacks_->streamInfo().setResponseFlag(StreamInfo::CoreResponseFlag::UpstreamOverflow);
    } else if (retry_status == RetryStatus::NoRetryLimitExceeded) {
      callbacks_->streamInfo().setResponseFlag(
          StreamInfo::CoreResponseFlag::UpstreamRetryLimitExceeded);
    }
  }
}

void Filter::onPerTryIdleTimeout(UpstreamRequest& upstream_request) {
  onPerTryTimeoutCommon(upstream_request,
                        cluster_->trafficStats()->upstream_rq_per_try_idle_timeout_,
                        StreamInfo::ResponseCodeDetails::get().UpstreamPerTryIdleTimeout);
}

void Filter::onPerTryTimeout(UpstreamRequest& upstream_request) {
  onPerTryTimeoutCommon(upstream_request, cluster_->trafficStats()->upstream_rq_per_try_timeout_,
                        StreamInfo::ResponseCodeDetails::get().UpstreamPerTryTimeout);
}

void Filter::onPerTryTimeoutCommon(UpstreamRequest& upstream_request, Stats::Counter& error_counter,
                                   const std::string& response_code_details) {
  if (hedging_params_.hedge_on_per_try_timeout_) {
    onSoftPerTryTimeout(upstream_request);
    return;
  }

  error_counter.inc();
  if (upstream_request.upstreamHost()) {
    upstream_request.upstreamHost()->stats().rq_timeout_.inc();
  }

  upstream_request.resetStream();

  updateOutlierDetection(Upstream::Outlier::Result::LocalOriginTimeout, upstream_request,
                         absl::optional<uint64_t>(enumToInt(timeout_response_code_)));

  if (maybeRetryReset(Http::StreamResetReason::LocalReset, upstream_request, TimeoutRetry::Yes)) {
    return;
  }

  chargeUpstreamAbort(timeout_response_code_, false, upstream_request);

  // Remove this upstream request from the list now that we're done with it.
  upstream_request.removeFromList(upstream_requests_);
  onUpstreamTimeoutAbort(StreamInfo::CoreResponseFlag::UpstreamRequestTimeout,
                         response_code_details);
}

void Filter::onStreamMaxDurationReached(UpstreamRequest& upstream_request) {
  upstream_request.resetStream();

  if (maybeRetryReset(Http::StreamResetReason::LocalReset, upstream_request, TimeoutRetry::No)) {
    return;
  }

  upstream_request.removeFromList(upstream_requests_);
  cleanup();

  callbacks_->streamInfo().setResponseFlag(
      StreamInfo::CoreResponseFlag::UpstreamMaxStreamDurationReached);
  // Grab the const ref to call the const method of StreamInfo.
  const auto& stream_info = callbacks_->streamInfo();
  const bool downstream_decode_complete =
      stream_info.downstreamTiming().has_value() &&
      stream_info.downstreamTiming().value().get().lastDownstreamRxByteReceived().has_value();

  // sendLocalReply may instead reset the stream if downstream_response_started_ is true.
  callbacks_->sendLocalReply(
      Http::Utility::maybeRequestTimeoutCode(downstream_decode_complete),
      "upstream max stream duration reached", modify_headers_, absl::nullopt,
      StreamInfo::ResponseCodeDetails::get().UpstreamMaxStreamDurationReached);
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
      stats_.rq_reset_after_downstream_response_started_.inc();
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

void Filter::onUpstreamTimeoutAbort(StreamInfo::CoreResponseFlag response_flags,
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

void Filter::onUpstreamAbort(Http::Code code, StreamInfo::CoreResponseFlag response_flags,
                             absl::string_view body, bool dropped, absl::string_view details) {
  // If we have not yet sent anything downstream, send a response with an appropriate status code.
  // Otherwise just reset the ongoing response.
  callbacks_->streamInfo().setResponseFlag(response_flags);
  // This will destroy any created retry timers.
  cleanup();
  // sendLocalReply may instead reset the stream if downstream_response_started_ is true.
  callbacks_->sendLocalReply(
      code, body,
      [dropped, this](Http::ResponseHeaderMap& headers) {
        if (dropped && !config_->suppress_envoy_headers_) {
          headers.addReference(Http::Headers::get().EnvoyOverloaded,
                               Http::Headers::get().EnvoyOverloadedValues.True);
        }
        modify_headers_(headers);
      },
      absl::nullopt, details);
}

bool Filter::maybeRetryReset(Http::StreamResetReason reset_reason,
                             UpstreamRequest& upstream_request, TimeoutRetry is_timeout_retry) {
  // We don't retry if we already started the response, don't have a retry policy defined,
  // or if we've already retried this upstream request (currently only possible if a per
  // try timeout occurred and hedge_on_per_try_timeout is enabled).
  if (downstream_response_started_ || !retry_state_ || upstream_request.retried()) {
    return false;
  }
  RetryState::Http3Used was_using_http3 = RetryState::Http3Used::Unknown;
  if (upstream_request.hadUpstream()) {
    was_using_http3 = (upstream_request.streamInfo().protocol().has_value() &&
                       upstream_request.streamInfo().protocol().value() == Http::Protocol::Http3)
                          ? RetryState::Http3Used::Yes
                          : RetryState::Http3Used::No;
  }

  // If the current request in this router has sent data to the upstream, we consider the request
  // started.
  upstream_request_started_ |= upstream_request.streamInfo()
                                   .upstreamInfo()
                                   ->upstreamTiming()
                                   .first_upstream_tx_byte_sent_.has_value();

  const RetryStatus retry_status = retry_state_->shouldRetryReset(
      reset_reason, was_using_http3,
      [this, can_send_early_data = upstream_request.upstreamStreamOptions().can_send_early_data_,
       can_use_http3 = upstream_request.upstreamStreamOptions().can_use_http3_,
       is_timeout_retry](bool disable_http3) -> void {
        // This retry might be because of ConnectionFailure of 0-RTT handshake. In this case, though
        // the original request is retried with the same can_send_early_data setting, it will not be
        // sent as early data by the underlying connection pool grid.
        doRetry(can_send_early_data, disable_http3 ? false : can_use_http3, is_timeout_retry);
      },
      upstream_request_started_);
  if (retry_status == RetryStatus::Yes) {
    runRetryOptionsPredicates(upstream_request);
    pending_retries_++;

    if (upstream_request.upstreamHost()) {
      upstream_request.upstreamHost()->stats().rq_error_.inc();
    }

    auto request_ptr = upstream_request.removeFromList(upstream_requests_);
    callbacks_->dispatcher().deferredDelete(std::move(request_ptr));
    return true;
  } else if (retry_status == RetryStatus::NoOverflow) {
    callbacks_->streamInfo().setResponseFlag(StreamInfo::CoreResponseFlag::UpstreamOverflow);
  } else if (retry_status == RetryStatus::NoRetryLimitExceeded) {
    callbacks_->streamInfo().setResponseFlag(
        StreamInfo::CoreResponseFlag::UpstreamRetryLimitExceeded);
  }

  return false;
}

void Filter::onUpstreamReset(Http::StreamResetReason reset_reason,
                             absl::string_view transport_failure_reason,
                             UpstreamRequest& upstream_request) {
  ENVOY_STREAM_LOG(debug, "upstream reset: reset reason: {}, transport failure reason: {}",
                   *callbacks_, Http::Utility::resetReasonToString(reset_reason),
                   transport_failure_reason);

  const bool dropped = reset_reason == Http::StreamResetReason::Overflow;

  // Ignore upstream reset caused by a resource overflow.
  // Currently, circuit breakers can only produce this reset reason.
  // It means that this reason is cluster-wise, not upstream-related.
  // Therefore removing an upstream in the case of an overloaded cluster
  // would make the situation even worse.
  // https://github.com/envoyproxy/envoy/issues/25487
  if (!dropped) {
    // TODO: The reset may also come from upstream over the wire. In this case it should be
    // treated as external origin error and distinguished from local origin error.
    // This matters only when running OutlierDetection with split_external_local_origin_errors
    // config param set to true.
    updateOutlierDetection(Upstream::Outlier::Result::LocalOriginConnectFailed, upstream_request,
                           absl::nullopt);
  }

  if (maybeRetryReset(reset_reason, upstream_request, TimeoutRetry::No)) {
    return;
  }

  const Http::Code error_code = (reset_reason == Http::StreamResetReason::ProtocolError)
                                    ? Http::Code::BadGateway
                                    : Http::Code::ServiceUnavailable;
  chargeUpstreamAbort(error_code, dropped, upstream_request);
  auto request_ptr = upstream_request.removeFromList(upstream_requests_);
  callbacks_->dispatcher().deferredDelete(std::move(request_ptr));

  // If there are other in-flight requests that might see an upstream response,
  // don't return anything downstream.
  if (numRequestsAwaitingHeaders() > 0 || pending_retries_ > 0) {
    return;
  }

  const StreamInfo::CoreResponseFlag response_flags = streamResetReasonToResponseFlag(reset_reason);

  const std::string body =
      absl::StrCat("upstream connect error or disconnect/reset before headers. ",
                   (is_retry_ ? "retried and the latest " : ""),
                   "reset reason: ", Http::Utility::resetReasonToString(reset_reason),
                   !transport_failure_reason.empty() ? ", transport failure reason: " : "",
                   transport_failure_reason);
  const std::string& basic_details =
      downstream_response_started_ ? StreamInfo::ResponseCodeDetails::get().LateUpstreamReset
                                   : StreamInfo::ResponseCodeDetails::get().EarlyUpstreamReset;
  const std::string details = StringUtil::replaceAllEmptySpace(absl::StrCat(
      basic_details, "{", Http::Utility::resetReasonToString(reset_reason),
      transport_failure_reason.empty() ? "" : absl::StrCat("|", transport_failure_reason), "}"));
  onUpstreamAbort(error_code, response_flags, body, dropped, details);
}

void Filter::onUpstreamHostSelected(Upstream::HostDescriptionConstSharedPtr host,
                                    bool pool_success) {
  if (retry_state_ && host) {
    retry_state_->onHostAttempted(host);
  }

  if (!pool_success) {
    return;
  }

  if (request_vcluster_) {
    // The cluster increases its upstream_rq_total_ counter right before firing this onPoolReady
    // callback. Hence, the upstream request increases the virtual cluster's upstream_rq_total_ stat
    // here.
    request_vcluster_->stats().upstream_rq_total_.inc();
  }
  if (route_stats_context_.has_value()) {
    // The cluster increases its upstream_rq_total_ counter right before firing this onPoolReady
    // callback. Hence, the upstream request increases the route level upstream_rq_total_ stat
    // here.
    route_stats_context_->stats().upstream_rq_total_.inc();
  }
}

StreamInfo::CoreResponseFlag
Filter::streamResetReasonToResponseFlag(Http::StreamResetReason reset_reason) {
  switch (reset_reason) {
  case Http::StreamResetReason::LocalConnectionFailure:
  case Http::StreamResetReason::RemoteConnectionFailure:
  case Http::StreamResetReason::ConnectionTimeout:
    return StreamInfo::CoreResponseFlag::UpstreamConnectionFailure;
  case Http::StreamResetReason::ConnectionTermination:
    return StreamInfo::CoreResponseFlag::UpstreamConnectionTermination;
  case Http::StreamResetReason::LocalReset:
  case Http::StreamResetReason::LocalRefusedStreamReset:
  case Http::StreamResetReason::Http1PrematureUpstreamHalfClose:
    return StreamInfo::CoreResponseFlag::LocalReset;
  case Http::StreamResetReason::Overflow:
    return StreamInfo::CoreResponseFlag::UpstreamOverflow;
  case Http::StreamResetReason::RemoteReset:
  case Http::StreamResetReason::RemoteRefusedStreamReset:
  case Http::StreamResetReason::ConnectError:
    return StreamInfo::CoreResponseFlag::UpstreamRemoteReset;
  case Http::StreamResetReason::ProtocolError:
    return StreamInfo::CoreResponseFlag::UpstreamProtocolError;
  case Http::StreamResetReason::OverloadManager:
    return StreamInfo::CoreResponseFlag::OverloadManager;
  }

  PANIC_DUE_TO_CORRUPT_ENUM;
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

void Filter::onUpstream1xxHeaders(Http::ResponseHeaderMapPtr&& headers,
                                  UpstreamRequest& upstream_request) {
  const uint64_t response_code = Http::Utility::getResponseStatus(*headers);
  chargeUpstreamCode(response_code, *headers, upstream_request.upstreamHost(), false);
  ENVOY_STREAM_LOG(debug, "upstream 1xx ({}).", *callbacks_, response_code);

  downstream_response_started_ = true;
  final_upstream_request_ = &upstream_request;
  resetOtherUpstreams(upstream_request);

  // Don't send retries after 100-Continue has been sent on. Arguably we could attempt to do a
  // retry, assume the next upstream would also send an 100-Continue and swallow the second one
  // but it's sketchy (as the subsequent upstream might not send a 100-Continue) and not worth
  // the complexity until someone asks for it.
  retry_state_.reset();

  callbacks_->encode1xxHeaders(std::move(headers));
}

void Filter::resetAll() {
  while (!upstream_requests_.empty()) {
    auto request_ptr = upstream_requests_.back()->removeFromList(upstream_requests_);
    request_ptr->resetStream();
    callbacks_->dispatcher().deferredDelete(std::move(request_ptr));
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

  maybeProcessOrcaLoadReport(*headers, upstream_request);

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
      bool retry_as_early_data; // Not going to be used as we are not retrying.
      could_not_retry = retry_state_->wouldRetryFromHeaders(*headers, *downstream_headers_,
                                                            retry_as_early_data) !=
                        RetryState::RetryDecision::NoRetry;
    } else {
      const RetryStatus retry_status = retry_state_->shouldRetryHeaders(
          *headers, *downstream_headers_,
          [this, can_use_http3 = upstream_request.upstreamStreamOptions().can_use_http3_,
           had_early_data = upstream_request.upstreamStreamOptions().can_send_early_data_](
              bool disable_early_data) -> void {
            doRetry((disable_early_data ? false : had_early_data), can_use_http3, TimeoutRetry::No);
          });
      if (retry_status == RetryStatus::Yes) {
        runRetryOptionsPredicates(upstream_request);
        pending_retries_++;
        upstream_request.upstreamHost()->stats().rq_error_.inc();
        Http::CodeStats& code_stats = httpContext().codeStats();
        code_stats.chargeBasicResponseStat(cluster_->statsScope(), stats_.stat_names_.retry_,
                                           static_cast<Http::Code>(response_code),
                                           exclude_http_code_stats_);

        if (!end_stream || !upstream_request.encodeComplete()) {
          upstream_request.resetStream();
        }
        auto request_ptr = upstream_request.removeFromList(upstream_requests_);
        callbacks_->dispatcher().deferredDelete(std::move(request_ptr));
        return;
      } else if (retry_status == RetryStatus::NoOverflow) {
        callbacks_->streamInfo().setResponseFlag(StreamInfo::CoreResponseFlag::UpstreamOverflow);
        could_not_retry = true;
      } else if (retry_status == RetryStatus::NoRetryLimitExceeded) {
        callbacks_->streamInfo().setResponseFlag(
            StreamInfo::CoreResponseFlag::UpstreamRetryLimitExceeded);
        could_not_retry = true;
      }
    }
  }

  if (route_entry_->internalRedirectPolicy().enabled() &&
      route_entry_->internalRedirectPolicy().shouldRedirectForResponseCode(
          static_cast<Http::Code>(response_code)) &&
      setupRedirect(*headers)) {
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
    auto request_ptr = upstream_request.removeFromList(upstream_requests_);
    request_ptr->resetStream();
    callbacks_->dispatcher().deferredDelete(std::move(request_ptr));
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
    if (!config_->suppress_envoy_headers_) {
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

  callbacks_->streamInfo().setResponseCodeDetails(
      StreamInfo::ResponseCodeDetails::get().ViaUpstream);

  callbacks_->streamInfo().setResponseCode(response_code);

  // TODO(zuercher): If access to response_headers_to_add (at any level) is ever needed outside
  // Router::Filter we'll need to find a better location for this work. One possibility is to
  // provide finalizeResponseHeaders functions on the Router::Config and VirtualHost interfaces.
  route_entry_->finalizeResponseHeaders(*headers, callbacks_->streamInfo());

  downstream_response_started_ = true;
  final_upstream_request_ = &upstream_request;
  // Make sure that for request hedging, we end up with the correct final upstream info.
  callbacks_->streamInfo().setUpstreamInfo(final_upstream_request_->streamInfo().upstreamInfo());
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

  maybeProcessOrcaLoadReport(*trailers, upstream_request);

  onUpstreamComplete(upstream_request);

  callbacks_->encodeTrailers(std::move(trailers));
}

void Filter::onUpstreamMetadata(Http::MetadataMapPtr&& metadata_map) {
  callbacks_->encodeMetadata(std::move(metadata_map));
}

void Filter::onUpstreamComplete(UpstreamRequest& upstream_request) {
  if (!downstream_end_stream_) {
    if (allow_multiplexed_upstream_half_close_) {
      // Continue request if downstream is not done yet.
      return;
    }
    upstream_request.resetStream();
  }
  Event::Dispatcher& dispatcher = callbacks_->dispatcher();
  std::chrono::milliseconds response_time = std::chrono::duration_cast<std::chrono::milliseconds>(
      dispatcher.timeSource().monotonicTime() - downstream_request_complete_time_);

  Upstream::ClusterTimeoutBudgetStatsOptRef tb_stats = cluster()->timeoutBudgetStats();
  if (tb_stats.has_value()) {
    tb_stats->get().upstream_rq_timeout_budget_percent_used_.recordValue(
        FilterUtility::percentageOfTimeout(response_time, timeout_.global_timeout_));
  }

  if (config_->emit_dynamic_stats_ && !callbacks_->streamInfo().healthCheck() &&
      DateUtil::timePointValid(downstream_request_complete_time_)) {
    upstream_request.upstreamHost()->outlierDetector().putResponseTime(response_time);
    const bool internal_request = Http::HeaderUtility::isEnvoyInternalRequest(*downstream_headers_);

    Http::CodeStats& code_stats = httpContext().codeStats();
    Http::CodeStats::ResponseTimingInfo info{
        config_->scope_,
        cluster_->statsScope(),
        config_->empty_stat_name_,
        response_time,
        upstream_request.upstreamCanary(),
        internal_request,
        route_->virtualHost().statName(),
        request_vcluster_ ? request_vcluster_->statName() : config_->empty_stat_name_,
        route_stats_context_.has_value() ? route_stats_context_->statName()
                                         : config_->empty_stat_name_,
        config_->zone_name_,
        upstreamZone(upstream_request.upstreamHost())};

    code_stats.chargeResponseTiming(info);

    if (alt_stat_prefix_ != nullptr) {
      Http::CodeStats::ResponseTimingInfo info{config_->scope_,
                                               cluster_->statsScope(),
                                               alt_stat_prefix_->statName(),
                                               response_time,
                                               upstream_request.upstreamCanary(),
                                               internal_request,
                                               config_->empty_stat_name_,
                                               config_->empty_stat_name_,
                                               config_->empty_stat_name_,
                                               config_->zone_name_,
                                               upstreamZone(upstream_request.upstreamHost())};

      code_stats.chargeResponseTiming(info);
    }
  }

  // Defer deletion as this is generally called under the stack of the upstream
  // request, and immediate deletion is dangerous.
  callbacks_->dispatcher().deferredDelete(upstream_request.removeFromList(upstream_requests_));
  cleanup();
}

bool Filter::setupRedirect(const Http::ResponseHeaderMap& headers) {
  ENVOY_STREAM_LOG(debug, "attempting internal redirect", *callbacks_);
  const Http::HeaderEntry* location = headers.Location();

  const uint64_t status_code = Http::Utility::getResponseStatus(headers);

  // Redirects are not supported for streaming requests yet.
  if (downstream_end_stream_ && (!request_buffer_overflowed_ || !callbacks_->decodingBuffer()) &&
      location != nullptr &&
      convertRequestHeadersForInternalRedirect(*downstream_headers_, headers, *location,
                                               status_code) &&
      callbacks_->recreateStream(&headers)) {
    ENVOY_STREAM_LOG(debug, "Internal redirect succeeded", *callbacks_);
    cluster_->trafficStats()->upstream_internal_redirect_succeeded_total_.inc();
    return true;
  }
  // convertRequestHeadersForInternalRedirect logs failure reasons but log
  // details for other failure modes here.
  if (!downstream_end_stream_) {
    ENVOY_STREAM_LOG(trace, "Internal redirect failed: request incomplete", *callbacks_);
  } else if (request_buffer_overflowed_) {
    ENVOY_STREAM_LOG(trace, "Internal redirect failed: request body overflow", *callbacks_);
  } else if (location == nullptr) {
    ENVOY_STREAM_LOG(trace, "Internal redirect failed: missing location header", *callbacks_);
  }

  cluster_->trafficStats()->upstream_internal_redirect_failed_total_.inc();
  return false;
}

bool Filter::convertRequestHeadersForInternalRedirect(
    Http::RequestHeaderMap& downstream_headers, const Http::ResponseHeaderMap& upstream_headers,
    const Http::HeaderEntry& internal_redirect, uint64_t status_code) {
  if (!downstream_headers.Path()) {
    ENVOY_STREAM_LOG(trace, "Internal redirect failed: no path in downstream_headers", *callbacks_);
    return false;
  }

  absl::string_view redirect_url = internal_redirect.value().getStringView();
  // Make sure the redirect response contains a URL to redirect to.
  if (redirect_url.empty()) {
    stats_.passthrough_internal_redirect_bad_location_.inc();
    ENVOY_STREAM_LOG(trace, "Internal redirect failed: empty location", *callbacks_);
    return false;
  }
  Http::Utility::Url absolute_url;
  if (!absolute_url.initialize(redirect_url, false)) {
    stats_.passthrough_internal_redirect_bad_location_.inc();
    ENVOY_STREAM_LOG(trace, "Internal redirect failed: invalid location {}", *callbacks_,
                     redirect_url);
    return false;
  }

  const auto& policy = route_entry_->internalRedirectPolicy();
  // Don't change the scheme from the original request
  const bool scheme_is_http = schemeIsHttp(downstream_headers, callbacks_->connection());
  const bool target_is_http = Http::Utility::schemeIsHttp(absolute_url.scheme());
  if (!policy.isCrossSchemeRedirectAllowed() && scheme_is_http != target_is_http) {
    ENVOY_STREAM_LOG(trace, "Internal redirect failed: incorrect scheme for {}", *callbacks_,
                     redirect_url);
    stats_.passthrough_internal_redirect_unsafe_scheme_.inc();
    return false;
  }

  const StreamInfo::FilterStateSharedPtr& filter_state = callbacks_->streamInfo().filterState();
  // Make sure that performing the redirect won't result in exceeding the configured number of
  // redirects allowed for this route.
  StreamInfo::UInt32Accessor* num_internal_redirect{};

  if (num_internal_redirect = filter_state->getDataMutable<StreamInfo::UInt32Accessor>(
          NumInternalRedirectsFilterStateName);
      num_internal_redirect == nullptr) {
    auto state = std::make_shared<StreamInfo::UInt32AccessorImpl>(0);
    num_internal_redirect = state.get();

    filter_state->setData(NumInternalRedirectsFilterStateName, std::move(state),
                          StreamInfo::FilterState::StateType::Mutable,
                          StreamInfo::FilterState::LifeSpan::Request);
  }

  if (num_internal_redirect->value() >= policy.maxInternalRedirects()) {
    ENVOY_STREAM_LOG(trace, "Internal redirect failed: redirect limits exceeded.", *callbacks_);
    stats_.passthrough_internal_redirect_too_many_redirects_.inc();
    return false;
  }
  // Copy the old values, so they can be restored if the redirect fails.
  const bool scheme_is_set = (downstream_headers.Scheme() != nullptr);

  std::unique_ptr<Http::RequestHeaderMapImpl> saved_headers = Http::RequestHeaderMapImpl::create();
  Http::RequestHeaderMapImpl::copyFrom(*saved_headers, downstream_headers);

  for (const Http::LowerCaseString& header :
       route_entry_->internalRedirectPolicy().responseHeadersToCopy()) {
    Http::HeaderMap::GetResult result = upstream_headers.get(header);
    Http::HeaderMap::GetResult downstream_result = downstream_headers.get(header);
    if (result.empty()) {
      // Clear headers if present, else do nothing:
      if (downstream_result.empty()) {
        continue;
      }
      downstream_headers.remove(header);
    } else {
      // The header exists in the response, copy into the downstream headers
      if (!downstream_result.empty()) {
        downstream_headers.remove(header);
      }
      for (size_t idx = 0; idx < result.size(); idx++) {
        downstream_headers.addCopy(header, result[idx]->value().getStringView());
      }
    }
  }

  Cleanup restore_original_headers(
      [&downstream_headers, scheme_is_set, scheme_is_http, &saved_headers]() {
        downstream_headers.clear();
        if (scheme_is_set) {
          downstream_headers.setScheme(scheme_is_http ? Http::Headers::get().SchemeValues.Http
                                                      : Http::Headers::get().SchemeValues.Https);
        }

        Http::RequestHeaderMapImpl::copyFrom(downstream_headers, *saved_headers);
      });

  // Replace the original host, scheme and path.
  downstream_headers.setScheme(absolute_url.scheme());
  downstream_headers.setHost(absolute_url.hostAndPort());

  auto path_and_query = absolute_url.pathAndQueryParams();
  if (Runtime::runtimeFeatureEnabled("envoy.reloadable_features.http_reject_path_with_fragment")) {
    // Envoy treats internal redirect as a new request and will reject it if URI path
    // contains #fragment. However the Location header is allowed to have #fragment in URI path. To
    // prevent Envoy from rejecting internal redirect, strip the #fragment from Location URI if it
    // is present.
    auto fragment_pos = path_and_query.find('#');
    path_and_query = path_and_query.substr(0, fragment_pos);
  }
  downstream_headers.setPath(path_and_query);

  // Only clear the route cache if there are downstream callbacks. There aren't, for example,
  // for async connections.
  if (callbacks_->downstreamCallbacks()) {
    callbacks_->downstreamCallbacks()->clearRouteCache();
  }
  const auto route = callbacks_->route();
  // Don't allow a redirect to a non existing route.
  if (!route) {
    stats_.passthrough_internal_redirect_no_route_.inc();
    ENVOY_STREAM_LOG(trace, "Internal redirect failed: no route found", *callbacks_);
    return false;
  }

  const auto& route_name = route->routeName();
  for (const auto& predicate : policy.predicates()) {
    if (!predicate->acceptTargetRoute(*filter_state, route_name, !scheme_is_http,
                                      !target_is_http)) {
      stats_.passthrough_internal_redirect_predicate_.inc();
      ENVOY_STREAM_LOG(trace,
                       "Internal redirect failed: rejecting redirect targeting {}, by {} predicate",
                       *callbacks_, route_name, predicate->name());
      return false;
    }
  }

  // See https://tools.ietf.org/html/rfc7231#section-6.4.4.
  if (status_code == enumToInt(Http::Code::SeeOther) &&
      downstream_headers.getMethodValue() != Http::Headers::get().MethodValues.Get &&
      downstream_headers.getMethodValue() != Http::Headers::get().MethodValues.Head) {
    downstream_headers.setMethod(Http::Headers::get().MethodValues.Get);
    downstream_headers.remove(Http::Headers::get().ContentLength);
    callbacks_->modifyDecodingBuffer([](Buffer::Instance& data) { data.drain(data.length()); });
  }

  num_internal_redirect->increment();
  restore_original_headers.cancel();
  // Preserve the original request URL for the second pass.
  downstream_headers.setEnvoyOriginalUrl(
      absl::StrCat(scheme_is_http ? Http::Headers::get().SchemeValues.Http
                                  : Http::Headers::get().SchemeValues.Https,
                   "://", saved_headers->getHostValue(), saved_headers->getPathValue()));
  return true;
}

void Filter::runRetryOptionsPredicates(UpstreamRequest& retriable_request) {
  for (const auto& options_predicate : route_entry_->retryPolicy().retryOptionsPredicates()) {
    const Upstream::RetryOptionsPredicate::UpdateOptionsParameters parameters{
        retriable_request.streamInfo(), upstreamSocketOptions()};
    auto ret = options_predicate->updateOptions(parameters);
    if (ret.new_upstream_socket_options_.has_value()) {
      upstream_options_ = ret.new_upstream_socket_options_.value();
    }
  }
}

void Filter::doRetry(bool can_send_early_data, bool can_use_http3, TimeoutRetry is_timeout_retry) {
  ENVOY_STREAM_LOG(debug, "performing retry", *callbacks_);

  is_retry_ = true;
  attempt_count_++;
  callbacks_->streamInfo().setAttemptCount(attempt_count_);
  ASSERT(pending_retries_ > 0);
  pending_retries_--;

  // Clusters can technically get removed by CDS during a retry. Make sure it still exists.
  const auto cluster = config_->cm_.getThreadLocalCluster(route_entry_->clusterName());
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
  UpstreamRequestPtr upstream_request = std::make_unique<UpstreamRequest>(
      *this, std::move(generic_conn_pool), can_send_early_data, can_use_http3,
      allow_multiplexed_upstream_half_close_ /*enable_half_close*/);

  if (include_attempt_count_in_request_) {
    downstream_headers_->setEnvoyAttemptCount(attempt_count_);
  }

  if (include_timeout_retry_header_in_request_) {
    downstream_headers_->setEnvoyIsTimeoutRetry(is_timeout_retry == TimeoutRetry::Yes ? "true"
                                                                                      : "false");
  }

  // The request timeouts only account for time elapsed since the downstream request completed
  // which might not have happened yet (in which case zero time has elapsed.)
  std::chrono::milliseconds elapsed_time = std::chrono::milliseconds::zero();

  if (DateUtil::timePointValid(downstream_request_complete_time_)) {
    Event::Dispatcher& dispatcher = callbacks_->dispatcher();
    elapsed_time = std::chrono::duration_cast<std::chrono::milliseconds>(
        dispatcher.timeSource().monotonicTime() - downstream_request_complete_time_);
  }

  FilterUtility::setTimeoutHeaders(elapsed_time.count(), timeout_, *route_entry_,
                                   *downstream_headers_, !config_->suppress_envoy_headers_,
                                   grpc_request_, hedging_params_.hedge_on_per_try_timeout_);

  UpstreamRequest* upstream_request_tmp = upstream_request.get();
  LinkedList::moveIntoList(std::move(upstream_request), upstream_requests_);
  upstream_requests_.front()->acceptHeadersFromRouter(
      !callbacks_->decodingBuffer() && !downstream_trailers_ && downstream_end_stream_);
  // It's possible we got immediately reset which means the upstream request we just
  // added to the front of the list might have been removed, so we need to check to make
  // sure we don't send data on the wrong request.
  if (!upstream_requests_.empty() && (upstream_requests_.front().get() == upstream_request_tmp)) {
    if (callbacks_->decodingBuffer()) {
      // If we are doing a retry we need to make a copy.
      Buffer::OwnedImpl copy(*callbacks_->decodingBuffer());
      upstream_requests_.front()->acceptDataFromRouter(copy, !downstream_trailers_ &&
                                                                 downstream_end_stream_);
    }

    if (downstream_trailers_) {
      upstream_requests_.front()->acceptTrailersFromRouter(*downstream_trailers_);
    }
  }
}

uint32_t Filter::numRequestsAwaitingHeaders() {
  return std::count_if(upstream_requests_.begin(), upstream_requests_.end(),
                       [](const auto& req) -> bool { return req->awaitingHeaders(); });
}

bool Filter::checkDropOverload(Upstream::ThreadLocalCluster& cluster,
                               std::function<void(Http::ResponseHeaderMap&)>& modify_headers) {
  if (cluster.dropOverload().value()) {
    ENVOY_STREAM_LOG(debug, "Router filter: cluster DROP_OVERLOAD configuration: {}", *callbacks_,
                     cluster.dropOverload().value());
    if (config_->random_.bernoulli(cluster.dropOverload())) {
      ENVOY_STREAM_LOG(debug, "The request is dropped by DROP_OVERLOAD", *callbacks_);
      callbacks_->streamInfo().setResponseFlag(StreamInfo::CoreResponseFlag::DropOverLoad);
      chargeUpstreamCode(Http::Code::ServiceUnavailable, nullptr, true);
      callbacks_->sendLocalReply(
          Http::Code::ServiceUnavailable, "drop overload",
          [modify_headers, this](Http::ResponseHeaderMap& headers) {
            if (!config_->suppress_envoy_headers_) {
              headers.addReference(Http::Headers::get().EnvoyDropOverload,
                                   Http::Headers::get().EnvoyDropOverloadValues.True);
            }
            modify_headers(headers);
          },
          absl::nullopt, StreamInfo::ResponseCodeDetails::get().DropOverload);

      cluster.info()->loadReportStats().upstream_rq_drop_overload_.inc();
      return true;
    }
  }
  return false;
}

void Filter::maybeProcessOrcaLoadReport(const Envoy::Http::HeaderMap& headers_or_trailers,
                                        UpstreamRequest& upstream_request) {
  // Process the load report only once, so if response has report in headers,
  // then don't process it in trailers.
  if (orca_load_report_received_) {
    return;
  }
  // Check whether we need to send the load report to the LRS or invoke the ORCA
  // callbacks.
  auto host = upstream_request.upstreamHost();
  const bool need_to_send_load_report =
      (host != nullptr) && cluster_->lrsReportMetricNames().has_value();
  if (!need_to_send_load_report && orca_load_report_callbacks_.expired()) {
    return;
  }

  absl::StatusOr<xds::data::orca::v3::OrcaLoadReport> orca_load_report =
      Envoy::Orca::parseOrcaLoadReportHeaders(headers_or_trailers);
  if (!orca_load_report.ok()) {
    ENVOY_STREAM_LOG(trace, "Headers don't have orca load report: {}", *callbacks_,
                     orca_load_report.status().message());
    return;
  }

  orca_load_report_received_ = true;

  if (need_to_send_load_report) {
    ENVOY_STREAM_LOG(trace, "Adding ORCA load report {} to load metrics", *callbacks_,
                     orca_load_report->DebugString());
    Envoy::Orca::addOrcaLoadReportToLoadMetricStats(cluster_->lrsReportMetricNames().value(),
                                                    orca_load_report.value(),
                                                    host->loadMetricStats());
  }
  if (auto callbacks = orca_load_report_callbacks_.lock(); callbacks != nullptr) {
    const absl::Status status = callbacks->onOrcaLoadReport(*orca_load_report, *host);
    if (!status.ok()) {
      ENVOY_STREAM_LOG(error, "Failed to invoke OrcaLoadReportCallbacks: {}", *callbacks_,
                       status.message());
    }
  }
}

RetryStatePtr
ProdFilter::createRetryState(const RetryPolicy& policy, Http::RequestHeaderMap& request_headers,
                             const Upstream::ClusterInfo& cluster, const VirtualCluster* vcluster,
                             RouteStatsContextOptRef route_stats_context,
                             Server::Configuration::CommonFactoryContext& context,
                             Event::Dispatcher& dispatcher, Upstream::ResourcePriority priority) {
  std::unique_ptr<RetryStateImpl> retry_state =
      RetryStateImpl::create(policy, request_headers, cluster, vcluster, route_stats_context,
                             context, dispatcher, priority);
  if (retry_state != nullptr && retry_state->isAutomaticallyConfiguredForHttp3()) {
    // Since doing retry will make Envoy to buffer the request body, if upstream using HTTP/3 is the
    // only reason for doing retry, set the retry shadow buffer limit to 0 so that we don't retry or
    // buffer safe requests with body which is not common.
    setRetryShadowBufferLimit(0);
  }
  return retry_state;
}

} // namespace Router
} // namespace Envoy
