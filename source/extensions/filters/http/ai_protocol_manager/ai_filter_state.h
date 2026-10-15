#pragma once

#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "envoy/router/string_accessor.h"
#include "envoy/stream_info/filter_state.h"

#include "source/extensions/filters/http/ai_protocol_manager/llm_binding.h"
#include "source/extensions/filters/http/ai_protocol_manager/token_usage.h"

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AiProtocolManager {

// Five transcoding instructions. A filter ahead of the transcoder filter can set
// any subset of them per request. The transcoder uses what is set and falls
// back to the route's declarations and its built-in defaults if unset.
namespace FilterStateKeys {
// A RequestLlmProtocol: the wire API of the request as received.
constexpr absl::string_view LlmProtocolRequest = "envoy.ai.llm_protocol.request";
// A ResponseLlmProtocol: the wire API of the target backend model.
constexpr absl::string_view LlmProtocolResponse = "envoy.ai.llm_protocol.response";
// An LlmBindingObject: how the request as received carries its API on HTTP.
constexpr absl::string_view BindingRequest = "envoy.ai.binding.request";
// An LlmBindingObject: how the upstream expects the backend's API on HTTP.
constexpr absl::string_view BindingResponse = "envoy.ai.binding.response";
// An LlmBindingReadObject: what the transcoder read from the client's path.
constexpr absl::string_view BindingRead = "envoy.ai.binding.read";
// The model the request names, as the request_info AI filter records it.
constexpr absl::string_view ModelRequest = "envoy.ai.model.request";
// The model to send upstream, when it differs from the one the request names. It replaces the model
// in the request body and in the rewritten path.
constexpr absl::string_view ModelResolved = "envoy.ai.model.resolved";
} // namespace FilterStateKeys

// A wire API held in filter state. It is created from and serializes as an LLMProtocol value name,
// such as ANTHROPIC_MESSAGES.
class LlmProtocolFilterState : public StreamInfo::FilterState::Object {
public:
  explicit LlmProtocolFilterState(LLMProtocol protocol) : protocol_(protocol) {}

  LLMProtocol protocol() const { return protocol_; }

  // StreamInfo::FilterState::Object
  std::optional<std::string> serializeAsString() const override;
  bool hasFieldSupport() const override { return true; }
  FieldType getField(absl::string_view field_name) const override;

protected:
  // The protocol the object at `key` holds; Unspecified when none is set.
  static LLMProtocol read(const StreamInfo::FilterState& filter_state, absl::string_view key);

private:
  const LLMProtocol protocol_;
};

// The request's wire API, set by a filter that knows the caller better than the route does.
class RequestLlmProtocol : public LlmProtocolFilterState {
public:
  using LlmProtocolFilterState::LlmProtocolFilterState;

  // Unspecified when no object is set.
  static LLMProtocol fromFilterState(const StreamInfo::FilterState& filter_state) {
    return read(filter_state, FilterStateKeys::LlmProtocolRequest);
  }
};

// The upstream's wire API, set by a filter that chose the upstream, such as a model router.
class ResponseLlmProtocol : public LlmProtocolFilterState {
public:
  using LlmProtocolFilterState::LlmProtocolFilterState;

  // Unspecified when no object is set.
  static LLMProtocol fromFilterState(const StreamInfo::FilterState& filter_state) {
    return read(filter_state, FilterStateKeys::LlmProtocolResponse);
  }

  // The backend's wire API in effect for this stream: the filter state object when one is set,
  // else `route_declared`. Every reader of the backend's API (the transcoder's legs, token usage
  // extraction) resolves it through here, so they cannot disagree.
  static LLMProtocol resolve(const StreamInfo::FilterState& filter_state,
                             LLMProtocol route_declared) {
    const LLMProtocol declared = fromFilterState(filter_state);
    return declared != LLMProtocol::Unspecified ? declared : route_declared;
  }
};

// A binding in filter state, created from a URI pattern (`/...`) or an LLMBinding in JSON
// (`{...}`). A value that does not parse keeps its error for the reader. It never changes once
// set, so it may be shared, e.g. set once per connection.
class LlmBindingObject : public Router::StringAccessor {
public:
  explicit LlmBindingObject(absl::string_view value);

  // The binding at `key`: null when no object is set, or the error of one that does not parse. A
  // plain string another writer left there is parsed as the factory does.
  static absl::StatusOr<LlmBindingConstSharedPtr>
  fromFilterState(const StreamInfo::FilterState& filter_state, absl::string_view key);

  // Null when `status()` is not ok.
  const LlmBindingConstSharedPtr& binding() const { return binding_; }
  const absl::Status& status() const { return status_; }

  // Router::StringAccessor
  absl::string_view asString() const override { return source_; }

  // StreamInfo::FilterState::Object
  std::optional<std::string> serializeAsString() const override { return source_; }

private:
  const std::string source_;
  LlmBindingConstSharedPtr binding_;
  absl::Status status_;
};

// What a request leg read from the client's path under the binding the request arrived in. The
// transcoder sets it once per stream; field access reads `model` and the path variables by name.
class LlmBindingReadObject : public StreamInfo::FilterState::Object {
public:
  explicit LlmBindingReadObject(LlmBindingRead read) : read_(std::move(read)) {}

  // The read the transcoder left in `filter_state`; null when there is none.
  static const LlmBindingRead* fromFilterState(const StreamInfo::FilterState& filter_state);

  const LlmBindingRead& read() const { return read_; }

  // StreamInfo::FilterState::Object
  bool hasFieldSupport() const override { return true; }
  FieldType getField(absl::string_view field_name) const override;

private:
  const LlmBindingRead read_;
};

// The string a Router::StringAccessor at `key` holds; nullopt when no object is set.
std::optional<absl::string_view> stringFromFilterState(const StreamInfo::FilterState& filter_state,
                                                       absl::string_view key);

} // namespace AiProtocolManager
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
