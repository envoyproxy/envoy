#include "source/extensions/http/ai_filters/transcoder/filter.h"

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "source/common/buffer/buffer_impl.h"
#include "source/common/coroutine/status_macros.h"

#include "absl/status/statusor.h"
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

namespace {

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

TranscoderFilterConfig::TranscoderFilterConfig(const TranscoderProto& proto,
                                               TranscodingEngine engine, Stats::Scope& scope)
    : stats_(TranscoderFilterStats{ALL_TRANSCODER_FILTER_STATS(
          POOL_COUNTER_PREFIX(scope, "ai_protocol_manager.transcoder."))}),
      request_handling_(proto.request_handling()), response_handling_(proto.response_handling()),
      engine_(std::move(engine)) {}

TranscoderFilter::TranscoderFilter(TranscoderFilterConfigSharedPtr config,
                                   const AiFilterContext& context)
    : config_(std::move(config)), source_protocol_(context.request_protocol),
      target_protocol_(context.response_protocol), request_headers_(context.request_headers),
      request_path_(std::string(context.request_headers.getPathValue())),
      created_(std::chrono::duration_cast<std::chrono::seconds>(
                   context.stream_info.startTime().time_since_epoch())
                   .count()),
      request_model_(config_->engine().modelFromRequestPath(source_protocol_, request_path_)) {}

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

// A `TO_IR` request comes from the client in the dialect the route declared; a `FROM_IR` one goes
// to the backend in the target's dialect. As with responses, every dialect rule is the engine's,
// down to what a dialect names in the request path rather than the body.
absl::Status TranscoderFilter::transcodeRequest(nlohmann::json& json) {
  const bool to_ir = config_->requestHandling() == TranscoderProto::TO_IR;
  const LLMProtocol dialect = to_ir ? source_protocol_ : target_protocol_;
  if (dialect == LLMProtocol::Unspecified) {
    config_->stats().unresolved_.inc();
    return absl::InvalidArgumentError(
        to_ir
            ? "transcoder: the route declared no wire API, so the payload's source schema is "
              "unknown"
            : "transcoder: no target backend protocol is set, so the payload has no target schema");
  }

  // A `FROM_IR` leg validates against the target's schema inside the engine, so a document the
  // upstream would reject fails here rather than over the network.
  TranscodeContext ctx;
  ctx.request_path = request_path_;
  const absl::Status status = config_->engine().transcode(
      {PayloadKind::Request, to_ir ? TranscodeDirection::ToIr : TranscodeDirection::FromIr,
       dialect},
      ctx, json);
  if (!status.ok()) {
    config_->stats().failed_.inc();
    return status;
  }
  if (!ctx.ir_model.empty()) {
    request_model_ = std::move(ctx.ir_model);
  }
  if (ctx.rewritten_path.has_value()) {
    request_headers_.setPath(*ctx.rewritten_path);
  }
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
  const LLMProtocol dialect = to_ir ? target_protocol_ : source_protocol_;
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
