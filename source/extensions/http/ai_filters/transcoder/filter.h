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

  envoy::extensions::http::ai_filters::transcoder::v3::Transcoder::Direction direction() const {
    return direction_;
  }
  const HttpFilters::AiProtocolManager::TranscodingEngine& engine() const { return engine_; }
  const TranscoderFilterStats& stats() const { return stats_; }

private:
  const TranscoderFilterStats stats_;
  const envoy::extensions::http::ai_filters::transcoder::v3::Transcoder::Direction direction_;
  // Owned by the config, not rebuilt per stream: registering the dialect packs validates every
  // rule set against its schema, which is config-load work rather than per-request work.
  const HttpFilters::AiProtocolManager::TranscodingEngine engine_;
};
using TranscoderFilterConfigSharedPtr = std::shared_ptr<const TranscoderFilterConfig>;

// Bidirectional AI filter that translates payloads between vendor schemas and the canonical IR
// (OpenAI Chat Completions) via the declarative transcoding engine.
//
// Two instances of this filter bracket the intermediate AI filters in the chain, so those filters
// only ever see the canonical IR regardless of what the client speaks or what the backend expects:
//
//   client -> transcoder(TO_IR) -> ...ai filters... -> transcoder(FROM_IR) -> backend
//
// `FilterManager` runs the chain forward on the request path and in reverse on the response path,
// so a single `direction` value describes both legs of an instance:
//
//   - `TO_IR` (client boundary):
//       * decode():  client `source_protocol` -> IR   (`to_ir`)
//       * encode*():  IR -> client `source_protocol`  (`from_ir`)   [not yet implemented]
//   - `FROM_IR` (backend boundary):
//       * decode():  IR -> backend `target_protocol`  (`from_ir`)
//       * encode*():  backend `target_protocol` -> IR (`to_ir`)     [not yet implemented]
//
// ---------------------------------------------------------------------------------------------
// Response path -- deliberately NOT implemented yet. Read before adding `encodeSSE()` /
// `encodeUnary()` overrides here.
//
// The shape above is symmetric, and wiring the two encode coroutines to `transcodeToIr()` /
// `transcodeFromIr()` compiles and even passes naive tests. It is nonetheless wrong today,
// for two reasons that both live in the engine rather than in this filter:
//
//  1. `DialectTranscodePack` carries only REQUEST rule sets. Its `to_ir` / `from_ir` rules encode
//     request-shaped mappings (Gemini `contents` <-> OpenAI `messages`, Anthropic `max_tokens`
//     <-> `max_completion_tokens`). A response needs an entirely different mapping (Gemini
//     `candidates` <-> OpenAI `choices`, Anthropic `content` blocks <-> `choices[].message`).
//     Running the request rules over a response document silently produces garbage.
//
//  2. `TranscodingEngine::transcodeFromIr()` finishes by calling
//     `dialect_schema->validateRequest()`. Handed a response document that validator fails by
//     construction -- a Gemini response has no `contents`, so it is rejected with
//     "missing required field: contents".
//
// Failing there is worse than not translating: the encode coroutines signal failure by returning
// a non-OK status, which `ResponseFilterManager` treats as "the response is no longer
// trustworthy" and tears the stream down -- after earlier frames have already been serialized and
// flushed to the client. The client would see a truncated, half-translated stream.
//
// So both coroutines are intentionally left unimplemented. The base class defaults return
// `absl::OkStatus()` immediately, which splices this filter out of the response pipeline and lets
// frames and field batches flow past it untouched. An untranslated response reaching a client
// that asked for another dialect is a visible, diagnosable bug; a corrupted or truncated one is
// not.
//
// TODO(ginama): implement the response legs once the engine grows response support. That needs:
//   (a) `response_to_ir` / `response_from_ir` rule sets on `DialectTranscodePack`, verified at
//       registration time the same way the request rule sets already are;
//   (b) `PayloadSchema::validateResponse()`, so the converted document is checked against the
//       response schema instead of the request schema;
//   (c) a decision on the failure contract -- this filter should almost certainly forward a frame
//       untranslated (counting `transcoder.failed`) rather than tear down a stream whose earlier
//       frames the client already holds;
//   (d) a streaming-friendly `encodeUnary()`. Reassembling the whole `FlattenJsonField` stream
//       into one DOM before transcoding would undo the bound that the flattening codec and the
//       external buffer exist to enforce, so either transcode incrementally or cap the buffered
//       document explicitly.
// ---------------------------------------------------------------------------------------------
class TranscoderFilter : public HttpFilters::AiProtocolManager::AiFilter,
                         public Logger::Loggable<Logger::Id::ai_protocol_manager> {
public:
  TranscoderFilter(TranscoderFilterConfigSharedPtr config,
                   const HttpFilters::AiProtocolManager::AiFilterContext& context);

  // Target backend protocol used by the `FROM_IR` leg.
  //
  // TODO(ginama): this is a placeholder, not the intended end state. Process-global mutable state
  // is shared by every listener, route and worker in the process, so a proxy fronting more than
  // one backend vendor cannot work -- the last writer wins for everyone. It also sits outside
  // Envoy's configuration model, where config is immutable after load and scoped to a config
  // object. Replace it with per-route/per-cluster configuration (resolved from the cluster the
  // route selected) once the routing story is settled.
  static void setTargetProtocol(HttpFilters::AiProtocolManager::LLMProtocol protocol);
  static HttpFilters::AiProtocolManager::LLMProtocol targetProtocol();

  // HttpFilters::AiProtocolManager::AiFilter
  //
  // `encodeSSE()` and `encodeUnary()` are deliberately not overridden; see the note above.
  Coroutine::Task<absl::Status>
  decode(HttpFilters::AiProtocolManager::AiRequestReceiver receive_request,
         HttpFilters::AiProtocolManager::AiRequestPropagator propagate_request,
         HttpFilters::AiProtocolManager::LocalReplier reply_locally) override;

private:
  absl::Status transcodeRequest(nlohmann::json& json);
  absl::Status transcodeToIr(nlohmann::json& json);
  absl::Status transcodeFromIr(nlohmann::json& json);

  static std::atomic<HttpFilters::AiProtocolManager::LLMProtocol> target_protocol_;

  TranscoderFilterConfigSharedPtr config_;
  const HttpFilters::AiProtocolManager::LLMProtocol source_protocol_;
  // Copied rather than referenced: `AiFilterContext`'s referents belong to the stream and must
  // not be read after the request is propagated.
  const std::string request_path_;
};

} // namespace Transcoder
} // namespace AiFilters
} // namespace Extensions
} // namespace Envoy
