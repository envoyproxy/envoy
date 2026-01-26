#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "envoy/extensions/filters/http/ext_authz/v3/ext_authz.pb.h"
#include "envoy/grpc/async_client_manager.h"
#include "envoy/http/filter.h"
#include "envoy/runtime/runtime.h"
#include "envoy/server/factory_context.h"
#include "envoy/service/auth/v3/external_auth.pb.h"
#include "envoy/stats/scope.h"
#include "envoy/stats/stats_macros.h"
#include "envoy/upstream/cluster_manager.h"

#include "source/common/common/assert.h"
#include "source/common/common/logger.h"
#include "source/common/common/matchers.h"
#include "source/common/common/utility.h"
#include "source/common/grpc/typed_async_client.h"
#include "source/common/http/codes.h"
#include "source/common/http/header_map_impl.h"
#include "source/common/runtime/runtime_protos.h"
#include "source/extensions/filters/common/ext_authz/check_request_utils.h"
#include "source/extensions/filters/common/ext_authz/ext_authz.h"
#include "source/extensions/filters/common/ext_authz/ext_authz_grpc_impl.h"
#include "source/extensions/filters/common/ext_authz/ext_authz_http_impl.h"
#include "source/extensions/filters/common/mutation_rules/mutation_rules.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ExtAuthz {

/**
 * All stats for the Ext Authz filter. @see stats_macros.h
 */

#define ALL_EXT_AUTHZ_FILTER_STATS(COUNTER)                                                        \
  COUNTER(ok)                                                                                      \
  COUNTER(denied)                                                                                  \
  COUNTER(error)                                                                                   \
  COUNTER(disabled)                                                                                \
  COUNTER(failure_mode_allowed)                                                                    \
  COUNTER(invalid)                                                                                 \
  COUNTER(ignored_dynamic_metadata)                                                                \
  COUNTER(filter_state_name_collision)                                                             \
  COUNTER(omitted_response_headers)                                                                \
  COUNTER(request_header_limits_reached)                                                           \
  COUNTER(response_header_limits_reached)

/**
 * Wrapper struct for ext_authz filter stats. @see stats_macros.h
 */
struct ExtAuthzFilterStats {
  ALL_EXT_AUTHZ_FILTER_STATS(GENERATE_COUNTER_STRUCT)
};

class ExtAuthzLoggingInfo : public Envoy::StreamInfo::FilterState::Object {
public:
  explicit ExtAuthzLoggingInfo(const absl::optional<Envoy::Protobuf::Struct> filter_metadata)
      : filter_metadata_(filter_metadata) {}

  const absl::optional<Protobuf::Struct>& filterMetadata() const { return filter_metadata_; }
  absl::optional<std::chrono::microseconds> latency() const { return latency_; };
  absl::optional<uint64_t> bytesSent() const { return bytes_sent_; }
  absl::optional<uint64_t> bytesReceived() const { return bytes_received_; }
  Upstream::ClusterInfoConstSharedPtr clusterInfo() const { return cluster_info_; }
  Upstream::HostDescriptionConstSharedPtr upstreamHost() const { return upstream_host_; }
  // Gets the gRPC status returned by the authorization server when it is making a gRPC call.
  const absl::optional<Grpc::Status::GrpcStatus>& grpcStatus() const { return grpc_status_; }

  void setLatency(std::chrono::microseconds ms) { latency_ = ms; };
  void setBytesSent(uint64_t bytes_sent) { bytes_sent_ = bytes_sent; }
  void setBytesReceived(uint64_t bytes_received) { bytes_received_ = bytes_received; }
  void setClusterInfo(Upstream::ClusterInfoConstSharedPtr cluster_info) {
    cluster_info_ = std::move(cluster_info);
  }
  void setUpstreamHost(Upstream::HostDescriptionConstSharedPtr upstream_host) {
    upstream_host_ = std::move(upstream_host);
  }
  // Sets the gRPC status returned by the authorization server when it is making a gRPC call.
  void setGrpcStatus(const Grpc::Status::GrpcStatus& grpc_status) { grpc_status_ = grpc_status; }

  bool hasFieldSupport() const override { return true; }
  Envoy::StreamInfo::FilterState::Object::FieldType
  getField(absl::string_view field_name) const override {
    if (field_name == "latency_us" && latency_.has_value()) {
      return int64_t(latency_.value().count());
    } else if (field_name == "bytesSent" && bytes_sent_.has_value()) {
      return int64_t(bytes_sent_.value());
    } else if (field_name == "bytesReceived" && bytes_received_.has_value()) {
      return int64_t(bytes_received_.value());
    }
    return {};
  }

  // For convenience in testing.
  void clearLatency() { latency_ = absl::nullopt; };
  void clearBytesSent() { bytes_sent_ = absl::nullopt; }
  void clearBytesReceived() { bytes_received_ = absl::nullopt; }
  void clearClusterInfo() { cluster_info_ = nullptr; }
  void clearUpstreamHost() { upstream_host_ = nullptr; }

private:
  const absl::optional<Envoy::Protobuf::Struct> filter_metadata_;
  absl::optional<std::chrono::microseconds> latency_;
  // The following stats are populated for ext_authz filters using Envoy gRPC only.
  absl::optional<uint64_t> bytes_sent_;
  absl::optional<uint64_t> bytes_received_;
  Upstream::ClusterInfoConstSharedPtr cluster_info_;
  Upstream::HostDescriptionConstSharedPtr upstream_host_;
  // The gRPC status returned by the authorization server when it is making a gRPC call.
  absl::optional<Grpc::Status::GrpcStatus> grpc_status_;
};

/**
 * Configuration for the External Authorization (ext_authz) filter.
 */
class FilterConfig {
  using LabelsMap = Protobuf::Map<std::string, std::string>;

public:
  FilterConfig(const envoy::extensions::filters::http::ext_authz::v3::ExtAuthz& config,
               Stats::Scope& scope, const std::string& stats_prefix,
               Server::Configuration::ServerFactoryContext& factory_context);

  bool allowPartialMessage() const { return allow_partial_message_; }

  bool withRequestBody() const { return max_request_bytes_ > 0; }

  bool failureModeAllow() const { return failure_mode_allow_; }

  bool failureModeAllowHeaderAdd() const { return failure_mode_allow_header_add_; }

  bool clearRouteCache() const { return clear_route_cache_; }

  uint32_t maxRequestBytes() const { return max_request_bytes_; }

  uint32_t maxDeniedResponseBodyBytes() const { return max_denied_response_body_bytes_; }

  bool packAsBytes() const { return pack_as_bytes_; }

  bool headersAsBytes() const { return encode_raw_headers_; }

  Filters::Common::MutationRules::CheckResult
  checkDecoderHeaderMutation(const Filters::Common::MutationRules::CheckOperation& operation,
                             const Http::LowerCaseString& key, absl::string_view value) const {
    if (!decoder_header_mutation_checker_.has_value()) {
      return Filters::Common::MutationRules::CheckResult::OK;
    }
    return decoder_header_mutation_checker_->check(operation, key, value);
  }

  // Used for headers_to_remove to avoid a redundant pseudo header check.
  bool hasDecoderHeaderMutationRules() const {
    return decoder_header_mutation_checker_.has_value();
  }

  bool enableDynamicMetadataIngestion() const { return enable_dynamic_metadata_ingestion_; }

  Http::Code statusOnError() const { return status_on_error_; }

  bool validateMutations() const { return validate_mutations_; }

  bool filterEnabled(const envoy::config::core::v3::Metadata& metadata) {
    const bool enabled = filter_enabled_.has_value() ? filter_enabled_->enabled() : true;
    const bool enabled_metadata =
        filter_enabled_metadata_.has_value() ? filter_enabled_metadata_->match(metadata) : true;
    return enabled && enabled_metadata;
  }

  bool denyAtDisable() {
    return deny_at_disable_.has_value() ? deny_at_disable_->enabled() : false;
  }

  Stats::Scope& scope() { return scope_; }

  Http::Context& httpContext() { return http_context_; }

  const std::vector<std::string>& metadataContextNamespaces() {
    return metadata_context_namespaces_;
  }

  const std::vector<std::string>& typedMetadataContextNamespaces() {
    return typed_metadata_context_namespaces_;
  }

  const std::vector<std::string>& routeMetadataContextNamespaces() {
    return route_metadata_context_namespaces_;
  }

  const std::vector<std::string>& routeTypedMetadataContextNamespaces() {
    return route_typed_metadata_context_namespaces_;
  }

  const ExtAuthzFilterStats& stats() const { return stats_; }

  void incCounter(Stats::Scope& scope, Stats::StatName name) {
    scope.counterFromStatName(name).inc();
  }

  bool includePeerCertificate() const { return include_peer_certificate_; }
  bool includeTLSSession() const { return include_tls_session_; }
  const LabelsMap& destinationLabels() const { return destination_labels_; }

  const absl::optional<Protobuf::Struct>& filterMetadata() const { return filter_metadata_; }

  bool emitFilterStateStats() const { return emit_filter_state_stats_; }

  bool enforceResponseHeaderLimits() const { return enforce_response_header_limits_; }

  bool chargeClusterResponseStats() const { return charge_cluster_response_stats_; }

  const Filters::Common::ExtAuthz::MatcherSharedPtr& allowedHeadersMatcher() const {
    return allowed_headers_matcher_;
  }

  const Filters::Common::ExtAuthz::MatcherSharedPtr& disallowedHeadersMatcher() const {
    return disallowed_headers_matcher_;
  }

private:
  static Http::Code toErrorCode(uint64_t status) {
    const auto code = static_cast<Http::Code>(status);
    if (code >= Http::Code::Continue && code <= Http::Code::NetworkAuthenticationRequired) {
      return code;
    }
    return Http::Code::Forbidden;
  }

  ExtAuthzFilterStats generateStats(const std::string& prefix,
                                    const std::string& filter_stats_prefix, Stats::Scope& scope) {
    const std::string final_prefix = absl::StrCat(prefix, "ext_authz.", filter_stats_prefix);
    return {ALL_EXT_AUTHZ_FILTER_STATS(POOL_COUNTER_PREFIX(scope, final_prefix))};
  }

  // This generates ext_authz.<optional filter_stats_prefix>.name, for example: ext_authz.waf.ok
  // when filter_stats_prefix is "waf", and ext_authz.ok when filter_stats_prefix is empty.
  const std::string createPoolStatName(const std::string& filter_stats_prefix,
                                       const std::string& name) {
    return absl::StrCat("ext_authz",
                        filter_stats_prefix.empty() ? EMPTY_STRING
                                                    : absl::StrCat(".", filter_stats_prefix),
                        ".", name);
  }

  const bool allow_partial_message_;
  const bool failure_mode_allow_;
  const bool failure_mode_allow_header_add_;
  const bool clear_route_cache_;
  const uint32_t max_request_bytes_;
  const uint32_t max_denied_response_body_bytes_;
  const bool pack_as_bytes_;
  const bool encode_raw_headers_;
  const Http::Code status_on_error_;
  const bool validate_mutations_;
  Stats::Scope& scope_;
  const absl::optional<Filters::Common::MutationRules::Checker> decoder_header_mutation_checker_;
  const bool enable_dynamic_metadata_ingestion_;
  Runtime::Loader& runtime_;
  Http::Context& http_context_;
  LabelsMap destination_labels_;
  const absl::optional<Protobuf::Struct> filter_metadata_;
  const bool emit_filter_state_stats_;
  const bool enforce_response_header_limits_;

  const absl::optional<Runtime::FractionalPercent> filter_enabled_;
  const absl::optional<Matchers::MetadataMatcher> filter_enabled_metadata_;
  const absl::optional<Runtime::FeatureFlag> deny_at_disable_;

  // TODO(nezdolik): stop using pool as part of deprecating cluster scope stats.
  Stats::StatNamePool pool_;

  const std::vector<std::string> metadata_context_namespaces_;
  const std::vector<std::string> typed_metadata_context_namespaces_;
  const std::vector<std::string> route_metadata_context_namespaces_;
  const std::vector<std::string> route_typed_metadata_context_namespaces_;

  const bool include_peer_certificate_;
  const bool include_tls_session_;
  const bool charge_cluster_response_stats_;

  // The stats for the filter.
  ExtAuthzFilterStats stats_;

  Filters::Common::ExtAuthz::MatcherSharedPtr allowed_headers_matcher_;
  Filters::Common::ExtAuthz::MatcherSharedPtr disallowed_headers_matcher_;

public:
  // TODO(nezdolik): deprecate cluster scope stats counters in favor of filter scope stats
  // (ExtAuthzFilterStats stats_).
  const Stats::StatName ext_authz_ok_;
  const Stats::StatName ext_authz_denied_;
  const Stats::StatName ext_authz_error_;
  const Stats::StatName ext_authz_invalid_;
  const Stats::StatName ext_authz_failure_mode_allowed_;
};

using FilterConfigSharedPtr = std::shared_ptr<FilterConfig>;

/**
 * Per route settings for ExtAuth. Allows customizing the CheckRequest on a
 * virtualhost\route\weighted cluster level.
 */
class FilterConfigPerRoute : public Router::RouteSpecificFilterConfig {
public:
  using ContextExtensionsMap = Protobuf::Map<std::string, std::string>;

  FilterConfigPerRoute(
      const envoy::extensions::filters::http::ext_authz::v3::ExtAuthzPerRoute& config)
      : context_extensions_(config.has_check_settings()
                                ? config.check_settings().context_extensions()
                                : ContextExtensionsMap()),
        check_settings_(config.has_check_settings()
                            ? config.check_settings()
                            : envoy::extensions::filters::http::ext_authz::v3::CheckSettings()),
        disabled_(config.disabled()),
        grpc_service_(config.has_check_settings() && config.check_settings().has_grpc_service()
                          ? absl::make_optional(config.check_settings().grpc_service())
                          : absl::nullopt),
        http_service_(config.has_check_settings() && config.check_settings().has_http_service()
                          ? absl::make_optional(config.check_settings().http_service())
                          : absl::nullopt) {
    if (config.has_check_settings() && config.check_settings().disable_request_body_buffering() &&
        config.check_settings().has_with_request_body()) {
      ExceptionUtil::throwEnvoyException(
          "Invalid configuration for check_settings. Only one of disable_request_body_buffering or "
          "with_request_body can be set.");
    }
  }

  // This constructor is used as a way to merge more-specific config into less-specific config in a
  // clearly defined way (e.g. route config into VH config). All fields on this class must be const
  // and thus must be initialized in the constructor initialization list.
  FilterConfigPerRoute(const FilterConfigPerRoute& less_specific,
                       const FilterConfigPerRoute& more_specific);

  void merge(const FilterConfigPerRoute& other);

  /**
   * @return Context extensions to add to the CheckRequest.
   */
  const ContextExtensionsMap& contextExtensions() const { return context_extensions_; }
  // Allow moving the context extensions out of this object.
  ContextExtensionsMap&& takeContextExtensions() { return std::move(context_extensions_); }

  bool disabled() const { return disabled_; }

  const envoy::extensions::filters::http::ext_authz::v3::CheckSettings& checkSettings() const {
    return check_settings_;
  }

  /**
   * @return The gRPC service override for this route, if any.
   */
  const absl::optional<const envoy::config::core::v3::GrpcService>& grpcService() const {
    return grpc_service_;
  }

  /**
   * @return The HTTP service override for this route, if any.
   */
  const absl::optional<const envoy::extensions::filters::http::ext_authz::v3::HttpService>&
  httpService() const {
    return http_service_;
  }

private:
  // We save the context extensions as a protobuf map instead of a std::map as this allows us to
  // move it to the CheckRequest, thus avoiding a copy that would incur by converting it.
  ContextExtensionsMap context_extensions_;
  envoy::extensions::filters::http::ext_authz::v3::CheckSettings check_settings_;
  const bool disabled_;
  const absl::optional<const envoy::config::core::v3::GrpcService> grpc_service_;
  const absl::optional<const envoy::extensions::filters::http::ext_authz::v3::HttpService>
      http_service_;
};

/**
 * HTTP ext_authz filter. Depending on the route configuration, this filter calls the global
 * ext_authz service before allowing further filter iteration.
 */
class Filter : public Logger::Loggable<Logger::Id::ext_authz>,
               public Http::StreamFilter,
               public Filters::Common::ExtAuthz::RequestCallbacks {
public:
  Filter(const FilterConfigSharedPtr& config, Filters::Common::ExtAuthz::ClientPtr&& client)
      : config_(config), client_(std::move(client)), stats_(config->stats()) {}

  // Constructor that includes server context for per-route service support.
  Filter(const FilterConfigSharedPtr& config, Filters::Common::ExtAuthz::ClientPtr&& client,
         Server::Configuration::ServerFactoryContext& server_context)
      : config_(config), client_(std::move(client)), server_context_(&server_context),
        stats_(config->stats()) {}

  // Http::StreamFilterBase
  void onDestroy() override;

  // Http::StreamDecoderFilter
  Http::FilterHeadersStatus decodeHeaders(Http::RequestHeaderMap& headers,
                                          bool end_stream) override;
  Http::FilterDataStatus decodeData(Buffer::Instance& data, bool end_stream) override;
  Http::FilterTrailersStatus decodeTrailers(Http::RequestTrailerMap& trailers) override;
  void setDecoderFilterCallbacks(Http::StreamDecoderFilterCallbacks& callbacks) override;

  // Http::StreamEncoderFilter
  Http::Filter1xxHeadersStatus encode1xxHeaders(Http::ResponseHeaderMap& headers) override;
  Http::FilterHeadersStatus encodeHeaders(Http::ResponseHeaderMap& headers,
                                          bool end_stream) override;
  Http::FilterDataStatus encodeData(Buffer::Instance& data, bool end_stream) override;
  Http::FilterTrailersStatus encodeTrailers(Http::ResponseTrailerMap& trailers) override;
  Http::FilterMetadataStatus encodeMetadata(Http::MetadataMap& trailers) override;
  void setEncoderFilterCallbacks(Http::StreamEncoderFilterCallbacks& callbacks) override;

  // ExtAuthz::RequestCallbacks
  void onComplete(Filters::Common::ExtAuthz::ResponsePtr&&) override;

private:
  // Convenience function for the following:
  // 1. If `validate_mutations` is set to true, validate header key and value.
  // 2. If `decoder_header_mutation_rules` is set, check that mutation is allowed.
  Filters::Common::MutationRules::CheckResult
  validateAndCheckDecoderHeaderMutation(Filters::Common::MutationRules::CheckOperation operation,
                                        absl::string_view key, absl::string_view value) const;

  void responseHeaderLimitsReached();

  // Called when the filter is configured to reject invalid responses & the authz response contains
  // invalid header or query parameters. Sends a local response with the configured rejection status
  // code.
  void rejectResponse();

  // Validates error response headers and clears custom attributes if invalid headers are found.
  // Returns true if headers are valid or validation is disabled, false if headers are invalid.
  bool
  validateAndClearInvalidErrorResponseAttributes(Filters::Common::ExtAuthz::ResponsePtr& response);

  // Create a new gRPC client for per-route gRPC service configuration.
  Filters::Common::ExtAuthz::ClientPtr
  createPerRouteGrpcClient(const envoy::config::core::v3::GrpcService& grpc_service);

  // Create a new HTTP client for per-route HTTP service configuration.
  Filters::Common::ExtAuthz::ClientPtr createPerRouteHttpClient(
      const envoy::extensions::filters::http::ext_authz::v3::HttpService& http_service);

  absl::optional<MonotonicTime> start_time_;
  void addResponseHeaders(Http::HeaderMap& header_map, const Http::HeaderVector& headers);
  void initiateCall(const Http::RequestHeaderMap& headers);
  void continueDecoding();
  bool isBufferFull(uint64_t num_bytes_processing) const;
  void updateLoggingInfo(const absl::optional<Grpc::Status::GrpcStatus>& grpc_status);

  // This holds a set of flags defined in per-route configuration.
  struct PerRouteFlags {
    const bool skip_check_;
    const envoy::extensions::filters::http::ext_authz::v3::CheckSettings& check_settings_;
  };
  PerRouteFlags getPerRouteFlags(const Router::RouteConstSharedPtr& route) const;

  // State of this filter's communication with the external authorization service.
  // The filter has either not started calling the external service, in the middle of calling
  // it or has completed.
  enum class State { NotStarted, Calling, Complete };

  // FilterReturn is used to capture what the return code should be to the filter chain.
  // if this filter is either in the middle of calling the service or the result is denied then
  // the filter chain should stop. Otherwise, the filter chain can continue to the next filter.
  enum class FilterReturn { ContinueDecoding, StopDecoding };

  Http::HeaderMapPtr getHeaderMap(const Filters::Common::ExtAuthz::ResponsePtr& response);
  FilterConfigSharedPtr config_;
  Filters::Common::ExtAuthz::ClientPtr client_;
  // Server context for creating per-route clients.
  Server::Configuration::ServerFactoryContext* server_context_{nullptr};
  Http::StreamDecoderFilterCallbacks* decoder_callbacks_{};
  Http::StreamEncoderFilterCallbacks* encoder_callbacks_{};
  Http::RequestHeaderMap* request_headers_;
  // Ordered list of response header mutations to apply during encoding.
  Filters::Common::ExtAuthz::HeaderMutationVector response_header_mutations_{};
  State state_{State::NotStarted};
  FilterReturn filter_return_{FilterReturn::ContinueDecoding};
  Upstream::ClusterInfoConstSharedPtr cluster_;
  // The stats for the filter.
  ExtAuthzFilterStats stats_;
  ExtAuthzLoggingInfo* logging_info_{nullptr};

  // This is used to hold the final configs after we merge them with per-route configs.
  bool allow_partial_message_{};
  uint32_t max_request_bytes_{};

  // Used to identify if the callback to onComplete() is synchronous (on the stack) or asynchronous.
  bool initiating_call_{};
  bool buffer_data_{};
  bool skip_check_{false};
  envoy::service::auth::v3::CheckRequest check_request_{};
};

} // namespace ExtAuthz
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
