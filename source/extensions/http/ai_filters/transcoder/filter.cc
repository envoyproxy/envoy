#include "source/extensions/http/ai_filters/transcoder/filter.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "source/common/buffer/buffer_impl.h"
#include "source/common/coroutine/status_macros.h"
#include "source/extensions/filters/http/ai_protocol_manager/json_readers.h"

#include "absl/status/statusor.h"
#include "absl/strings/ascii.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
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
using HttpFilters::AiProtocolManager::AiResponseStreamPropagator;
using HttpFilters::AiProtocolManager::AiResponseStreamReceiver;
using HttpFilters::AiProtocolManager::FieldPathSegment;
using HttpFilters::AiProtocolManager::FlatteningJsonDecoder;
using HttpFilters::AiProtocolManager::FlattenJsonField;
using HttpFilters::AiProtocolManager::LLMProtocol;
using HttpFilters::AiProtocolManager::LocalReplier;
using HttpFilters::AiProtocolManager::PayloadKind;
using HttpFilters::AiProtocolManager::SseEventPtr;
using HttpFilters::AiProtocolManager::SseStreamPropagator;
using HttpFilters::AiProtocolManager::SseStreamReceiver;
using HttpFilters::AiProtocolManager::TranscodeContext;
using HttpFilters::AiProtocolManager::TranscodeDirection;
using HttpFilters::AiProtocolManager::TranscodeLeg;
using HttpFilters::AiProtocolManager::TranscodeStreamState;
using HttpFilters::AiProtocolManager::TranscodingEngine;
using TranscoderProto = envoy::extensions::http::ai_filters::transcoder::v3::Transcoder;
namespace Keys = HttpFilters::AiProtocolManager::Keys;

namespace {

constexpr absl::string_view StreamOptions = "stream_options";

struct GeminiTarget {
  std::string model;
  bool stream{false};
};

// Extracts the model and streaming mode from a Gemini request path:
// `.../models/{model}:generateContent` or `.../models/{model}:streamGenerateContent`, which Vertex
// nests under a longer prefix.
//
// TODO(ginama): this duplicates `readGeminiTarget()` in the request_info AI filter's
// extractor.cc, which parses the same paths for the same reason. Factor the two into one shared
// helper in the AI Protocol Manager rather than letting a third copy appear.
std::optional<GeminiTarget> geminiTargetFromPath(absl::string_view path) {
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
  return GeminiTarget{std::string(model), operation == "streamGenerateContent"};
}

// The model becomes a path segment, so anything that could escape it is refused.
bool isGeminiModelId(absl::string_view model) {
  return !model.empty() && std::all_of(model.begin(), model.end(), [](char c) {
    return absl::ascii_isalnum(c) || c == '-' || c == '.' || c == '_';
  });
}

// Reconstructs a JSON document from a sequence of flattened leaf fields.
nlohmann::json unflattenFields(const std::vector<FlattenJsonField>& fields) {
  nlohmann::json root = nlohmann::json::object();
  for (const FlattenJsonField& field : fields) {
    const auto path = field.field_path();
    if (path.empty()) {
      root = field.node();
      continue;
    }
    nlohmann::json* curr = &root;
    for (size_t i = 0; i < path.size(); ++i) {
      const FieldPathSegment& seg = path[i];
      if (absl::holds_alternative<std::string>(seg)) {
        const std::string& key = absl::get<std::string>(seg);
        if (!curr->is_object()) {
          *curr = nlohmann::json::object();
        }
        curr = &((*curr)[key]);
      } else {
        const size_t idx = absl::get<size_t>(seg);
        if (!curr->is_array()) {
          *curr = nlohmann::json::array();
        }
        while (curr->size() <= idx) {
          curr->push_back(nullptr);
        }
        curr = &((*curr)[idx]);
      }
    }
    if (curr->is_string() && field.node().is_string()) {
      curr->get_ref<std::string&>().append(field.node().get_ref<const std::string&>());
    } else {
      *curr = field.node();
    }
  }
  return root;
}

} // namespace

std::atomic<LLMProtocol> TranscoderFilter::target_protocol_{LLMProtocol::Unspecified};

void TranscoderFilter::setTargetProtocol(LLMProtocol protocol) {
  target_protocol_.store(protocol, std::memory_order_relaxed);
}

LLMProtocol TranscoderFilter::targetProtocol() {
  return target_protocol_.load(std::memory_order_relaxed);
}

LLMProtocol TranscoderFilter::effectiveTargetProtocol() const {
  const LLMProtocol override_protocol = targetProtocol();
  return override_protocol != LLMProtocol::Unspecified ? override_protocol : route_target_protocol_;
}

TranscoderFilterConfig::TranscoderFilterConfig(const TranscoderProto& proto,
                                               TranscodingEngine engine, Stats::Scope& scope)
    : stats_(TranscoderFilterStats{ALL_TRANSCODER_FILTER_STATS(
          POOL_COUNTER_PREFIX(scope, "ai_protocol_manager.transcoder."))}),
      request_handling_(proto.request_handling()), response_handling_(proto.response_handling()),
      engine_(std::move(engine)) {}

TranscoderFilter::TranscoderFilter(TranscoderFilterConfigSharedPtr config,
                                   const AiFilterContext& context)
    : config_(std::move(config)), source_protocol_(context.request_protocol),
      route_target_protocol_(context.response_protocol), request_headers_(context.request_headers),
      request_path_(std::string(context.request_headers.getPathValue())),
      created_(std::chrono::duration_cast<std::chrono::seconds>(
                   context.stream_info.startTime().time_since_epoch())
                   .count()) {
  const std::optional<GeminiTarget> gemini_target = geminiTargetFromPath(request_path_);
  if (gemini_target.has_value()) {
    request_model_ = gemini_target->model;
  }
}

Coroutine::Task<absl::Status> TranscoderFilter::decode(AiRequestReceiver receive_request,
                                                       AiRequestPropagator propagate_request,
                                                       LocalReplier reply_locally) {
  ASSIGN_OR_CO_RETURN(AiRequestPtr request, co_await std::move(receive_request)());

  if (config_->requestHandling() == TranscoderProto::DIRECTION_UNSPECIFIED) {
    co_return co_await std::move(propagate_request)(std::move(request));
  }

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

Coroutine::Task<absl::Status>
TranscoderFilter::encodeUnary(AiResponseStreamReceiver receive_response,
                              AiResponseStreamPropagator propagate_response) {
  const std::optional<TranscodeLeg> leg = responseLeg(PayloadKind::Response);
  if (!leg.has_value()) {
    co_return absl::OkStatus();
  }

  std::vector<FlattenJsonField> all_fields;
  while (true) {
    ASSIGN_OR_CO_RETURN(std::vector<FlattenJsonField> batch, co_await receive_response());
    if (batch.empty()) {
      break;
    }
    all_fields.insert(all_fields.end(), std::make_move_iterator(batch.begin()),
                      std::make_move_iterator(batch.end()));
  }

  if (all_fields.empty()) {
    co_return absl::OkStatus();
  }

  nlohmann::json doc = unflattenFields(all_fields);
  TranscodeContext ctx = responseContext();
  const absl::Status status = config_->engine().transcode(*leg, ctx, doc);
  if (!status.ok()) {
    config_->stats().failed_.inc();
    ENVOY_LOG(debug, "transcoder: forwarding unary response untranslated: {}", status.message());
    CO_RETURN_IF_ERROR(co_await propagate_response(std::move(all_fields)));
    co_return absl::OkStatus();
  }

  config_->stats().transcoded_.inc();
  FlatteningJsonDecoder decoder;
  Buffer::OwnedImpl serialized(doc.dump());
  ASSIGN_OR_CO_RETURN(std::vector<FlattenJsonField> transcoded_fields,
                      decoder.onData(serialized, /*end_stream=*/true));
  if (!transcoded_fields.empty()) {
    CO_RETURN_IF_ERROR(co_await propagate_response(std::move(transcoded_fields)));
  }
  co_return absl::OkStatus();
}

Coroutine::Task<absl::Status> TranscoderFilter::encodeSSE(SseStreamReceiver receive_sse,
                                                          SseStreamPropagator propagate_sse) {
  const std::optional<TranscodeLeg> leg = responseLeg(PayloadKind::StreamEvent);
  if (!leg.has_value()) {
    co_return absl::OkStatus();
  }

  // The stream's memory lives exactly as long as the stream: this coroutine.
  TranscodeStreamState stream_state;
  TranscodeContext ctx = responseContext();
  ctx.stream_state = &stream_state;
  const TranscodingEngine& engine = config_->engine();

  while (true) {
    ASSIGN_OR_CO_RETURN(std::optional<SseEventPtr> event_opt, co_await receive_sse());
    if (!event_opt.has_value()) {
      break;
    }

    SseEventPtr event = std::move(*event_opt);
    absl::StatusOr<std::vector<SseEventPtr>> transcoded =
        engine.transcodeStreamEvent(*leg, ctx, event);
    if (!transcoded.ok()) {
      // The engine leaves a refused event untouched, so it can still go out as it came in.
      config_->stats().failed_.inc();
      ENVOY_LOG(debug, "transcoder: forwarding SSE frame untranslated: {}",
                transcoded.status().message());
      CO_RETURN_IF_ERROR(co_await propagate_sse(std::move(event)));
      continue;
    }
    if (transcoded->empty()) {
      continue;
    }
    config_->stats().transcoded_.inc();
    for (SseEventPtr& out : *transcoded) {
      CO_RETURN_IF_ERROR(co_await propagate_sse(std::move(out)));
    }
  }

  absl::StatusOr<std::vector<SseEventPtr>> trailer = engine.finishStream(*leg, ctx);
  if (!trailer.ok()) {
    config_->stats().failed_.inc();
    ENVOY_LOG(debug, "transcoder: cannot end the transcoded SSE stream: {}",
              trailer.status().message());
    co_return absl::OkStatus();
  }
  for (SseEventPtr& out : *trailer) {
    CO_RETURN_IF_ERROR(co_await propagate_sse(std::move(out)));
  }
  co_return absl::OkStatus();
}

absl::Status TranscoderFilter::transcodeRequest(nlohmann::json& json) {
  return config_->requestHandling() == TranscoderProto::TO_IR ? transcodeToIr(json)
                                                              : transcodeFromIr(json);
}

absl::Status TranscoderFilter::transcodeToIr(nlohmann::json& json) {
  if (source_protocol_ == LLMProtocol::Unspecified) {
    config_->stats().unresolved_.inc();
    return absl::InvalidArgumentError(
        "transcoder: the route declared no wire API, so the payload's source schema is unknown");
  }

  // The IR is OpenAI Chat Completions, which requires `model`. Anthropic carries it in the body
  // so it converts directly, but Gemini's API puts it in the request path and its schema has no
  // `model` property at all. The engine only ever sees an `nlohmann::json&` and has no access to
  // headers, so the lift has to happen here, before the payload is handed over. The streaming
  // mode is in the path too, and the `FROM_IR` leg rebuilds the path from both.
  if (source_protocol_ == LLMProtocol::GeminiGenerateContent) {
    if (const std::optional<GeminiTarget> target = geminiTargetFromPath(request_path_);
        target.has_value()) {
      if (!json.contains(Keys::Model)) {
        json[std::string(Keys::Model)] = target->model;
      }
      if (target->stream && !json.contains(Keys::Stream)) {
        json[std::string(Keys::Stream)] = true;
      }
    }
  }

  TranscodeContext ctx;
  const absl::Status status = config_->engine().transcode(
      {PayloadKind::Request, TranscodeDirection::ToIr, source_protocol_}, ctx, json);
  if (!status.ok()) {
    config_->stats().failed_.inc();
    return status;
  }
  if (!ctx.ir_model.empty()) {
    request_model_ = std::move(ctx.ir_model);
  }
  return status;
}

absl::Status TranscoderFilter::transcodeFromIr(nlohmann::json& json) {
  const LLMProtocol target = effectiveTargetProtocol();
  if (target == LLMProtocol::Unspecified) {
    config_->stats().unresolved_.inc();
    return absl::InvalidArgumentError(
        "transcoder: no target backend protocol is set, so the payload has no target schema");
  }

  // Validation against the target's schema happens inside the engine, so a document the upstream
  // would reject fails here rather than over the network.
  TranscodeContext ctx;
  absl::Status status = config_->engine().transcode(
      {PayloadKind::Request, TranscodeDirection::FromIr, target}, ctx, json);
  if (status.ok() && target == LLMProtocol::GeminiGenerateContent) {
    status = moveTargetToGeminiPath(json);
  }
  if (!status.ok()) {
    config_->stats().failed_.inc();
    return status;
  }
  if (!ctx.ir_model.empty()) {
    request_model_ = std::move(ctx.ir_model);
  }
  return status;
}

// Gemini names the model and the streaming mode in the path, and rejects the IR's `stream` and
// `stream_options` as unknown fields. The path is the Gemini API's; a route in front of another
// endpoint layout, such as Vertex AI's, rewrites the `/v1beta/models/` prefix.
absl::Status TranscoderFilter::moveTargetToGeminiPath(nlohmann::json& json) {
  const auto model = json.find(Keys::Model);
  if (model == json.end() || !model->is_string() ||
      !isGeminiModelId(model->get_ref<const std::string&>())) {
    return absl::InvalidArgumentError("transcoder: a Gemini target needs `model` to be a model id");
  }
  const auto stream = json.find(Keys::Stream);
  const bool streaming = stream != json.end() && stream->is_boolean() && stream->get<bool>();
  request_headers_.setPath(
      absl::StrCat("/v1beta/models/", model->get_ref<const std::string&>(),
                   streaming ? ":streamGenerateContent?alt=sse" : ":generateContent"));
  json.erase(std::string(Keys::Model));
  json.erase(std::string(Keys::Stream));
  json.erase(std::string(StreamOptions));
  return absl::OkStatus();
}

// A `TO_IR` response comes back from the backend in the target's dialect; a `FROM_IR` one goes
// back to the client in the dialect the route declared. Every dialect rule is the engine's: the
// filter only picks the leg and hands over what the payload does not carry.
std::optional<TranscodeLeg> TranscoderFilter::responseLeg(PayloadKind kind) {
  const auto handling = config_->responseHandling();
  if (handling == TranscoderProto::DIRECTION_UNSPECIFIED) {
    return std::nullopt;
  }
  const bool to_ir = handling == TranscoderProto::TO_IR;
  const LLMProtocol dialect = to_ir ? effectiveTargetProtocol() : source_protocol_;
  if (dialect == LLMProtocol::Unspecified) {
    config_->stats().unresolved_.inc();
    ENVOY_LOG(debug, "transcoder: forwarding the response untranslated: {}",
              to_ir ? "no target backend protocol is set for response TO_IR transcoding"
                    : "the route declared no wire API for response FROM_IR transcoding");
    return std::nullopt;
  }
  return TranscodeLeg{kind, to_ir ? TranscodeDirection::ToIr : TranscodeDirection::FromIr, dialect};
}

TranscodeContext TranscoderFilter::responseContext() const {
  TranscodeContext ctx;
  ctx.request_model = request_model_;
  ctx.now_unix_seconds = created_;
  return ctx;
}

} // namespace Transcoder
} // namespace AiFilters
} // namespace Extensions
} // namespace Envoy
