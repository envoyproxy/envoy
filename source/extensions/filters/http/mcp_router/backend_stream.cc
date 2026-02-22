#include "source/extensions/filters/http/mcp_router/backend_stream.h"

#include "source/common/http/headers.h"
#include "source/common/http/sse/sse_parser.h"
#include "source/common/http/utility.h"
#include "source/common/json/json_loader.h"

#include "absl/strings/ascii.h"
#include "absl/strings/str_split.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace McpRouter {

namespace {

// Extract media type from Content-Type header (before any semicolon).
inline absl::string_view extractMediaType(absl::string_view content_type) {
  const std::vector<absl::string_view> parts =
      absl::StrSplit(content_type, absl::MaxSplits(';', 1));
  return absl::StripAsciiWhitespace(parts.front());
}

} // namespace

ResponseContentType detectContentType(absl::string_view content_type_header) {
  if (content_type_header.empty()) {
    return ResponseContentType::Unknown;
  }
  const absl::string_view media_type = extractMediaType(content_type_header);
  if (absl::EqualsIgnoreCase(media_type, Http::Headers::get().ContentTypeValues.TextEventStream)) {
    return ResponseContentType::Sse;
  }
  if (absl::EqualsIgnoreCase(media_type, Http::Headers::get().ContentTypeValues.Json)) {
    return ResponseContentType::Json;
  }
  return ResponseContentType::Unknown;
}

SseMessageType classifyMessage(absl::string_view json_data, int64_t request_id) {
  auto parsed_or = Json::Factory::loadFromString(std::string(json_data));
  if (!parsed_or.ok()) {
    return SseMessageType::Unknown;
  }

  auto id_or = (*parsed_or)->getInteger("id");
  auto method_or = (*parsed_or)->getString("method");
  auto result_or = (*parsed_or)->getObject("result");
  auto error_or = (*parsed_or)->getObject("error");

  // Has result or error with matching ID -> Response
  if ((result_or.ok() && *result_or) || (error_or.ok() && *error_or)) {
    if (id_or.ok() && *id_or == request_id) {
      return SseMessageType::Response;
    }
  }

  // Has method but no ID -> Notification
  if (method_or.ok() && !id_or.ok()) {
    return SseMessageType::Notification;
  }

  // Has method AND ID -> Server-to-client request
  if (method_or.ok() && id_or.ok()) {
    return SseMessageType::ServerRequest;
  }

  return SseMessageType::Unknown;
}

BackendStreamCallbacks::BackendStreamCallbacks(const std::string& backend_name,
                                               std::function<void(BackendResponse)> on_complete,
                                               int64_t request_id, bool aggregate_mode,
                                               std::weak_ptr<SseStreamHandler> parent,
                                               bool streaming_enabled)
    : backend_name_(backend_name), on_complete_(std::move(on_complete)), request_id_(request_id),
      aggregate_mode_(aggregate_mode), parent_(std::move(parent)),
      streaming_enabled_(streaming_enabled) {
  response_.backend_name = backend_name;
}

void BackendStreamCallbacks::onHeaders(Http::ResponseHeaderMapPtr&& headers, bool end_stream) {
  if (headers && headers->Status()) {
    response_.status_code = Http::Utility::getResponseStatus(*headers);
    response_.success = (response_.status_code >= 200 && response_.status_code < 300);

    // Detect content type.
    if (headers->ContentType()) {
      response_.content_type = detectContentType(headers->getContentTypeValue());
      ENVOY_LOG(debug, "Backend '{}' response content-type: {} (detected: {})", backend_name_,
                headers->getContentTypeValue(), static_cast<int>(response_.content_type));
    }

    // Extract session ID from response header.
    auto session_header = headers->get(Http::LowerCaseString("mcp-session-id"));
    if (!session_header.empty()) {
      response_.session_id = std::string(session_header[0]->value().getStringView());
    }

    // In streaming mode for SSE, forward headers immediately.
    if (streaming_enabled_ && response_.isSse() && response_.success) {
      if (auto parent = parent_.lock()) {
        streaming_started_ = true;
        parent->pushSseHeaders(std::move(headers), end_stream);
        // For SSE streaming, we may get end_stream=false on headers.
        // Don't complete yet - wait for data or actual end_stream.
        if (end_stream) {
          complete();
        }
        return;
      }
    }
  }

  if (end_stream) {
    complete();
  }
}

void BackendStreamCallbacks::onData(Buffer::Instance& data, bool end_stream) {
  ENVOY_LOG(debug, "onData Backend '{}' data_size={}, end_stream: {}", backend_name_, data.length(),
            end_stream);

  // In streaming mode for SSE, forward data immediately without buffering.
  if (streaming_started_ && streaming_enabled_) {
    if (auto parent = parent_.lock()) {
      ENVOY_LOG(debug, "onData Backend '{}': streaming SSE data directly, size={}", backend_name_,
                data.length());
      parent->pushSseData(data, end_stream);
      if (end_stream) {
        complete();
      }
      return;
    }
  }

  // Buffer the body for aggregation mode or non-SSE responses.
  response_.body.append(data.toString());
  ENVOY_LOG(debug, "onData Backend '{}' buffered body_size: {}", backend_name_,
            response_.body.size());

  // For SSE in aggregate mode, always try to parse to extract the JSON-RPC response.
  // This must run even when end_stream=true to handle responses that arrive in a single chunk.
  ENVOY_LOG(debug, "onData Backend '{}': aggregate_mode={}, isSse={}", backend_name_,
            aggregate_mode_, response_.isSse());
  if (aggregate_mode_ && response_.isSse()) {
    if (tryParseSseResponse() && !end_stream) {
      ENVOY_LOG(
          debug,
          "Backend '{}' SSE aggregation: found valid response, without waiting for end_stream",
          backend_name_);
      complete();
      return;
    }
  }

  if (end_stream) {
    complete();
  }
}

bool BackendStreamCallbacks::tryParseSseResponse() {
  // Return cached result if we already found a response.
  if (found_response_) {
    return true;
  }

  if (response_.body.size() <= parse_offset_) {
    return false;
  }

  // Parse SSE events incrementally from where we left off.
  absl::string_view remaining(response_.body);
  remaining = remaining.substr(parse_offset_);

  while (!remaining.empty()) {
    // Look for complete SSE events (terminated by blank line).
    // findEventEnd returns {event_start, event_end, next_event_start}.
    auto [event_start, event_end, next_start] =
        Http::Sse::SseParser::findEventEnd(remaining, false);
    ENVOY_LOG(debug,
              "tryParseSseResponse: remaining_size={}, event_start={}, event_end={}, next_start={}",
              remaining.size(), event_start, event_end, next_start);
    if (event_start == absl::string_view::npos) {
      // No complete event found yet.
      ENVOY_LOG(debug, "tryParseSseResponse: no complete event found yet");
      return false;
    }

    // TODO(botengyao): also handle event id for resumption with composite Last-Event-ID.
    // Parse the event to extract the data field.
    auto event_str = remaining.substr(event_start, event_end - event_start);
    auto parsed_event = Http::Sse::SseParser::parseEvent(event_str);
    std::string data = parsed_event.data.value_or("");
    ENVOY_LOG(debug, "tryParseSseResponse: extracted data_size={}, data='{}'", data.size(),
              data.substr(0, 100));
    if (!data.empty()) {
      SseMessageType msg_type = classifyMessage(data, request_id_);
      ENVOY_LOG(debug, "tryParseSseResponse: classified message type={}",
                static_cast<int>(msg_type));

      switch (msg_type) {
      case SseMessageType::Response:
        // Found matching response - buffer for aggregation.
        response_.extracted_jsonrpc = std::move(data);
        found_response_ = true;
        return true;

      case SseMessageType::Notification:
      case SseMessageType::ServerRequest:
        // TODO(botengyao): could handle progressToken here with suffix.
        // Forward intermediate events immediately to client.
        if (auto parent = parent_.lock()) {
          parent->pushSseEvent(backend_name_, data, msg_type);
        } else {
          ENVOY_LOG(debug, "tryParseSseResponse: parent destroyed, cannot forward {} event",
                    msg_type == SseMessageType::Notification ? "notification" : "server_request");
        }
        break;
      case SseMessageType::Unknown:
      default:
        ENVOY_LOG(debug, "tryParseSseResponse: unknown message type, skipping");
        break;
      }
    }

    // Move to next event and update parse offset.
    parse_offset_ += next_start;
    if (next_start >= remaining.size()) {
      break;
    }
    remaining = remaining.substr(next_start);
  }

  return false;
}

void BackendStreamCallbacks::onTrailers(Http::ResponseTrailerMapPtr&&) { complete(); }

void BackendStreamCallbacks::onComplete() { complete(); }

void BackendStreamCallbacks::onReset() {
  if (completed_) {
    return;
  }

  response_.success = false;
  response_.error = "Stream reset";
  // For streaming mode, notify via error callback only if streaming hasn't started yet.
  // If streaming has already started (headers sent), we can't send error headers -
  // fall through to complete() which will call onStreamingComplete().
  if (streaming_enabled_ && !streaming_started_) {
    if (auto parent = parent_.lock()) {
      parent->onStreamingError(response_.error);
    }
    completed_ = true;
    return;
  }
  complete();
}

void BackendStreamCallbacks::complete() {
  if (!completed_) {
    completed_ = true;
    ENVOY_LOG(debug,
              "Backend '{}' complete: status={}, content_type={}, body_size={}, streaming={}",
              backend_name_, response_.status_code, static_cast<int>(response_.content_type),
              response_.body.size(), streaming_started_);

    // In streaming mode, notify completion via parent method if parent is still alive.
    if (streaming_enabled_ && streaming_started_) {
      if (auto parent = parent_.lock()) {
        parent->onStreamingComplete();
      } else {
        ENVOY_LOG(debug,
                  "Backend '{}' complete: parent filter destroyed, ignoring streaming callback",
                  backend_name_);
      }
      return;
    }

    // For non-streaming mode, call the regular on_complete.
    if (on_complete_) {
      on_complete_(std::move(response_));
    }
  }
}

} // namespace McpRouter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
