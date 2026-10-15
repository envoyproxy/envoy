#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "envoy/extensions/http/ai_filters/transcoder/v3/transcoder.pb.h"
#include "envoy/stats/scope.h"
#include "envoy/stats/stats_macros.h"
#include "envoy/stream_info/filter_state.h"

#include "source/common/common/logger.h"
#include "source/extensions/filters/http/ai_protocol_manager/ai_filter.h"
#include "source/extensions/filters/http/ai_protocol_manager/ai_filter_state.h"
#include "source/extensions/filters/http/ai_protocol_manager/flattening_json_codec.h"
#include "source/extensions/filters/http/ai_protocol_manager/llm_binding.h"
#include "source/extensions/filters/http/ai_protocol_manager/transcoding_engine.h"

#include "absl/status/statusor.h"
#include "absl/types/span.h"
#include "nlohmann/json_fwd.hpp"

namespace Envoy {
namespace Extensions {
namespace AiFilters {
namespace Transcoder {

// A unary response body reaches an AI filter as the leaf fields `FlatteningJsonDecoder` flattens it
// into, while the engine rewrites whole documents. These two convert between the two forms without
// serializing the document in between.
//
// Rebuilds the document `fields` flatten. A string that arrives as partial chunks
// (`FlattenJsonField::is_partial()`) is joined back into one.
nlohmann::json
unflattenFields(absl::Span<const HttpFilters::AiProtocolManager::FlattenJsonField> fields);
// Flattens `json` into the leaf fields, in the same order, that `FlatteningJsonDecoder` emits for
// its serialization: one per scalar and per empty object or array, none of them partial.
std::vector<HttpFilters::AiProtocolManager::FlattenJsonField>
flattenJson(const nlohmann::json& json);

// `transcoded` counts payloads rewritten and accepted by the target schema. The two failure
// counters are kept apart because they mean different things operationally: `unresolved` is a
// configuration or client problem (the source or target protocol is unset/unknown, or an
// instruction left in filter state cannot be followed), while `failed` is a transcoding problem
// (the rules ran but produced something the target schema rejected).
#define ALL_TRANSCODER_FILTER_STATS(COUNTER)                                                       \
  COUNTER(transcoded)                                                                              \
  COUNTER(unresolved)                                                                              \
  COUNTER(failed)

struct TranscoderFilterStats {
  ALL_TRANSCODER_FILTER_STATS(GENERATE_COUNTER_STRUCT)
};

class TranscoderFilterConfig {
public:
  TranscoderFilterConfig(
      const envoy::extensions::http::ai_filters::transcoder::v3::Transcoder& proto,
      HttpFilters::AiProtocolManager::TranscodingEngine engine, Stats::Scope& scope);

  envoy::extensions::http::ai_filters::transcoder::v3::Transcoder::Direction
  requestHandling() const {
    return request_handling_;
  }
  envoy::extensions::http::ai_filters::transcoder::v3::Transcoder::Direction
  responseHandling() const {
    return response_handling_;
  }
  const HttpFilters::AiProtocolManager::TranscodingEngine& engine() const { return engine_; }
  const TranscoderFilterStats& stats() const { return stats_; }

private:
  const TranscoderFilterStats stats_;
  const envoy::extensions::http::ai_filters::transcoder::v3::Transcoder::Direction
      request_handling_;
  const envoy::extensions::http::ai_filters::transcoder::v3::Transcoder::Direction
      response_handling_;
  // Owned by the config, not rebuilt per stream: registering the dialect packs validates every
  // rule set against its schema, which is config-load work rather than per-request work.
  const HttpFilters::AiProtocolManager::TranscodingEngine engine_;
};
using TranscoderFilterConfigSharedPtr = std::shared_ptr<const TranscoderFilterConfig>;

// Bidirectional AI filter that translates request and response payloads between vendor schemas
// and the canonical IR (OpenAI Chat Completions).
//
// Each instance configures `request_handling` (`TO_IR` / `FROM_IR`) and/or `response_handling`
// (`TO_IR` / `FROM_IR`). Leaving `response_handling` unset (`DIRECTION_UNSPECIFIED`) disables
// response transcoding for that instance so responses splice out immediately and pass through
// untouched.
//
// The route declares the client's and the backend's dialects, and each dialect's pack says how
// its API is carried on HTTP. A filter ahead of this one, such as a model router or an ext_proc
// callout, can override any of that for one request through the transcoding instructions in
// filter state (`HttpFilters::AiProtocolManager::FilterStateKeys`): the backend's dialect, the
// binding the client's request arrived in, the binding the backend expects, and the model to send
// upstream. Whatever is not set keeps its route and built-in default.
class TranscoderFilter : public HttpFilters::AiProtocolManager::AiFilter,
                         public Logger::Loggable<Logger::Id::ai_protocol_manager> {
public:
  TranscoderFilter(TranscoderFilterConfigSharedPtr config,
                   const HttpFilters::AiProtocolManager::AiFilterContext& context);

  // HttpFilters::AiProtocolManager::AiFilter
  Coroutine::Task<absl::Status>
  decode(HttpFilters::AiProtocolManager::AiRequestReceiver receive_request,
         HttpFilters::AiProtocolManager::AiRequestPropagator propagate_request,
         HttpFilters::AiProtocolManager::LocalReplier reply_locally) override;

  Coroutine::Task<absl::Status>
  encodeSSE(HttpFilters::AiProtocolManager::SseStreamReceiver receive_sse,
            HttpFilters::AiProtocolManager::SseStreamPropagator propagate_sse) override;

  Coroutine::Task<absl::Status> encodeUnary(
      HttpFilters::AiProtocolManager::AiResponseStreamReceiver receive_response,
      HttpFilters::AiProtocolManager::AiResponseStreamPropagator propagate_response) override;

private:
  // The transcoding instructions in filter state that the request legs follow; each is unset when
  // no filter ahead of this one left it.
  struct Instructions {
    HttpFilters::AiProtocolManager::LlmBindingConstSharedPtr request_binding;
    HttpFilters::AiProtocolManager::LlmBindingConstSharedPtr response_binding;
    std::string resolved_model;
  };

  // Reads the instructions once the request has arrived, so a filter ahead of this one in the
  // same chain has had its turn to set them, and applies the backend's dialect to
  // `target_protocol_`. Fails (counted as `unresolved`) on a binding that does not parse.
  absl::StatusOr<Instructions> readInstructions();
  // On failure, `unresolved` says whether the binding needs a value the client's path did not
  // supply, which is the proxy's configuration at fault rather than the request.
  absl::Status transcodeRequest(nlohmann::json& json, const Instructions& instructions,
                                bool& unresolved);
  // Leaves what a request `ToIr` leg read in this stream's `envoy.ai.binding.read`, for the
  // backend-side leg and for field access.
  void recordRead(HttpFilters::AiProtocolManager::LlmBindingRead&& read);
  // The model the client's path names, for a request that leaves this instance without the IR's:
  // what a request leg recorded, or else the path read again under `request_binding`. Called
  // before the path is rewritten.
  std::string modelFromPath(
      const HttpFilters::AiProtocolManager::LlmBindingConstSharedPtr& request_binding) const;

  // The engine leg that transcodes this stream's response as `kind`, or `std::nullopt` when the
  // response passes through: response transcoding is off, or the dialect it needs is unknown.
  std::optional<HttpFilters::AiProtocolManager::TranscodeLeg>
  responseLeg(HttpFilters::AiProtocolManager::PayloadKind kind);
  // The leg `handling` runs on a payload of `kind`, or an error (counted as `unresolved`) when the
  // route did not declare the dialect it needs.
  absl::StatusOr<HttpFilters::AiProtocolManager::TranscodeLeg>
  resolveLeg(envoy::extensions::http::ai_filters::transcoder::v3::Transcoder::Direction handling,
             HttpFilters::AiProtocolManager::PayloadKind kind);
  // What the response legs need beyond the payload.
  HttpFilters::AiProtocolManager::TranscodeContext responseContext() const;

  TranscoderFilterConfigSharedPtr config_;
  // The client's dialect: the route's request protocol.
  const HttpFilters::AiProtocolManager::LLMProtocol source_protocol_;
  // The backend's dialect: the route's response protocol
  // (`AiProtocolManagerPerRoute.response.llm_protocol`), unless the instructions name another.
  HttpFilters::AiProtocolManager::LLMProtocol target_protocol_;
  // Only touched by decode() before the request is propagated; see `AiFilterContext`.
  Http::RequestHeaderMap& request_headers_;
  // The stream's filter state, where the instructions are left. Held by pointer: the stream info
  // itself is only valid for as long as `AiFilterContext` says.
  const StreamInfo::FilterStateSharedPtr filter_state_;
  // When the stream started, in seconds since the Unix epoch: the `created` time of an IR
  // response whose dialect does not carry one.
  const int64_t created_;
  // The model the request named, for a response that does not name its own: the IR's, as a request
  // leg reports it, or else the one the client's request path names (as Gemini's does).
  std::string request_model_;
};

} // namespace Transcoder
} // namespace AiFilters
} // namespace Extensions
} // namespace Envoy
