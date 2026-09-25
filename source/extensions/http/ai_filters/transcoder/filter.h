#pragma once

#include <atomic>
#include <memory>
#include <string>

#include "envoy/extensions/http/ai_filters/transcoder/v3/transcoder.pb.h"
#include "envoy/stats/scope.h"
#include "envoy/stats/stats_macros.h"

#include "source/common/common/logger.h"
#include "source/extensions/filters/http/ai_protocol_manager/ai_filter.h"
#include "source/extensions/filters/http/ai_protocol_manager/transcoding_engine.h"

#include "nlohmann/json_fwd.hpp"

namespace Envoy {
namespace Extensions {
namespace AiFilters {
namespace Transcoder {

// `transcoded` counts payloads rewritten and accepted by the target schema. The two failure
// counters are kept apart because they mean different things operationally: `unresolved` is a
// configuration or client problem (the source or target protocol is unset/unknown), while `failed`
// is a transcoding problem (the rules ran but produced something the target schema rejected).
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
class TranscoderFilter : public HttpFilters::AiProtocolManager::AiFilter,
                         public Logger::Loggable<Logger::Id::ai_protocol_manager> {
public:
  TranscoderFilter(TranscoderFilterConfigSharedPtr config,
                   const HttpFilters::AiProtocolManager::AiFilterContext& context);

  // Target backend protocol override used in unit tests; when Unspecified, falls back to the
  // per-route response protocol (`AiProtocolManagerPerRoute.response.llm_protocol`).
  static void setTargetProtocol(HttpFilters::AiProtocolManager::LLMProtocol protocol);
  static HttpFilters::AiProtocolManager::LLMProtocol targetProtocol();

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
  HttpFilters::AiProtocolManager::LLMProtocol effectiveTargetProtocol() const;

  absl::Status transcodeRequest(nlohmann::json& json);
  absl::Status transcodeToIr(nlohmann::json& json);
  absl::Status transcodeFromIr(nlohmann::json& json);
  absl::Status moveTargetToGeminiPath(nlohmann::json& json);

  absl::Status transcodeResponse(nlohmann::json& json);
  absl::Status transcodeResponseToIr(nlohmann::json& json);
  absl::Status transcodeResponseFromIr(nlohmann::json& json);

  absl::Status transcodeSseEvent(HttpFilters::AiProtocolManager::SseEvent& event,
                                 bool& should_drop);
  absl::Status transcodeSseEventToIr(HttpFilters::AiProtocolManager::SseEvent& event,
                                     bool& should_drop);
  absl::Status transcodeSseEventFromIr(HttpFilters::AiProtocolManager::SseEvent& event,
                                       bool& should_drop);

  static std::atomic<HttpFilters::AiProtocolManager::LLMProtocol> target_protocol_;

  TranscoderFilterConfigSharedPtr config_;
  const HttpFilters::AiProtocolManager::LLMProtocol source_protocol_;
  const HttpFilters::AiProtocolManager::LLMProtocol route_target_protocol_;
  // Only touched by decode() before the request is propagated; see `AiFilterContext`.
  Http::RequestHeaderMap& request_headers_;
  // Copied rather than referenced: `AiFilterContext`'s referents belong to the stream and must
  // not be read after the request is propagated.
  const std::string request_path_;
  std::string sse_stream_id_{"chatcmpl-transcoded"};
  std::string sse_stream_model_;
  bool sse_done_emitted_{false};
};

} // namespace Transcoder
} // namespace AiFilters
} // namespace Extensions
} // namespace Envoy
