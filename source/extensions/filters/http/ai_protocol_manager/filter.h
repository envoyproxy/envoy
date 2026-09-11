#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <utility>

#include "envoy/extensions/filters/http/ai_protocol_manager/v3/ai_protocol_manager.pb.h"
#include "envoy/router/router.h"
#include "envoy/server/factory_context.h"
#include "envoy/stats/scope.h"

#include "source/common/common/logger.h"
#include "source/extensions/filters/http/ai_protocol_manager/ai_filter.h"
#include "source/extensions/filters/http/ai_protocol_manager/api_protocol_conversion.h"
#include "source/extensions/filters/http/ai_protocol_manager/buffer_manager.h"
#include "source/extensions/filters/http/ai_protocol_manager/external_buffer.h"
#include "source/extensions/filters/http/ai_protocol_manager/filter_manager.h"
#include "source/extensions/filters/http/ai_protocol_manager/json_with_ext_buf.h"
#include "source/extensions/filters/http/ai_protocol_manager/json_with_ext_buf_parser.h"
#include "source/extensions/filters/http/ai_protocol_manager/response_handler.h"
#include "source/extensions/filters/http/ai_protocol_manager/stats.h"
#include "source/extensions/filters/http/common/pass_through_filter.h"

#include "absl/status/status.h"
#include "absl/status/statusor.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AiProtocolManager {

using PerRouteProto =
    envoy::extensions::filters::http::ai_protocol_manager::v3::AiProtocolManagerPerRoute;

class FilterConfig;
using FilterConfigSharedPtr = std::shared_ptr<const FilterConfig>;

class FilterConfig {
public:
  FilterConfig(
      const envoy::extensions::filters::http::ai_protocol_manager::v3::AiProtocolManager& proto,
      Stats::Scope& scope, AiFilterFactories&& ai_filter_factories = {});

  static absl::StatusOr<FilterConfigSharedPtr>
  create(const envoy::extensions::filters::http::ai_protocol_manager::v3::AiProtocolManager& proto,
         Server::Configuration::ServerFactoryContext& context, Stats::Scope& scope);

  bool requestHandlingEnabled() const { return request_handling_enabled_; }
  const AiFilterFactories& aiFilterFactories() const { return ai_filter_factories_; }
  bool parseUnconfiguredRoutes() const { return parse_unconfigured_routes_; }
  uint32_t inlineStringThresholdBytes() const { return inline_string_threshold_bytes_; }
  bool tokenUsageEnabled() const { return token_usage_enabled_; }
  bool includeUnconfiguredRoutes() const { return include_unconfigured_routes_; }
  ApiProtocol defaultApiProtocol() const { return default_api_protocol_; }
  const std::string& metadataNamespace() const { return metadata_namespace_; }
  uint32_t maxSseEventSize() const { return max_sse_event_size_; }
  uint32_t maxJsonBodySize() const { return max_json_body_size_; }
  uint32_t maxParsedSseEvents() const { return max_parsed_sse_events_; }
  AiProtocolManagerStats& stats() const { return stats_; }

private:
  // Mutable so the shared const config can increment its thread-safe counters.
  mutable AiProtocolManagerStats stats_;
  const bool request_handling_enabled_ = false;
  const bool parse_unconfigured_routes_ = false;
  const uint32_t inline_string_threshold_bytes_ = 0;
  const bool token_usage_enabled_ = false;
  const bool include_unconfigured_routes_ = false;
  const ApiProtocol default_api_protocol_ = ApiProtocol::Unspecified;
  const std::string metadata_namespace_;
  const uint32_t max_sse_event_size_ = 0;
  const uint32_t max_json_body_size_ = 0;
  const uint32_t max_parsed_sse_events_ = 0;
  const AiFilterFactories ai_filter_factories_;
};

// Request and response wire APIs are separate; protocol translation can make them differ.
class RouteConfig : public Router::RouteSpecificFilterConfig {
public:
  explicit RouteConfig(const PerRouteProto& proto)
      : has_request_(proto.has_request()),
        request_protocol_(protocolFromProto(proto.request().api_protocol())),
        response_protocol_(protocolFromProto(proto.response().api_protocol())) {}

  // Whether the filter holds and validates this route's request payload.
  bool hasRequest() const { return has_request_; }
  ApiProtocol requestProtocol() const { return request_protocol_; }
  ApiProtocol responseProtocol() const { return response_protocol_; }

  ApiProtocol effectiveResponseProtocol() const {
    return response_protocol_ != ApiProtocol::Unspecified ? response_protocol_ : request_protocol_;
  }

private:
  const bool has_request_ = false;
  const ApiProtocol request_protocol_ = ApiProtocol::Unspecified;
  const ApiProtocol response_protocol_ = ApiProtocol::Unspecified;
};

// AI Protocol Manager HTTP filter (alpha). Holds a request, offloads its body to an
// ExternalBuffer while parsing it, then replays it once validated and run through the AI
// filters. The encode path never stops iteration or mutates the response; it only publishes
// token usage as dynamic metadata.
// Each frame reaches the parser before the BufferManager, so parser offsets are buffer offsets.
class AiProtocolManagerFilter : public Http::PassThroughFilter,
                                public Logger::Loggable<Logger::Id::filter> {
public:
  AiProtocolManagerFilter(ExternalBufferFactory& buffer_factory, FilterConfigSharedPtr config)
      : buffer_factory_(buffer_factory), config_(std::move(config)) {}

  // Http::StreamFilterBase
  void onDestroy() override;

  // Http::StreamDecoderFilter
  Http::FilterHeadersStatus decodeHeaders(Http::RequestHeaderMap& headers,
                                          bool end_stream) override;
  Http::FilterDataStatus decodeData(Buffer::Instance& data, bool end_stream) override;
  Http::FilterTrailersStatus decodeTrailers(Http::RequestTrailerMap& trailers) override;

  // Http::StreamEncoderFilter
  Http::FilterHeadersStatus encodeHeaders(Http::ResponseHeaderMap& headers,
                                          bool end_stream) override;
  Http::FilterDataStatus encodeData(Buffer::Instance& data, bool end_stream) override;
  Http::FilterTrailersStatus encodeTrailers(Http::ResponseTrailerMap& trailers) override;

private:
  // Returns false only if the payload was rejected, so the caller must drop the frame;
  // a failed best-effort parse abandons parsing and returns true.
  bool feedParser(const Buffer::Instance& data, bool end_stream);

  void rejectInvalidPayload(const absl::Status& status);

  // A parse failure is fatal only on an AI endpoint, so Envoy and the upstream cannot read one
  // body differently.
  bool isAiEndpoint() const { return route_has_request_; }

  uint32_t inlineStringThresholdBytes() const;

  // Called exactly once, at response end of stream (data or trailers).
  void finalizeResponseHandling();

  void finalizeDecode(bool has_trailers);

  ExternalBufferFactory& buffer_factory_;
  FilterConfigSharedPtr config_;

  // Non-null exactly when decodeHeaders() chose to inspect this stream; outlives request_parser_.
  BufferManagerPtr decode_manager_;

  // Copied, not held by pointer: a mid-stream route re-resolve would leave it dangling.
  bool route_has_request_{false};
  ApiProtocol route_request_protocol_{ApiProtocol::Unspecified};

  JsonWithExtBuf request_json_;
  // Reset once parsing completes, is abandoned, or fails the request.
  std::unique_ptr<JsonWithExtBufParser> request_parser_;

  // Once set, later frames on the dying stream are dropped, not offloaded.
  bool payload_rejected_{false};

  Http::RequestHeaderMap* request_headers_{nullptr};

  std::unique_ptr<FilterManager> filter_manager_;

  ResponseHandlerPtr response_handler_;
  bool response_finalized_{false};
};

} // namespace AiProtocolManager
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
