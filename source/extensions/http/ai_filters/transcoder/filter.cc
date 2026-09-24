#include "source/extensions/http/ai_filters/transcoder/filter.h"

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "source/common/buffer/buffer_impl.h"
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
using HttpFilters::AiProtocolManager::AiResponseStreamPropagator;
using HttpFilters::AiProtocolManager::AiResponseStreamReceiver;
using HttpFilters::AiProtocolManager::FieldPathSegment;
using HttpFilters::AiProtocolManager::FlatteningJsonDecoder;
using HttpFilters::AiProtocolManager::FlattenJsonField;
using HttpFilters::AiProtocolManager::LLMProtocol;
using HttpFilters::AiProtocolManager::LocalReplier;
using HttpFilters::AiProtocolManager::SseEvent;
using HttpFilters::AiProtocolManager::SseEventPtr;
using HttpFilters::AiProtocolManager::SseStreamPropagator;
using HttpFilters::AiProtocolManager::SseStreamReceiver;
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

std::string mapGeminiFinishReasonToIr(absl::string_view reason) {
  if (reason == "STOP") {
    return "stop";
  }
  if (reason == "MAX_TOKENS") {
    return "length";
  }
  if (reason == "SAFETY" || reason == "RECITATION" || reason == "BLOCKLIST") {
    return "content_filter";
  }
  return "stop";
}

std::string mapIrFinishReasonToGemini(absl::string_view reason) {
  if (reason == "stop") {
    return "STOP";
  }
  if (reason == "length") {
    return "MAX_TOKENS";
  }
  if (reason == "content_filter") {
    return "SAFETY";
  }
  return "STOP";
}

std::string mapAnthropicStopReasonToIr(absl::string_view reason) {
  if (reason == "end_turn" || reason == "stop_sequence") {
    return "stop";
  }
  if (reason == "max_tokens") {
    return "length";
  }
  if (reason == "tool_use") {
    return "tool_calls";
  }
  return "stop";
}

std::string mapIrFinishReasonToAnthropic(absl::string_view reason) {
  if (reason == "stop") {
    return "end_turn";
  }
  if (reason == "length") {
    return "max_tokens";
  }
  if (reason == "tool_calls") {
    return "tool_use";
  }
  return "end_turn";
}

void transcodeGeminiUsageToIr(const nlohmann::json& src, nlohmann::json& dst) {
  if (!src.contains("usageMetadata") || !src["usageMetadata"].is_object()) {
    return;
  }
  const auto& meta = src["usageMetadata"];
  nlohmann::json usage = nlohmann::json::object();
  if (meta.contains("promptTokenCount") && meta["promptTokenCount"].is_number_integer()) {
    usage["prompt_tokens"] = meta["promptTokenCount"];
  }
  if (meta.contains("candidatesTokenCount") && meta["candidatesTokenCount"].is_number_integer()) {
    usage["completion_tokens"] = meta["candidatesTokenCount"];
  }
  if (meta.contains("totalTokenCount") && meta["totalTokenCount"].is_number_integer()) {
    usage["total_tokens"] = meta["totalTokenCount"];
  }
  if (!usage.empty()) {
    dst["usage"] = std::move(usage);
  }
}

void transcodeIrUsageToGemini(const nlohmann::json& src, nlohmann::json& dst) {
  if (!src.contains("usage") || !src["usage"].is_object()) {
    return;
  }
  const auto& usage = src["usage"];
  nlohmann::json meta = nlohmann::json::object();
  if (usage.contains("prompt_tokens") && usage["prompt_tokens"].is_number_integer()) {
    meta["promptTokenCount"] = usage["prompt_tokens"];
  }
  if (usage.contains("completion_tokens") && usage["completion_tokens"].is_number_integer()) {
    meta["candidatesTokenCount"] = usage["completion_tokens"];
  }
  if (usage.contains("total_tokens") && usage["total_tokens"].is_number_integer()) {
    meta["totalTokenCount"] = usage["total_tokens"];
  }
  if (!meta.empty()) {
    dst["usageMetadata"] = std::move(meta);
  }
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
      direction_(proto.direction()), engine_(std::move(engine)) {}

TranscoderFilter::TranscoderFilter(TranscoderFilterConfigSharedPtr config,
                                   const AiFilterContext& context)
    : config_(std::move(config)), source_protocol_(context.request_protocol),
      route_target_protocol_(context.response_protocol),
      request_path_(std::string(context.request_headers.getPathValue())),
      sse_stream_model_(modelFromGeminiPath(request_path_).value_or("transcoded-model")) {}

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

Coroutine::Task<absl::Status>
TranscoderFilter::encodeUnary(AiResponseStreamReceiver receive_response,
                              AiResponseStreamPropagator propagate_response) {
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
  const absl::Status status = transcodeResponse(doc);
  if (!status.ok()) {
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
  while (true) {
    ASSIGN_OR_CO_RETURN(std::optional<SseEventPtr> event_opt, co_await receive_sse());
    if (!event_opt.has_value()) {
      if (config_->direction() == TranscoderProto::FROM_IR &&
          effectiveTargetProtocol() == LLMProtocol::GeminiGenerateContent && !sse_done_emitted_) {
        auto done_event = std::make_unique<SseEvent>();
        done_event->set_raw_data(std::make_unique<Buffer::OwnedImpl>("[DONE]"));
        CO_RETURN_IF_ERROR(co_await propagate_sse(std::move(done_event)));
        sse_done_emitted_ = true;
      }
      co_return absl::OkStatus();
    }

    SseEventPtr event = std::move(*event_opt);
    bool should_drop = false;
    const absl::Status status = transcodeSseEvent(*event, should_drop);
    if (!status.ok()) {
      ENVOY_LOG(debug, "transcoder: forwarding SSE frame untranslated: {}", status.message());
      CO_RETURN_IF_ERROR(co_await propagate_sse(std::move(event)));
      continue;
    }
    if (should_drop) {
      continue;
    }
    config_->stats().transcoded_.inc();
    CO_RETURN_IF_ERROR(co_await propagate_sse(std::move(event)));
  }
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
  const LLMProtocol target = effectiveTargetProtocol();
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

absl::Status TranscoderFilter::transcodeResponse(nlohmann::json& json) {
  // On the response path, FilterManager runs the chain in reverse:
  // - The FROM_IR (backend boundary) instance runs first and translates Backend -> IR.
  // - The TO_IR (client boundary) instance runs second and translates IR -> Client.
  return config_->direction() == TranscoderProto::FROM_IR ? transcodeResponseToIr(json)
                                                          : transcodeResponseFromIr(json);
}

absl::Status TranscoderFilter::transcodeResponseToIr(nlohmann::json& json) {
  const LLMProtocol target = effectiveTargetProtocol();
  if (target == LLMProtocol::Unspecified) {
    config_->stats().unresolved_.inc();
    return absl::InvalidArgumentError(
        "transcoder: no target backend protocol is set for response TO_IR transcoding");
  }

  if (target == LLMProtocol::OpenAiChatCompletions) {
    return absl::OkStatus();
  }

  if (target == LLMProtocol::GeminiGenerateContent) {
    if (!json.is_object() || !json.contains("candidates") || !json["candidates"].is_array()) {
      config_->stats().failed_.inc();
      return absl::InvalidArgumentError("transcoder: Gemini response missing `candidates` array");
    }
    nlohmann::json out = nlohmann::json::object();
    out["id"] = json.value("responseId", "chatcmpl-transcoded");
    out["object"] = "chat.completion";
    out["model"] = json.value("modelVersion", sse_stream_model_);
    nlohmann::json choices = nlohmann::json::array();
    for (size_t i = 0; i < json["candidates"].size(); ++i) {
      const auto& cand = json["candidates"][i];
      nlohmann::json choice = nlohmann::json::object();
      choice["index"] = cand.value("index", static_cast<int>(i));
      std::string text;
      if (cand.contains("content") && cand["content"].is_object() &&
          cand["content"].contains("parts") && cand["content"]["parts"].is_array()) {
        for (const auto& part : cand["content"]["parts"]) {
          if (part.is_object() && part.contains("text") && part["text"].is_string()) {
            text.append(part["text"].get<std::string>());
          }
        }
      }
      choice["message"] = {{"role", "assistant"}, {"content", std::move(text)}};
      if (cand.contains("finishReason") && cand["finishReason"].is_string()) {
        choice["finish_reason"] =
            mapGeminiFinishReasonToIr(cand["finishReason"].get<std::string>());
      } else {
        choice["finish_reason"] = "stop";
      }
      choices.push_back(std::move(choice));
    }
    out["choices"] = std::move(choices);
    transcodeGeminiUsageToIr(json, out);
    json = std::move(out);
    return absl::OkStatus();
  }

  if (target == LLMProtocol::AnthropicMessages) {
    if (!json.is_object() || !json.contains("content") || !json["content"].is_array()) {
      config_->stats().failed_.inc();
      return absl::InvalidArgumentError("transcoder: Anthropic response missing `content` array");
    }
    nlohmann::json out = nlohmann::json::object();
    out["id"] = json.value("id", "chatcmpl-transcoded");
    out["object"] = "chat.completion";
    out["model"] = json.value("model", sse_stream_model_);
    std::string text;
    for (const auto& block : json["content"]) {
      if (block.is_object() && block.value("type", "") == "text" && block.contains("text") &&
          block["text"].is_string()) {
        text.append(block["text"].get<std::string>());
      }
    }
    std::string finish_reason = "stop";
    if (json.contains("stop_reason") && json["stop_reason"].is_string()) {
      finish_reason = mapAnthropicStopReasonToIr(json["stop_reason"].get<std::string>());
    }
    out["choices"] = nlohmann::json::array(
        {{{"index", 0},
          {"message", {{"role", json.value("role", "assistant")}, {"content", std::move(text)}}},
          {"finish_reason", std::move(finish_reason)}}});
    if (json.contains("usage") && json["usage"].is_object()) {
      const auto& u = json["usage"];
      const int prompt_tokens = u.value("input_tokens", 0);
      const int completion_tokens = u.value("output_tokens", 0);
      out["usage"] = {{"prompt_tokens", prompt_tokens},
                      {"completion_tokens", completion_tokens},
                      {"total_tokens", prompt_tokens + completion_tokens}};
    }
    json = std::move(out);
    return absl::OkStatus();
  }

  config_->stats().failed_.inc();
  return absl::InvalidArgumentError("transcoder: unsupported target protocol for response TO_IR");
}

absl::Status TranscoderFilter::transcodeResponseFromIr(nlohmann::json& json) {
  if (source_protocol_ == LLMProtocol::Unspecified) {
    config_->stats().unresolved_.inc();
    return absl::InvalidArgumentError(
        "transcoder: the route declared no wire API for response FROM_IR transcoding");
  }

  if (source_protocol_ == LLMProtocol::OpenAiChatCompletions) {
    return absl::OkStatus();
  }

  if (!json.is_object() || !json.contains("choices") || !json["choices"].is_array() ||
      json["choices"].empty()) {
    config_->stats().failed_.inc();
    return absl::InvalidArgumentError("transcoder: IR response missing non-empty `choices` array");
  }

  if (source_protocol_ == LLMProtocol::GeminiGenerateContent) {
    nlohmann::json out = nlohmann::json::object();
    nlohmann::json candidates = nlohmann::json::array();
    for (size_t i = 0; i < json["choices"].size(); ++i) {
      const auto& choice = json["choices"][i];
      std::string text;
      if (choice.contains("message") && choice["message"].is_object() &&
          choice["message"].contains("content") && choice["message"]["content"].is_string()) {
        text = choice["message"]["content"].get<std::string>();
      }
      nlohmann::json cand = nlohmann::json::object();
      cand["index"] = choice.value("index", static_cast<int>(i));
      cand["content"] = {{"role", "model"},
                         {"parts", nlohmann::json::array({{{"text", std::move(text)}}})}};
      if (choice.contains("finish_reason") && choice["finish_reason"].is_string()) {
        cand["finishReason"] =
            mapIrFinishReasonToGemini(choice["finish_reason"].get<std::string>());
      } else {
        cand["finishReason"] = "STOP";
      }
      candidates.push_back(std::move(cand));
    }
    out["candidates"] = std::move(candidates);
    if (json.contains("model") && json["model"].is_string()) {
      out["modelVersion"] = json["model"];
    }
    transcodeIrUsageToGemini(json, out);
    json = std::move(out);
    return absl::OkStatus();
  }

  if (source_protocol_ == LLMProtocol::AnthropicMessages) {
    const auto& first_choice = json["choices"][0];
    std::string text;
    if (first_choice.contains("message") && first_choice["message"].is_object() &&
        first_choice["message"].contains("content") &&
        first_choice["message"]["content"].is_string()) {
      text = first_choice["message"]["content"].get<std::string>();
    }
    std::string stop_reason = "end_turn";
    if (first_choice.contains("finish_reason") && first_choice["finish_reason"].is_string()) {
      stop_reason = mapIrFinishReasonToAnthropic(first_choice["finish_reason"].get<std::string>());
    }
    nlohmann::json out = nlohmann::json::object();
    out["id"] = json.value("id", "msg_transcoded");
    out["type"] = "message";
    out["role"] = "assistant";
    out["model"] = json.value("model", sse_stream_model_);
    out["content"] = nlohmann::json::array({{{"type", "text"}, {"text", std::move(text)}}});
    out["stop_reason"] = std::move(stop_reason);
    if (json.contains("usage") && json["usage"].is_object()) {
      const auto& u = json["usage"];
      out["usage"] = {{"input_tokens", u.value("prompt_tokens", 0)},
                      {"output_tokens", u.value("completion_tokens", 0)}};
    }
    json = std::move(out);
    return absl::OkStatus();
  }

  config_->stats().failed_.inc();
  return absl::InvalidArgumentError("transcoder: unsupported source protocol for response FROM_IR");
}

absl::Status TranscoderFilter::transcodeSseEvent(SseEvent& event, bool& should_drop) {
  return config_->direction() == TranscoderProto::FROM_IR
             ? transcodeSseEventToIr(event, should_drop)
             : transcodeSseEventFromIr(event, should_drop);
}

absl::Status TranscoderFilter::transcodeSseEventToIr(SseEvent& event, bool& should_drop) {
  const LLMProtocol target = effectiveTargetProtocol();
  if (target == LLMProtocol::Unspecified) {
    config_->stats().unresolved_.inc();
    return absl::InvalidArgumentError(
        "transcoder: no target backend protocol is set for SSE response TO_IR transcoding");
  }

  if (target == LLMProtocol::OpenAiChatCompletions) {
    return absl::OkStatus();
  }

  if (!event.is_json()) {
    return absl::OkStatus();
  }

  nlohmann::json& doc = event.json().json();
  if (!doc.is_object()) {
    config_->stats().failed_.inc();
    return absl::InvalidArgumentError("transcoder: SSE JSON payload is not an object");
  }

  if (target == LLMProtocol::GeminiGenerateContent) {
    if (!doc.contains("candidates") || !doc["candidates"].is_array()) {
      config_->stats().failed_.inc();
      return absl::InvalidArgumentError("transcoder: Gemini SSE chunk missing `candidates` array");
    }
    nlohmann::json chunk = nlohmann::json::object();
    chunk["id"] = doc.value("responseId", sse_stream_id_);
    chunk["object"] = "chat.completion.chunk";
    chunk["model"] = doc.value("modelVersion", sse_stream_model_);
    nlohmann::json choices = nlohmann::json::array();
    for (size_t i = 0; i < doc["candidates"].size(); ++i) {
      const auto& cand = doc["candidates"][i];
      nlohmann::json choice = nlohmann::json::object();
      choice["index"] = cand.value("index", static_cast<int>(i));
      nlohmann::json delta = nlohmann::json::object();
      if (cand.contains("content") && cand["content"].is_object() &&
          cand["content"].contains("parts") && cand["content"]["parts"].is_array()) {
        std::string text;
        for (const auto& part : cand["content"]["parts"]) {
          if (part.is_object() && part.contains("text") && part["text"].is_string()) {
            text.append(part["text"].get<std::string>());
          }
        }
        delta["role"] = "assistant";
        delta["content"] = std::move(text);
      }
      choice["delta"] = std::move(delta);
      if (cand.contains("finishReason") && cand["finishReason"].is_string()) {
        choice["finish_reason"] =
            mapGeminiFinishReasonToIr(cand["finishReason"].get<std::string>());
      } else {
        choice["finish_reason"] = nullptr;
      }
      choices.push_back(std::move(choice));
    }
    chunk["choices"] = std::move(choices);
    transcodeGeminiUsageToIr(doc, chunk);
    doc = std::move(chunk);
    event.clear_event();
    return absl::OkStatus();
  }

  if (target == LLMProtocol::AnthropicMessages) {
    const std::string event_type = doc.value("type", std::string(event.event()));
    if (event_type == "message_start") {
      if (doc.contains("message") && doc["message"].is_object()) {
        const auto& msg = doc["message"];
        sse_stream_id_ = msg.value("id", sse_stream_id_);
        sse_stream_model_ = msg.value("model", sse_stream_model_);
      }
      doc = {
          {"id", sse_stream_id_},
          {"object", "chat.completion.chunk"},
          {"model", sse_stream_model_},
          {"choices", nlohmann::json::array({{{"index", 0},
                                              {"delta", {{"role", "assistant"}, {"content", ""}}},
                                              {"finish_reason", nullptr}}})}};
      event.clear_event();
      return absl::OkStatus();
    }
    if (event_type == "content_block_delta") {
      std::string text;
      if (doc.contains("delta") && doc["delta"].is_object() && doc["delta"].contains("text") &&
          doc["delta"]["text"].is_string()) {
        text = doc["delta"]["text"].get<std::string>();
      }
      doc = {{"id", sse_stream_id_},
             {"object", "chat.completion.chunk"},
             {"model", sse_stream_model_},
             {"choices", nlohmann::json::array({{{"index", doc.value("index", 0)},
                                                 {"delta", {{"content", std::move(text)}}},
                                                 {"finish_reason", nullptr}}})}};
      event.clear_event();
      return absl::OkStatus();
    }
    if (event_type == "message_delta") {
      std::string finish_reason = "stop";
      if (doc.contains("delta") && doc["delta"].is_object() &&
          doc["delta"].contains("stop_reason") && doc["delta"]["stop_reason"].is_string()) {
        finish_reason = mapAnthropicStopReasonToIr(doc["delta"]["stop_reason"].get<std::string>());
      }
      nlohmann::json chunk = {
          {"id", sse_stream_id_},
          {"object", "chat.completion.chunk"},
          {"model", sse_stream_model_},
          {"choices", nlohmann::json::array({{{"index", 0},
                                              {"delta", nlohmann::json::object()},
                                              {"finish_reason", finish_reason}}})}};
      if (doc.contains("usage") && doc["usage"].is_object()) {
        const int completion_tokens = doc["usage"].value("output_tokens", 0);
        chunk["usage"] = {{"completion_tokens", completion_tokens}};
      }
      doc = std::move(chunk);
      event.clear_event();
      return absl::OkStatus();
    }
    if (event_type == "message_stop") {
      event.clear_event();
      event.set_raw_data(std::make_unique<Buffer::OwnedImpl>("[DONE]"));
      sse_done_emitted_ = true;
      return absl::OkStatus();
    }
    if (event_type == "ping" || event_type == "content_block_start" ||
        event_type == "content_block_stop") {
      should_drop = true;
      return absl::OkStatus();
    }

    config_->stats().failed_.inc();
    return absl::InvalidArgumentError("transcoder: unrecognized Anthropic SSE event type");
  }

  config_->stats().failed_.inc();
  return absl::InvalidArgumentError("transcoder: unsupported target protocol for SSE TO_IR");
}

absl::Status TranscoderFilter::transcodeSseEventFromIr(SseEvent& event, bool& should_drop) {
  if (source_protocol_ == LLMProtocol::Unspecified) {
    config_->stats().unresolved_.inc();
    return absl::InvalidArgumentError(
        "transcoder: the route declared no wire API for SSE response FROM_IR transcoding");
  }

  if (source_protocol_ == LLMProtocol::OpenAiChatCompletions) {
    return absl::OkStatus();
  }

  if (!event.is_json()) {
    const absl::string_view raw = event.raw_data_as_string();
    if (raw == "[DONE]") {
      if (source_protocol_ == LLMProtocol::GeminiGenerateContent) {
        should_drop = true;
        return absl::OkStatus();
      }
      if (source_protocol_ == LLMProtocol::AnthropicMessages) {
        HttpFilters::AiProtocolManager::JsonWithExtBuf stop_json;
        stop_json.setJson({{"type", "message_stop"}});
        event.set_json(std::move(stop_json));
        (void)event.set_event("message_stop");
        return absl::OkStatus();
      }
    }
    return absl::OkStatus();
  }

  nlohmann::json& doc = event.json().json();
  if (!doc.is_object() || !doc.contains("choices") || !doc["choices"].is_array() ||
      doc["choices"].empty()) {
    config_->stats().failed_.inc();
    return absl::InvalidArgumentError("transcoder: IR SSE chunk missing `choices` array");
  }

  if (source_protocol_ == LLMProtocol::GeminiGenerateContent) {
    nlohmann::json out = nlohmann::json::object();
    nlohmann::json candidates = nlohmann::json::array();
    for (size_t i = 0; i < doc["choices"].size(); ++i) {
      const auto& choice = doc["choices"][i];
      std::string text;
      if (choice.contains("delta") && choice["delta"].is_object() &&
          choice["delta"].contains("content") && choice["delta"]["content"].is_string()) {
        text = choice["delta"]["content"].get<std::string>();
      }
      nlohmann::json cand = nlohmann::json::object();
      cand["index"] = choice.value("index", static_cast<int>(i));
      cand["content"] = {{"role", "model"},
                         {"parts", nlohmann::json::array({{{"text", std::move(text)}}})}};
      if (choice.contains("finish_reason") && choice["finish_reason"].is_string()) {
        cand["finishReason"] =
            mapIrFinishReasonToGemini(choice["finish_reason"].get<std::string>());
      }
      candidates.push_back(std::move(cand));
    }
    out["candidates"] = std::move(candidates);
    if (doc.contains("model") && doc["model"].is_string()) {
      out["modelVersion"] = doc["model"];
    }
    transcodeIrUsageToGemini(doc, out);
    doc = std::move(out);
    event.clear_event();
    return absl::OkStatus();
  }

  if (source_protocol_ == LLMProtocol::AnthropicMessages) {
    const auto& choice = doc["choices"][0];
    if (choice.contains("delta") && choice["delta"].is_object() &&
        choice["delta"].contains("content") && choice["delta"]["content"].is_string()) {
      const std::string text = choice["delta"]["content"].get<std::string>();
      doc = {{"type", "content_block_delta"},
             {"index", choice.value("index", 0)},
             {"delta", {{"type", "text_delta"}, {"text", text}}}};
      (void)event.set_event("content_block_delta");
      return absl::OkStatus();
    }
    if (choice.contains("finish_reason") && choice["finish_reason"].is_string()) {
      const std::string stop_reason =
          mapIrFinishReasonToAnthropic(choice["finish_reason"].get<std::string>());
      nlohmann::json out = {{"type", "message_delta"},
                            {"delta", {{"stop_reason", stop_reason}, {"stop_sequence", nullptr}}}};
      if (doc.contains("usage") && doc["usage"].is_object()) {
        out["usage"] = {{"output_tokens", doc["usage"].value("completion_tokens", 0)}};
      }
      doc = std::move(out);
      (void)event.set_event("message_delta");
      return absl::OkStatus();
    }
    doc = {{"type", "message_start"},
           {"message",
            {{"id", doc.value("id", "msg_transcoded")},
             {"type", "message"},
             {"role", "assistant"},
             {"model", doc.value("model", sse_stream_model_)},
             {"content", nlohmann::json::array()}}}};
    (void)event.set_event("message_start");
    return absl::OkStatus();
  }

  config_->stats().failed_.inc();
  return absl::InvalidArgumentError("transcoder: unsupported source protocol for SSE FROM_IR");
}

} // namespace Transcoder
} // namespace AiFilters
} // namespace Extensions
} // namespace Envoy
