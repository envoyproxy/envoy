#include "source/extensions/http/ai_filters/mcp_json_rest_bridge/filter.h"

#include <memory>
#include <optional>
#include <utility>

#include "source/common/buffer/buffer_impl.h"
#include "source/common/coroutine/status_macros.h"
#include "source/common/http/headers.h"
#include "source/extensions/filters/http/ai_protocol_manager/json_with_ext_buf.h"

#include "absl/algorithm/container.h"
#include "nlohmann/json.hpp"

namespace Envoy {
namespace Extensions {
namespace AiFilters {
namespace McpJsonRestBridge {

namespace {

using HttpFilters::AiProtocolManager::AiRequest;
using HttpFilters::AiProtocolManager::JsonWithExtBuf;
using HttpFilters::AiProtocolManager::LocalReplier;
using HttpFilters::AiProtocolManager::LocalReply;
using HttpFilters::AiProtocolManager::RawResponsePropagator;
using HttpFilters::AiProtocolManager::RawResponseReceiver;
using HttpFilters::AiProtocolManager::ResponseMode;
using HttpFilters::AiProtocolManager::UnparsedRequest;
using RequestStatus = Bridge::McpJsonRestBridge::RequestStatus;
using ResponseStatus = Bridge::McpJsonRestBridge::ResponseStatus;

// Whether the manager left a string of `node` in the external buffer.
bool hasExternalRef(const nlohmann::json& node) {
  if (JsonWithExtBuf::isExternalRef(node)) {
    return true;
  }
  return node.is_structured() && absl::c_any_of(node, hasExternalRef);
}

Buffer::InstancePtr toBuffer(absl::string_view data) {
  return std::make_unique<Buffer::OwnedImpl>(data);
}

} // namespace

McpJsonRestBridgeFilter::McpJsonRestBridgeFilter(
    Bridge::McpJsonRestBridgeFilterConfigSharedPtr config,
    const HttpFilters::AiProtocolManager::AiFilterContext& context)
    : context_(context), bridge_(std::move(config), *this) {}

absl::Status McpJsonRestBridgeFilter::decodeSync(AiRequest& request, LocalReplier reply_locally) {
  if (!bridgeRequest(request)) {
    std::move(reply_locally)(*std::move(local_reply_));
  }
  return absl::OkStatus();
}

bool McpJsonRestBridgeFilter::bridgeRequest(AiRequest& request) {
  switch (bridge_.onRequestHeaders(context_.request_headers, /*end_stream=*/false, routeConfig())) {
  case RequestStatus::PassThrough:
    return true;
  case RequestStatus::Replied:
    return false;
  default:
    break;
  }
  if (!bridge_.onRequestBodySize(context_.request_payload_bytes)) {
    return false;
  }
  // The bridge reads argument values, which it cannot do from a reference.
  if (hasExternalRef(request.json())) {
    bridge_.onRequestBodyTooLarge();
    return false;
  }

  nlohmann::json body;
  switch (bridge_.onRequestBody(request.json(), context_.request_headers, routeConfig(), body)) {
  case RequestStatus::Replied:
    return false;
  case RequestStatus::Forward:
    if (body.is_null()) {
      request.dropBody();
    } else {
      request.json() = std::move(body);
    }
    break;
  case RequestStatus::ForwardNoBody:
    request.dropBody();
    break;
  default:
    break;
  }
  return true;
}

std::optional<LocalReply> McpJsonRestBridgeFilter::onUnparsedRequest(UnparsedRequest reason) {
  const bool end_stream = reason == UnparsedRequest::NoBody;
  if (bridge_.onRequestHeaders(context_.request_headers, end_stream, routeConfig()) ==
          RequestStatus::NeedsBody &&
      bridge_.onRequestBodySize(context_.request_payload_bytes)) {
    bridge_.onRequestBodyInvalid();
  }
  return std::exchange(local_reply_, std::nullopt);
}

ResponseMode McpJsonRestBridgeFilter::onResponseHeaders(Http::ResponseHeaderMap& headers,
                                                        bool end_stream) {
  response_status_ = bridge_.onResponseHeaders(headers, end_stream, response_body_);
  switch (response_status_) {
  case ResponseStatus::PassThrough:
    return ResponseMode::Parsed;
  case ResponseStatus::Complete:
  case ResponseStatus::Streaming:
    return ResponseMode::Raw;
  case ResponseStatus::Buffered:
    break;
  }
  response_headers_ = &headers;
  return ResponseMode::RawHoldHeaders;
}

Coroutine::Task<absl::Status> McpJsonRestBridgeFilter::encodeRaw(RawResponseReceiver receive,
                                                                 RawResponsePropagator propagate,
                                                                 LocalReplier reply_locally) {
  switch (response_status_) {
  case ResponseStatus::PassThrough:
    break;
  case ResponseStatus::Complete:
    co_return co_await propagate(toBuffer(response_body_));
  case ResponseStatus::Streaming:
    co_return co_await encodeStreaming(std::move(receive), std::move(propagate),
                                       std::move(reply_locally));
  case ResponseStatus::Buffered:
    co_return co_await encodeBuffered(std::move(receive), std::move(propagate),
                                      std::move(reply_locally));
  }
  co_return absl::OkStatus();
}

Coroutine::Task<absl::Status> McpJsonRestBridgeFilter::encodeStreaming(
    RawResponseReceiver receive, RawResponsePropagator propagate, LocalReplier reply_locally) {
  bool end_stream = false;
  while (!end_stream) {
    ASSIGN_OR_CO_RETURN(std::optional<Buffer::InstancePtr> chunk, co_await receive());
    end_stream = !chunk.has_value();
    Buffer::InstancePtr data = end_stream ? toBuffer("") : std::move(*chunk);
    if (!bridge_.encodeStreamingData(*data, end_stream)) {
      std::move(reply_locally)(*std::move(local_reply_));
      co_return absl::OkStatus();
    }
    CO_RETURN_IF_ERROR(co_await propagate(std::move(data)));
  }
  co_return absl::OkStatus();
}

Coroutine::Task<absl::Status> McpJsonRestBridgeFilter::encodeBuffered(
    RawResponseReceiver receive, RawResponsePropagator propagate, LocalReplier reply_locally) {
  Buffer::OwnedImpl body;
  while (true) {
    ASSIGN_OR_CO_RETURN(std::optional<Buffer::InstancePtr> chunk, co_await receive());
    if (!chunk.has_value()) {
      break;
    }
    if (!bridge_.onResponseBodySize(body.length() + (*chunk)->length(), *response_headers_)) {
      std::move(reply_locally)(*std::move(local_reply_));
      co_return absl::OkStatus();
    }
    body.move(**chunk);
  }
  co_return co_await propagate(toBuffer(bridge_.encodeJsonRpcData(body, *response_headers_)));
}

void McpJsonRestBridgeFilter::sendLocalReply(
    Http::Code code, absl::string_view body,
    std::function<void(Http::ResponseHeaderMap&)> modify_headers,
    Grpc::Status::GrpcStatus grpc_status, absl::string_view details) {
  local_reply_ = LocalReply{.code = code,
                            .body = std::string(body),
                            .details = std::string(details),
                            .modify_headers = std::move(modify_headers),
                            .grpc_status = grpc_status};
  // A reply in place of a response is complete as the bridge sends it.
  if (response_status_ != ResponseStatus::PassThrough) {
    return;
  }
  // The HTTP filter gives a failed request's error its JSON-RPC envelope as the reply passes back
  // through it. The manager sends an AI filter's reply as it is, so it gets the envelope here.
  std::optional<std::string> error = bridge_.jsonRpcError(body);
  if (!error.has_value()) {
    return;
  }
  local_reply_->body = *std::move(error);
  local_reply_->modify_headers =
      [modify_headers = std::move(local_reply_->modify_headers)](Http::ResponseHeaderMap& headers) {
        if (modify_headers != nullptr) {
          modify_headers(headers);
        }
        headers.setContentType(Http::Headers::get().ContentTypeValues.Json);
      };
}

uint64_t McpJsonRestBridgeFilter::streamId() const {
  const OptRef<const StreamInfo::StreamIdProvider> provider =
      context_.stream_info.getStreamIdProvider();
  return provider.has_value() ? provider->toInteger().value_or(0) : 0;
}

const Bridge::McpJsonRestBridgePerRouteConfig* McpJsonRestBridgeFilter::routeConfig() const {
  return dynamic_cast<const Bridge::McpJsonRestBridgePerRouteConfig*>(context_.route_config.get());
}

} // namespace McpJsonRestBridge
} // namespace AiFilters
} // namespace Extensions
} // namespace Envoy
