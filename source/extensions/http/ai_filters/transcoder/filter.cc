#include "source/extensions/http/ai_filters/transcoder/filter.h"

#include <optional>
#include <string>
#include <utility>

#include "source/common/coroutine/status_macros.h"
#include "source/extensions/filters/http/ai_protocol_manager/json_readers.h"

#include "absl/strings/match.h"
#include "absl/strings/string_view.h"
#include "nlohmann/json.hpp"

namespace Envoy {
namespace Extensions {
namespace AiFilters {
namespace Transcoder {

using HttpFilters::AiProtocolManager::AiFilterContext;
using HttpFilters::AiProtocolManager::AiRequestPropagator;
using HttpFilters::AiProtocolManager::AiRequestPtr;
using HttpFilters::AiProtocolManager::AiRequestReceiver;
using HttpFilters::AiProtocolManager::LLMProtocol;
using HttpFilters::AiProtocolManager::LocalReplier;
using HttpFilters::AiProtocolManager::TranscodingEngine;
using TranscoderProto = envoy::extensions::http::ai_filters::transcoder::v3::Transcoder;

namespace {

// Extracts the model from a Gemini request path: `.../models/{model}:generateContent` or
// `.../models/{model}:streamGenerateContent`, which Vertex nests under a longer prefix.
//
// TODO(ginama): this duplicates `readGeminiTarget()` in the request_info AI filter's
// extractor.cc, which parses the same paths for the same reason. Factor the two into one shared
// helper in the AI Protocol Manager rather than letting a third copy appear.
std::optional<std::string> modelFromGeminiPath(absl::string_view path) {
  path = path.substr(0, path.find('?'));
  const size_t last_slash = path.rfind('/');
  if (last_slash == absl::string_view::npos ||
      !absl::EndsWith(path.substr(0, last_slash), "/models")) {
    return std::nullopt;
  }
  const absl::string_view segment = path.substr(last_slash + 1);
  const size_t colon = segment.find(':');
  if (colon == absl::string_view::npos) {
    return std::nullopt;
  }
  const absl::string_view model = segment.substr(0, colon);
  const absl::string_view operation = segment.substr(colon + 1);
  if (model.empty() || (operation != "generateContent" && operation != "streamGenerateContent")) {
    return std::nullopt;
  }
  return std::string(model);
}

} // namespace

std::atomic<LLMProtocol> TranscoderFilter::target_protocol_{LLMProtocol::Unspecified};

void TranscoderFilter::setTargetProtocol(LLMProtocol protocol) {
  target_protocol_.store(protocol, std::memory_order_relaxed);
}

LLMProtocol TranscoderFilter::targetProtocol() {
  return target_protocol_.load(std::memory_order_relaxed);
}

TranscoderFilterConfig::TranscoderFilterConfig(const TranscoderProto& proto,
                                               TranscodingEngine engine, Stats::Scope& scope)
    : stats_(TranscoderFilterStats{ALL_TRANSCODER_FILTER_STATS(
          POOL_COUNTER_PREFIX(scope, "ai_protocol_manager.transcoder."))}),
      direction_(proto.direction()), engine_(std::move(engine)) {}

TranscoderFilter::TranscoderFilter(TranscoderFilterConfigSharedPtr config,
                                   const AiFilterContext& context)
    : config_(std::move(config)), source_protocol_(context.request_protocol),
      request_path_(std::string(context.request_headers.getPathValue())) {}

Coroutine::Task<absl::Status> TranscoderFilter::decode(AiRequestReceiver receive_request,
                                                       AiRequestPropagator propagate_request,
                                                       LocalReplier reply_locally) {
  ASSIGN_OR_CO_RETURN(AiRequestPtr request, co_await std::move(receive_request)());

  const absl::Status status = transcodeRequest(request->json());
  if (!status.ok()) {
    // A partially transcoded payload must never reach the next filter or the upstream: the rules
    // mutate the document in place, so a mid-rule failure leaves a document that is neither the
    // source shape nor the target shape. Failing the request is the only safe outcome. Unlike the
    // response path there is nothing on the wire yet, so a local reply is a clean failure.
    ENVOY_LOG(debug, "transcoder: rejecting request: {}", status.message());
    std::move(reply_locally)(Http::Code::BadRequest, std::string(status.message()));
    co_return absl::OkStatus();
  }

  config_->stats().transcoded_.inc();
  co_return co_await std::move(propagate_request)(std::move(request));
}

absl::Status TranscoderFilter::transcodeRequest(nlohmann::json& json) {
  return config_->direction() == TranscoderProto::TO_IR ? transcodeToIr(json)
                                                        : transcodeFromIr(json);
}

absl::Status TranscoderFilter::transcodeToIr(nlohmann::json& json) {
  namespace Keys = HttpFilters::AiProtocolManager::Keys;

  if (source_protocol_ == LLMProtocol::Unspecified) {
    config_->stats().unresolved_.inc();
    return absl::InvalidArgumentError(
        "transcoder: the route declared no wire API, so the payload's source schema is unknown");
  }

  // The IR is OpenAI Chat Completions, which requires `model`. Anthropic carries it in the body
  // so it converts directly, but Gemini's API puts it in the request path and its schema has no
  // `model` property at all. The engine only ever sees an `nlohmann::json&` and has no access to
  // headers, so the lift has to happen here, before the payload is handed over.
  if (source_protocol_ == LLMProtocol::GeminiGenerateContent && !json.contains(Keys::Model)) {
    if (const std::optional<std::string> model = modelFromGeminiPath(request_path_);
        model.has_value()) {
      json[std::string(Keys::Model)] = *model;
    }
  }

  const absl::Status status = config_->engine().transcodeToIr(source_protocol_, json);
  if (!status.ok()) {
    config_->stats().failed_.inc();
  }
  return status;
}

absl::Status TranscoderFilter::transcodeFromIr(nlohmann::json& json) {
  const LLMProtocol target = targetProtocol();
  if (target == LLMProtocol::Unspecified) {
    config_->stats().unresolved_.inc();
    return absl::InvalidArgumentError(
        "transcoder: no target backend protocol is set, so the payload has no target schema");
  }

  // Validation against the target's schema happens inside the engine, so a document the upstream
  // would reject fails here rather than over the network.
  const absl::Status status = config_->engine().transcodeFromIr(target, json);
  if (!status.ok()) {
    config_->stats().failed_.inc();
  }
  return status;
}

} // namespace Transcoder
} // namespace AiFilters
} // namespace Extensions
} // namespace Envoy
