#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <utility>

#include "envoy/extensions/filters/http/ai_protocol_manager/v3/ai_protocol_manager.pb.h"
#include "envoy/stats/scope.h"

#include "source/common/common/logger.h"
#include "source/extensions/filters/http/ai_protocol_manager/buffer_manager.h"
#include "source/extensions/filters/http/ai_protocol_manager/external_buffer.h"
#include "source/extensions/filters/http/ai_protocol_manager/response_handler.h"
#include "source/extensions/filters/http/common/pass_through_filter.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AiProtocolManager {

// Filter-level configuration shared by every stream: parsed response-handling
// settings plus the filter's stats. One instance per listener/cluster filter
// config; shared across downstream and upstream installations alike.
class FilterConfig {
public:
  FilterConfig(
      const envoy::extensions::filters::http::ai_protocol_manager::v3::AiProtocolManager& config,
      Stats::Scope& scope);

  bool responseHandlingEnabled() const { return response_handling_enabled_; }
  bool requestOffloadEnabled() const { return request_offload_enabled_; }
  ApiFormat apiFormat() const { return api_format_; }
  const std::string& metadataNamespace() const { return metadata_namespace_; }
  uint32_t maxEventSize() const { return max_event_size_; }
  uint32_t maxInspectedBodySize() const { return max_inspected_body_size_; }
  uint32_t maxParsedEvents() const { return max_parsed_events_; }
  AiProtocolManagerStats& stats() { return stats_; }

private:
  AiProtocolManagerStats stats_;
  const bool response_handling_enabled_;
  const bool request_offload_enabled_;
  const ApiFormat api_format_;
  const std::string metadata_namespace_;
  const uint32_t max_event_size_;
  const uint32_t max_inspected_body_size_;
  const uint32_t max_parsed_events_;
};

using FilterConfigSharedPtr = std::shared_ptr<FilterConfig>;

// AI Protocol Manager HTTP filter (alpha).
//
// Decode (request) path: offloads the request payload into an ExternalBuffer
// as it arrives, holding the headers here meanwhile, then replays the body
// back into the chain (the first injected frame releases the held headers).
// The offload/replay pipeline lives in BufferManager. Disabled entirely by
// request_handling.payload_offload_enabled=false (pure passthrough).
//
// Encode (response) path: observe-only token-usage extraction. 2xx SSE/JSON
// responses are teed into a ResponseHandler; at end of stream the normalized
// usage is published as dynamic metadata on the downstream StreamInfo, from
// downstream and upstream installations alike. The filter never stops
// iteration or mutates the response; extraction works on bounded side state,
// synchronously on the encode callbacks, and no handler failure can affect
// the stream.
class AiProtocolManagerFilter : public Http::PassThroughFilter,
                                public Logger::Loggable<Logger::Id::filter> {
public:
  AiProtocolManagerFilter(ExternalBufferFactory& buffer_factory, FilterConfigSharedPtr config)
      : buffer_factory_(buffer_factory), config_(std::move(config)) {}

  // Http::StreamFilterBase
  void onDestroy() override;

  // Http::StreamDecoderFilter
  void setDecoderFilterCallbacks(Http::StreamDecoderFilterCallbacks& callbacks) override;
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
  // Publish the accumulated token usage as dynamic metadata and account stats.
  // Called exactly once, at response end of stream (data or trailers).
  void finalizeResponseHandling();

  ExternalBufferFactory& buffer_factory_;
  FilterConfigSharedPtr config_;
  BufferManagerPtr decode_manager_;
  ResponseHandlerPtr response_handler_;
  bool response_finalized_{false};
};

} // namespace AiProtocolManager
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
